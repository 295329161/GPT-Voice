#include "core/terminal.h"
#include "esp32_s3_szp.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
static int compare(const void *a, const void *b) {
    const file_entry_t *x = a, *y = b;
    if (x->directory != y->directory)
        return y->directory - x->directory;
    return strcasecmp(x->name, y->name);
}
static void listing(const char *path) {
    if (!storage_path_valid(path)) {
        terminal_notice("无效路径");
        return;
    }
    DIR *d = opendir(path);
    if (!d) {
        terminal_notice("目录不可用，请检查 SD 卡");
        return;
    }
    file_entry_t *list = calloc(FILE_LIMIT, sizeof(*list));
    if (!list) {
        closedir(d);
        return;
    }
    int n = 0;
    struct dirent *e;
    bool truncated = false;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        if (n == FILE_LIMIT) {
            truncated = true;
            break;
        }
        char p[PATH_SIZE];
        struct stat st;
        if (snprintf(p, sizeof(p), "%s/%s", path, e->d_name) >= sizeof(p) || stat(p, &st))
            continue;
        snprintf(list[n].name, sizeof(list[n].name), "%s", e->d_name);
        list[n].directory = S_ISDIR(st.st_mode);
        list[n++].size = st.st_size;
    }
    closedir(d);
    qsort(list, n, sizeof(*list), compare);
    uint64_t total_bytes = 0, free_bytes = 0;
    esp_vfs_fat_info("/sdcard", &total_bytes, &free_bytes);
    state_lock();
    state->sd_total = total_bytes;
    state->sd_free = free_bytes;
    memcpy(state->files, list, n * sizeof(*list));
    state->file_count = n;
    strcpy(state->directory, path);
    state->files_generation++;
    state_unlock();
    free(list);
    terminal_notice(truncated ? "目录较大，仅显示前 96 项" : "已读取 %d 项", n);
}
void storage_job(const terminal_job_t *j) {
    if (j->kind == JOB_MOUNT) {
        esp_err_t e = state->mounted ? ESP_OK : bsp_sdcard_mount();
        state_lock();
        state->mounted = e == ESP_OK;
        state_unlock();
        if (e != ESP_OK) {
            terminal_notice("SD 挂载失败：%s", esp_err_to_name(e));
            return;
        }
        listing("/sdcard");
        return;
    }
    if (!state->mounted) {
        terminal_notice("请插入 SD 卡并挂载");
        return;
    }
    if (j->kind == JOB_EJECT) {
        media_job(&(terminal_job_t){.kind = JOB_MUSIC_TOGGLE, .value = -1});
        if (media_busy_path("/sdcard")) {
            terminal_notice("音乐尚未停止，请稍后卸载");
            return;
        }
        esp_err_t e = bsp_sdcard_unmount();
        if (e == ESP_OK) {
            state_lock();
            state->mounted = false;
            state->file_count = 0;
            state->files_generation++;
            state_unlock();
        }
        terminal_notice(e == ESP_OK ? "SD 卡已安全卸载" : "卸载失败");
        return;
    }
    if (!storage_path_valid(j->a)) {
        terminal_notice("无效路径");
        return;
    }
    if (j->kind == JOB_LIST) {
        struct stat st;
        if ((!strcmp(j->a, "/sdcard/Music") || !strcmp(j->a, "/sdcard/Pictures")) &&
            stat(j->a, &st))
            listing("/sdcard");
        else
            listing(j->a);
        return;
    }
    if (!strcmp(j->a, "/sdcard")) {
        terminal_notice("不能修改 SD 根目录");
        return;
    }
    if (media_busy_path(j->a)) {
        terminal_notice("文件正在使用，请先停止播放");
        return;
    }
    int ret = -1;
    if (j->kind == JOB_MKDIR)
        ret = mkdir(j->a, 0775);
    else if (j->kind == JOB_DELETE) {
        struct stat st;
        if (!stat(j->a, &st))
            ret = S_ISDIR(st.st_mode) ? rmdir(j->a) : unlink(j->a);
    } else if (j->kind == JOB_RENAME && storage_name_valid(j->b)) {
        char dest[PATH_SIZE];
        snprintf(dest, sizeof(dest), "%s", j->a);
        char *slash = strrchr(dest, '/');
        if (slash) {
            *slash = 0;
            size_t n = strlen(dest);
            if (n + strlen(j->b) + 2 < sizeof(dest)) {
                strcat(dest, "/");
                strcat(dest, j->b);
                struct stat st;
                if (stat(dest, &st) && errno == ENOENT)
                    ret = rename(j->a, dest);
            }
        }
    }
    if (ret) {
        terminal_notice("文件操作失败：%s", strerror(errno));
        return;
    }
    char dir[PATH_SIZE];
    state_lock();
    strcpy(dir, state->directory);
    state_unlock();
    listing(dir);
}

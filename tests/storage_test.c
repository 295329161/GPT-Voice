#include "core/terminal.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static terminal_state_t saved_state;
terminal_state_t *state = &saved_state;
static int mode, position, open_calls;
static bool usb_busy;
static struct dirent entry;
static char notice[192];
void state_lock(void) {}
void state_unlock(void) {}
void terminal_notice(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(notice, sizeof(notice), fmt, args);
    va_end(args);
}
const char *esp_err_to_name(esp_err_t e) { (void)e; return "test error"; }
esp_err_t bsp_sdcard_mount(void) { return ESP_OK; }
esp_err_t bsp_sdcard_unmount(void) { return ESP_OK; }
esp_err_t esp_vfs_fat_info(const char *p, uint64_t *t, uint64_t *f) {
    (void)p; *t = 100; *f = 50; return ESP_OK;
}
void media_job(const terminal_job_t *j) { (void)j; }
bool media_busy_path(const char *p) { (void)p; return false; }
bool usb_transfer_busy(void) { return usb_busy; }

DIR *__wrap_opendir(const char *p) { (void)p; open_calls++; position = 0; return (DIR *)&entry; }
int __wrap_closedir(DIR *d) { (void)d; return 0; }
struct dirent *__wrap_readdir(DIR *d) {
    (void)d;
    if (mode == 1 && position == 1) { errno = EIO; return NULL; }
    if (position >= (mode == 2 ? 2 : 1)) return NULL;
    strcpy(entry.d_name, mode == 3 ? "bad?name" : "new.txt");
    ++position;
    return &entry;
}
int __wrap_stat(const char *p, struct stat *s) {
    (void)p;
    if (mode == 4) { errno = EIO; return -1; }
    memset(s, 0, sizeof(*s)); s->st_size = 42; return 0;
}
static void reset(int value) {
    memset(state, 0, sizeof(*state));
    state->mounted = true;
    state->file_count = 1;
    strcpy(state->files[0].name, "original.txt");
    strcpy(state->directory, "/sdcard/old");
    state->files_generation = 7;
    *notice = 0; mode = value;
}
int main(void) {
    terminal_job_t list = {.kind = JOB_LIST, .a = "/sdcard"};
    for (int failure = 1; failure <= 4; failure++) {
        reset(failure); storage_job(&list);
        assert(state->files_generation == 7);
        assert(state->file_count == 1);
        assert(!strcmp(state->files[0].name, "original.txt"));
        assert(!strcmp(state->directory, "/sdcard/old"));
        assert(strstr(notice, "SD"));
    }
    reset(0); storage_job(&list);
    assert(state->files_generation == 8 && state->file_count == 1);
    assert(!strcmp(state->files[0].name, "new.txt"));
    assert(!strcmp(state->directory, "/sdcard"));
    assert(!*notice);
    reset(0); usb_busy = true; open_calls = 0;
    storage_job(&list);
    assert(open_calls == 0 && state->files_generation == 7);
    assert(!strcmp(state->files[0].name, "original.txt"));
    assert(strstr(notice, "USB"));
    usb_busy = false;
    puts("SD read errors, duplicate/invalid entries and stat failures preserve last valid listing PASS");
}

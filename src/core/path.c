#include "path.h"
#include <string.h>
bool storage_name_valid(const char *name) {
    if (!name || !*name || strlen(name) >= 256 || !strcmp(name, ".") || !strcmp(name, ".."))
        return false;
    if (strpbrk(name, "/\\:*?\"<>|\r\n"))
        return false;
    size_t n = strlen(name);
    if (name[n - 1] == '.' || name[n - 1] == ' ')
        return false;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
        if (*p < 32)
            return false;
    return true;
}
bool storage_path_valid(const char *path) {
    if (!path || strlen(path) >= 512)
        return false;
    if (!strcmp(path, "/sdcard"))
        return true;
    if (strncmp(path, "/sdcard/", 8))
        return false;
    const char *p = path + 8;
    while (*p) {
        const char *end = strchr(p, '/');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (!n || n >= 256)
            return false;
        char component[256];
        memcpy(component, p, n);
        component[n] = 0;
        if (!storage_name_valid(component))
            return false;
        if (!end)
            return true;
        p = end + 1;
    }
    return false;
}

#ifndef EDGE_INSTALL_H
#define EDGE_INSTALL_H
#include "../src/platform.h"
#define PATH_CAP 1024
#define FILE_COUNT 4
#define SETUP_MUTEX L"Local\\EdgeIsland.Setup.1"
typedef struct {
    wchar_t programs[PATH_CAP], dir[PATH_CAP], link[PATH_CAP], app[PATH_CAP], uninstaller[PATH_CAP];
} Locations;
extern const wchar_t *payload_names[FILE_COUNT];
int locations(Locations *p);
int join_path(wchar_t *dst, const wchar_t *parent, const wchar_t *leaf);
int exists(const wchar_t *path);
int remove_file(const wchar_t *path);
int make_directory(const wchar_t *p);
int has_arg(const wchar_t *arg);
int close_installed(const Locations *p);
int create_link(const Locations *p);
HANDLE install_lock(void);
void fail_ui(int quiet, const wchar_t *message);
int reg_text(HKEY key, const wchar_t *name, const wchar_t *text);
int start_file(const wchar_t *file, const wchar_t *dir);
#endif

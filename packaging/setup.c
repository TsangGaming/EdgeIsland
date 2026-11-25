#include "wizard.h"
#include "../src/version.h"
#include "../src/language.h"
typedef struct {
    wchar_t final[PATH_CAP], stage[PATH_CAP], backup[PATH_CAP];
    int replaced, put;
} Item;
static Item files[FILE_COUNT];
static Locations paths;
static int extract(HINSTANCE instance, int id, const wchar_t *path)
{
    HRSRC r = FindResourceW(instance, MAKEINTRESOURCEW(id), RT_RCDATA);
    DWORD bytes, written = 0;
    HGLOBAL h;
    HANDLE f;
    void *src;
    int ok;
    if (!r) {
        return 0;
    }
    bytes = SizeofResource(instance, r);
    h = LoadResource(instance, r);
    src = h ? LockResource(h) : NULL;
    if (!src || !bytes) {
        return 0;
    }
    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        return 0;
    }
    ok = WriteFile(f, src, bytes, &written, NULL) && written == bytes&&FlushFileBuffers(f);
    CloseHandle(f);
    return ok;
}
static int register_uninstall(const Locations *p)
{
    HKEY k;
    DWORD d, one = 1;
    wchar_t cmd[PATH_CAP + 32];
    int ok;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, UNINSTALL_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, &d)) {
        return 0;
    }
    _snwprintf(cmd, COUNT(cmd) - 1, L"\"%ls\"", p->uninstaller);
    ok = reg_text(k, L"UninstallString", cmd);
    lstrcatW(cmd, L" /quiet");
    ok = ok&&reg_text(k, L"QuietUninstallString", cmd) &&
         reg_text(k, L"InstallLocation", p->dir) && reg_text(k, L"DisplayIcon", p->app) &&
         reg_text(k, L"DisplayVersion", EDGE_VERSION_W) &&
         reg_text(k, L"Publisher", L"Edge Island contributors") &&
         RegSetValueExW(k, L"NoModify", 0, REG_DWORD, (BYTE *)&one, 4) == ERROR_SUCCESS&&
         RegSetValueExW(k, L"NoRepair", 0, REG_DWORD, (BYTE *)&one, 4) == ERROR_SUCCESS&&reg_text(k, L"DisplayName",
             PRODUCT);
    RegCloseKey(k);
    if (!ok && d == REG_CREATED_NEW_KEY) {
        RegDeleteKeyW(HKEY_CURRENT_USER, UNINSTALL_KEY);
    }
    return ok;
}
static void progress(HWND window, int completed)
{
    if (window) { SendMessageW(window, SETUP_PROGRESS, completed, 0); }
}
int install_files(HINSTANCE instance, const Locations *destination, HWND window)
{
    int i, committed = 0, had_link = exists(destination->link), ok = 0;
    ZeroMemory(files, sizeof files);
    if (!make_directory(destination->programs) || !make_directory(destination->dir)) {
        return 0;
    }
    progress(window, 1);
    for (i = 0; i < FILE_COUNT; i++) {
        Item *f = &files[i];
        wchar_t tmp[80];
        if (!join_path(f->final, destination->dir, payload_names[i])) { goto done; }
        _snwprintf(tmp, 79, L".%ls.new", payload_names[i]);
        if (!join_path(f->stage, destination->dir, tmp)) { goto done; }
        _snwprintf(tmp, 79, L".%ls.previous", payload_names[i]);
        if (!join_path(f->backup, destination->dir, tmp)) { goto done; }
        if (exists(f->backup) && !exists(f->final) && !MoveFileExW(f->backup, f->final, MOVEFILE_WRITE_THROUGH)) {
            goto done;
        }
        if (!remove_file(f->backup) || !extract(instance, 101 + i, f->stage)) {
            goto done;
        }
        progress(window, 2 + i);
    }
    if (!close_installed(destination)) {
        goto done;
    }
    for (i = 0; i < FILE_COUNT; i++) {
        Item *f = &files[i];
        if (exists(f->final)) {
            if (!MoveFileExW(f->final, f->backup, MOVEFILE_WRITE_THROUGH)) {
                goto done;
            }
            f->replaced = 1;
        }
        if (!MoveFileExW(f->stage, f->final, MOVEFILE_WRITE_THROUGH)) {
            goto done;
        }
        f->put = 1;
        progress(window, 2 + FILE_COUNT + i);
    }
    if (!create_link(destination)) {
        goto done;
    }
    committed = 1;
    progress(window, SETUP_PROGRESS_MAX - 1);
    if (!register_uninstall(destination)) {
        goto done;
    }
    ok = 1;
done:
    for (i = FILE_COUNT - 1; i >= 0; i--) {
        Item *f = &files[i];
        if (!*f->final) {
            continue;
        }
        if (!ok) {
            if (f->put) {
                remove_file(f->final);
            }
            if (f->replaced) {
                MoveFileExW(f->backup, f->final, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            }
        } else {
            remove_file(f->backup);
        }
        if (*f->stage) { remove_file(f->stage); }
    }
    if (!ok) {
        if (committed && !had_link) {
            remove_file(destination->link);
        }
        RemoveDirectoryW(destination->dir);
    } else {
        progress(window, SETUP_PROGRESS_MAX);
    }
    return ok;
}
int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev, LPSTR args, int show)
{
    int quiet = has_arg(L"/quiet"), launch = !has_arg(L"/nolaunch");
    int installed;
    HANDLE lock;
    (void)prev;
    (void)args;
    (void)show;
    if (!locations(&paths)) {
        fail_ui(quiet, ui_text(ui_language(), L"Could not locate the current user's installation folder.",
                              L"找不到当前用户的安装文件夹。"));
        return 1;
    }
    if (!quiet) { return setup_wizard(instance, &paths, launch); }
    lock = install_lock();
    if (!lock) { return 3; }
    installed = install_files(instance, &paths, NULL);
    if (installed && launch) { start_file(paths.app, paths.dir); }
    ReleaseMutex(lock);
    CloseHandle(lock);
    return installed ? 0 : 1;
}
#ifdef EDGE_FREESTANDING
void edge_entry(void)
{
    ExitProcess((UINT)WinMain(GetModuleHandleW(NULL), NULL, NULL, SW_SHOW));
}
#endif

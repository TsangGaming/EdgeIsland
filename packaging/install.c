#include "install.h"
const wchar_t *payload_names[FILE_COUNT] = {L"EdgeIsland.exe", L"Uninstall.exe", L"LICENSE.txt", L"PRIVACY.md"};
extern HRESULT WINAPI SHGetFolderPathW(HWND, int, HANDLE, DWORD, wchar_t *);
#ifdef __TINYC__
extern HRESULT WINAPI CoInitializeEx(void *, DWORD);
extern void WINAPI CoUninitialize(void);
extern HRESULT WINAPI CoCreateInstance(const GUID *, void *, DWORD, const GUID *, void **);
#else
#include <objbase.h>
#endif
extern BOOL WINAPI QueryFullProcessImageNameW(HANDLE, DWORD, LPWSTR, PDWORD);
typedef struct ShellObj {
    void **vt;
} ShellObj;
typedef HRESULT(WINAPI *SetString)(ShellObj *, const wchar_t *);
typedef HRESULT(WINAPI *As)(ShellObj *, const GUID *, void **);
typedef HRESULT(WINAPI *Save)(ShellObj *, const wchar_t *, BOOL);
typedef ULONG(WINAPI *Drop)(ShellObj *);
int join_path(wchar_t *dst, const wchar_t *parent, const wchar_t *leaf)
{
    int n = lstrlenW(parent), k = lstrlenW(leaf);
    if (!n || n + k + 2 >= PATH_CAP) {
        return 0;
    }
    copy_text(dst, PATH_CAP, parent);
    if (dst[n - 1] != L'\\') {
        lstrcatW(dst, L"\\");
    }
    lstrcatW(dst, leaf);
    return 1;
}
int locations(Locations *p)
{
    wchar_t local[MAX_PATH], menu[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, 0x1c, NULL, 0, local)) || FAILED(SHGetFolderPathW(NULL, 2, NULL, 0, menu))) {
        return 0;
    }
    return join_path(p->programs, local, L"Programs") && join_path(p->dir, p->programs, L"EdgeIsland") &&
           join_path(p->app, p->dir, payload_names[0]) && join_path(p->uninstaller, p->dir, payload_names[1]) &&
           join_path(p->link, menu, L"Edge Island.lnk");
}
int exists(const wchar_t *p)
{
    return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}
int remove_file(const wchar_t *p)
{
    DWORD e;
    if (DeleteFileW(p)) {
        return 1;
    }
    e = GetLastError();
    return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND;
}
int make_directory(const wchar_t *p)
{
    DWORD a;
    if (CreateDirectoryW(p, NULL)) {
        return 1;
    }
    a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES&&(a &FILE_ATTRIBUTE_DIRECTORY) && !(a &FILE_ATTRIBUTE_REPARSE_POINT);
}
int has_arg(const wchar_t *arg)
{
    int i, n, yes = 0;
    wchar_t **v = CommandLineToArgvW(GetCommandLineW(), &n);
    if (!v) {
        return 0;
    }
    for (i = 1; i < n; i++)if (!lstrcmpiW(v[i], arg)) {
            yes = 1;
        }
    LocalFree(v);
    return yes;
}
HANDLE install_lock(void)
{
    HANDLE lock = CreateMutexW(NULL, TRUE, SETUP_MUTEX);
    if (lock && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(lock);
        return NULL;
    }
    return lock;
}
void fail_ui(int quiet, const wchar_t *m)
{
    if (!quiet) {
        MessageBoxW(NULL, m, PRODUCT, MB_ICONERROR | MB_OK);
    }
}
int close_installed(const Locations *p)
{
    HWND h = FindWindowW(CLASS_NAME, NULL);
    HANDLE proc;
    DWORD pid = 0, size = PATH_CAP;
    wchar_t actual[PATH_CAP];
    int ok;
    if (!h) {
        return 1;
    }
    GetWindowThreadProcessId(h, &pid);
    proc = OpenProcess(SYNCHRONIZE | 0x1000, FALSE, pid);
    if (!proc) {
        return 0;
    }
    if (!QueryFullProcessImageNameW(proc, 0, actual, &size)) {
        CloseHandle(proc);
        return 0;
    }
    if (lstrcmpiW(actual, p->app)) {
        CloseHandle(proc);
        return 1;
    }
    PostMessageW(h, WM_CLOSE, 0, 0);
    ok = WaitForSingleObject(proc, 8000) == WAIT_OBJECT_0;
    CloseHandle(proc);
    return ok;
}
int create_link(const Locations *p)
{
    static const GUID cls = {0x00021401, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
    static const GUID link_iid = {0x000214f9, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
    static const GUID persist_iid = {0x0000010b, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
    HRESULT init = CoInitializeEx(NULL, 2), hr;
    ShellObj *l = NULL, *f = NULL;
    if (FAILED(init)) {
        return 0;
    }
    hr = CoCreateInstance(&cls, NULL, 1, &link_iid, (void **)&l);
    if (SUCCEEDED(hr)) {
        hr = ((SetString)l->vt[20])(l, p->app);
    }
    if (SUCCEEDED(hr)) {
        hr = ((SetString)l->vt[9])(l, p->dir);
    }
    if (SUCCEEDED(hr)) {
        hr = ((As)l->vt[0])(l, &persist_iid, (void **)&f);
    }
    if (SUCCEEDED(hr)) {
        hr = ((Save)f->vt[6])(f, p->link, TRUE);
    }
    if (f) {
        ((Drop)f->vt[2])(f);
    }
    if (l) {
        ((Drop)l->vt[2])(l);
    }
    CoUninitialize();
    return SUCCEEDED(hr);
}
int reg_text(HKEY key, const wchar_t *name, const wchar_t *text)
{
    return RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)text,
                          (lstrlenW(text) + 1) * sizeof(wchar_t)) == ERROR_SUCCESS;
}
int start_file(const wchar_t *file, const wchar_t *dir)
{
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    int ok;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    ok = CreateProcessW(file, NULL, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    return ok;
}

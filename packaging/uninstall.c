#include "install.h"
#include "../src/language.h"
#include "../src/startup.h"
static Locations paths;
/* A running Windows image cannot generally unlink itself. Only when launched from the
   installed path, let the system shell wait for this PID and remove two fixed paths.
   No input path enters the command and no PowerShell policy is modified. */
static int finish_own_file(void)
{
    static wchar_t sys[MAX_PATH], shell[PATH_CAP], cmd[1500];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    if (!GetSystemDirectoryW(sys, COUNT(sys)) || !join_path(shell, sys, L"WindowsPowerShell\\v1.0\\powershell.exe")) {
        return 0;
    }
    _snwprintf(cmd, COUNT(cmd) - 1, L"\"%ls\" -NoLogo -NoProfile -NonInteractive -Command \""
               L"Wait-Process -Id %lu -ErrorAction SilentlyContinue; "
               L"$d=[IO.Path]::Combine([Environment]::GetFolderPath('LocalApplicationData'),'Programs','EdgeIsland'); "
               L"$u=[IO.Path]::Combine($d,'Uninstall.exe'); "
               L"$m=[Threading.Mutex]::new($false,'Local\\EdgeIsland.Setup.1'); "
               L"try { if($m.WaitOne(10000)){ try { "
               L"if(-not [IO.File]::Exists([IO.Path]::Combine($d,'EdgeIsland.exe'))){[IO.File]::Delete($u); "
               L"if([IO.Directory]::Exists($d) -and [IO.Directory]::GetFileSystemEntries($d).Length -eq 0){[IO.Directory]::Delete($d)} } "
               L"} finally {$m.ReleaseMutex()} } } finally {$m.Dispose()}\"", shell, GetCurrentProcessId());
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessW(shell, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        return 0;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 1;
}
static int erase_key(const wchar_t *key)
{
    LONG e = RegDeleteKeyW(HKEY_CURRENT_USER, key);
    return e == ERROR_SUCCESS || e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND;
}
int WINAPI WinMain(HINSTANCE ins, HINSTANCE prev, LPSTR args, int show)
{
    int language = ui_language();
    int quiet = has_arg(L"/quiet"), i, own, ok = 1;
    HANDLE lock;
    static wchar_t self[PATH_CAP], file[PATH_CAP], tmp[90];
    (void)ins;
    (void)prev;
    (void)args;
    (void)show;
    if (!locations(&paths) || !GetModuleFileNameW(NULL, self, COUNT(self))) {
        return 1;
    }
    if (!quiet &&
            MessageBoxW(NULL, ui_text(language, L"Uninstall Edge Island and remove this user's preferences and timer?",
                                      L"卸载 Edge Island，并删除当前用户的设置和计时器？"),
                        ui_text(language, L"Uninstall Edge Island", L"卸载 Edge Island"), MB_OKCANCEL | MB_ICONQUESTION) != IDOK) {
        return 2;
    }
    lock = install_lock();
    if (!lock) {
        fail_ui(quiet, ui_text(language, L"Another Edge Island installer or uninstaller is running.",
                              L"另一个 Edge Island 安装或卸载程序正在运行。"));
        return 3;
    }
    own = !lstrcmpiW(self, paths.uninstaller);
    if (!close_installed(&paths)) {
        CloseHandle(lock);
        fail_ui(quiet, ui_text(language, L"Close Edge Island and try uninstalling it again.",
                              L"请关闭 Edge Island，然后重新卸载。"));
        return 1;
    }
    if (!startup_set(0)) {
        CloseHandle(lock);
        fail_ui(quiet, ui_text(language, L"Could not remove the Windows sign-in entry. Check your permissions and try again.",
                              L"无法清理当前用户的登录启动项。请检查权限后重试。"));
        return 1;
    }
    for (i = 0; i < FILE_COUNT; i++) {
        if (!(i == 1 && own)) {
            join_path(file, paths.dir, payload_names[i]);
            if (!remove_file(file)) {
                ok = 0;
            }
        }
        _snwprintf(tmp, 89, L".%ls.new", payload_names[i]);
        join_path(file, paths.dir, tmp);
        if (!remove_file(file)) {
            ok = 0;
        }
        _snwprintf(tmp, 89, L".%ls.previous", payload_names[i]);
        join_path(file, paths.dir, tmp);
        if (!remove_file(file)) {
            ok = 0;
        }
    }
    if (ok && own) {
        ok = finish_own_file();
    }
    if (ok) {
        ok = remove_file(paths.link) && erase_key(APP_KEY) && erase_key(UNINSTALL_KEY);
        if (!own) {
            RemoveDirectoryW(paths.dir);
        }
    }
    if (!ok) {
        fail_ui(quiet, ui_text(language, L"Uninstall could not finish. Close Edge Island and run the standalone uninstaller again.",
                              L"卸载未完成。请关闭 Edge Island，再运行下载的独立卸载程序。"));
    } else if (!quiet) {
        MessageBoxW(NULL, ui_text(language, L"Edge Island has been uninstalled.", L"Edge Island 已卸载。"),
                    PRODUCT, MB_OK | MB_ICONINFORMATION);
    }
    ReleaseMutex(lock);
    CloseHandle(lock);
    return ok ? 0 : 1;
}
#ifdef EDGE_FREESTANDING
void edge_entry(void)
{
    ExitProcess((UINT)WinMain(GetModuleHandleW(NULL), NULL, NULL, SW_SHOW));
}
#endif

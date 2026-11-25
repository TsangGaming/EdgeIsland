#include "platform.h"
#include "startup.h"

static const wchar_t run_key[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t value_name[] = L"EdgeIsland";

/* Windows limits a Run command, including the quotes, to 260 characters. */
static int startup_command(wchar_t *command)
{
    DWORD n = GetModuleFileNameW(NULL, command + 1, MAX_PATH - 1);
    if (!n || n >= MAX_PATH - 1) {
        return 0;
    }
    command[0] = command[n + 1] = L'"';
    command[n + 2] = 0;
    return 1;
}
int startup_registered(void)
{
    HKEY k;
    wchar_t expected[MAX_PATH + 1], actual[MAX_PATH + 1];
    DWORD type = 0, bytes = sizeof actual;
    LONG err;
    if (!startup_command(expected) ||
        RegOpenKeyExW(HKEY_CURRENT_USER, run_key, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) {
        return 0;
    }
    err = RegQueryValueExW(k, value_name, NULL, &type, (BYTE *)actual, &bytes);
    RegCloseKey(k);
    return err == ERROR_SUCCESS && type == REG_SZ && bytes == (lstrlenW(expected) + 1) * sizeof(wchar_t) &&
           actual[bytes / sizeof(wchar_t) - 1] == 0 && !lstrcmpiW(actual, expected);
}
int startup_set(int enabled)
{
    HKEY k;
    LONG err;
    wchar_t cmd[MAX_PATH + 1];
    if (enabled) {
        if (!startup_command(cmd) ||
            RegCreateKeyExW(HKEY_CURRENT_USER, run_key, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS) {
            return 0;
        }
        err = RegSetValueExW(k, value_name, 0, REG_SZ, (const BYTE *)cmd,
                             (lstrlenW(cmd) + 1) * sizeof(wchar_t));
    } else {
        err = RegOpenKeyExW(HKEY_CURRENT_USER, run_key, 0, KEY_SET_VALUE, &k);
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            return 1;
        }
        /* A read-only Run key with no value for us needs no cleanup. */
        if (err == ERROR_ACCESS_DENIED &&
            RegOpenKeyExW(HKEY_CURRENT_USER, run_key, 0, KEY_QUERY_VALUE, &k) == ERROR_SUCCESS) {
            DWORD size = 0;
            err = RegQueryValueExW(k, value_name, NULL, NULL, NULL, &size);
            RegCloseKey(k);
            return err == ERROR_FILE_NOT_FOUND;
        }
        if (err != ERROR_SUCCESS) {
            return 0;
        }
        err = RegDeleteValueW(k, value_name);
    }
    RegCloseKey(k);
    return err == ERROR_SUCCESS || (!enabled && err == ERROR_FILE_NOT_FOUND);
}

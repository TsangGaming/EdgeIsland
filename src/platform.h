#ifndef EDGE_PLATFORM_H
#define EDGE_PLATFORM_H
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <shellapi.h>
#ifdef __TINYC__
/* The small TinyCC SDK only declares the shell execution functions. */
#ifndef NIM_ADD
typedef struct {
    DWORD cbSize;
    HWND hWnd;
    UINT uID, uFlags, uCallbackMessage;
    HICON hIcon;
    wchar_t szTip[128];
    DWORD dwState, dwStateMask;
    wchar_t szInfo[256];
    UINT uTimeout;
    wchar_t szInfoTitle[64];
    DWORD dwInfoFlags;
    GUID guidItem;
    HICON hBalloonIcon;
} NOTIFYICONDATAW;
#define NIM_ADD 0
#define NIM_MODIFY 1
#define NIM_DELETE 2
#define NIF_MESSAGE 1
#define NIF_ICON 2
#define NIF_TIP 4
#define NIF_INFO 16
#define NIIF_INFO 1
extern BOOL WINAPI Shell_NotifyIconW(DWORD, NOTIFYICONDATAW *);
#endif
extern ULONGLONG WINAPI GetTickCount64(void);
#endif
#include <wchar.h>
#include <stdio.h>
#include <string.h>
#include "model.h"
#define COUNT(a) ((int)(sizeof(a)/sizeof((a)[0])))
#define CLASS_NAME L"EdgeIsland.Overlay.1"
#define APP_KEY L"Software\\EdgeIsland"
#define UNINSTALL_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\EdgeIsland"
#define PRODUCT L"Edge Island"
#define MSG_MEDIA (WM_APP+2)
static void copy_text(wchar_t *dst, int n, const wchar_t *src)
{
    if (n > 0) {
        lstrcpynW(dst, src ? src : L"", n);
    }
}
static unsigned long long ft64(FILETIME ft)
{
    return ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}
static unsigned long long wall_ms(void)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ft64(ft) / 10000;
}
static void number2(wchar_t *out, int cap, int a, int b)
{
    _snwprintf(out, cap - 1, L"%02d:%02d", a, b);
    out[cap - 1] = 0;
}
#endif

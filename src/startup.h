#ifndef EDGE_STARTUP_H
#define EDGE_STARTUP_H
#ifdef EDGE_STORE_MSIX
#include <windows.h>
#endif
#ifdef __cplusplus
extern "C" {
#endif
#ifdef EDGE_STORE_MSIX
#define EDGE_STORE_STARTUP_RESULT (WM_APP+20)
/* toggle=0 queries; toggle=1 toggles the actual Windows state. Result is posted
   to hwnd: wParam 0 unknown, 1 disabled, 2 enabled; lParam 1 success, 0 failure. */
int startup_store_begin(HWND hwnd, int toggle);
#else
int startup_registered(void);
int startup_set(int enabled);
#endif
#ifdef __cplusplus
}
#endif
#endif

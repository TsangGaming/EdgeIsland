#ifndef EDGE_WIZARD_H
#define EDGE_WIZARD_H
#include "install.h"

#define IDD_SETUP_WIZARD 200
#define IDC_SETUP_HEADING 201
#define IDC_SETUP_BODY 202
#define IDC_SETUP_DETAIL 203
#define IDC_SETUP_LANGUAGE 204
#define IDC_SETUP_PATH 205
#define IDC_SETUP_PROGRESS 206
#define IDC_SETUP_LAUNCH 207
#define IDC_SETUP_BACK 208
#define WIZARD_RAIL_WIDTH 90
#define WIZARD_FOOTER_TOP 190

#define SETUP_PROGRESS (WM_APP + 10)
#define SETUP_COMPLETE (WM_APP + 11)
#define SETUP_DPI (WM_APP + 12)
#define SETUP_PROGRESS_MAX (FILE_COUNT * 2 + 3)

/* The caller holds the installer mutex while copying and registering the files. */
int install_files(HINSTANCE instance, const Locations *paths, HWND progress_window);
int setup_wizard(HINSTANCE instance, const Locations *paths, int launch);
#endif

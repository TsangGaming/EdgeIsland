#include "wizard.h"
#include "../src/language.h"
#include "../src/version.h"
#ifdef __TINYC__
/* The bundled Win32 headers omit commctrl.h. */
typedef struct { DWORD dwSize, dwICC; } INITCOMMONCONTROLSEX;
extern BOOL WINAPI InitCommonControlsEx(const INITCOMMONCONTROLSEX *);
#define ICC_PROGRESS_CLASS 0x20
#define PBM_SETPOS (WM_USER + 2)
#define PBM_SETRANGE32 (WM_USER + 6)
#else
#include <commctrl.h>
#endif

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

enum SetupPage { SETUP_WELCOME, SETUP_DESTINATION, SETUP_READY, SETUP_INSTALLING, SETUP_FINISHED, SETUP_FAILED };

typedef struct {
    HINSTANCE instance;
    const Locations *paths;
    enum SetupPage page;
    int language, offer_launch, result, language_saved;
    HFONT heading_font;
    HICON icon;
} SetupWizard;

static SetupWizard wizard;
#define TR(en, zh) ui_text(wizard.language, en, zh)

static void show_control(HWND dialog, int id, int visible)
{
    ShowWindow(GetDlgItem(dialog, id), visible ? SW_SHOWNA : SW_HIDE);
}

static void heading_font(HWND dialog)
{
    RECT size = {0, 0, 0, 13};
    HFONT font;
    MapDialogRect(dialog, &size);
    font = CreateFontW(-size.bottom, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH, TR(L"Segoe UI", L"Microsoft YaHei UI"));
    if (font) {
        SendMessageW(GetDlgItem(dialog, IDC_SETUP_HEADING), WM_SETFONT, (WPARAM)font, TRUE);
        if (wizard.heading_font) { DeleteObject(wizard.heading_font); }
        wizard.heading_font = font;
    }
}

static void setup_page(HWND dialog, enum SetupPage page, int focus_next)
{
    const wchar_t *title = L"", *body = L"", *detail = L"";
    const wchar_t *next = TR(L"&Next >", L"下一步(&N) >");
    wchar_t welcome_text[240];
    int show_path = page == SETUP_DESTINATION || page == SETUP_READY;

    wizard.page = page;
    switch (page) {
    case SETUP_WELCOME:
        title = TR(L"Welcome to Edge Island Setup", L"欢迎使用 Edge Island 安装向导");
        _snwprintf(welcome_text, COUNT(welcome_text) - 1,
                   TR(L"Setup will install version %ls for your Windows account.",
                      L"安装向导将为当前 Windows 用户安装 %ls 版。"), EDGE_VERSION_W);
#ifndef EDGE_SIGNING_CANDIDATE
        lstrcatW(welcome_text, TR(L"\r\n\r\nUnsigned test build.", L"\r\n\r\n未签名测试版。"));
#endif
        body = welcome_text;
        detail = TR(L"Setup &language", L"安装语言(&L)");
        break;
    case SETUP_DESTINATION:
        title = TR(L"Installation folder", L"安装位置");
        body = TR(L"Edge Island will be installed in the folder below for your Windows account.",
                  L"Edge Island 将安装到下方文件夹，供当前 Windows 用户使用。");
        break;
    case SETUP_READY:
        title = TR(L"Ready to install", L"准备安装");
        body = TR(L"Setup will copy the program files and add Edge Island to the Start menu.",
                  L"安装向导将复制程序文件，并在开始菜单添加 Edge Island。");
        detail = TR(L"Installation folder", L"安装文件夹");
        next = TR(L"&Install", L"安装(&I)");
        break;
    case SETUP_INSTALLING:
        title = TR(L"Installing Edge Island", L"正在安装 Edge Island");
        body = TR(L"Setup may close Edge Island briefly during an upgrade.",
                  L"覆盖升级期间，正在运行的 Edge Island 会暂时关闭。");
        detail = TR(L"Preparing files…", L"正在准备文件…");
        next = TR(L"&Install", L"安装(&I)");
        break;
    case SETUP_FINISHED:
        title = TR(L"Installation complete", L"安装完成");
        body = wizard.language_saved ? TR(L"You can open Edge Island from the Start menu.", L"现在可以从开始菜单打开 Edge Island。") :
               TR(L"Setup could not save your language. You can change it in Edge Island settings.",
                  L"安装向导未能保存语言。你可以在 Edge Island 设置中更改。");
        next = TR(L"&Finish", L"完成(&F)");
        break;
    case SETUP_FAILED:
        title = TR(L"Installation could not finish", L"安装未完成");
        body = wizard.result == 3 ?
               TR(L"Another Edge Island installer or uninstaller is running. Close it before trying again.",
                  L"另一个 Edge Island 安装或卸载程序正在运行。请关闭它后重试。") :
               TR(L"Close Edge Island and check that your Windows account can write to the installation folder, then try again.",
                  L"请关闭 Edge Island，确认当前 Windows 用户能写入安装文件夹，然后重试。");
        detail = TR(L"Installation folder", L"安装文件夹");
        show_path = 1;
        next = TR(L"&Retry", L"重试(&R)");
        break;
    }
    SetWindowTextW(dialog, TR(L"Edge Island Setup", L"Edge Island 安装向导"));
    SetDlgItemTextW(dialog, IDC_SETUP_HEADING, title);
    SetDlgItemTextW(dialog, IDC_SETUP_BODY, body);
    SetDlgItemTextW(dialog, IDC_SETUP_DETAIL, detail);
    SetDlgItemTextW(dialog, IDC_SETUP_LAUNCH, TR(L"&Open Edge Island", L"打开 Edge Island(&O)"));
    SetDlgItemTextW(dialog, IDC_SETUP_BACK, TR(L"< &Back", L"< 上一步(&B)"));
    SetDlgItemTextW(dialog, IDOK, next);
    SetDlgItemTextW(dialog, IDCANCEL, page == SETUP_FAILED ? TR(L"&Close", L"关闭(&C)") : TR(L"Cancel", L"取消"));
    show_control(dialog, IDC_SETUP_DETAIL, *detail != 0);
    show_control(dialog, IDC_SETUP_LANGUAGE, page == SETUP_WELCOME);
    show_control(dialog, IDC_SETUP_PATH, show_path);
    show_control(dialog, IDC_SETUP_PROGRESS, page == SETUP_INSTALLING);
    show_control(dialog, IDC_SETUP_LAUNCH, page == SETUP_FINISHED && wizard.offer_launch);
    show_control(dialog, IDC_SETUP_BACK, page != SETUP_FINISHED);
    show_control(dialog, IDCANCEL, page != SETUP_FINISHED);
    EnableWindow(GetDlgItem(dialog, IDC_SETUP_BACK), page == SETUP_DESTINATION || page == SETUP_READY);
    EnableWindow(GetDlgItem(dialog, IDOK), page != SETUP_INSTALLING);
    EnableWindow(GetDlgItem(dialog, IDCANCEL), page != SETUP_INSTALLING);
    if (focus_next) {
        SendMessageW(dialog, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(dialog, IDOK), TRUE);
    }
}

static void paint_wizard(HWND dialog)
{
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(dialog, &paint);
    RECT client, rail = {0, 0, WIZARD_RAIL_WIDTH, WIZARD_FOOTER_TOP};
    RECT footer, separator, icon_size = {0, 0, 0, 30};
    HBRUSH brush = (HBRUSH)GetStockObject(DC_BRUSH);
    GetClientRect(dialog, &client);
    MapDialogRect(dialog, &rail);
    MapDialogRect(dialog, &icon_size);
    footer = (RECT){0, rail.bottom, client.right, client.bottom};
    separator = (RECT){0, rail.bottom, client.right, rail.bottom + 1};
    FillRect(dc, &client, GetSysColorBrush(COLOR_WINDOW));
    SetDCBrushColor(dc, RGB(31, 26, 47));
    FillRect(dc, &rail, brush);
    if (wizard.icon) {
        int size = icon_size.bottom;
        DrawIconEx(dc, (rail.right - size) / 2, size, wizard.icon, size, size, 0, NULL, DI_NORMAL);
    }
    FillRect(dc, &footer, GetSysColorBrush(COLOR_3DFACE));
    FillRect(dc, &separator, GetSysColorBrush(COLOR_3DSHADOW));
    EndPaint(dialog, &paint);
}

static DWORD WINAPI install_worker(void *context)
{
    HWND dialog = (HWND)context;
    HANDLE lock = install_lock();
    int result = 3, saved = 1;
    if (lock) {
        result = install_files(wizard.instance, wizard.paths, dialog) ? 0 : 1;
        if (!result && wizard.language != ui_language()) {
            saved = save_ui_language(wizard.language);
        }
        ReleaseMutex(lock);
        CloseHandle(lock);
    }
    SendMessageW(dialog, SETUP_COMPLETE, result, saved);
    return result;
}

static void begin_install(HWND dialog)
{
    HANDLE thread;
    setup_page(dialog, SETUP_INSTALLING, 0);
    SendMessageW(GetDlgItem(dialog, IDC_SETUP_PROGRESS), PBM_SETPOS, 0, 0);
    InvalidateRect(dialog, NULL, FALSE);
    UpdateWindow(dialog);
    thread = CreateThread(NULL, 0, install_worker, dialog, 0, NULL);
    if (thread) {
        CloseHandle(thread);
    } else {
        wizard.result = 1;
        setup_page(dialog, SETUP_FAILED, 1);
    }
}

static void advance(HWND dialog)
{
    switch (wizard.page) {
    case SETUP_WELCOME: setup_page(dialog, SETUP_DESTINATION, 1); break;
    case SETUP_DESTINATION: setup_page(dialog, SETUP_READY, 1); break;
    case SETUP_READY:
    case SETUP_FAILED: begin_install(dialog); break;
    case SETUP_FINISHED:
        if (wizard.offer_launch &&
            SendMessageW(GetDlgItem(dialog, IDC_SETUP_LAUNCH), BM_GETCHECK, 0, 0) == BST_CHECKED &&
            !start_file(wizard.paths->app, wizard.paths->dir)) {
            MessageBoxW(dialog, TR(L"Edge Island was installed. Open it from the Start menu.",
                                   L"Edge Island 已安装。请从开始菜单打开。"), PRODUCT, MB_ICONINFORMATION);
        }
        EndDialog(dialog, 0);
        break;
    case SETUP_INSTALLING: break;
    }
}

static INT_PTR CALLBACK wizard_dialog(HWND dialog, UINT message, WPARAM wp, LPARAM lp)
{
    switch (message) {
    case WM_INITDIALOG: {
        HWND language_box = GetDlgItem(dialog, IDC_SETUP_LANGUAGE);
        SendMessageW(language_box, CB_ADDSTRING, 0, (LPARAM)L"English");
        SendMessageW(language_box, CB_ADDSTRING, 0, (LPARAM)L"简体中文");
        SendMessageW(language_box, CB_SETCURSEL, wizard.language, 0);
        SetDlgItemTextW(dialog, IDC_SETUP_PATH, wizard.paths->dir);
        SendMessageW(GetDlgItem(dialog, IDC_SETUP_LAUNCH), BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(GetDlgItem(dialog, IDC_SETUP_PROGRESS), PBM_SETRANGE32, 0, SETUP_PROGRESS_MAX);
        if (wizard.icon) {
            SendMessageW(dialog, WM_SETICON, ICON_BIG, (LPARAM)wizard.icon);
            SendMessageW(dialog, WM_SETICON, ICON_SMALL, (LPARAM)wizard.icon);
        }
        heading_font(dialog);
        setup_page(dialog, SETUP_WELCOME, 1);
        return FALSE;
    }
    case WM_ERASEBKGND:
        return TRUE;
    case WM_PAINT:
        paint_wizard(dialog);
        return TRUE;
    case WM_CTLCOLORBTN:
        if ((HWND)lp != GetDlgItem(dialog, IDC_SETUP_LAUNCH)) { break; }
        /* The launch checkbox shares the white content area. */
        /* fall through */
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wp, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor((HDC)wp, GetSysColor(COLOR_WINDOW));
        return (INT_PTR)GetSysColorBrush(COLOR_WINDOW);
    case WM_DPICHANGED:
        PostMessageW(dialog, SETUP_DPI, 0, 0);
        break;
    case SETUP_DPI:
        heading_font(dialog);
        InvalidateRect(dialog, NULL, TRUE);
        return TRUE;
    case SETUP_PROGRESS: {
        const wchar_t *status;
        if (wp >= SETUP_PROGRESS_MAX - 1) {
            status = TR(L"Registering Edge Island with Windows…", L"正在向 Windows 注册 Edge Island…");
        } else if (wp > FILE_COUNT + 1) {
            status = TR(L"Installing program files…", L"正在安装程序文件…");
        } else {
            status = TR(L"Preparing files…", L"正在准备文件…");
        }
        SetDlgItemTextW(dialog, IDC_SETUP_DETAIL, status);
        SendMessageW(GetDlgItem(dialog, IDC_SETUP_PROGRESS), PBM_SETPOS, wp, 0);
        return TRUE;
    }
    case SETUP_COMPLETE:
        wizard.result = (int)wp;
        wizard.language_saved = (int)lp;
        setup_page(dialog, wp ? SETUP_FAILED : SETUP_FINISHED, 1);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_SETUP_LANGUAGE:
            if (HIWORD(wp) == CBN_SELCHANGE && wizard.page == SETUP_WELCOME) {
                LRESULT selected = SendMessageW(GetDlgItem(dialog, IDC_SETUP_LANGUAGE), CB_GETCURSEL, 0, 0);
                if (selected == UI_ENGLISH || selected == UI_SIMPLIFIED_CHINESE) {
                    wizard.language = (int)selected;
                    heading_font(dialog);
                    setup_page(dialog, SETUP_WELCOME, 0);
                }
            }
            return TRUE;
        case IDC_SETUP_BACK:
            if (wizard.page == SETUP_DESTINATION) { setup_page(dialog, SETUP_WELCOME, 1); }
            else if (wizard.page == SETUP_READY) { setup_page(dialog, SETUP_DESTINATION, 1); }
            return TRUE;
        case IDOK:
            advance(dialog);
            return TRUE;
        case IDCANCEL:
            SendMessageW(dialog, WM_CLOSE, 0, 0);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        if (wizard.page != SETUP_INSTALLING) {
            EndDialog(dialog, wizard.page == SETUP_FINISHED ? 0 : wizard.page == SETUP_FAILED ? wizard.result : 2);
        }
        return TRUE;
    }
    return FALSE;
}

int setup_wizard(HINSTANCE instance, const Locations *paths, int launch)
{
    INITCOMMONCONTROLSEX controls = {sizeof controls, ICC_PROGRESS_CLASS};
    INT_PTR result;
    ZeroMemory(&wizard, sizeof wizard);
    wizard.instance = instance;
    wizard.paths = paths;
    wizard.language = ui_language();
    wizard.offer_launch = launch;
    wizard.icon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 64, 64, 0);
    result = InitCommonControlsEx(&controls) ?
             DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETUP_WIZARD), NULL, wizard_dialog, 0) : -1;
    if (wizard.heading_font) { DeleteObject(wizard.heading_font); }
    if (wizard.icon) { DestroyIcon(wizard.icon); }
    if (result == -1) {
        fail_ui(0, TR(L"Could not open the Edge Island Setup Wizard.", L"无法打开 Edge Island 安装向导。"));
        return 1;
    }
    return (int)result;
}

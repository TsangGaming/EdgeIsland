#include "island.h"
#include "audio.h"
#include "startup.h"
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif
#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#endif
extern BOOL WINAPI QueryFullProcessImageNameW(HANDLE, DWORD, LPWSTR, PDWORD);
extern HRESULT WINAPI GetDpiForMonitor(HMONITOR, int, UINT *, UINT *);
typedef struct {
    UINT version;
    void *callback;
    BOOL suppress_thread, suppress_codecs;
} GdiStart;
extern int WINAPI GdiplusStartup(ULONG_PTR *, const GdiStart *, void *);
extern void WINAPI GdiplusShutdown(ULONG_PTR);
#define TIMER_MAIN 10
#define TIMER_MOTION 11
#define TIMER_WAVE 12
#define TIMER_DRAG 13
#define TRAY_MSG (WM_APP+1)
#define SHOW_MSG (WM_APP+3)
#define FOREGROUND_MSG (WM_APP+4)
#define SETTINGS_VERSION 1
#define TR(en, zh) ui_text(island.language, en, zh)
Island island;
typedef struct {
    DWORD version, locked, eco, hide_full, sound, edge, fraction, minutes, state, seconds;
    unsigned long long deadline;
    wchar_t device[32];
} Preferences;
typedef struct {
    MONITORINFO base;
    wchar_t szDevice[32];
} DisplayInfo;
typedef char MonitorLayoutCheck[sizeof(DisplayInfo) == sizeof(MONITORINFOEXW) ? 1 : -1];
typedef struct {
    HMONITOR handle;
    DisplayInfo info;
} Screen;
static Screen displays[32];
static int display_count, screen_id, area_ready, tracking, gesture, down_id, moving, prevent_hide, menu_active;
static int pointer_inside;
static int drag_edge, drag_alt, pull_distance, wheel_delta, wave_period, motion_duration;
static Box pending_frame;
static unsigned long long last_drag_paint;
static int drag_frame_pending;
static unsigned long long motion_start, last_media, last_stats, last_power;
static unsigned long long last_clock_minute;
static int last_period, tray_ready, media_started;
static DWORD last_fg_pid;
static Box motion_from, motion_to, down_frame;
static POINT down_cursor;
static NOTIFYICONDATAW tray;
static HWINEVENTHOOK fg_hook;
static UINT taskbar_created;
static unsigned long long old_i, old_k, old_u;
static int have_times;
static ULONG_PTR gdip;

static void read_switch(HKEY key, const wchar_t *name, int *setting)
{
    DWORD value, type, bytes = sizeof value;
    if (RegQueryValueExW(key, name, NULL, &type, (BYTE *)&value, &bytes) == ERROR_SUCCESS &&
        type == REG_DWORD && bytes == sizeof value && value <= 1) {
        *setting = (int)value;
    }
}
static void load_settings(void)
{
    HKEY key;
    Preferences p;
    DWORD bytes = sizeof p, type = 0;
    island.locked = island.eco = island.hide_full = island.sound = island.first = 1;
    island.music_clock = island.hover_expand = 1;
    island.startup = startup_registered();
    island.anchor.edge = TOP;
    island.anchor.fraction = 5000;
    timer_select(&island.timer, 25);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, APP_KEY, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return;
    }
    if (RegQueryValueExW(key, L"Settings", NULL, &type, (BYTE *)&p, &bytes) == ERROR_SUCCESS && type == REG_BINARY &&
            bytes == sizeof p && p.version == SETTINGS_VERSION) {
        island.first = 0;
        island.locked = !!p.locked;
        island.eco = !!p.eco;
        island.hide_full = !!p.hide_full;
        island.sound = !!p.sound;
        island.anchor.edge = bounded(p.edge, TOP, LEFT_EDGE);
        island.anchor.fraction = bounded(p.fraction, 0, 10000);
        p.device[31] = 0;
        copy_text(island.device, COUNT(island.device), p.device);
        timer_select(&island.timer, bounded(p.minutes, 1, 180));
        island.timer.state = bounded(p.state, READY, FINISHED);
        island.timer.remaining = bounded(p.seconds, 0, 10800);
        island.timer.deadline = p.deadline;
    }
    read_switch(key, L"MediaClock", &island.music_clock);
    read_switch(key, L"HoverExpand", &island.hover_expand);
    RegCloseKey(key);
}
static int write_switch(const wchar_t *name, int enabled, int *setting)
{
    HKEY key;
    DWORD value = !!enabled;
    LONG err;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, APP_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS) {
        return 0;
    }
    err = RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof value);
    RegCloseKey(key);
    if (err == ERROR_SUCCESS) { *setting = (int)value; }
    return err == ERROR_SUCCESS;
}
static void save_settings(void)
{
    HKEY key;
    Preferences p;
    ZeroMemory(&p, sizeof p);
    p.version = SETTINGS_VERSION;
    p.locked = island.locked;
    p.eco = island.eco;
    p.hide_full = island.hide_full;
    p.sound = island.sound;
    p.edge = island.anchor.edge;
    p.fraction = island.anchor.fraction;
    p.minutes = island.timer.minutes;
    p.state = island.timer.state;
    p.seconds = island.timer.remaining;
    p.deadline = island.timer.deadline;
    copy_text(p.device, COUNT(p.device), island.device);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, APP_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(key, L"Settings", 0, REG_BINARY, (BYTE *)&p, sizeof p);
        RegCloseKey(key);
    }
}
static BOOL CALLBACK enumerate(HMONITOR m, HDC dc, LPRECT r, LPARAM unused)
{
    Screen *s;
    (void)dc;
    (void)r;
    (void)unused;
    if (display_count >= COUNT(displays)) {
        return FALSE;
    }
    s = &displays[display_count];
    s->handle = m;
    s->info.base.cbSize = sizeof(s->info);
    if (GetMonitorInfoW(m, (MONITORINFO *)&s->info)) {
        display_count++;
    }
    return TRUE;
}
static void monitors(void)
{
    int i, primary = 0, found = -1;
    display_count = 0;
    EnumDisplayMonitors(NULL, NULL, enumerate, 0);
    if (!display_count) {
        Screen *s = &displays[0];
        ZeroMemory(s, sizeof *s);
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &s->info.base.rcWork, 0);
        s->info.base.rcMonitor = s->info.base.rcWork;
        display_count = 1;
    }
    for (i = 0; i < display_count; i++) {
        if (displays[i].info.base.dwFlags & MONITORINFOF_PRIMARY) {
            primary = i;
        }
        if (*island.device && !lstrcmpW(displays[i].info.szDevice, island.device)) {
            found = i;
        }
    }
    screen_id = found < 0 ? primary : found;
}
static Box work(void)
{
    RECT r = displays[screen_id].info.base.rcWork;
    Box b = {r.left, r.top, r.right - r.left, r.bottom - r.top};
    return b;
}
static int unit(int x)
{
    return MulDiv(x, island.dpi, 96);
}
static void scale(void)
{
    UINT x = 96, y = 96;
    Box w = work();
    int next;
    if (displays[screen_id].handle) {
        GetDpiForMonitor(displays[screen_id].handle, 0, &x, &y);
    }
    next = bounded(x, 72, 480);
    if (w.w > 20 && (PANEL_W + 16) * next > w.w * 96) {
        next = w.w * 96 / (PANEL_W + 16);
    }
    if (w.h > 20 && (PANEL_H + 16) * next > w.h * 96) {
        next = w.h * 96 / (PANEL_H + 16);
    }
    next = bounded(next, 36, 480);
    if (next != island.dpi) {
        island.dpi = next;
        forget_fonts();
    }
}
static Box target(int expanded)
{
    return anchored(work(), unit(expanded ? PANEL_W : compact_width()), unit(expanded ? PANEL_H : 36),
                    unit(6), island.locked, island.anchor);
}
static void redraw(void)
{
    if (!island.hidden) {
        InvalidateRect(island.hwnd, NULL, FALSE);
    }
}
static void place(Box b)
{
    int resized = b.w != island.frame.w || b.h != island.frame.h;
    int repaint = resized || !area_ready;
    if (b.w < 1 || b.h < 1) {
        return;
    }
    if (b.x != island.frame.x || b.y != island.frame.y || resized || !area_ready) {
        island.frame = b;
        SetWindowPos(island.hwnd, HWND_TOPMOST, b.x, b.y, b.w, b.h,
                     SWP_NOACTIVATE | SWP_NOOWNERZORDER | (repaint ? SWP_NOCOPYBITS : 0));
        if (repaint) {
            int rad = b.h < unit(50) ? b.h : unit(48);
            HRGN rg = CreateRoundRectRgn(0, 0, b.w + 1, b.h + 1, rad, rad);
            if (rg && !SetWindowRgn(island.hwnd, rg, FALSE)) {
                DeleteObject(rg);
            }
        }
        area_ready = 1;
        if (repaint) { redraw(); }
    }
}
static void read_effects(void)
{
    BOOL value = TRUE;
    SystemParametersInfoW(0x103e, 0, &value, 0); /* SPI_GETCLIENTAREAANIMATION */
    island.effects = !!value;
}
static void sync_wave_timer(void)
{
    RECT rect;
    int period = 0;
    if (!island.hidden && !moving && !tracking && island.effects && wave_area(&rect)) {
        period = !island.eco ? 33 : island.ac == 1 ? 50 : 80;
    }
    if (period != wave_period) {
        if (period) {
            wave_period = SetTimer(island.hwnd, TIMER_WAVE, period, NULL) ? period : 0;
        } else {
            KillTimer(island.hwnd, TIMER_WAVE);
            wave_period = 0;
        }
    }
}
static unsigned long long animation_ms(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER n;
    if ((!freq.QuadPart && !QueryPerformanceFrequency(&freq)) || !QueryPerformanceCounter(&n)) {
        return GetTickCount64();
    }
    return n.QuadPart / freq.QuadPart * 1000 + (n.QuadPart % freq.QuadPart) * 1000 / freq.QuadPart;
}
static int same_box(Box a, Box b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}
static void layout(int animate)
{
    Box b;
    if (!display_count || !island.hwnd || tracking) {
        return;
    }
    b = target(island.expanded);
    if (animate && moving && same_box(motion_to, b)) {
        return;
    }
    if (animate && area_ready && island.effects && !island.hidden && !same_box(island.frame, b)) {
        int travel = b.h > island.frame.h ? b.h - island.frame.h : island.frame.h - b.h;
        motion_from = island.frame;
        motion_to = b;
        motion_start = animation_ms();
        motion_duration = 100 + bounded(MulDiv(travel, 95, unit(PANEL_H - 36)), 0, 95);
        if (SetTimer(island.hwnd, TIMER_MOTION, 15, NULL)) {
            moving = 1;
            sync_wave_timer();
            return;
        }
    }
    if (moving) { KillTimer(island.hwnd, TIMER_MOTION); }
    moving = 0;
    place(b);
    sync_wave_timer();
}
static void animate_frame(void)
{
    unsigned long long dt;
    int remaining;
    if (!moving) { return; }
    dt = animation_ms() - motion_start;
    if (dt >= (unsigned)motion_duration) {
        moving = 0;
        KillTimer(island.hwnd, TIMER_MOTION);
        place(motion_to);
        if (!same_box(motion_to, target(island.expanded))) { layout(1); }
        else { sync_wave_timer(); }
        redraw();
        return;
    }
    remaining = 1000 - (int)dt * 1000 / motion_duration;
    place(interpolate_box(motion_from, motion_to, 1000 - remaining * remaining * remaining / 1000000));
    UpdateWindow(island.hwnd);
}
static void cadence(void)
{
    int period = (island.hidden ? 10000 : island.expanded || island.timer.state == RUNNING || island.power_notice ||
                  island.volume_until ? 1000 : island.eco ? (island.ac == 1 ? 5000 : 10000) : 2500);
    if (!island.hidden && island.media.needs_retry && period > 2500) { period = 2500; }
    if (island.hidden && island.timer.state == RUNNING && island.timer.deadline > island.now) {
        unsigned long long left = island.timer.deadline - island.now;
        if (left < (unsigned)period) { period = bounded((int)left, 10, period); }
    }
    if (!island.hidden && (compact_activity() == 0 || island.music_clock)) {
        int until_minute = (int)(60000 - island.now % 60000);
        if (until_minute < period) { period = bounded(until_minute, 10, period); }
    }
    if (period != last_period) {
        SetTimer(island.hwnd, TIMER_MAIN, period, NULL);
        last_period = period;
    }
    sync_wave_timer();
}
static void tray_icon(void)
{
    ZeroMemory(&tray, sizeof tray);
    tray.cbSize = sizeof tray;
    tray.hWnd = island.hwnd;
    tray.uID = 1;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tray.uCallbackMessage = TRAY_MSG;
    tray.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1));
    if (!tray.hIcon) {
        tray.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    }
    copy_text(tray.szTip, COUNT(tray.szTip), PRODUCT);
    tray_ready = Shell_NotifyIconW(NIM_ADD, &tray);
}
static void complete_timer(void)
{
    save_settings();
    if (island.sound) {
        MessageBeep(MB_ICONINFORMATION);
    }
    if (tray_ready) {
        NOTIFYICONDATAW n = tray;
        n.uFlags = NIF_INFO;
        n.dwInfoFlags = NIIF_INFO | 0x10; /* NIIF_NOSOUND */
        copy_text(n.szInfoTitle, COUNT(n.szInfoTitle), TR(L"Edge Island · Timer complete", L"Edge Island · 计时完成"));
        copy_text(n.szInfo, COUNT(n.szInfo), TR(L"Your focus session has ended.", L"这段专注时间已结束。"));
        Shell_NotifyIconW(NIM_MODIFY, &n);
    }
}
static void sample_power(int notice)
{
    SYSTEM_POWER_STATUS p;
    int previous = island.ac, bat = island.battery;
    if (!GetSystemPowerStatus(&p)) {
        return;
    }
    island.ac = p.ACLineStatus == 255 ? -1 : p.ACLineStatus;
    island.battery = (p.BatteryFlag == 128 || p.BatteryLifePercent > 100) ? -1 : p.BatteryLifePercent;
    if (notice && (previous >= 0 && previous != island.ac || bat > 20 && island.battery >= 0 && island.battery <= 20 &&
                   island.ac == 0)) {
        island.power_notice = (previous != island.ac) ? 1 : 3;
        island.power_until = island.now + 6500;
    }
}
static void sample_stats(void)
{
    FILETIME i, k, u;
    MEMORYSTATUSEX mem;
    unsigned long long ii, kk, uu;
    if (GetSystemTimes(&i, &k, &u)) {
        ii = ft64(i);
        kk = ft64(k);
        uu = ft64(u);
        if (have_times) {
            island.cpu = cpu_load(ii, kk, uu, old_i, old_k, old_u);
            if (island.cpu >= 0) {
                if (island.graph_count == 48) {
                    memmove(island.graph, island.graph + 1, 47 * sizeof(int));
                    island.graph_count--;
                }
                island.graph[island.graph_count++] = island.cpu;
            }
        }
        old_i = ii;
        old_k = kk;
        old_u = uu;
        have_times = 1;
    }
    mem.dwLength = sizeof mem;
    if (GlobalMemoryStatusEx(&mem)) {
        island.ram = mem.dwMemoryLoad;
        island.ram_total = mem.ullTotalPhys;
        island.ram_used = mem.ullTotalPhys - mem.ullAvailPhys;
    }
}
static void foreground(void)
{
    HWND f = GetForegroundWindow();
    DWORD pid = 0;
    HANDLE process;
    wchar_t file[1024], *base, *ext;
    DWORD len = COUNT(file);
    if (!f || f == island.hwnd) {
        return;
    }
    GetWindowThreadProcessId(f, &pid);
    if (!pid || pid == last_fg_pid) {
        return;
    }
    last_fg_pid = pid;
    island.fg_start = island.now;
    island.foreground[0] = 0;
    island.foreground_explorer = 0;
    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return;
    }
    if (QueryFullProcessImageNameW(process, 0, file, &len)) {
        base = wcsrchr(file, L'\\');
        base = base ? base + 1 : file;
        ext = wcsrchr(base, L'.');
        if (ext) {
            *ext = 0;
        }
        if (!lstrcmpiW(base, L"chrome")) {
            base = L"Google Chrome";
        } else if (!lstrcmpiW(base, L"msedge")) {
            base = L"Microsoft Edge";
        } else if (!lstrcmpiW(base, L"explorer")) {
            island.foreground_explorer = 1;
        } else if (!lstrcmpiW(base, L"firefox")) {
            base = L"Firefox";
        }
        copy_text(island.foreground, COUNT(island.foreground), base);
    }
    CloseHandle(process);
}
static void check_full(void)
{
    HWND f = GetForegroundWindow();
    RECT r, s;
    wchar_t klass[64];
    int hide = 0;
    if (prevent_hide || menu_active) {
        return;
    }
    if (island.hide_full && f && f != island.hwnd && GetWindowRect(f, &r) &&
            MonitorFromWindow(f, MONITOR_DEFAULTTONEAREST) == displays[screen_id].handle) {
        GetClassNameW(f, klass, COUNT(klass));
        s = displays[screen_id].info.base.rcMonitor;
        hide = lstrcmpW(klass, L"Progman") && lstrcmpW(klass, L"WorkerW") && lstrcmpW(klass, L"Shell_TrayWnd") &&
               lstrcmpW(klass, L"Shell_SecondaryTrayWnd") &&
               r.left <= s.left&&r.top <= s.top&&r.right >= s.right&&r.bottom >= s.bottom;
    }
    if (hide != island.hidden) {
        island.hidden = hide;
        if (hide) {
            if (tracking) { ReleaseCapture(); }
            if (island.hover_expand && pointer_inside) { island.expanded = 0; }
            pointer_inside = 0;
        }
        media_suspend(hide);
        layout(0);
        ShowWindow(island.hwnd, hide ? SW_HIDE : SW_SHOWNOACTIVATE);
        cadence();
        if (!hide) {
            media_request(MEDIA_REFRESH, 0);
            redraw();
        }
    }
}
static void tick(int force)
{
    unsigned long long mono = GetTickCount64(), prev_minute = last_clock_minute;
    int stats_ms, media_ms, finished, sampled = 0, prev_bat = island.battery, prev_mode = compact_activity();
    island.now = wall_ms();
    last_clock_minute = island.now / 60000;
    finished = timer_tick(&island.timer, island.now);
    if (finished) {
        complete_timer();
    }
    if (force || mono - last_power >= 30000) {
        sample_power(!force);
        last_power = mono;
    }
    if (island.power_notice && island.now >= island.power_until) {
        island.power_notice = 0;
    }
    if (island.volume_until && island.now >= island.volume_until) {
        island.volume_until = 0;
    }
    check_full();
    stats_ms = island.eco ? (island.ac == 1 ? 2000 : 4000) : 1000;
    if (!island.hidden && island.expanded && (island.page == HOME || island.page == PERFORMANCE) && (force ||
            mono - last_stats >= (unsigned)stats_ms)) {
        sample_stats();
        sampled = 1;
        last_stats = mono;
    }
    media_ms = island.expanded&&(island.page == HOME ||
                                 island.page == MUSIC) ? (island.eco ? 8000 : 3000) : (island.media.events_ok ? (island.ac == 1 ? 45000 : 60000) :
                                     (island.eco ? (island.ac == 1 ? 10000 : 20000) : 6000));
    if (island.media.needs_retry) { media_ms = 2500; }
    if (!island.hidden && (force || mono - last_media >= (unsigned)media_ms)) {
        media_request(MEDIA_REFRESH, 0);
        last_media = mono;
    }
    if (!moving && !tracking && !island.hidden) {
        layout(0);
    }
    cadence();
    if (!island.hidden && (force || finished || sampled || prev_bat != island.battery || prev_mode != compact_activity() ||
            (island.expanded && (island.page == HOME || (island.page == MUSIC && island.media.playing))) ||
            (island.timer.state == RUNNING && (island.expanded ? island.page == FOCUS : compact_activity() == 2)) ||
            (prev_minute != island.now / 60000 && (island.expanded ? island.page == MUSIC && island.music_clock :
             compact_activity() == 0 || (compact_activity() == 1 && island.music_clock))))) {
        redraw();
    }
}
static void expand(int yes, int page, int activate)
{
    if (island.page != page) { discard_transition(); }
    island.expanded = yes;
    island.page = page;
    island.keyboard = A_NONE;
    island.hover = A_NONE;
    island.now = wall_ms();
    if (yes) {
        if (activate) { SetForegroundWindow(island.hwnd); }
        if (page == HOME || page == PERFORMANCE) { sample_stats(); }
        foreground();
        media_request(MEDIA_REFRESH, 0);
        last_media = GetTickCount64();
    }
    layout(1);
    cadence();
    redraw();
}
static int activity_page(void)
{
    int activity = compact_activity();
    return activity == 1 ? MUSIC : activity == 2 ? FOCUS : HOME;
}
static void pointer_presence(int inside)
{
    TRACKMOUSEEVENT event = {sizeof event, TME_LEAVE, island.hwnd, 0};
    if (tracking || menu_active || island.hidden) { return; }
    if (inside) { TrackMouseEvent(&event); }
    if (inside == pointer_inside) { return; }
    pointer_inside = inside;
    if (!island.hover_expand || island.expanded == inside) { return; }
    if (!inside) { prevent_hide = 0; }
    expand(inside, inside ? activity_page() : island.page, 0);
}
static void sync_pointer(void)
{
    POINT p;
    if (GetCursorPos(&p)) { pointer_presence(WindowFromPoint(p) == island.hwnd); }
}
static void reveal(void)
{
    prevent_hide = 1;
    island.hidden = 0;
    media_suspend(0);
    ShowWindow(island.hwnd, SW_SHOWNORMAL);
    expand(1, HOME, 1);
}
static void act(int id, int click_x)
{
    int mins[] = {5, 15, 25, 50}, i;
    island.now = wall_ms();
    if (id >= A_HOME && id <= A_OPTIONS) {
        island.page = id - A_HOME;
        if (island.page == OPTIONS) { island.startup = startup_registered(); }
        discard_transition();
        island.keyboard = A_NONE;
        tick(1);
        redraw();
        return;
    }
    if (id >= A_5 && id <= A_50) {
        if (island.timer.state == RUNNING || island.timer.state == PAUSED) {
            return;
        }
        timer_select(&island.timer, mins[id - A_5]);
        save_settings();
    }
    switch (id) {
    case A_OPEN:
        expand(1, activity_page(), 1);
        return;
    case A_COLLAPSE:
        prevent_hide = 0;
        expand(0, island.page, 0);
        return;
    case A_PLAY:
        media_request(MEDIA_TOGGLE, 0);
        break;
    case A_PREVIOUS:
        media_request(MEDIA_PREV, 0);
        break;
    case A_NEXT:
        media_request(MEDIA_NEXT, 0);
        break;
    case A_SEEK:
        for (i = 0; i < island.hit_count; i++)if (island.hits[i].id == id) {
                Hit h = island.hits[i];
                if (click_x >= 0) {
                    media_request(MEDIA_SEEK, bounded((click_x - h.x) * 1000 / h.w, 0, 1000));
                }
            }
        break;
    case A_TIMER:
        timer_toggle(&island.timer, island.now);
        island.compact_choice = CHOICE_TIMER;
        save_settings();
        break;
    case A_RESET:
        timer_select(&island.timer, island.timer.minutes);
        if (island.compact_choice == CHOICE_TIMER) { island.compact_choice = CHOICE_AUTO; }
        save_settings();
        break;
    case A_CENTER:
    case A_EDGE:
        island.locked = id == A_CENTER;
        save_settings();
        layout(1);
        break;
    case A_ECO:
        island.eco = !island.eco;
        save_settings();
        break;
    case A_FULLSCREEN:
        island.hide_full = !island.hide_full;
        prevent_hide = 0;
        save_settings();
        break;
    case A_SOUND:
        island.sound = !island.sound;
        save_settings();
        break;
    case A_MUSIC_CLOCK:
        if (!write_switch(L"MediaClock", !island.music_clock, &island.music_clock)) {
            MessageBoxW(island.hwnd, TR(L"Could not save the clock preference.", L"无法保存时钟设置。"), PRODUCT, MB_ICONINFORMATION);
        }
        break;
    case A_HOVER_MODE:
    case A_MANUAL_MODE:
        if (!write_switch(L"HoverExpand", id == A_HOVER_MODE, &island.hover_expand)) {
            MessageBoxW(island.hwnd, TR(L"Could not save the expansion mode.", L"无法保存展开方式。"), PRODUCT, MB_ICONINFORMATION);
        }
        break;
    case A_LANGUAGE_EN:
    case A_LANGUAGE_ZH: {
        int language = id == A_LANGUAGE_ZH ? UI_SIMPLIFIED_CHINESE : UI_ENGLISH;
        if (language == island.language) { break; }
        if (save_ui_language(language)) {
            island.language = language;
            discard_transition();
        } else {
            MessageBoxW(island.hwnd, TR(L"Could not save the language setting.", L"无法保存语言设置。"), PRODUCT, MB_ICONINFORMATION);
        }
        break;
    }
    case A_STARTUP:
        if (!startup_set(!startup_registered())) {
            MessageBoxW(island.hwnd, TR(L"Could not change the Windows sign-in setting. Check the app path and your permissions.",
                                       L"无法更改登录启动设置。请检查程序路径和当前用户的权限。"), PRODUCT, MB_ICONINFORMATION);
        }
        island.startup = startup_registered();
        break;
    case A_MONITOR:
        monitors();
        screen_id = (screen_id + 1) % display_count;
        copy_text(island.device, COUNT(island.device), displays[screen_id].info.szDevice);
        scale();
        save_settings();
        layout(0);
        break;
    case A_QUIT:
        PostMessageW(island.hwnd, WM_CLOSE, 0, 0);
        break;
    }
    cadence();
    redraw();
}
static void menu(void)
{
    POINT p;
    HMENU h = CreatePopupMenu();
    int sel;
    if (!h) {
        return;
    }
    AppendMenuW(h, MF_STRING, 1, TR(L"Open Edge Island", L"打开 Edge Island"));
    AppendMenuW(h, MF_STRING, 2, TR(L"Settings", L"设置"));
    AppendMenuW(h, MF_SEPARATOR, 0, NULL);
    AppendMenuW(h, MF_STRING | (island.locked ? MF_CHECKED : 0), 3, TR(L"Top center", L"固定上方正中"));
    AppendMenuW(h, MF_STRING | (!island.locked ? MF_CHECKED : 0), 4, TR(L"Dock to an edge", L"拖动到屏幕边缘"));
    AppendMenuW(h, MF_STRING, 5, TR(L"Switch display", L"切换显示器"));
    AppendMenuW(h, MF_SEPARATOR, 0, NULL);
    AppendMenuW(h, MF_STRING, 6, TR(L"Exit", L"退出"));
    GetCursorPos(&p);
    menu_active = 1;
    SetForegroundWindow(island.hwnd);
    sel = TrackPopupMenu(h, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, p.x, p.y, 0, island.hwnd, NULL);
    DestroyMenu(h);
    menu_active = 0;
    PostMessageW(island.hwnd, WM_NULL, 0, 0);
    if (sel == 1 || sel == 2) {
        pointer_inside = 0;
        reveal();
        if (sel == 2) {
            act(A_OPTIONS, -1);
        }
    } else if (sel == 3 || sel == 4) {
        act(sel == 3 ? A_CENTER : A_EDGE, -1);
    } else if (sel == 5) {
        act(A_MONITOR, -1);
    } else if (sel == 6) {
        act(A_QUIT, -1);
    }
    sync_pointer();
}
static void wheel(int direction)
{
    int choices[3], n = 0, i, current = compact_activity();
    choices[n++] = 0;
    if (island.media.available) {
        choices[n++] = 1;
    }
    if (island.timer.state != READY) {
        choices[n++] = 2;
    }
    for (i = 0; i < n; i++)if (choices[i] == current) {
            break;
        }
    current = choices[(i + n + (direction > 0 ? 1 : -1)) % n];
    island.compact_choice = current ? current : CHOICE_CLOCK;
    layout(0);
    redraw();
}
static void keyboard(WPARAM key)
{
    int i, n = island.hit_count, dir = GetKeyState(VK_SHIFT) < 0 ? -1 : 1;
    if (key == VK_ESCAPE) {
        act(A_COLLAPSE, -1);
        return;
    }
    if (key == VK_TAB && n) {
        for (i = 0; i < n; i++)if (island.hits[i].id == island.keyboard) {
                break;
            }
        if (i == n) {
            i = dir > 0 ? -1 : 0;
        }
        island.keyboard = island.hits[(i + dir + n) % n].id;
        redraw();
        return;
    }
    if (key == VK_SPACE || key == VK_RETURN) {
        act(island.keyboard ? island.keyboard : island.expanded ? A_COLLAPSE : A_OPEN, -1);
        return;
    }
    if (key == VK_LEFT || key == VK_RIGHT) {
        if (!island.expanded) {
            wheel(key == VK_RIGHT ? 1 : -1);
        } else if (island.keyboard == A_SEEK && island.media.end > island.media.begin) {
            double frac = (double)(island.media.position - island.media.begin) / (island.media.end - island.media.begin);
            media_request(MEDIA_SEEK, bounded((int)(1000 * frac) + (key == VK_RIGHT ? 50 : -50), 0, 1000));
        } else {
            act(A_HOME + (island.page + (key == VK_RIGHT ? 1 : 4)) % 5, -1);
        }
    }
}
static void cancel_drag_frame(void)
{
    if (drag_frame_pending) {
        KillTimer(island.hwnd, TIMER_DRAG);
        drag_frame_pending = 0;
    }
}
static void preview_drag(Box frame, int flush)
{
    unsigned long long now = animation_ms(), elapsed = now - last_drag_paint;
    pending_frame = frame;
    if (!flush && elapsed < 15) {
        if (drag_frame_pending || SetTimer(island.hwnd, TIMER_DRAG, 15 - (UINT)elapsed, NULL)) {
            drag_frame_pending = 1;
            return;
        }
    }
    cancel_drag_frame();
    place(frame);
    if (!island.hidden) { UpdateWindow(island.hwnd); }
    last_drag_paint = now;
}
static void move_drag(POINT p, int flush)
{
    int dx = p.x - down_cursor.x, dy = p.y - down_cursor.y;
    int lx = MulDiv(dx, 96, island.dpi), ly = MulDiv(dy, 96, island.dpi);
    if (gesture == G_PENDING) {
        if (down_id != A_NONE && down_id != A_OPEN) { return; }
        gesture = drag_gesture(drag_edge, lx, ly, island.locked, !island.expanded, drag_alt);
        if (gesture == G_PULL) {
            island.page = activity_page();
            island.hover = island.keyboard = A_NONE;
            discard_transition();
        }
    }
    if (gesture == G_PULL) {
        pull_distance = inward_distance(drag_edge, lx, ly);
        if (island.effects) {
            Box preview = interpolate_box(down_frame, target(1), bounded(pull_distance, 0, PULL_TRAVEL_DIP) * 1000 / PULL_TRAVEL_DIP);
            preview_drag(preview, flush);
        }
    } else if (gesture == G_MOVE) {
        Box b = down_frame;
        b.x += dx;
        b.y += dy;
        preview_drag(b, flush);
    }
}
static POINT screen_point(HWND hwnd, LPARAM lp)
{
    POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
    ClientToScreen(hwnd, &p);
    return p;
}
static void end_drag(POINT p)
{
    HMONITOR m = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
    int i;
    Box b = island.frame;
    for (i = 0; i < display_count; i++)if (displays[i].handle == m) {
            screen_id = i;
            break;
        }
    copy_text(island.device, COUNT(island.device), displays[screen_id].info.szDevice);
    scale();
    island.anchor = nearest_edge(work(), b.x, b.y, b.w, b.h, unit(6));
    save_settings();
    layout(1);
}
static void CALLBACK foreground_event(HWINEVENTHOOK hook, DWORD event, HWND hw, LONG obj, LONG child, DWORD thread,
                                      DWORD time)
{
    (void)hook;
    (void)event;
    (void)hw;
    (void)obj;
    (void)child;
    (void)thread;
    (void)time;
    PostMessageW(island.hwnd, FOREGROUND_MSG, 0, 0);
}
static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    int x = MulDiv((short)LOWORD(lp), 96, island.dpi ? island.dpi : 96), y = MulDiv((short)HIWORD(lp), 96,
            island.dpi ? island.dpi : 96);
    POINT p;
    if (taskbar_created && msg == taskbar_created) {
        tray_icon();
        return 0;
    }
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT r;
        HDC h = BeginPaint(hwnd, &ps);
        GetClientRect(hwnd, &r);
        if (r.right > 0 && r.bottom > 0) {
            paint_island(h, r.right, r.bottom, &ps.rcPaint);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_MOTION) {
            animate_frame();
        } else if (wp == TIMER_WAVE) {
            RECT rect;
            if (wave_period && wave_area(&rect)) { InvalidateRect(hwnd, &rect, FALSE); }
        } else if (wp == TIMER_DRAG && tracking && drag_frame_pending) {
            preview_drag(pending_frame, 1);
        } else if (wp == TIMER_MAIN) {
            tick(0);
        }
        return 0;
    case WM_KEYDOWN:
        keyboard(wp);
        return 0;
    case WM_LBUTTONDOWN:
        if (moving) {
            return 0;
        }
        SetFocus(hwnd);
        tracking = 1;
        gesture = G_PENDING;
        drag_edge = island.locked ? TOP : island.anchor.edge;
        drag_alt = GetKeyState(VK_MENU) < 0;
        pull_distance = 0;
        last_drag_paint = 0;
        down_id = hit_test(x, y);
        down_frame = island.frame;
        down_cursor = screen_point(hwnd, lp);
        SetCapture(hwnd);
        sync_wave_timer();
        return 0;
    case WM_MOUSEMOVE:
        if (tracking) {
            move_drag(screen_point(hwnd, lp), 0);
        } else {
            int h = hit_test(x, y);
            pointer_presence(1);
            if (h != island.hover) {
                island.hover = h;
                redraw();
            }
            SetCursor(LoadCursorW(NULL, h ? IDC_HAND : IDC_ARROW));
        }
        return 0;
    case WM_MOUSELEAVE:
        sync_pointer();
        island.hover = 0;
        redraw();
        return 0;
    case WM_LBUTTONUP:
        if (tracking) {
            p = screen_point(hwnd, lp);
            move_drag(p, 1);
            cancel_drag_frame();
            tracking = 0;
            ReleaseCapture();
            if (gesture == G_PULL) {
                expand(pull_distance >= PULL_OPEN_DIP, island.page, 1);
            } else if (gesture == G_MOVE) {
                end_drag(p);
            } else if (gesture == G_PENDING && down_id == hit_test(x, y)) {
                act(down_id, x);
            }
            sync_wave_timer();
            sync_pointer();
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (tracking) {
            tracking = 0;
            cancel_drag_frame();
            layout(1);
            sync_wave_timer();
            sync_pointer();
            redraw();
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (!tracking) {
            int delta = (short)HIWORD(wp);
            if ((wheel_delta < 0 && delta > 0) || (wheel_delta > 0 && delta < 0)) { wheel_delta = 0; }
            wheel_delta += delta;
            if (wheel_delta >= WHEEL_DELTA || wheel_delta <= -WHEEL_DELTA) {
                wheel_delta %= WHEEL_DELTA;
                if (!island.expanded && (LOWORD(wp) & MK_SHIFT)) { wheel(delta); }
                else if (!island.expanded && delta < 0) { act(A_OPEN, -1); }
                else if (island.expanded && delta > 0 && !(LOWORD(wp) & MK_SHIFT)) { act(A_COLLAPSE, -1); }
            }
        }
        return 0;
    case WM_CONTEXTMENU:
        menu();
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        if (island.hwnd) {
            read_effects();
            if (tracking) { ReleaseCapture(); }
            monitors();
            scale();
            layout(0);
            redraw();
        }
        return 0;
    case WM_DPICHANGED:
        if (!tracking && !moving && area_ready) {
            PostMessageW(hwnd, WM_DISPLAYCHANGE, 0, 0);
        }
        return 0;
    case WM_POWERBROADCAST:
        island.now = wall_ms();
        sample_power(1);
        tick(0);
        redraw();
        return TRUE;
    case FOREGROUND_MSG:
        island.now = wall_ms();
        if (GetForegroundWindow() != hwnd) {
            prevent_hide = 0;
        }
        foreground();
        check_full();
        if (island.expanded && island.page == HOME) {
            redraw();
        }
        return 0;
    case MSG_VOLUME:
        island.volume = (int)wp;
        island.muted = (int)lp;
        island.now = wall_ms();
        island.volume_until = island.now + 2500;
        if (!moving && !tracking && !island.hidden) {
            layout(0);
        }
        cadence();
        redraw();
        return 0;
    case MSG_REBIND_AUDIO:
        audio_rebind();
        return 0;
    case SHOW_MSG:
        reveal();
        return 0;
    case TRAY_MSG:
        if (lp == WM_RBUTTONUP) {
            menu();
        } else if (lp == WM_LBUTTONUP) {
            if (island.expanded && !island.hidden) {
                act(A_COLLAPSE, -1);
            } else {
                reveal();
            }
        }
        return 0;
    case MSG_MEDIA: {
        Media *m = (Media *)lp;
        HBITMAP old = island.media.cover;
        if (m->playing && (!island.media.playing || m->session_changed || lstrcmpW(m->title, island.media.title))) {
            island.compact_choice = CHOICE_AUTO;
        }
        if (m->cover_changed) {
            if (old) {
                DeleteObject(old);
            }
        } else {
            m->cover = old;
        }
        island.media = *m;
        HeapFree(GetProcessHeap(), 0, m);
        discard_transition();
        if (!moving && !tracking) {
            layout(1);
        }
        sync_wave_timer();
        cadence();
        redraw();
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_MAIN);
        KillTimer(hwnd, TIMER_MOTION);
        KillTimer(hwnd, TIMER_WAVE);
        cancel_drag_frame();
        if (tray_ready) {
            Shell_NotifyIconW(NIM_DELETE, &tray);
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR args, int show)
{
    WNDCLASSW cl;
    MSG msg;
    HANDLE single;
    GdiStart start = {1, NULL, FALSE, FALSE};
    int status;
    (void)previous;
    (void)args;
    (void)show;
    single = CreateMutexW(NULL, FALSE, L"Local\\EdgeIsland.Overlay.1");
    if (single && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND h = FindWindowW(CLASS_NAME, NULL);
        if (h) {
            PostMessageW(h, SHOW_MSG, 0, 0);
        }
        CloseHandle(single);
        return 0;
    }
    island.language = ui_language();
    if (GdiplusStartup(&gdip, &start, NULL)) {
        MessageBoxW(NULL, TR(L"Could not start the Windows graphics service.", L"无法启动 Windows 图形服务。"), PRODUCT, MB_ICONERROR);
        return 1;
    }
    load_settings();
    read_effects();
    island.cpu = island.ram = island.battery = island.ac = -1;
    island.dpi = 96;
    island.now = wall_ms();
    sample_power(0);
    monitors();
    scale();
    island.expanded = island.first && !island.hover_expand;
    island.page = HOME;
    ZeroMemory(&cl, sizeof cl);
    cl.lpfnWndProc = window_proc;
    cl.hInstance = instance;
    cl.lpszClassName = CLASS_NAME;
    cl.hCursor = LoadCursorW(NULL, IDC_ARROW);
    cl.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    RegisterClassW(&cl);
    island.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, CLASS_NAME, PRODUCT, WS_POPUP, 0, 0, 1, 1, NULL,
                                  NULL, instance, NULL);
    if (!island.hwnd) {
        MessageBoxW(NULL, TR(L"Could not create the desktop overlay.", L"无法建立灵动岛窗口。"), PRODUCT, MB_ICONERROR);
        return 2;
    }
    taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    layout(0);
    tray_icon();
    audio_start(island.hwnd);
    media_started = media_start(island.hwnd);
    fg_hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, foreground_event, 0, 0,
                              WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    foreground();
    sample_stats();
    tick(1);
    if (!island.hidden) {
        ShowWindow(island.hwnd, SW_SHOWNOACTIVATE);
        sync_pointer();
    }
    UpdateWindow(island.hwnd);
    if (island.first) {
        save_settings();
    }
    while ((status = GetMessageW(&msg, NULL, 0, 0)) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (fg_hook) {
        UnhookWinEvent(fg_hook);
    }
    if (media_started) {
        media_stop();
    }
    if (island.media.cover) {
        DeleteObject(island.media.cover);
    }
    audio_stop();
    draw_release();
    GdiplusShutdown(gdip);
    if (single) {
        CloseHandle(single);
    }
    return status < 0 ? 1 : 0;
}
#ifdef EDGE_FREESTANDING
void edge_entry(void)
{
    ExitProcess((UINT)WinMain(GetModuleHandleW(NULL), NULL, NULL, SW_SHOW));
}
#endif

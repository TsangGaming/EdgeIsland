#include "island.h"
#define TR(en, zh) ui_text(island.language, en, zh)
#include "version.h"
/* Shapes use the system GDI+ renderer. Text uses ClearType and Windows font fallback. */
extern int WINAPI GdipCreateFromHDC(HDC, void **);
extern int WINAPI GdipDeleteGraphics(void *);
extern int WINAPI GdipGetDC(void *, HDC *);
extern int WINAPI GdipReleaseDC(void *, HDC);
extern int WINAPI GdipSetSmoothingMode(void *, int);
extern int WINAPI GdipCreateSolidFill(DWORD, void **);
extern int WINAPI GdipDeleteBrush(void *);
extern int WINAPI GdipFillEllipseI(void *, void *, int, int, int, int);
extern int WINAPI GdipFillRectangleI(void *, void *, int, int, int, int);
extern int WINAPI GdipCreatePath(int, void **);
extern int WINAPI GdipAddPathArcI(void *, int, int, int, int, float, float);
extern int WINAPI GdipClosePathFigure(void *);
extern int WINAPI GdipFillPath(void *, void *, void *);
extern int WINAPI GdipDeletePath(void *);
extern int WINAPI GdipCreatePen1(DWORD, float, int, void **);
extern int WINAPI GdipDeletePen(void *);
extern int WINAPI GdipDrawLineI(void *, void *, int, int, int, int);
extern int WINAPI GdipDrawArcI(void *, void *, int, int, int, int, float, float);
static HDC dc;
static void *g;
typedef struct {
    HDC dc;
    HBITMAP bitmap;
    HGDIOBJ previous;
    int w, h;
} Surface;
static Surface frame, panel_image, pill_image;
static int snapshots_ready, last_w, last_h;
static struct {
    int px, weight, latin;
    HFONT handle;
} fonts[64];
static int nf;
enum { BG = 0x0d0e11, CARD = 0x1c1d22, SOFT = 0x282930, HOVER = 0x34353d, FG = 0xf5f4f7, MUTED = 0xaaa9b4,
       DIM = 0x83838e, VIOLET = 0xbfb0ff, MINT = 0xa2e5c3, AMBER = 0xf0c17d, WHITE = 0xffffff, DARK = 0x201b30
     };
static int px(int v)
{
    return MulDiv(v, island.dpi, 96);
}
static COLORREF rgb(int c)
{
    return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255);
}
static DWORD argb(int c)
{
    return 0xff000000u | (DWORD)c;
}
static void release_surface(Surface *s)
{
    if (s->dc) {
        SelectObject(s->dc, s->previous);
        DeleteObject(s->bitmap);
        DeleteDC(s->dc);
        ZeroMemory(s, sizeof *s);
    }
}
static int reserve(Surface *s, HDC target, int w, int h)
{
    HBITMAP bitmap;
    HGDIOBJ previous;
    HDC mem;
    if (s->dc && w <= s->w && h <= s->h) {
        return 1;
    }
    mem = CreateCompatibleDC(target);
    bitmap = mem ? CreateCompatibleBitmap(target, w, h) : NULL;
    previous = bitmap ? SelectObject(mem, bitmap) : NULL;
    if (!previous || previous == HGDI_ERROR) {
        if (bitmap) { DeleteObject(bitmap); }
        if (mem) { DeleteDC(mem); }
        return 0;
    }
    release_surface(s);
    s->dc = mem;
    s->bitmap = bitmap;
    s->previous = previous;
    s->w = w;
    s->h = h;
    return 1;
}
void discard_transition(void)
{
    snapshots_ready = 0;
}
void forget_fonts(void)
{
    int i;
    for (i = 0; i < nf; i++) {
        DeleteObject(fonts[i].handle);
    }
    nf = 0;
    discard_transition();
    last_w = last_h = 0;
}
void draw_release(void)
{
    forget_fonts();
    release_surface(&frame);
    release_surface(&panel_image);
    release_surface(&pill_image);
}
static HFONT font(int size, int weight, int latin)
{
    int i, z = px(size);
    for (i = 0; i < nf; i++)if (fonts[i].px == z && fonts[i].weight == weight && fonts[i].latin == latin) {
            return fonts[i].handle;
        }
    if (nf >= COUNT(fonts)) {
        return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
    fonts[nf].px = z;
    fonts[nf].weight = weight;
    fonts[nf].latin = latin;
    fonts[nf].handle = CreateFontW(-z, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, latin ? L"Segoe UI" : L"Microsoft YaHei UI");
    return fonts[nf++].handle;
}
static void txt(int x, int y, int w, int h, int size, int weight, int col, const wchar_t *s, int flags)
{
    RECT r = {px(x), px(y), px(x + w), px(y + h)};
    HGDIOBJ old;
    HDC text_dc = dc;
    if (!s || !*s || w <= 0) {
        return;
    }
    if (g && GdipGetDC(g, &text_dc)) { return; }
    SetBkMode(text_dc, TRANSPARENT);
    old = SelectObject(text_dc, font(size, weight, (flags & 16) != 0 || island.language == UI_ENGLISH));
    SetTextColor(text_dc, rgb(col));
    DrawTextW(text_dc, s, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS | ((flags & 1) ? DT_CENTER :
              (flags & 2) ? DT_RIGHT : DT_LEFT));
    SelectObject(text_dc, old);
    if (g) { GdipReleaseDC(g, text_dc); }
}
static void shape(int x, int y, int w, int h, int r, int color)
{
    void *brush = NULL, *path = NULL;
    int a = px(x), b = px(y), c = px(x + w) - a, d = px(y + h) - b, diam = px(r * 2);
    if (w <= 0 || h <= 0 || !g || GdipCreateSolidFill(argb(color), &brush)) {
        return;
    }
    if (diam > c) {
        diam = c;
    }
    if (diam > d) {
        diam = d;
    }
    if (r == 0) {
        GdipFillRectangleI(g, brush, a, b, c, d);
    } else if (c == d && diam == c) {
        GdipFillEllipseI(g, brush, a, b, c, d);
    } else if (!GdipCreatePath(0, &path)) {
        GdipAddPathArcI(path, a, b, diam, diam, 180, 90);
        GdipAddPathArcI(path, a + c - diam, b, diam, diam, 270, 90);
        GdipAddPathArcI(path, a + c - diam, b + d - diam, diam, diam, 0, 90);
        GdipAddPathArcI(path, a, b + d - diam, diam, diam, 90, 90);
        GdipClosePathFigure(path);
        GdipFillPath(g, brush, path);
        GdipDeletePath(path);
    }
    GdipDeleteBrush(brush);
}
static void line(int x1, int y1, int x2, int y2, int col, float thick)
{
    void *pen = NULL;
    if (g && !GdipCreatePen1(argb(col), thick * island.dpi / 96.f, 2, &pen)) {
        GdipDrawLineI(g, pen, px(x1), px(y1), px(x2), px(y2));
        GdipDeletePen(pen);
    }
}
static void arc(int x, int y, int n, int col, float start, float sweep, float thick)
{
    void *p = NULL;
    if (g && !GdipCreatePen1(argb(col), thick * island.dpi / 96.f, 2, &p)) {
        GdipDrawArcI(g, p, px(x), px(y), px(n), px(n), start, sweep);
        GdipDeletePen(p);
    }
}
static void point(int x, int y, int size, int color)
{
    shape(x, y, size, size, size, color);
}
static void paint_bars(HDC dst, int x, int y, int color, int active)
{
    int i;
    unsigned long long time = active && island.effects ? GetTickCount64() : 0;
    HBRUSH brush = (HBRUSH)GetStockObject(DC_BRUSH);
    SetDCBrushColor(dst, rgb(color));
    for (i = 0; i < 5; i++) {
        int h = active ? wave_height(time, i) : (i % 2 ? 7 : 4);
        int left = x + i * 5, top = y - h / 2;
        RECT r = {px(left), px(top), px(left + 3), px(top + h)};
        FillRect(dst, &r, brush);
    }
}
static void waves(int x, int y, int color, int active)
{
    HDC dst = dc;
    if (!g || !GdipGetDC(g, &dst)) {
        paint_bars(dst, x, y, color, active);
        if (g) { GdipReleaseDC(g, dst); }
    }
}
static void note(int x, int y, int c)
{
    line(x + 4, y + 4, x + 15, y + 1, c, 2);
    line(x + 4, y + 4, x + 4, y + 15, c, 2);
    line(x + 15, y + 1, x + 15, y + 12, c, 2);
    shape(x - 2, y + 13, 7, 5, 3, c);
    shape(x + 9, y + 10, 7, 5, 3, c);
}
static void clock_icon(int x, int y, int color)
{
    arc(x, y, 15, color, 0, 360, 1.6f);
    line(x + 7, y + 3, x + 7, y + 8, color, 1.6f);
    line(x + 7, y + 8, x + 11, y + 10, color, 1.6f);
}
static int add(int id, int x, int y, int w, int h)
{
    Hit *p;
    if (island.hit_count >= COUNT(island.hits)) {
        return 0;
    }
    p = &island.hits[island.hit_count++];
    p->id = id;
    p->x = x;
    p->y = y;
    p->w = w;
    p->h = h;
    return island.hover == id || island.keyboard == id;
}
int hit_test(int x, int y)
{
    int i;
    for (i = island.hit_count - 1; i >= 0; i--) {
        Hit p = island.hits[i];
        if (x >= p.x && x < p.x + p.w && y >= p.y && y < p.y + p.h) {
            return p.id;
        }
    }
    return A_NONE;
}
static void pill(int id, int x, int y, int w, int h, const wchar_t *label, int active, int enabled)
{
    int hi = enabled ? add(id, x, y, w, h) : 0;
    shape(x, y, w, h, h / 2, active ? VIOLET : hi ? HOVER : SOFT);
    txt(x + 2, y, w - 4, h, 13, 600, active ? DARK : enabled ? FG : DIM, label, 1);
}
static void percent(wchar_t *out, int n, int value)
{
    if (value < 0) {
        copy_text(out, n, L"—");
    } else {
        _snwprintf(out, n - 1, L"%d%%", value);
    }
    out[n - 1] = 0;
}
static void clock_text(wchar_t *out, int cap)
{
    SYSTEMTIME tm;
    GetLocalTime(&tm);
    number2(out, cap, tm.wHour, tm.wMinute);
}
static void date_text(wchar_t *out, int n)
{
    SYSTEMTIME t;
    static const wchar_t *zh_day[] = {L"日", L"一", L"二", L"三", L"四", L"五", L"六"};
    static const wchar_t *en_day[] = {L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"};
    static const wchar_t *month[] = {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
                                    L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
    GetLocalTime(&t);
    if (island.language == UI_SIMPLIFIED_CHINESE) {
        _snwprintf(out, n - 1, L"%u月%u日  星期%ls", t.wMonth, t.wDay, zh_day[t.wDayOfWeek]);
    } else {
        _snwprintf(out, n - 1, L"%ls, %ls %u", en_day[t.wDayOfWeek], month[t.wMonth - 1], t.wDay);
    }
    out[n - 1] = 0;
}
static void countdown(wchar_t *out, int n)
{
    int s = timer_left(&island.timer, island.now);
    number2(out, n, s / 60, s % 60);
}
static void header(void)
{
    const wchar_t *names[] = {
        TR(L"Home", L"概览"), TR(L"Media", L"媒体"), TR(L"Timer", L"计时"),
        TR(L"Stats", L"性能"), TR(L"Settings", L"设置")
    };
    int i;
    shape(16, 14, 290, 31, 16, CARD);
    for (i = 0; i < 5; i++) {
        int x = 18 + i * 57, sel = island.page == i, hi = add(A_HOME + i, x, 16, 56, 27);
        if (sel || hi) {
            shape(x, 16, 56, 27, 14, sel ? FG : HOVER);
        }
        txt(x, 16, 56, 27, 13, 600, sel ? BG : MUTED, names[i], 1);
    }
    if (add(A_COLLAPSE, 346, 11, 32, 34)) {
        shape(346, 12, 32, 32, 16, SOFT);
    }
    line(354, 30, 361, 24, MUTED, 1.7f);
    line(361, 24, 368, 30, MUTED, 1.7f);
}
static void cover(int x, int y, int size)
{
    if (island.media.cover) {
        HDC image_dc = dc;
        HDC mem;
        if (g && GdipGetDC(g, &image_dc)) { return; }
        mem = CreateCompatibleDC(image_dc);
        HGDIOBJ prev = SelectObject(mem, island.media.cover);
        SetStretchBltMode(image_dc, HALFTONE);
        SetBrushOrgEx(image_dc, 0, 0, NULL);
        StretchBlt(image_dc, px(x), px(y), px(size), px(size), mem, 0, 0, 128, 128, SRCCOPY);
        SelectObject(mem, prev);
        DeleteDC(mem);
        if (g) { GdipReleaseDC(g, image_dc); }
    } else {
        shape(x, y, size, size, 11, 0x343043);
        note(x + size / 2 - 7, y + size / 2 - 9, VIOLET);
    }
}
static void media_names(int x, int y, int w)
{
    Media *m = &island.media;
    txt(x, y, w, 24, 16, 650, FG,
        m->available ? (*m->title ? m->title : TR(L"Current media", L"当前媒体")) : TR(L"No media playing", L"尚无媒体播放"), 0);
    txt(x, y + 24, w, 20, 12, 400, MUTED,
        m->available ? (*m->artist ? m->artist : (m->playing ? TR(L"Playing", L"正在播放") : TR(L"Paused", L"已暂停"))) :
        (m->service_ok ? TR(L"Start playing in a browser or player", L"在浏览器或播放器开始播放") :
         TR(L"Waiting for Windows media", L"等待 Windows 媒体服务")), 0);
}
static int can_toggle(void)
{
    Media *m = &island.media;
    return m->available&&(m->can_toggle || (m->playing ? m->can_pause : m->can_play));
}
static void glyph_play(int x, int y, int color, int pause)
{
    if (pause) {
        shape(x - 6, y - 8, 4, 16, 1, color);
        shape(x + 2, y - 8, 4, 16, 1, color);
    } else {
        int i;
        for (i = 0; i < 10; i++) {
            line(x - 4 + i, y - 7 + i * 7 / 10, x - 4 + i, y + 7 - i * 7 / 10, color, 1.6f);
        }
    }
}
static void control(int id, int x, int y, int size, int enabled)
{
    int hi = enabled ? add(id, x, y, size, size) : 0, cx = x + size / 2, cy = y + size / 2, primary = id == A_PLAY;
    int col = primary ? (enabled ? BG : DIM) : (enabled ? FG : 0x56565e);
    shape(x, y, size, size, size / 2, primary ? (enabled ? (hi ? VIOLET : FG) : SOFT) : (hi ? HOVER : CARD));
    if (primary) {
        glyph_play(cx, cy, col, island.media.playing);
    } else {
        int dir = id == A_NEXT ? 1 : -1;
        int i;
        for (i = 0; i < 8; i++) {
            line(cx + dir*(i - 5), cy - 6 + i * 6 / 8, cx + dir*(i - 5), cy + 6 - i * 6 / 8, col, 1.5f);
        }
        line(cx + dir * 5, cy - 6, cx + dir * 5, cy + 6, col, 1.8f);
    }
}
static void timeline(int x, int y, int w)
{
    Media *m = &island.media;
    long long pos = m->position;
    int secs, total, ratio;
    wchar_t a[24], b[24];
    if (m->available && m->end > m->begin) {
        if (m->playing && island.now > m->observed && m->observed > 0) {
            pos += (long long)((long long)(island.now - m->observed) * m->rate * 10000.0);
        }
        if (pos < m->begin) {
            pos = m->begin;
        }
        if (pos > m->end) {
            pos = m->end;
        }
        secs = (int)((pos - m->begin) / 10000000);
        total = (int)((m->end - m->begin) / 10000000);
        ratio = (int)((double)(pos - m->begin) * w / (m->end - m->begin));
        number2(a, COUNT(a), secs / 60, secs % 60);
        number2(b, COUNT(b), total / 60, total % 60);
        if (m->can_seek && m->max_seek > m->min_seek) {
            add(A_SEEK, x, y - 5, w, 18);
        }
    } else {
        copy_text(a, COUNT(a), L"—");
        copy_text(b, COUNT(b), L"—");
        ratio = 0;
    }
    shape(x, y, w, 4, 2, SOFT);
    if (ratio) {
        shape(x, y, ratio, 4, 2, VIOLET);
    }
    txt(x, y + 6, 84, 18, 11, 500, DIM, a, 16);
    txt(x + w - 84, y + 6, 84, 18, 11, 500, DIM, b, 18);
}
static void home(void)
{
    wchar_t t[100], s[48];
    int cpu = island.cpu;
    const wchar_t *app_name = island.foreground;
    if (island.foreground_explorer) {
        app_name = TR(L"File Explorer", L"文件资源管理器");
    } else if (!*app_name) {
        app_name = island.fg_start ? TR(L"Active app", L"前台应用") : TR(L"Desktop", L"桌面");
    }
    clock_text(t, COUNT(t));
    txt(19, 52, 176, 55, 43, 650, FG, t, 16);
    date_text(t, COUNT(t));
    txt(22, 107, 177, 20, 13, 400, MUTED, t, 0);
    arc(213, 63, 43, SOFT, 0, 360, 4);
    if (cpu > 0) {
        arc(213, 63, 43, MINT, -90, 3.6f * cpu, 4);
    }
    point(231, 81, 7, MINT);
    percent(s, COUNT(s), cpu);
    _snwprintf(t, COUNT(t) - 1, L"CPU  %ls", s);
    txt(270, 59, 104, 27, 19, 650, FG, t, 16);
    percent(s, COUNT(s), island.ram);
    _snwprintf(t, COUNT(t) - 1, L"RAM  %ls", s);
    txt(271, 87, 103, 19, 13, 500, MUTED, t, 16);
    if (island.battery >= 0) {
        _snwprintf(t, COUNT(t) - 1, L"%ls %d%%", island.ac == 1 ? TR(L"Plugged in", L"已接电源") :
                   TR(L"Battery", L"电池"), island.battery);
    } else {
        copy_text(t, COUNT(t), island.ac == 1 ? TR(L"Plugged in", L"已接电源") : TR(L"Power unavailable", L"电源信息不可用"));
    }
    txt(207, 111, 166, 19, 12, 500, island.ac == 1 ? MINT : AMBER, t, 2);
    shape(17, 139, 362, 52, 14, CARD);
    shape(29, 150, 30, 30, 8, 0x30343c);
    line(36, 157, 50, 157, MINT, 1.8f);
    line(36, 157, 36, 168, MINT, 1.8f);
    line(36, 168, 50, 168, MINT, 1.8f);
    line(50, 157, 50, 168, MINT, 1.8f);
    line(40, 172, 46, 172, MINT, 1.5f);
    txt(69, 143, 199, 22, 14, 600, FG, app_name, 0);
    txt(69, 165, 208, 17, 11, 400, MUTED, TR(L"Foreground app", L"当前前台应用"), 0);
    {
        int fg = island.fg_start && island.now >= island.fg_start ? (int)((island.now - island.fg_start) / 1000) : 0;
        _snwprintf(t, COUNT(t) - 1, L"%02d:%02d:%02d", fg / 3600, (fg / 60) % 60, fg % 60);
    }
    txt(269, 150, 96, 30, 16, 600, FG, t, 18);
    cover(21, 201, 45);
    media_names(78, 201, island.media.available ? 259 : 294);
    if (island.media.available) {
        waves(348, 221, VIOLET, island.media.playing);
    }
    timeline(23, 256, 350);
    control(A_PREVIOUS, 121, 282, 35, island.media.can_prev);
    control(A_PLAY, 177, 276, 43, can_toggle());
    control(A_NEXT, 241, 282, 35, island.media.can_next);
}
static void music(void)
{
    Media *m = &island.media;
    wchar_t clock[8];
    cover(22, 64, 84);
    txt(121, 63, 137, 22, 12, 600, VIOLET,
        m->available ? (m->playing ? TR(L"Now playing", L"正在播放") : TR(L"Media connected", L"媒体已连接")) :
        TR(L"System media", L"系统媒体"), 0);
    if (m->available) {
        if (island.music_clock) {
            clock_text(clock, COUNT(clock));
            txt(264, 61, 68, 26, 15, 600, MUTED, clock, 18);
        }
        waves(348, 74, VIOLET, m->playing);
    }
    media_names(121, 88, 244);
    timeline(24, 181, 348);
    control(A_PREVIOUS, 99, 239, 49, m->can_prev);
    control(A_PLAY, 168, 227, 62, can_toggle());
    control(A_NEXT, 248, 239, 49, m->can_next);
}
static void focus(void)
{
    wchar_t t[40];
    int i, mins[] = {5, 15, 25, 50}, state = island.timer.state;
    txt(23, 59, 240, 22, 14, 600, AMBER, TR(L"Focus timer", L"专注计时"), 0);
    countdown(t, COUNT(t));
    txt(19, 78, 356, 88, 65, 600, FG, t, 17);
    txt(20, 164, 356, 22, 13, 500, MUTED,
        state == RUNNING ? TR(L"Counting down", L"倒计时进行中") : state == PAUSED ? TR(L"Paused", L"已暂停") :
        state == FINISHED ? TR(L"Time for a break", L"时间到，休息一下") : TR(L"Choose a duration", L"选择时长"), 1);
    for (i = 0; i < 4; i++) {
        _snwprintf(t, COUNT(t) - 1, TR(L"%d min", L"%d 分钟"), mins[i]);
        pill(A_5 + i, 24 + i * 90, 201, 78, 34, t, island.timer.minutes == mins[i], state != RUNNING&&state != PAUSED);
    }
    pill(A_TIMER, 43, 259, 201, 45, state == RUNNING ? TR(L"Pause", L"暂停") :
         state == PAUSED ? TR(L"Resume", L"继续") : state == FINISHED ? TR(L"Start again", L"再来一次") :
         TR(L"Start timer", L"开始计时"), 1, 1);
    pill(A_RESET, 253, 259, 99, 45, TR(L"Reset", L"重置"), 0, state != READY);
}
static void perf(void)
{
    wchar_t t[90], v[20];
    int i, n = island.graph_count;
    txt(23, 55, 181, 26, 16, 600, FG, TR(L"Processor", L"处理器"), 0);
    percent(v, COUNT(v), island.cpu);
    txt(242, 53, 131, 29, 22, 650, MINT, v, 18);
    shape(20, 91, 356, 74, 13, CARD);
    for (i = 0; i < 3; i++) {
        line(33, 108 + i * 20, 362, 108 + i * 20, 0x313237, 1);
    }
    for (i = 1; i < n; i++) {
        line(361 - (n - i) * 7, 152 - island.graph[i - 1] * 50 / 100, 361 - (n - i - 1) * 7,
             152 - island.graph[i] * 50 / 100, MINT, 2);
    }
    if (n) {
        point(358, 149 - island.graph[n - 1] * 50 / 100, 6, MINT);
    }
    txt(23, 173, 123, 27, 15, 600, FG, TR(L"Memory", L"内存"), 0);
    percent(v, COUNT(v), island.ram);
    if (island.ram_total) {
        _snwprintf(t, COUNT(t) - 1, L"%.1f / %.1f GB    %ls", (long long)island.ram_used / 1073741824.0,
                   (long long)island.ram_total / 1073741824.0, v);
    } else {
        copy_text(t, COUNT(t), L"—");
    }
    txt(129, 173, 244, 27, 14, 600, MUTED, t, 18);
    shape(24, 207, 348, 5, 3, SOFT);
    if (island.ram > 0) {
        shape(24, 207, 348 * island.ram / 100, 5, 3, VIOLET);
    }
    txt(23, 231, 97, 27, 15, 600, FG, TR(L"Power", L"电源"), 0);
    if (island.battery >= 0) {
        _snwprintf(t, COUNT(t) - 1, L"%ls  ·  %d%%", island.ac == 1 ? TR(L"Plugged in", L"已接电源") :
                   TR(L"On battery", L"使用电池"), island.battery);
    } else {
        copy_text(t, COUNT(t), island.ac == 1 ? TR(L"Plugged in", L"已接电源") : TR(L"No battery reading", L"暂无电池读数"));
    }
    txt(123, 231, 250, 27, 14, 500, AMBER, t, 2);
    txt(24, 286, 348, 22, 12, 400, DIM,
        island.eco ? TR(L"Power-saving refresh", L"节能刷新已开启") : TR(L"Standard refresh", L"标准刷新"), 0);
}
static void options(void)
{
    int i;
    int vals[] = {island.eco, island.hide_full, island.sound, island.music_clock, island.startup};
    const wchar_t *labels[] = {
        TR(L"Power-saving refresh", L"节能刷新"), TR(L"Hide in full screen", L"全屏时自动隐藏"),
        TR(L"Play timer sound", L"计时结束提示音"), TR(L"Show clock during playback", L"播放音乐时显示时钟"),
        TR(L"Launch at Windows sign-in", L"登录 Windows 时自动启动")
    };
    txt(22, 47, 135, 24, 15, 600, FG, TR(L"Placement", L"摆放位置"), 0);
    pill(A_MONITOR, 259, 47, 115, 24, TR(L"Switch display", L"切换显示器"), 0, 1);
    pill(A_CENTER, 21, 74, 170, 29, TR(L"Top center", L"固定上方正中"), island.locked, 1);
    pill(A_EDGE, 202, 74, 172, 29, TR(L"Dock to an edge", L"拖动到屏幕边缘"), !island.locked, 1);
    txt(23, 109, 81, 25, 14, 600, FG, TR(L"Open with", L"展开方式"), 0);
    pill(A_HOVER_MODE, 109, 109, 121, 25, TR(L"Hover", L"鼠标悬停"), island.hover_expand, 1);
    pill(A_MANUAL_MODE, 235, 109, 139, 25, TR(L"Click or drag", L"点击或拖动"), !island.hover_expand, 1);
    txt(23, 139, 81, 25, 14, 600, FG, TR(L"Language", L"语言"), 0);
    pill(A_LANGUAGE_EN, 109, 139, 121, 25, L"English", island.language == UI_ENGLISH, 1);
    pill(A_LANGUAGE_ZH, 235, 139, 139, 25, L"简体中文", island.language == UI_SIMPLIFIED_CHINESE, 1);
    for (i = 0; i < COUNT(vals); i++) {
        int y = 168 + i * 24;
        int hi = add(A_ECO + i, 20, y, 355, 24);
        if (hi) {
            shape(20, y, 355, 24, 9, CARD);
        }
        txt(24, y, 281, 24, 13, 500, FG, labels[i], 0);
        shape(331, y + 3, 39, 18, 9, vals[i] ? VIOLET : SOFT);
        point(333 + (vals[i] ? 19 : 0), y + 5, 14, vals[i] ? DARK : MUTED);
    }
    pill(A_QUIT, 273, 295, 100, 29, TR(L"Quit", L"退出应用"), 0, 1);
#ifdef EDGE_SIGNING_CANDIDATE
    txt(22, 294, 243, 30, 12, 400, DIM, L"Edge Island  /  " EDGE_VERSION_W, 16);
#else
    txt(22, 294, 243, 30, 12, 400, DIM, TR(L"Edge Island " EDGE_VERSION_W L" · Unsigned",
                                        L"Edge Island " EDGE_VERSION_W L" · 未签名测试版"), 0);
#endif
}
int compact_activity(void)
{
    int mode = island.compact_choice;
    if (island.volume_until && island.now < island.volume_until) {
        return 4;
    }
    if (island.power_notice && island.now < island.power_until) {
        return 3;
    }
    if (mode == CHOICE_MEDIA && island.media.available) {
        return 1;
    }
    if (mode == CHOICE_TIMER && island.timer.state != READY) {
        return 2;
    }
    if (mode == CHOICE_CLOCK) {
        return 0;
    }
    if (island.media.available && island.media.playing) { return 1; }
    if (island.timer.state != READY) {
        return 2;
    }
    return island.media.available ? 1 : 0;
}
int compact_width(void)
{
    switch (compact_activity()) {
    case 1: return island.music_clock ? 316 : 260;
    case 2: return 170;
    case 3: case 4: return 188;
    default: return island.battery >= 0 ? 150 : 104;
    }
}
static void compact(int w, int h)
{
    int m = compact_activity();
    wchar_t t[100];
    int cy = h / 2;
    add(A_OPEN, 0, 0, w, h);
    if (m == 1) {
        cover(10, cy - 12, 24);
        txt(43, 0, w - (island.music_clock ? 150 : 90), h, 14, 600, FG,
            *island.media.title ? island.media.title : (island.media.playing ? TR(L"Playing", L"正在播放") :
            TR(L"Current media", L"当前媒体")), 0);
        if (island.music_clock) {
            clock_text(t, COUNT(t));
            txt(w - 103, 0, 55, h, 15, 600, MUTED, t, 18);
        }
        waves(w - 37, cy, VIOLET, island.media.playing);
    } else if (m == 2) {
        clock_icon(15, cy - 8, AMBER);
        countdown(t, COUNT(t));
        txt(42, 0, w - 100, h, 17, 650, FG, t, 16);
        txt(w - 64, 0, 49, h, 12, 500, AMBER,
            island.timer.state == PAUSED ? TR(L"Paused", L"暂停") : island.timer.state == FINISHED ? TR(L"Done", L"完成") :
            TR(L"Focus", L"专注"), 2);
    } else if (m == 4) {
        int v = island.muted ? 0 : island.volume;
        txt(12, 0, 46, h, 12, 600, MUTED, island.muted ? TR(L"Muted", L"静音") : TR(L"Volume", L"音量"), 1);
        shape(61, cy - 2, w - 119, 4, 2, SOFT);
        if (v) {
            shape(61, cy - 2, (w - 119)*v / 100, 4, 2, VIOLET);
        }
        percent(t, COUNT(t), v);
        txt(w - 52, 0, 38, h, 13, 600, FG, t, 18);
    } else if (m == 3) {
        txt(13, 0, w - 26, h, 14, 600, island.ac == 1 ? MINT : AMBER,
            island.power_notice == 3 ? TR(L"Low battery", L"电池电量偏低") :
            island.ac == 1 ? TR(L"Plugged in", L"已接上电源") : TR(L"On battery", L"正在使用电池"), 1);
    } else {
        clock_text(t, COUNT(t));
        point(16, cy - 3, 7, MINT);
        txt(33, 0, w - (island.battery >= 0 ? 92 : 43), h, 17, 650, FG, t, 16);
        if (island.battery >= 0) {
            percent(t, COUNT(t), island.battery);
            txt(w - 57, 0, 43, h, 12, 600, MUTED, t, 18);
        }
    }
}
static void fill_background(HDC dst, const RECT *r)
{
    SetDCBrushColor(dst, rgb(BG));
    FillRect(dst, r, (HBRUSH)GetStockObject(DC_BRUSH));
}
static void draw_scene(HDC dst, int width, int height, int panel)
{
    RECT r = {0, 0, width, height};
    int i;
    dc = dst;
    fill_background(dc, &r);
    GdipCreateFromHDC(dc, &g);
    if (g) {
        GdipSetSmoothingMode(g, 4);
    }
    island.hit_count = 0;
    if (!panel) {
        compact(MulDiv(width, 96, island.dpi), MulDiv(height, 96, island.dpi));
    } else {
        header();
        switch (island.page) {
        case HOME:
            home();
            break;
        case MUSIC:
            music();
            break;
        case FOCUS:
            focus();
            break;
        case PERFORMANCE:
            perf();
            break;
        case OPTIONS:
            options();
            break;
        }
    }
    for (i = 0; i < island.hit_count; i++)if (island.keyboard == island.hits[i].id) {
            Hit p = island.hits[i];
            line(p.x + p.w / 2 - 7, p.y + p.h - 3, p.x + p.w / 2 + 7, p.y + p.h - 3, AMBER, 2);
        }
    if (g) {
        GdipDeleteGraphics(g);
    }
    g = NULL;
    dc = NULL;
}
static int wave_center(int *x, int *y)
{
    if (!island.media.available) { return 0; }
    if (island.expanded) {
        if (island.page != MUSIC && island.page != HOME) { return 0; }
        *x = 348;
        *y = island.page == MUSIC ? 74 : 221;
    } else {
        if (compact_activity() != 1) { return 0; }
        *x = MulDiv(island.frame.w, 96, island.dpi) - 37;
        *y = 18;
    }
    return 1;
}
int wave_area(RECT *area)
{
    int x, y;
    if (!island.media.playing || !wave_center(&x, &y)) { return 0; }
    area->left = px(x - 1);
    area->right = px(x + 25);
    area->top = px(y - 12);
    area->bottom = px(y + 13);
    return 1;
}
static void blend_slice(HDC dst, HDC source, int dx, int dy, int w, int h, int sx, int sy, int alpha)
{
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, (BYTE)alpha, 0};
    if (alpha > 0 && w > 0 && h > 0) {
        if (alpha == 255 || !AlphaBlend(dst, dx, dy, w, h, source, sx, sy, w, h, blend)) {
            BitBlt(dst, dx, dy, w, h, source, sx, sy, SRCCOPY);
        }
    }
}
static int draw_transition(HDC target, int w, int h)
{
    int pw = px(PANEL_W), ph = px(PANEL_H), cw = px(compact_width()), ch = px(36);
    int visible = w < pw ? w : pw, pill_w = w < cw ? w : cw, pill_y;
    int t = bounded((h - ch) * 255 / (ph - ch), 0, 255);
    RECT client = {0, 0, w, h};
    if (!snapshots_ready) {
        if (!reserve(&panel_image, target, pw, ph) || !reserve(&pill_image, target, cw, ch)) {
            return 0;
        }
        draw_scene(panel_image.dc, pw, ph, 1);
        draw_scene(pill_image.dc, cw, ch, 0);
        snapshots_ready = 1;
    }
    fill_background(frame.dc, &client);
    /* Clip the unscaled panel so that its text stays sharp while the edge opens. */
    blend_slice(frame.dc, panel_image.dc, (w - visible) / 2, 0, visible, h,
                (pw - visible) / 2, 0, bounded(t * 2 - 30, 0, 255));
    pill_y = island.locked || island.anchor.edge == TOP ? 0 :
             island.anchor.edge == BOTTOM ? h - ch : (h - ch) / 2;
    blend_slice(frame.dc, pill_image.dc, (w - pill_w) / 2, pill_y, pill_w, ch,
                (cw - pill_w) / 2, 0, bounded(255 - t * 3, 0, 255));
    island.hit_count = 0;
    return 1;
}
void paint_island(HDC target, int w, int h, const RECT *dirty)
{
    RECT wave;
    int partial, x, y, transition = h > px(36) && h < px(PANEL_H);
    int panel_visible = h >= px(PANEL_H);
    int rw = transition ? px(PANEL_W) : w, rh = transition ? px(PANEL_H) : h;
    if (!reserve(&frame, target, rw > w ? rw : w, rh)) {
        draw_scene(target, w, h, panel_visible);
        last_w = last_h = 0;
        return;
    }
    partial = !transition && last_w == w && last_h == h && wave_area(&wave) &&
              dirty->left >= wave.left && dirty->right <= wave.right &&
              dirty->top >= wave.top && dirty->bottom <= wave.bottom;
    if (partial && wave_center(&x, &y)) {
        fill_background(frame.dc, &wave);
        paint_bars(frame.dc, x, y, VIOLET, 1);
    } else if (!transition || !draw_transition(target, w, h)) {
        draw_scene(frame.dc, w, h, panel_visible);
        if (snapshots_ready || panel_image.dc || pill_image.dc) {
            discard_transition();
            release_surface(&panel_image);
            release_surface(&pill_image);
        }
    }
    BitBlt(target, dirty->left, dirty->top, dirty->right - dirty->left, dirty->bottom - dirty->top,
           frame.dc, dirty->left, dirty->top, SRCCOPY);
    last_w = w;
    last_h = h;
}

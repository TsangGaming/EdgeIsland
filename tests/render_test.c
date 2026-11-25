/* Render through the shipping GDI renderer into a real 32-bit Windows DIB.
   The fake media session is confined to this test executable. */
#include "../src/island.h"
#include <stdlib.h>
Island island;
typedef struct { UINT version; void *hook; BOOL no_thread, no_codecs; } GdiStart;
extern int WINAPI GdiplusStartup(ULONG_PTR *, const GdiStart *, void *);
extern void WINAPI GdiplusShutdown(ULONG_PTR);

static void check(int ok, const char *message)
{
    if (!ok) { fprintf(stderr, "%s\n", message); exit(1); }
}
static unsigned digest(const DWORD *pixels, int stride, RECT r)
{
    unsigned out = 2166136261u;
    int x, y;
    for (y = r.top; y < r.bottom; y++) {
        for (x = r.left; x < r.right; x++) {
            out = (out ^ pixels[y * stride + x]) * 16777619u;
        }
    }
    return out;
}
static int dip(int n) { return MulDiv(n, island.dpi, 96); }
static void draw(HDC dc, int width, int height, const RECT *r)
{
    island.frame.w = width;
    island.frame.h = height;
    paint_island(dc, width, height, r);
    GdiFlush();
}
static void verify_dpi(int dpi)
{
    RECT pill, full, wave, outside, clock, intermediate, title, cover;
    BITMAPINFO info = {0};
    HDC dst = CreateCompatibleDC(NULL), screen = GetDC(NULL);
    HGDIOBJ original;
    HBITMAP dib;
    DWORD *pixels, *art_pixels;
    int w, h, i, changed = 0, hits;
    unsigned initial, other, paused, visible_clock, first_title, fallback, english_settings;
    island.dpi = dpi;
    island.expanded = 0;
    island.language = UI_ENGLISH;
    island.page = MUSIC;
    island.media.available = island.media.playing = island.music_clock = island.effects = 1;
    island.locked = 1;
    copy_text(island.media.title, COUNT(island.media.title), L"Local playback");
    w = dip(PANEL_W);
    h = dip(PANEL_H);
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
    check(dst && screen && dib && pixels, "GDI render target unavailable");
    original = SelectObject(dst, dib);
    full = (RECT){0, 0, w, h};
    island.timer.state = PAUSED;
    check(compact_activity() == 1, "An active song lost to a background timer");
    island.compact_choice = CHOICE_TIMER;
    check(compact_activity() == 2, "Explicit timer choice was ignored");
    island.compact_choice = CHOICE_AUTO;
    island.timer.state = READY;
    pill = (RECT){0, 0, dip(compact_width()), dip(36)};
    draw(dst, pill.right, pill.bottom, &pill);
    check(hit_test(10, 15) == A_OPEN && wave_area(&wave), "Missing compact media hitbox or bars");
    title = (RECT){dip(43), 0, dip(203), pill.bottom};
    cover = (RECT){dip(10), dip(6), dip(34), dip(30)};
    first_title = digest(pixels, w, title);
    fallback = digest(pixels, w, cover);
    copy_text(island.media.title, COUNT(island.media.title), L"Nevada");
    info.bmiHeader.biWidth = 128;
    info.bmiHeader.biHeight = -128;
    island.media.cover = CreateDIBSection(screen, &info, DIB_RGB_COLORS, (void **)&art_pixels, NULL, 0);
    check(island.media.cover && art_pixels, "Cannot create album art fixture");
    for (i = 0; i < 128*128; i++) { art_pixels[i] = 0x003a9c8b; }
    draw(dst, pill.right, pill.bottom, &pill);
    check(digest(pixels, w, title) != first_title, "Changing tracks did not change the compact song title");
    check(digest(pixels, w, cover) != fallback, "Compact capsule did not draw the supplied artwork");
    island.expanded = 1;
    draw(dst, pill.right, pill.bottom, &pill);
    check(hit_test(10, 15) == A_OPEN, "First expanding frame flashed the full header into a closed capsule");
    island.expanded = 0;
    outside = (RECT){0, 0, wave.left, pill.bottom};
    initial = digest(pixels, w, wave);
    other = digest(pixels, w, outside);
    for (i = 0; i < 10 && !changed; i++) {
        Sleep(45);
        draw(dst, pill.right, pill.bottom, &wave);
        changed = initial != digest(pixels, w, wave);
    }
    check(changed && other == digest(pixels, w, outside), "Playing indicator did not animate in isolation");
    island.media.playing = 0;
    draw(dst, pill.right, pill.bottom, &pill);
    paused = digest(pixels, w, wave);
    Sleep(60);
    draw(dst, pill.right, pill.bottom, &pill);
    check(!wave_area(&wave) && paused == digest(pixels, w, wave), "Paused indicator moved");
    island.media.playing = 1;
    island.effects = 0;
    draw(dst, pill.right, pill.bottom, &pill);
    paused = digest(pixels, w, wave);
    Sleep(70);
    draw(dst, pill.right, pill.bottom, &wave);
    check(paused == digest(pixels, w, wave), "Reduced motion should keep the indicator steady");
    island.effects = 1;
    intermediate = (RECT){0, 0, dip(330), dip(110)};
    draw(dst, intermediate.right, intermediate.bottom, &intermediate);
    check(island.hit_count == 0, "Transition exposed inactive panel buttons");
    draw(dst, w, h, &full);
    check(hit_test(272, 28) == A_OPTIONS, "Fully pulled panel reverted to the capsule before release");
    island.expanded = 1;
    draw(dst, w, h, &full);
    check(hit_test(272, 28) == A_OPTIONS && wave_area(&wave), "Expanded media header or bars missing");
    clock = (RECT){dip(265), dip(61), dip(333), dip(87)};
    visible_clock = digest(pixels, w, clock);
    island.music_clock = 0;
    draw(dst, w, h, &full);
    check(visible_clock != digest(pixels, w, clock) && compact_width() == 260, "Music clock switch did not affect layout");
    island.expanded = 0;
    pill.right = dip(compact_width());
    draw(dst, pill.right, pill.bottom, &pill);
    check(wave_area(&wave) && digest(pixels, w, cover) != fallback, "Removing the clock also removed media");
    initial = digest(pixels, w, wave);
    changed = 0;
    for (i = 0; i < 10 && !changed; i++) {
        Sleep(45);
        draw(dst, pill.right, pill.bottom, &wave);
        changed = initial != digest(pixels, w, wave);
    }
    check(changed, "Compact animation failed after closing the panel with the clock disabled");
    island.expanded = 1;
    island.page = OPTIONS;
    draw(dst, w, h, &full);
    check(hit_test(345, 252) == A_MUSIC_CLOCK && hit_test(344, 276) == A_STARTUP && !wave_area(&wave),
          "Settings hitboxes or animation visibility incorrect");
    hits = island.hit_count;
    check(hits >= 12, "Missing settings controls");
    check(hit_test(165, 122) == A_HOVER_MODE && hit_test(300, 122) == A_MANUAL_MODE, "Missing expansion mode controls");
    check(hit_test(165, 152) == A_LANGUAGE_EN && hit_test(300, 152) == A_LANGUAGE_ZH, "Missing language controls");
    clock = (RECT){dip(23), dip(139), dip(104), dip(164)};
    english_settings = digest(pixels, w, clock);
    island.language = UI_SIMPLIFIED_CHINESE;
    draw(dst, w, h, &full);
    check(digest(pixels, w, clock) != english_settings && hit_test(300, 152) == A_LANGUAGE_ZH,
          "Switching to Chinese did not update the Settings text in place");
    island.language = UI_ENGLISH;
    draw(dst, w, h, &full);
    check(digest(pixels, w, clock) == english_settings, "Switching back did not restore English");
    DeleteObject(island.media.cover);
    island.media.cover = NULL;
    SelectObject(dst, original);
    DeleteObject(dib);
    DeleteDC(dst);
    ReleaseDC(NULL, screen);
    draw_release();
}
int main(void)
{
    ULONG_PTR gdip;
    GdiStart init = {1, NULL, FALSE, FALSE};
    check(!GdiplusStartup(&gdip, &init, NULL), "Could not start GDI+");
    verify_dpi(96);
    verify_dpi(144);
    verify_dpi(192);
    GdiplusShutdown(gdip);
    puts("Language switching, music clock, playback, motion and hitboxes rendered at 100%, 150% and 200% DPI.");
    return 0;
}

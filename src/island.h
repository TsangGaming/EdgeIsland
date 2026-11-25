#ifndef EDGE_ISLAND_H
#define EDGE_ISLAND_H
#include "media.h"
#include "language.h"
enum Page { HOME, MUSIC, FOCUS, PERFORMANCE, OPTIONS };
enum CompactChoice { CHOICE_AUTO, CHOICE_MEDIA, CHOICE_TIMER, CHOICE_CLOCK };
enum Action { A_NONE, A_HOME, A_MUSIC, A_FOCUS, A_PERF, A_OPTIONS, A_COLLAPSE, A_PREVIOUS, A_PLAY, A_NEXT,
              A_SEEK, A_5, A_15, A_25, A_50, A_TIMER, A_RESET, A_CENTER, A_EDGE, A_ECO, A_FULLSCREEN, A_SOUND,
              A_MUSIC_CLOCK, A_STARTUP, A_MONITOR, A_QUIT, A_OPEN, A_HOVER_MODE, A_MANUAL_MODE,
              A_LANGUAGE_EN, A_LANGUAGE_ZH
            };
typedef struct {
    int id, x, y, w, h;
} Hit;
typedef struct {
    HWND hwnd;
    int dpi, expanded, page, locked, eco, hide_full, sound, hidden, compact_choice;
    int cpu, ram, battery, ac, power_notice, first, hover, keyboard;
    int volume, muted, music_clock, startup, effects, hover_expand, language, foreground_explorer;
    unsigned long long volume_until;
    int graph[48], graph_count;
    unsigned long long ram_used, ram_total, fg_start, power_until, now;
    wchar_t foreground[128], device[32];
    Anchor anchor;
    Timer timer;
    Media media;
    Hit hits[32];
    int hit_count;
    Box frame;
} Island;
extern Island island;
void paint_island(HDC target, int width, int height, const RECT *dirty);
int wave_area(RECT *area);
int compact_width(void);
void discard_transition(void);
void forget_fonts(void);
int hit_test(int x, int y);
int compact_activity(void); /* 0 clock, 1 media, 2 timer, 3 power */
void draw_release(void);
#endif

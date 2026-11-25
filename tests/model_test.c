#include "../src/model.h"
#include <assert.h>
#include <stdio.h>

static void timers(void)
{
    Timer t;
    timer_select(&t, 25);
    assert(t.remaining == 1500 && t.state == READY);
    timer_toggle(&t, 10000);
    assert(t.deadline == 1510000 && timer_left(&t, 10001) == 1500);
    assert(timer_left(&t, 11250) == 1499);
    timer_toggle(&t, 12200);
    assert(t.state == PAUSED&&t.remaining == 1498);
    assert(timer_left(&t, 9000000) == 1498);
    timer_toggle(&t, 9100000);
    assert(t.state == RUNNING);
    assert(!timer_tick(&t, t.deadline - 1) && timer_left(&t, t.deadline - 1) == 1);
    assert(timer_tick(&t, t.deadline) && t.state == FINISHED&&t.remaining == 0);
    assert(!timer_tick(&t, 99999999));
    timer_toggle(&t, 0);
    assert(t.deadline == 1500000);
    timer_select(&t, 0);
    assert(t.remaining == 60);
    timer_select(&t, 999);
    assert(t.minutes == 180);
}
static void geometry(void)
{
    Box monitors[] = {{0, 0, 1920, 1040}, {-2560, -90, 2560, 1390}, {0, -1080, 1366, 1040}, {37, 57, 760, 520}};
    int i, e, f, size;
    for (i = 0; i < 4; i++)for (e = 0; e < 4; e++)for (f = 0; f <= 10000; f += 125)for (size = 0; size < 2; size++) {
                    Anchor a = {e, f}, actual;
                    Box r = monitors[i], b, c, locked;
                    int w = size ? 396 : 150, h = size ? 334 : 36;
                    b = anchored(r, w, h, 6, 0, a);
                    assert(b.x >= r.x + 6 && b.x + b.w <= r.x + r.w - 6);
                    assert(b.y >= r.y + 6 && b.y + b.h <= r.y + r.h - 6);
                    actual = nearest_edge(r, b.x, b.y, w, h, 6);
                    c = anchored(r, w, h, 6, 0, actual);
                    assert(c.x - b.x >= -1 && c.x - b.x <= 1 && c.y - b.y >= -1 && c.y - b.y <= 1);
                    locked = anchored(r, w, h, 6, 1, a);
                    assert(locked.x == r.x + (r.w - w) / 2 && locked.y == r.y + 6);
                }
}
static void gestures_and_motion(void)
{
    int edge, p, i, k, varied = 0;
    Box closed = {-830, 7, 150, 36}, open = {-953, 7, PANEL_W, PANEL_H}, frame;
    for (edge = TOP; edge <= LEFT_EDGE; edge++) {
        int dx = edge == LEFT_EDGE ? 64 : edge == RIGHT_EDGE ? -64 : 0;
        int dy = edge == TOP ? 64 : edge == BOTTOM ? -64 : 0;
        assert(inward_distance(edge, dx, dy) >= PULL_OPEN_DIP);
        assert(drag_gesture(edge, dx, dy, 0, 1, 0) == G_PULL);
        assert(drag_gesture(edge, dx, dy, 0, 1, 1) == G_MOVE);
        assert(drag_gesture(edge, dx, dy, 0, 0, 0) == G_MOVE);
        assert(drag_gesture(edge, -dx, -dy, 1, 1, 0) == G_IGNORE);
        assert(drag_gesture(edge, 3, 5, 0, 1, 0) == G_PENDING);
    }
    assert(drag_gesture(TOP, 3, 20, 1, 1, 0) == G_PULL && inward_distance(TOP, 3, 20) < PULL_OPEN_DIP);
    assert(drag_gesture(TOP, 65, 5, 0, 1, 0) == G_MOVE);
    assert(drag_gesture(TOP, 65, 5, 1, 1, 0) == G_IGNORE);
    for (p = 0; p <= 1000; p++) {
        frame = interpolate_box(closed, open, p);
        assert(frame.w >= closed.w && frame.w <= open.w && frame.h >= closed.h && frame.h <= open.h);
        assert(frame.x <= closed.x && frame.x >= open.x && frame.y == open.y);
    }
    assert(interpolate_box(closed, open, -100).x == closed.x);
    assert(interpolate_box(closed, open, 1200).h == open.h);
    for (i = 0; i < 5; i++) {
        unsigned levels = 0;
        for (k = 0; k < 5000; k += 33) {
            int height = wave_height(k, i);
            assert(height >= 3 && height <= 22);
            levels |= 1u << height;
            if (i != 0 && height != wave_height(k, 0)) { varied = 1; }
        }
        assert(levels & (levels - 1)); /* Every bar actually changes during playback. */
    }
    assert(varied);
}
int main(void)
{
    timers();
    geometry();
    gestures_and_motion();
    assert(cpu_load(150, 160, 140, 100, 100, 100) == 50);
    assert(cpu_load(2, 2, 0, 0, 0, 0) == 0);
    assert(cpu_load(0, 2, 1, 0, 0, 0) == 100);
    assert(cpu_load(1, 1, 1, 1, 1, 1) == -1);
    assert(cpu_load(10, 2, 1, 0, 0, 0) == -1);
    puts("Passed timer transitions, CPU counters, 2,592 placements, four-edge pulls, interpolation and five animated bars.");
    return 0;
}

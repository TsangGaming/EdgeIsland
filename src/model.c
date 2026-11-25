#include "model.h"

int bounded(int x, int lo, int hi)
{
    return x < lo ? lo : x > hi ? hi : x;
}
static int distance(int a, int b)
{
    return a < b ? b - a : a - b;
}
Box anchored(Box work, int w, int h, int m, int locked, Anchor a)
{
    Box b;
    int along = bounded(a.fraction, 0, 10000);
    int sw = bounded(work.w - w - 2 * m, 0, 1000000), sh = bounded(work.h - h - 2 * m, 0, 1000000);
    b.w = w;
    b.h = h;
    if (locked) {
        a.edge = TOP;
        along = 5000;
    }
    b.x = work.x + m;
    b.y = work.y + m;
    if (a.edge == TOP || a.edge == BOTTOM) {
        b.x += (int)((long long)sw *along / 10000);
        if (a.edge == BOTTOM) {
            b.y += sh;
        }
    } else {
        b.y += (int)((long long)sh *along / 10000);
        if (a.edge == RIGHT_EDGE) {
            b.x += sw;
        }
    }
    return b;
}
Anchor nearest_edge(Box r, int x, int y, int w, int h, int m)
{
    int dx = bounded(r.w - w - 2 * m, 0, 1000000), dy = bounded(r.h - h - 2 * m, 0, 1000000);
    int sx = r.x + m, sy = r.y + m;
    int d[4] = {distance(y, sy), distance(x, sx + dx), distance(y, sy + dy), distance(x, sx)};
    Anchor a = {TOP, 5000};
    int i, span, pos;
    for (i = 1; i < 4; i++) if (d[i] < d[a.edge]) {
            a.edge = i;
        }
    span = (a.edge == TOP || a.edge == BOTTOM) ? dx : dy;
    pos = (a.edge == TOP || a.edge == BOTTOM) ? x - sx : y - sy;
    if (span) {
        a.fraction = (int)((long long)bounded(pos, 0, span) * 10000 / span);
    }
    return a;
}
int inward_distance(int edge, int dx, int dy)
{
    switch (edge) {
    case BOTTOM: return -dy;
    case LEFT_EDGE: return dx;
    case RIGHT_EDGE: return -dx;
    default: return dy;
    }
}
int drag_gesture(int edge, int dx, int dy, int locked, int can_pull, int force_move)
{
    int inward = inward_distance(edge, dx, dy);
    int along = edge == TOP || edge == BOTTOM ? dx : dy;
    if (distance(dx, 0) < 6 && distance(dy, 0) < 6) {
        return G_PENDING;
    }
    if (can_pull && (!force_move || locked) && inward >= 6 && inward >= distance(along, 0)) {
        return G_PULL;
    }
    return locked ? G_IGNORE : G_MOVE;
}
static int mix(int a, int b, int t)
{
    return a + (int)(((long long)b - a) * t / 1000);
}
Box interpolate_box(Box from, Box to, int amount)
{
    int t = bounded(amount, 0, 1000);
    Box b = {mix(from.x, to.x, t), mix(from.y, to.y, t),
             mix(from.w, to.w, t), mix(from.h, to.h, t)};
    return b;
}
int wave_height(unsigned long long ms, int bar)
{
    static const unsigned periods[] = {630, 835, 990, 715, 905};
    static const unsigned phases[] = {48, 271, 712, 440, 134};
    static const int peaks[] = {13, 19, 22, 16, 18};
    int i = bounded(bar, 0, 4);
    unsigned half = periods[i] / 2;
    unsigned pos = (unsigned)((ms % periods[i] + phases[i]) % periods[i]);
    int level = (int)((pos < half ? pos : periods[i] - pos) * 256 / half);
    level = level * level * (768 - 2 * level) / 65536;
    return 3 + (peaks[i] - 3) * level / 256;
}
void timer_select(Timer *t, int minutes)
{
    t->state = READY;
    t->minutes = bounded(minutes, 1, 180);
    t->remaining = t->minutes * 60;
    t->deadline = 0;
}
int timer_left(const Timer *t, unsigned long long now)
{
    unsigned long long ms;
    if (t->state != RUNNING) {
        return t->remaining;
    }
    if (t->deadline <= now) {
        return 0;
    }
    ms = t->deadline - now;
    return ms > 10800000ULL ? 10800 : (int)((ms + 999) / 1000);
}
void timer_toggle(Timer *t, unsigned long long now)
{
    if (t->state == RUNNING) {
        t->remaining = timer_left(t, now);
        t->state = t->remaining ? PAUSED : FINISHED;
        t->deadline = 0;
    } else {
        if (t->state != PAUSED) {
            t->remaining = t->minutes * 60;
        }
        t->deadline = now + (unsigned long long)t->remaining * 1000;
        t->state = RUNNING;
    }
}
int timer_tick(Timer *t, unsigned long long now)
{
    if (t->state == RUNNING && !timer_left(t, now)) {
        t->state = FINISHED;
        t->remaining = 0;
        t->deadline = 0;
        return 1;
    }
    return 0;
}
int cpu_load(unsigned long long i, unsigned long long k, unsigned long long u,
             unsigned long long oi, unsigned long long ok, unsigned long long ou)
{
    unsigned long long total, idle;
    if (k < ok || u < ou || i < oi) {
        return -1;
    }
    total = (k - ok) + (u - ou);
    idle = i - oi;
    if (!total || idle > total) {
        return -1;
    }
    return (int)(((total - idle) * 100 + total / 2) / total);
}

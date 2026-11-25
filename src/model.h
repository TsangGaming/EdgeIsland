#ifndef EDGE_MODEL_H
#define EDGE_MODEL_H

enum Edge { TOP, RIGHT_EDGE, BOTTOM, LEFT_EDGE };
enum TimerState { READY, RUNNING, PAUSED, FINISHED };
typedef struct {
    int x, y, w, h;
} Box;
typedef struct {
    int edge, fraction;
} Anchor; /* position along edge, 0..10000 */
enum Gesture { G_PENDING, G_PULL, G_MOVE, G_IGNORE };
#define PULL_OPEN_DIP 42
#define PULL_TRAVEL_DIP 156
typedef struct {
    int state, minutes, remaining;
    unsigned long long deadline;
} Timer;
#define PANEL_W 396
#define PANEL_H 334
int bounded(int x, int lo, int hi);
Box anchored(Box work, int w, int h, int margin, int locked, Anchor where);
Anchor nearest_edge(Box work, int x, int y, int w, int h, int margin);
int inward_distance(int edge, int dx, int dy);
int drag_gesture(int edge, int dx, int dy, int locked, int can_pull, int force_move);
Box interpolate_box(Box from, Box to, int amount); /* 0..1000 */
int wave_height(unsigned long long ms, int bar);
void timer_select(Timer *timer, int minutes);
int timer_left(const Timer *timer, unsigned long long now);
void timer_toggle(Timer *timer, unsigned long long now);
int timer_tick(Timer *timer, unsigned long long now);
int cpu_load(unsigned long long idle, unsigned long long kernel, unsigned long long user,
             unsigned long long old_idle, unsigned long long old_kernel, unsigned long long old_user);
#endif

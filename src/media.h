#ifndef EDGE_MEDIA_H
#define EDGE_MEDIA_H
#include "platform.h"
enum MediaCommand { MEDIA_REFRESH, MEDIA_TOGGLE, MEDIA_PREV, MEDIA_NEXT, MEDIA_SEEK };
typedef struct {
    int available, playing, can_play, can_pause, can_toggle, can_prev, can_next, can_seek;
    int service_ok, cover_changed, events_ok, session_changed, needs_retry;
    long long begin, end, position, min_seek, max_seek;
    unsigned long long observed;
    double rate;
    wchar_t title[256], artist[180];
    HBITMAP cover; /* Ownership transfers to the window via PostMessage. */
} Media;
int media_start(HWND hwnd);
void media_request(int command, int fraction);
void media_suspend(int yes);
void media_stop(void);
#endif

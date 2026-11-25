#ifndef EDGE_AUDIO_H
#define EDGE_AUDIO_H
#include "platform.h"
#define MSG_VOLUME (WM_APP+5)
#define MSG_REBIND_AUDIO (WM_APP+6)
void audio_start(HWND window);
void audio_rebind(void);
void audio_stop(void);
#endif

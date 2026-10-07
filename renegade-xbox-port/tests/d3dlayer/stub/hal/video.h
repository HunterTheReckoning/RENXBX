#pragma once
#define REFRESH_DEFAULT 0
extern int g_video_w, g_video_h, g_video_sets;
typedef struct { int width, height, bpp, refresh; } VIDEO_MODE;
static inline BOOL XVideoSetMode(int w, int h, int, int) { g_video_w = w; g_video_h = h; g_video_sets++; return TRUE; }
static inline VIDEO_MODE XVideoGetMode(void) { VIDEO_MODE m = { g_video_w, g_video_h, 32, 60 }; return m; }

/* Native-test stand-in for pbkit: records what the layer sends to the GPU. */
#pragma once
#include <stdint.h>
#include <windows.h>
#ifndef NV097_SET_ZSTENCIL_CLEAR_VALUE
#define NV097_SET_ZSTENCIL_CLEAR_VALUE 0x00001D8C
#endif
#ifndef NV097_SET_COLOR_CLEAR_VALUE
#define NV097_SET_COLOR_CLEAR_VALUE 0x00001D90
#endif
#ifndef NV097_CLEAR_SURFACE
#define NV097_CLEAR_SURFACE 0x00001D94
#endif
#define NV097_CLEAR_SURFACE_Z (1 << 0)
#define NV097_CLEAR_SURFACE_STENCIL (1 << 1)
#define NV097_CLEAR_SURFACE_COLOR 0x000000F0
#ifndef NV097_SET_CLEAR_RECT_HORIZONTAL
#define NV097_SET_CLEAR_RECT_HORIZONTAL 0x00001D98
#endif
#ifndef NV097_SET_CLEAR_RECT_VERTICAL
#define NV097_SET_CLEAR_RECT_VERTICAL 0x00001D9C
#endif
typedef struct { uint8_t red[256], green[256], blue[256]; } PB_GAMMA_RAMP;
struct PushRecord { DWORD method, value; };
extern PushRecord g_push[65536]; extern int g_push_count;
extern int g_pb_init, g_pb_reset, g_pb_finished, g_pb_kill, g_vbl, g_front_shown; extern PB_GAMMA_RAMP g_gamma;
extern uint32_t g_backbuffer[640 * 480];
#include "nv_regs_subset.h"
/* Commands are encoded as pbkit does (header = count << 18 | method, 0x40000000 = same method
** repeated) and decoded at pb_end the way the GPU reads them, into g_push. */
extern uint32_t g_cmd[4096]; extern uint32_t *g_cmd_start;
static inline uint32_t *pb_begin(void) { g_cmd_start = g_cmd; return g_cmd; }
static inline void pb_push(uint32_t *p, DWORD method, DWORD count) { *p = (count << 18) | method; }
static inline uint32_t *pb_push1(uint32_t *p, DWORD m, DWORD v) { pb_push(p, m, 1); p[1] = v; return p + 2; }
static inline uint32_t *pb_push2(uint32_t *p, DWORD m, DWORD v1, DWORD v2) { pb_push(p, m, 2); p[1] = v1; p[2] = v2; return p + 3; }
static inline void pb_end(uint32_t *p)
{
	for (uint32_t *q = g_cmd_start; q < p;) {
		DWORD header = *q++, count = (header >> 18) & 0x7FF, method = header & 0x1FFC;
		int same = (header & 0x40000000) != 0;
		for (DWORD i = 0; i < count && g_push_count < 65536; i++) {
			g_push[g_push_count].method = same ? method : method + i * 4;
			g_push[g_push_count].value = *q++;
			g_push_count++;
		}
	}
}
static inline int pb_init(void) { g_pb_init++; return 0; }
static inline void pb_kill(void) { g_pb_kill++; }
static inline void pb_show_front_screen(void) { g_front_shown++; }
static inline void pb_show_debug_screen(void) {}
static inline DWORD pb_wait_for_vbl(void) { return ++g_vbl; }
static inline void pb_reset(void) { g_pb_reset++; }
static inline void pb_target_back_buffer(void) {}
static inline int pb_busy(void) { return 0; }
static inline int pb_finished(void) { g_pb_finished++; return 0; }
static inline DWORD *pb_back_buffer(void) { return (DWORD *)g_backbuffer; }
static inline DWORD pb_back_buffer_pitch(void) { return 640 * 4; }
static inline DWORD pb_back_buffer_width(void) { return 640; }
static inline DWORD pb_back_buffer_height(void) { return 480; }
static inline void pb_set_gamma_ramp(const PB_GAMMA_RAMP *r) { g_gamma = *r; }

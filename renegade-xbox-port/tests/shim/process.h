/* Test stand-in for nxdk's process.h: runs the thread function immediately (synchronously). */
#pragma once
#include <stdint.h>
typedef unsigned (*_beginthreadex_proc_type)(void *);
inline int g_fail_next_thread = 0;   /* one copy shared by all files */
inline uintptr_t _beginthreadex(void *, unsigned, _beginthreadex_proc_type start, void *arg, unsigned, unsigned *)
{
	if (g_fail_next_thread) { g_fail_next_thread = 0; return 0; }
	start(arg);
	return 0x1234;
}

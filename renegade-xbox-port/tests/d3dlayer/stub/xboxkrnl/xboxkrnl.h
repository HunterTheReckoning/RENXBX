#pragma once
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#define PAGE_READWRITE 0x04
#define PAGE_WRITECOMBINE 0x400
typedef struct { ULONG Length; ULONG TotalPhysicalPages; ULONG AvailablePages; } MM_STATISTICS;
extern int g_allocs, g_frees;
static inline PVOID MmAllocateContiguousMemoryEx(ULONG size, ULONG, ULONG, ULONG align, ULONG) { g_allocs++; return aligned_alloc(align, (size + align - 1) / align * align); }
static inline void MmFreeContiguousMemory(PVOID p) { g_frees++; free(p); }
static inline long MmQueryStatistics(MM_STATISTICS *s) { s->AvailablePages = 1000; s->TotalPhysicalPages = 16384; return 0; }
#define DbgPrint printf

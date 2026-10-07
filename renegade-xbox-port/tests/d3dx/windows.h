/* minimal stand-in for nxdk's windows.h, for a native test of the D3DX math */
#pragma once
typedef unsigned long DWORD; typedef unsigned short WORD; typedef unsigned char BYTE; typedef int BOOL;
typedef unsigned int UINT; typedef long LONG; typedef int INT; typedef long HRESULT; typedef unsigned long ULONG;
typedef void *HWND; typedef union { struct { DWORD LowPart; LONG HighPart; }; long long QuadPart; } LARGE_INTEGER;
#define CONST const
#define S_OK ((HRESULT)0)

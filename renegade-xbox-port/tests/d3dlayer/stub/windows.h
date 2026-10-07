/* Native-test stand-in for nxdk's windows.h (just the types the Direct3D layer needs). */
#pragma once
#include <stdint.h>
typedef uint32_t DWORD; typedef uint16_t WORD; typedef uint8_t BYTE; typedef int BOOL; typedef unsigned int UINT;
typedef int32_t LONG; typedef int INT; typedef int32_t HRESULT; typedef uint32_t ULONG; typedef void *HWND; typedef void *PVOID;
typedef union { struct { DWORD LowPart; LONG HighPart; }; long long QuadPart; } LARGE_INTEGER;
#define CONST const
#define TRUE 1
#define FALSE 0
#define S_OK ((HRESULT)0)
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#define SUCCEEDED(hr) ((HRESULT)(hr) >= 0)
#define FAILED(hr) ((HRESULT)(hr) < 0)

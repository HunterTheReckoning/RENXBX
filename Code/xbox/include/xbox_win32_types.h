/*
** xbox_win32_types.h -- small Windows types that Direct3D 8 uses and nxdk does not define
** (Xbox build only). If a future nxdk version adds any of these, remove them here.
*/
#ifndef XBOX_WIN32_TYPES_H
#define XBOX_WIN32_TYPES_H

#include <windows.h>

#ifndef FLOAT
typedef float FLOAT;
#endif

typedef struct tagRECT {
	LONG left;
	LONG top;
	LONG right;
	LONG bottom;
} RECT, *PRECT, *LPRECT;
typedef const RECT *LPCRECT;

typedef struct tagPOINT {
	LONG x;
	LONG y;
} POINT, *PPOINT, *LPPOINT;

typedef struct _GUID {
	unsigned long  Data1;
	unsigned short Data2;
	unsigned short Data3;
	unsigned char  Data4[8];
} GUID, IID;
#ifdef __cplusplus
typedef const GUID &REFGUID;
typedef const IID &REFIID;
#else
typedef const GUID *REFGUID;
typedef const IID *REFIID;
#endif

typedef void *HMONITOR;

/* GDI handles. Only stored as opaque values by the engine's headers on Xbox; the one file
** that actually draws with GDI (fonts, in render2dsentence.cpp) is being replaced. */
typedef void *HDC;
typedef void *HFONT;
typedef void *HBITMAP;
typedef void *HGDIOBJ;

#ifndef LOWORD
#define LOWORD(l) ((WORD)((DWORD)(l) & 0xffff))
#define HIWORD(l) ((WORD)(((DWORD)(l) >> 16) & 0xffff))
#define MAKELONG(lo, hi) ((LONG)(((WORD)(lo)) | (((DWORD)((WORD)(hi))) << 16)))
#endif

typedef struct _RGNDATAHEADER {
	DWORD dwSize;
	DWORD iType;
	DWORD nCount;
	DWORD nRgnSize;
	RECT  rcBound;
} RGNDATAHEADER;
typedef struct _RGNDATA {
	RGNDATAHEADER rdh;
	char Buffer[1];
} RGNDATA;

typedef struct tagPALETTEENTRY {
	BYTE peRed;
	BYTE peGreen;
	BYTE peBlue;
	BYTE peFlags;
} PALETTEENTRY;

#ifndef MAKEFOURCC
#define MAKEFOURCC(ch0, ch1, ch2, ch3) \
	((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) | \
	 ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24))
#endif

#ifndef MAKE_HRESULT
#define MAKE_HRESULT(sev, fac, code) \
	((HRESULT)(((unsigned long)(sev) << 31) | ((unsigned long)(fac) << 16) | ((unsigned long)(code))))
#endif

#ifdef __cplusplus
/* The COM base interface. Direct3D objects in the Xbox build are plain C++ objects that
** keep the COM-style reference counting the engine relies on. */
struct IUnknown {
	virtual HRESULT QueryInterface(REFIID riid, void **object) = 0;
	virtual ULONG AddRef(void) = 0;
	virtual ULONG Release(void) = 0;
	virtual ~IUnknown() {}
};
#endif

#endif /* XBOX_WIN32_TYPES_H */

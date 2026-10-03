/*
** xbox_port.h -- definitions the nxdk environment lacks, for the original Xbox port.
**
** Included from always.h only when building with nxdk (NXDK is defined by nxdk-cc/cxx).
** Keep this file small: it is for missing types and trivial shims. Anything with real
** behaviour (file paths, timers, input) belongs in its own port module.
*/
#ifndef XBOX_PORT_H
#define XBOX_PORT_H

#ifdef NXDK

#include <tchar.h>

/* winnt.h on real Windows defines these; nxdk's tchar.h only defines _TCHAR. */
#ifndef _TCHAR_DEFINED
#define _TCHAR_DEFINED
typedef char TCHAR;
typedef char *PTCHAR;
#endif
typedef char *LPTSTR;
typedef const char *LPCTSTR;
#ifndef TEXT
#define TEXT(x) x
#endif

#include <windows.h>
#include <process.h>
#include <stdint.h>
#include <wchar.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* Path size limits from the MSVC CRT (stdlib.h), same values. */
#ifndef _MAX_PATH
#define _MAX_PATH  260
#endif
#ifndef _MAX_DRIVE
#define _MAX_DRIVE 3
#endif
#ifndef _MAX_DIR
#define _MAX_DIR   256
#endif
#ifndef _MAX_FNAME
#define _MAX_FNAME 256
#endif
#ifndef _MAX_EXT
#define _MAX_EXT   256
#endif

/* Code pages for the string conversion shims. */
#ifndef CP_ACP
#define CP_ACP 0
#endif

/* Single-underscore MSVC spelling of __int64. */
#ifndef _int64
#define _int64 __int64
#endif

/* Registry key handle type; only named in registry.h's private section on Xbox. */
typedef void *HKEY;

#ifndef GetComputerName
#define GetComputerName GetComputerNameA
#endif
#ifndef GetUserName
#define GetUserName GetUserNameA
#endif
#ifndef FormatMessage
#define FormatMessage FormatMessageA
#endif
#ifndef FORMAT_MESSAGE_ALLOCATE_BUFFER
#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define FORMAT_MESSAGE_IGNORE_INSERTS  0x00000200
#define FORMAT_MESSAGE_FROM_STRING     0x00000400
#define FORMAT_MESSAGE_FROM_HMODULE    0x00000800
#define FORMAT_MESSAGE_FROM_SYSTEM     0x00001000
#define FORMAT_MESSAGE_ARGUMENT_ARRAY  0x00002000
#endif
#ifndef MAKELANGID
#define MAKELANGID(p, s) ((((WORD)(s)) << 10) | (WORD)(p))
#define LANG_NEUTRAL 0x00
#define SUBLANG_DEFAULT 0x01
#endif

/* No processes on the Xbox; srandom.cpp only mixes this into a random seed. */
#ifndef getpid
#define getpid() ((int)GetCurrentThreadId())
#endif

/* Windows platform IDs (cpudetect.cpp compares against these; the Xbox matches none). */
#ifndef VER_PLATFORM_WIN32s
#define VER_PLATFORM_WIN32s        0
#define VER_PLATFORM_WIN32_WINDOWS 1
#define VER_PLATFORM_WIN32_NT      2
#endif

#ifndef strcmpi
#define strcmpi _stricmp
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* MSVC CRT functions missing from nxdk (implemented in xbox_port.cpp). */
void  _splitpath(const char *path, char *drive, char *dir, char *fname, char *ext);
char *_strlwr(char *s);
char *_strupr(char *s);
int   _wcsicmp(const wchar_t *a, const wchar_t *b);

/* Win32 functions missing from nxdk's winapi (implemented in xbox_port.cpp).
** The code-page conversions treat the "ANSI" code page as Latin-1, which covers
** the game's English text; the translation database stores Unicode directly. */
BOOL  FileTimeToDosDateTime(const FILETIME *ft, WORD *fat_date, WORD *fat_time);
int   MultiByteToWideChar(UINT code_page, DWORD flags, const char *src, int src_len,
                          wchar_t *dst, int dst_len);
int   WideCharToMultiByte(UINT code_page, DWORD flags, const wchar_t *src, int src_len,
                          char *dst, int dst_len, const char *default_char, BOOL *used_default);
void  DebugBreak(void);
BOOL  DosDateTimeToFileTime(WORD fat_date, WORD fat_time, FILETIME *ft);

/* MSVC's _beginthread, built on nxdk's _beginthreadex. Returns the thread handle, or
** (uintptr_t)-1 on failure as MSVC does. Unlike MSVC, the handle is not closed when the
** thread exits (ThreadClass keeps using it); the engine creates only a few threads. */
uintptr_t _beginthread(void (__cdecl *start)(void *), unsigned stack_size, void *arg);

/* The Xbox has no computer or user name: these return "XBOX" and "Player"
** (used only to help seed the secure random generator). */
BOOL  GetComputerNameA(char *buffer, DWORD *size);
BOOL  GetUserNameA(char *buffer, DWORD *size);

/* No system message table on the Xbox: formats "Windows error <code>" for debug messages. */
DWORD FormatMessageA(DWORD flags, const void *source, DWORD message_id, DWORD language_id,
                     char *buffer, DWORD size, va_list *args);

/* Wide printf with Microsoft semantics (%s = wide string, %S/%hs = narrow string,
** %c = wide char, %C/%hc = narrow char). Returns the character count, or -1 if the
** output did not fit (like MSVC's _vsnwprintf, the buffer is then not terminated). */
int   _vsnwprintf(wchar_t *buffer, size_t count, const wchar_t *format, va_list args);
int   _snwprintf(wchar_t *buffer, size_t count, const wchar_t *format, ...);

#ifdef __cplusplus
}
#endif

/* TCHAR string functions the engine uses that nxdk's tchar.h does not map
** (single-byte build, so these are the plain char versions). */
#ifndef _tcsclen
#define _tcsclen strlen
#endif
#ifndef _tcscmp
#define _tcscmp strcmp
#endif
#ifndef _tcsicmp
#define _tcsicmp _stricmp
#endif

#endif /* NXDK */
#endif /* XBOX_PORT_H */

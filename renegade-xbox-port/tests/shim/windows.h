typedef int BOOL; typedef unsigned short WORD; typedef unsigned int UINT; typedef unsigned long DWORD;
#define __cdecl
#define __stdcall
static DWORD GetCurrentThreadId(void){ return 4242; }
#define TRUE 1
#define FALSE 0
typedef struct { DWORD dwLowDateTime, dwHighDateTime; } FILETIME;
typedef struct { WORD wYear,wMonth,wDayOfWeek,wDay,wHour,wMinute,wSecond,wMilliseconds; } SYSTEMTIME;
static BOOL FileTimeToSystemTime(const FILETIME*, SYSTEMTIME* st){ st->wYear=2002; st->wMonth=2; st->wDay=26; st->wHour=13; st->wMinute=45; st->wSecond=31; return 1; }
static BOOL SystemTimeToFileTime(const SYSTEMTIME* st, FILETIME* ft){ ft->dwLowDateTime=st->wYear*10000+st->wMonth*100+st->wDay; ft->dwHighDateTime=st->wHour*10000+st->wMinute*100+st->wSecond; return 1; }

#include <strings.h>
static int _stricmp(const char *a, const char *b) { return strcasecmp(a, b); }  /* nxdk provides this */

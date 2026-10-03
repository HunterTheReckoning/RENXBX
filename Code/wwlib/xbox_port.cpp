/*
** xbox_port.cpp -- implementations of CRT and Win32 functions that nxdk lacks.
** Only compiled for the original Xbox port (NXDK defined).
*/
#include "xbox_port.h"

#ifdef NXDK

#include <string.h>
#include <ctype.h>
#include <wctype.h>

extern "C" {

void _splitpath(const char *path, char *drive, char *dir, char *fname, char *ext)
{
	if (drive) drive[0] = 0;
	if (dir) dir[0] = 0;
	if (fname) fname[0] = 0;
	if (ext) ext[0] = 0;
	if (!path) return;

	/* Drive: "D:" */
	if (path[0] && path[1] == ':') {
		if (drive) { drive[0] = path[0]; drive[1] = ':'; drive[2] = 0; }
		path += 2;
	}

	/* Directory: everything up to and including the last slash. */
	const char *last_slash = NULL;
	for (const char *p = path; *p; ++p) {
		if (*p == '\\' || *p == '/') last_slash = p;
	}
	const char *name = path;
	if (last_slash) {
		size_t len = (size_t)(last_slash - path) + 1;
		if (len > _MAX_DIR - 1) len = _MAX_DIR - 1;
		if (dir) { memcpy(dir, path, len); dir[len] = 0; }
		name = last_slash + 1;
	}

	/* Extension: from the last dot in the file name. */
	const char *dot = strrchr(name, '.');
	size_t name_len = dot ? (size_t)(dot - name) : strlen(name);
	if (name_len > _MAX_FNAME - 1) name_len = _MAX_FNAME - 1;
	if (fname) { memcpy(fname, name, name_len); fname[name_len] = 0; }
	if (ext && dot) {
		strncpy(ext, dot, _MAX_EXT - 1);
		ext[_MAX_EXT - 1] = 0;
	}
}

char *_strlwr(char *s)
{
	for (char *p = s; p && *p; ++p) *p = (char)tolower((unsigned char)*p);
	return s;
}

char *_strupr(char *s)
{
	for (char *p = s; p && *p; ++p) *p = (char)toupper((unsigned char)*p);
	return s;
}

int _wcsicmp(const wchar_t *a, const wchar_t *b)
{
	for (;; ++a, ++b) {
		wint_t ca = towlower(*a), cb = towlower(*b);
		if (ca != cb) return (ca < cb) ? -1 : 1;
		if (ca == 0) return 0;
	}
}

BOOL FileTimeToDosDateTime(const FILETIME *ft, WORD *fat_date, WORD *fat_time)
{
	SYSTEMTIME st;
	if (!ft || !fat_date || !fat_time || !FileTimeToSystemTime(ft, &st)) return FALSE;
	if (st.wYear < 1980 || st.wYear > 2107) return FALSE;  /* DOS date range */
	*fat_date = (WORD)(((st.wYear - 1980) << 9) | (st.wMonth << 5) | st.wDay);
	*fat_time = (WORD)((st.wHour << 11) | (st.wMinute << 5) | (st.wSecond / 2));
	return TRUE;
}

int MultiByteToWideChar(UINT code_page, DWORD flags, const char *src, int src_len,
                        wchar_t *dst, int dst_len)
{
	(void)code_page; (void)flags;
	if (!src) return 0;
	int n = (src_len < 0) ? (int)strlen(src) + 1 : src_len;  /* -1 includes the terminator */
	if (dst_len == 0) return n;                               /* size query */
	if (!dst || dst_len < n) return 0;
	for (int i = 0; i < n; ++i) dst[i] = (wchar_t)(unsigned char)src[i];  /* Latin-1 */
	return n;
}

int WideCharToMultiByte(UINT code_page, DWORD flags, const wchar_t *src, int src_len,
                        char *dst, int dst_len, const char *default_char, BOOL *used_default)
{
	(void)code_page; (void)flags;
	if (used_default) *used_default = FALSE;
	if (!src) return 0;
	int n = src_len;
	if (n < 0) { n = 0; while (src[n]) ++n; ++n; }           /* include the terminator */
	if (dst_len == 0) {                                        /* size query */
		if (used_default) {
			for (int i = 0; i < n; ++i) if (src[i] > 0xFF) { *used_default = TRUE; break; }
		}
		return n;
	}
	if (!dst || dst_len < n) return 0;
	char fallback = default_char ? *default_char : '?';
	for (int i = 0; i < n; ++i) {
		if (src[i] > 0xFF) {
			dst[i] = fallback;
			if (used_default) *used_default = TRUE;
		} else {
			dst[i] = (char)src[i];
		}
	}
	return n;
}

void DebugBreak(void)
{
	__asm__ volatile("int3");
}

} /* extern "C" */

#endif /* NXDK */

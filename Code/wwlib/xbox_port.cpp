/*
** xbox_port.cpp -- implementations of CRT and Win32 functions that nxdk lacks.
** Only compiled for the original Xbox port (NXDK defined).
*/
#include "xbox_port.h"

#ifdef NXDK

#include <string.h>
#include <ctype.h>
#include <wctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <process.h>

extern "C" {

void (*XboxPort_Trace)(const char *message) = NULL;

void Xbox_Trace_Format(const char *format, ...)
{
	if (!XboxPort_Trace) return;
	char line[160];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	XboxPort_Trace(line);
}

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

BOOL DosDateTimeToFileTime(WORD fat_date, WORD fat_time, FILETIME *ft)
{
	if (!ft) return FALSE;
	SYSTEMTIME st;
	memset(&st, 0, sizeof(st));
	st.wYear   = (WORD)(1980 + (fat_date >> 9));
	st.wMonth  = (WORD)((fat_date >> 5) & 0x0F);
	st.wDay    = (WORD)(fat_date & 0x1F);
	st.wHour   = (WORD)(fat_time >> 11);
	st.wMinute = (WORD)((fat_time >> 5) & 0x3F);
	st.wSecond = (WORD)((fat_time & 0x1F) * 2);
	return SystemTimeToFileTime(&st, ft);
}

/* --- Wide printf ---------------------------------------------------------------------- */

struct WideOut {
	wchar_t *buf; size_t cap; size_t len;
	void put(wchar_t c) { if (len < cap) buf[len] = c; ++len; }
};

static void put_padded_wide(WideOut &o, const wchar_t *s, size_t n, int width, bool left)
{
	int pad = (width > (int)n) ? width - (int)n : 0;
	if (!left) while (pad-- > 0) o.put(L' ');
	for (size_t i = 0; i < n; ++i) o.put(s[i]);
	if (left) while (pad-- > 0) o.put(L' ');
}

int _vsnwprintf(wchar_t *buffer, size_t count, const wchar_t *format, va_list args)
{
	WideOut o = { buffer, count, 0 };
	for (const wchar_t *f = format; *f; ++f) {
		if (*f != L'%') { o.put(*f); continue; }
		const wchar_t *spec_start = f++;
		if (*f == L'%') { o.put(L'%'); continue; }

		/* Collect the spec as narrow text for vsnprintf: flags, width, precision. */
		char spec[32]; int sl = 0; spec[sl++] = '%';
		bool left = false; int width = 0, precision = -1;
		while (*f == L'-' || *f == L'+' || *f == L' ' || *f == L'#' || *f == L'0') {
			if (*f == L'-') left = true;
			if (sl < 20) spec[sl++] = (char)*f;
			++f;
		}
		if (*f == L'*') { width = va_arg(args, int); if (width < 0) { left = true; width = -width; } ++f;
		                  sl += snprintf(spec + sl, sizeof(spec) - sl, "%d", width); }
		else while (*f >= L'0' && *f <= L'9') { width = width * 10 + (*f - L'0'); if (sl < 28) spec[sl++] = (char)*f; ++f; }
		if (*f == L'.') {
			++f; precision = 0; if (sl < 28) spec[sl++] = '.';
			if (*f == L'*') { precision = va_arg(args, int); ++f;
			                  sl += snprintf(spec + sl, sizeof(spec) - sl, "%d", precision < 0 ? 0 : precision); }
			else while (*f >= L'0' && *f <= L'9') { precision = precision * 10 + (*f - L'0'); if (sl < 28) spec[sl++] = (char)*f; ++f; }
		}

		/* Length modifiers, Microsoft style. */
		enum { LEN_NONE, LEN_H, LEN_L, LEN_LL, LEN_I64 } len = LEN_NONE;
		if (*f == L'h') { len = LEN_H; ++f; }
		else if (*f == L'l') { ++f; if (*f == L'l') { len = LEN_LL; ++f; } else len = LEN_L; }
		else if (*f == L'w') { len = LEN_L; ++f; }
		else if (f[0] == L'I' && f[1] == L'6' && f[2] == L'4') { len = LEN_I64; f += 3; }
		else if (*f == L'L') { ++f; }  /* long double: treated as double below */

		wchar_t conv = *f;
		if (!conv) break;

		switch (conv) {
		case L's': case L'S': {
			/* %s wide unless 'h'; %S narrow unless 'l'/'w'. */
			bool wide = (conv == L's') ? (len != LEN_H) : (len == LEN_L);
			if (wide) {
				const wchar_t *s = va_arg(args, const wchar_t *);
				if (!s) s = L"(null)";
				size_t n = 0; while (s[n] && (precision < 0 || (int)n < precision)) ++n;
				put_padded_wide(o, s, n, width, left);
			} else {
				const char *s = va_arg(args, const char *);
				if (!s) s = "(null)";
				size_t n = 0; while (s[n] && (precision < 0 || (int)n < precision)) ++n;
				int pad = (width > (int)n) ? width - (int)n : 0;
				if (!left) while (pad-- > 0) o.put(L' ');
				for (size_t i = 0; i < n; ++i) o.put((wchar_t)(unsigned char)s[i]);
				if (left) while (pad-- > 0) o.put(L' ');
			}
			break;
		}
		case L'c': case L'C': {
			int ch = va_arg(args, int);
			bool wide = (conv == L'c') ? (len != LEN_H) : (len == LEN_L);
			wchar_t wc = wide ? (wchar_t)ch : (wchar_t)(unsigned char)ch;
			put_padded_wide(o, &wc, 1, width, left);
			break;
		}
		case L'd': case L'i': case L'u': case L'x': case L'X': case L'o':
		case L'f': case L'F': case L'e': case L'E': case L'g': case L'G': case L'p': {
			char narrow[128];
			if (len == LEN_LL || len == LEN_I64) { spec[sl++] = 'l'; spec[sl++] = 'l'; }
			else if (len == LEN_L) spec[sl++] = 'l';
			else if (len == LEN_H) spec[sl++] = 'h';
			spec[sl++] = (char)conv; spec[sl] = 0;
			int n;
			if (conv == L'f' || conv == L'F' || conv == L'e' || conv == L'E' || conv == L'g' || conv == L'G')
				n = snprintf(narrow, sizeof(narrow), spec, va_arg(args, double));
			else if (conv == L'p')
				n = snprintf(narrow, sizeof(narrow), spec, va_arg(args, void *));
			else if (len == LEN_LL || len == LEN_I64)
				n = snprintf(narrow, sizeof(narrow), spec, va_arg(args, long long));
			else if (len == LEN_L)
				n = snprintf(narrow, sizeof(narrow), spec, va_arg(args, long));
			else
				n = snprintf(narrow, sizeof(narrow), spec, va_arg(args, int));
			if (n > (int)sizeof(narrow) - 1) n = sizeof(narrow) - 1;
			for (int i = 0; i < n; ++i) o.put((wchar_t)(unsigned char)narrow[i]);
			break;
		}
		case L'n': {
			int *out = va_arg(args, int *);
			if (out) *out = (int)o.len;
			break;
		}
		default:
			/* Unknown conversion: copy it through literally. */
			for (const wchar_t *c = spec_start; c <= f; ++c) o.put(*c);
			break;
		}
	}
	if (o.len < count) { buffer[o.len] = 0; return (int)o.len; }
	if (o.len == count) return (int)o.len;   /* fits exactly: MSVC leaves it unterminated */
	return -1;                               /* truncated */
}

int _snwprintf(wchar_t *buffer, size_t count, const wchar_t *format, ...)
{
	va_list args;
	va_start(args, format);
	int n = _vsnwprintf(buffer, count, format, args);
	va_end(args);
	return n;
}

/* --- Names, error text, threads ------------------------------------------------------- */

static BOOL copy_name(const char *name, char *buffer, DWORD *size)
{
	DWORD needed = (DWORD)strlen(name) + 1;
	if (!buffer || !size || *size < needed) { if (size) *size = needed; return FALSE; }
	memcpy(buffer, name, needed);
	*size = needed - 1;      /* Windows reports the length without the terminator */
	return TRUE;
}

BOOL GetComputerNameA(char *buffer, DWORD *size) { return copy_name("XBOX", buffer, size); }
BOOL GetUserNameA(char *buffer, DWORD *size)     { return copy_name("Player", buffer, size); }

DWORD FormatMessageA(DWORD flags, const void *source, DWORD message_id, DWORD language_id,
                     char *buffer, DWORD size, va_list *args)
{
	(void)flags; (void)source; (void)language_id; (void)args;
	if (!buffer || size == 0) return 0;
	int n = snprintf(buffer, size, "Windows error %lu", (unsigned long)message_id);
	if (n < 0) { buffer[0] = 0; return 0; }
	return (DWORD)((n < (int)size) ? n : (int)size - 1);
}

struct BeginThreadArgs {
	void (__cdecl *start)(void *);
	void *arg;
};

static unsigned __stdcall begin_thread_trampoline(void *p)
{
	BeginThreadArgs args = *(BeginThreadArgs *)p;
	free(p);
	args.start(args.arg);
	return 0;
}

uintptr_t _beginthread(void (__cdecl *start)(void *), unsigned stack_size, void *arg)
{
	BeginThreadArgs *args = (BeginThreadArgs *)malloc(sizeof(BeginThreadArgs));
	if (!args) return (uintptr_t)-1;
	args->start = start;
	args->arg = arg;
	uintptr_t handle = _beginthreadex(NULL, stack_size, begin_thread_trampoline, args, 0, NULL);
	if (!handle) { free(args); return (uintptr_t)-1; }
	return handle;
}


/* --- kernel32 string functions ------------------------------------------------------ */

char *lstrcpyA(char *dst, const char *src)
{
	return strcpy(dst, src);
}

char *lstrcpynA(char *dst, const char *src, int max_count)
{
	if (max_count <= 0) return dst;
	int i = 0;
	for (; i < max_count - 1 && src[i]; ++i) dst[i] = src[i];
	dst[i] = 0;
	return dst;
}

char *lstrcatA(char *dst, const char *src)
{
	return strcat(dst, src);
}

int lstrlenA(const char *s)
{
	return s ? (int)strlen(s) : 0;
}

int lstrcmpiA(const char *a, const char *b)
{
	return _stricmp(a, b);
}

DWORD GetCurrentDirectoryA(DWORD buffer_size, char *buffer)
{
	static const char dir[] = "D:\\";
	if (!buffer || buffer_size < sizeof(dir)) return sizeof(dir);
	memcpy(buffer, dir, sizeof(dir));
	return sizeof(dir) - 1;
}

void Xbox_Resolve_Path(const char *name, char *out, size_t out_size)
{
	if (!out || out_size == 0) return;
	if (!name) name = "";
	const char *rest = name;
	const char *prefix = "";
	if (name[0] && name[1] == ':') {
		prefix = "";                                  /* has a drive letter: keep as is */
	} else {
		for (;;) {                                    /* drop leading \, .\ and ..\ */
			if (rest[0] == '\\' || rest[0] == '/') { rest += 1; continue; }
			if (rest[0] == '.' && (rest[1] == '\\' || rest[1] == '/')) { rest += 2; continue; }
			if (rest[0] == '.' && rest[1] == '.' && (rest[2] == '\\' || rest[2] == '/')) { rest += 3; continue; }
			break;
		}
		prefix = "D:\\";
	}
	snprintf(out, out_size, "%s%s", prefix, rest);
	for (char *p = out; *p; ++p) if (*p == '/') *p = '\\';
}

wchar_t *lstrcpynW(wchar_t *dst, const wchar_t *src, int max_count)
{
	if (max_count <= 0) return dst;
	int i = 0;
	for (; i < max_count - 1 && src[i]; ++i) dst[i] = src[i];
	dst[i] = 0;
	return dst;
}

void DebugBreak(void)
{
	__asm__ volatile("int3");
}

} /* extern "C" */

#endif /* NXDK */

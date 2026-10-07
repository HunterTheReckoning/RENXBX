#include "xbox_port.h"
#include <stdio.h>
#include <string.h>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static int ran_with = 0;
static void __cdecl thread_fn(void *arg) { ran_with = *(int *)arg; }
int main() {
	int value = 77;
	uintptr_t h = _beginthread(thread_fn, 0, &value);
	CHECK(h == 0x1234 && ran_with == 77);                 /* argument reaches the thread */
	g_fail_next_thread = 1;
	CHECK(_beginthread(thread_fn, 0, &value) == (uintptr_t)-1);   /* MSVC failure value */
	char name[32]; DWORD size = sizeof(name);
	CHECK(GetComputerName(name, &size) && !strcmp(name, "XBOX") && size == 4);
	size = 3; CHECK(!GetComputerName(name, &size) && size == 5);  /* too small: reports needed size */
	size = sizeof(name); CHECK(GetUserName(name, &size) && !strcmp(name, "Player"));
	char msg[64];
	DWORD n = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, 5, 0, msg, sizeof(msg), NULL);
	CHECK(n > 0 && strstr(msg, "5") != NULL && n == strlen(msg));
	n = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, 123456, 0, msg, 8, NULL);
	CHECK(n == 7 && strlen(msg) == 7);                    /* truncated safely, terminated */
	CHECK(getpid() == 4242);
	char buf[8];
	CHECK(lstrcpyn(buf, "Renegade", 5) == buf && !strcmp(buf, "Rene"));   /* n-1 chars + terminator */
	CHECK(!strcmp(lstrcpyn(buf, "GDI", 8), "GDI"));
	buf[0] = 'X'; lstrcpyn(buf, "abc", 0); CHECK(buf[0] == 'X');         /* n = 0 writes nothing */
	lstrcpyn(buf, "abc", 1); CHECK(buf[0] == 0);                         /* n = 1: just the terminator */
	CHECK(lstrlen(NULL) == 0 && lstrlen("Nod") == 3);
	lstrcpy(buf, "Ob"); lstrcat(buf, "elisk"); CHECK(!strcmp(buf, "Obelisk"));
	CHECK(lstrcmpi("HAVOC", "havoc") == 0 && lstrcmpi("a", "B") < 0);
	char dir[16]; CHECK(GetCurrentDirectory(sizeof(dir), dir) == 3 && !strcmp(dir, "D:\\"));
	CHECK(GetCurrentDirectory(2, dir) == 4);                               /* too small: size needed */
	char rp[64];
	Xbox_Resolve_Path("ShatterPlanes0.w3d", rp, sizeof(rp)); CHECK(!strcmp(rp, "D:\\ShatterPlanes0.w3d"));
	Xbox_Resolve_Path("..\\ShatterPlanes0.w3d", rp, sizeof(rp)); CHECK(!strcmp(rp, "D:\\ShatterPlanes0.w3d"));
	Xbox_Resolve_Path(".\\data/always.dat", rp, sizeof(rp)); CHECK(!strcmp(rp, "D:\\data\\always.dat"));
	Xbox_Resolve_Path("\\dazzle.ini", rp, sizeof(rp)); CHECK(!strcmp(rp, "D:\\dazzle.ini"));
	Xbox_Resolve_Path("..\\..\\x.ini", rp, sizeof(rp)); CHECK(!strcmp(rp, "D:\\x.ini"));
	Xbox_Resolve_Path("E:\\renegade_settings.dat", rp, sizeof(rp)); CHECK(!strcmp(rp, "E:\\renegade_settings.dat"));
	Xbox_Resolve_Path("D:\\always.dat", rp, sizeof(rp)); CHECK(!strcmp(rp, "D:\\always.dat"));
	Xbox_Resolve_Path("a..b.txt", rp, sizeof(rp)); CHECK(!strcmp(rp, "D:\\a..b.txt"));       /* dots inside names kept */
	char tiny[6]; Xbox_Resolve_Path("verylongname", tiny, sizeof(tiny)); CHECK(strlen(tiny) == 5);   /* truncated safely */
	wchar_t w[4]; lstrcpynW(w, L"Orca", 4); CHECK(w[0] == 'O' && w[2] == 'c' && w[3] == 0);
	printf(fails ? "%d FAILED\n" : "all system shim tests passed\n", fails);
	return fails;
}

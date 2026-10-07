#include "xbox_settings_store.h"
#include <stdio.h>
#include <string.h>
using namespace XboxSettings;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static int count_cb_n; static char seen[256];
static void cb(const char *name, void *) { count_cb_n++; strcat(seen, name); strcat(seen, ","); }
static bool get_dword(const char *k, const char *n, unsigned *out) {
    Type t; const void *d; size_t sz;
    if (!Get(k, n, &t, &d, &sz) || t != TYPE_DWORD || sz != 4) return false;
    memcpy(out, d, 4); return true;
}
int main() {
    remove("s.dat"); remove("s.dat.tmp");
    Set_File("s.dat");
    CHECK(!Key_Exists("Software\\Westwood\\Renegade"));
    unsigned v = 1280; Set("Software\\Westwood\\Renegade\\Render", "Width", TYPE_DWORD, &v, 4);
    CHECK(Key_Exists("software\\westwood\\renegade\\render"));             /* case-insensitive */
    CHECK(Key_Exists("Software/Westwood/Renegade/Render/"));               /* slashes, trailing */
    unsigned got = 0; CHECK(get_dword("SOFTWARE\\WESTWOOD\\RENEGADE\\RENDER", "width", &got) && got == 1280);
    const char nick[] = "Havoc"; Set("Software\\Westwood\\Renegade", "Nickname", TYPE_STRING, nick, sizeof(nick));
    const unsigned short wide[] = { 'G','D','I',0 }; Set("Software\\Westwood\\Renegade", "Team", TYPE_WSTRING, wide, sizeof(wide));
    Set("Software\\Westwood\\Renegade", "Empty", TYPE_BINARY, "", 0);
    count_cb_n = 0; seen[0] = 0; List_Values("Software\\Westwood\\Renegade", cb, NULL);
    CHECK(count_cb_n == 3);                                                 /* not the Render sub-key's values */
    CHECK(strstr(seen, "Nickname") && strstr(seen, "Team") && strstr(seen, "Empty"));
    CHECK(Flush());
    /* Round trip through the file. */
    CHECK(Reload());
    CHECK(get_dword("Software\\Westwood\\Renegade\\Render", "Width", &got) && got == 1280);
    Type t; const void *d; size_t sz;
    CHECK(Get("Software\\Westwood\\Renegade", "Nickname", &t, &d, &sz) && t == TYPE_STRING && sz == 6 && !strcmp((const char *)d, "Havoc"));
    CHECK(Get("Software\\Westwood\\Renegade", "Team", &t, &d, &sz) && t == TYPE_WSTRING && sz == 8 && ((const unsigned short *)d)[2] == 'I');
    CHECK(Get("Software\\Westwood\\Renegade", "Empty", &t, &d, &sz) && sz == 0);
    count_cb_n = 0; seen[0] = 0; List_Values("Software\\Westwood\\Renegade", cb, NULL);
    CHECK(strstr(seen, "Nickname,") != NULL);                               /* original name case kept */
    /* Unchanged writes don't dirty; Flush without changes succeeds without rewriting. */
    remove("s.dat"); Set("Software\\Westwood\\Renegade\\Render", "Width", TYPE_DWORD, &v, 4);
    CHECK(Flush()); { FILE *f = fopen("s.dat", "rb"); CHECK(f == NULL); if (f) fclose(f); }
    v = 640; Set("Software\\Westwood\\Renegade\\Render", "Width", TYPE_DWORD, &v, 4); CHECK(Flush());
    /* Delete tree removes the key, sub-keys and their values, but not a similar-named sibling. */
    Set("Software\\Westwood\\RenegadeX", "Keep", TYPE_DWORD, &v, 4);
    Delete_Tree("Software\\Westwood\\Renegade");
    CHECK(!Key_Exists("Software\\Westwood\\Renegade\\Render"));
    CHECK(!Get("Software\\Westwood\\Renegade", "Nickname", NULL, NULL, NULL));
    CHECK(Get("Software\\Westwood\\RenegadeX", "Keep", NULL, NULL, NULL));
    Delete_Value("Software\\Westwood\\RenegadeX", "KEEP");
    CHECK(!Get("Software\\Westwood\\RenegadeX", "Keep", NULL, NULL, NULL));
    CHECK(Flush());
    /* Power cut between remove() and rename(): only the .tmp is left; it must be recovered. */
    v = 800; Set("Software\\Westwood\\Renegade\\Render", "Width", TYPE_DWORD, &v, 4); CHECK(Flush());
    rename("s.dat", "s.dat.tmp");
    CHECK(Reload());
    CHECK(get_dword("Software\\Westwood\\Renegade\\Render", "Width", &got) && got == 800);
    /* A truncated (damaged) file is rejected whole: start empty, never half-loaded. */
    CHECK(Flush()); { FILE *f = fopen("s.dat", "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fclose(f);
      FILE *g = fopen("s.dat", "rb"); char buf[4096]; size_t r = fread(buf, 1, n, g); fclose(g);
      FILE *h = fopen("s.dat", "wb"); fwrite(buf, 1, r - 3, h); fclose(h); }
    CHECK(!Reload());
    CHECK(!Key_Exists("Software\\Westwood\\Renegade\\Render"));
    /* Garbage file is rejected too. */
    { FILE *h = fopen("s.dat", "wb"); fputs("not a settings file", h); fclose(h); }
    CHECK(!Reload());
    printf(fails ? "%d FAILED\n" : "all settings store tests passed\n", fails);
    return fails;
}

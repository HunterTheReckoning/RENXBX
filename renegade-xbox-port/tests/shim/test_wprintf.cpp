#include "xbox_port.h"
#include <stdio.h>
#include <string.h>
static int fails=0;
static bool weq(const wchar_t*a,const char*b){ size_t i=0; for(;b[i];++i) if(a[i]!=(wchar_t)(unsigned char)b[i]) return false; return a[i]==0; }
static void show(const wchar_t*a){ for(;*a;++a) putchar(*a<128?(char)*a:'?'); }
#define T(expect, ...) do{ wchar_t buf[128]; int r=_snwprintf(buf,128,__VA_ARGS__); \
  if(!weq(buf,expect) || r!=(int)strlen(expect)){ printf("FAIL line %d: got '",__LINE__); show(buf); printf("' (%d) want '%s'\n",r,expect); fails++; } }while(0)
int main(){
  T("Credits: 1500", L"Credits: %d", 1500);
  T("Havoc vs Sakura", L"%s vs %s", L"Havoc", L"Sakura");          /* %s = wide (MS) */
  T("narrow: GDI", L"narrow: %S", "GDI");                            /* %S = narrow (MS) */
  T("narrow: Nod", L"narrow: %hs", "Nod");
  T("wide: Orca", L"wide: %ls", L"Orca");
  T("[  42] [42  ] [0042]", L"[%4d] [%-4d] [%04d]", 42, 42, 42);
  T("3.14 2.50", L"%.2f %.2f", 3.14159, 2.5);
  T("time 05:07", L"time %02d:%02d", 5, 7);
  T("ff FF 0x10", L"%x %X %#x", 255, 255, 16);
  T("[   Hi] [Hi   ]", L"[%5s] [%-5s]", L"Hi", L"Hi");
  T("[Hav]", L"[%.3s]", L"Havoc");
  T("[  7]", L"[%*d]", 3, 7);
  T("x=A y=B", L"x=%c y=%C", L'A', 'B');
  T("100%", L"100%%");
  T("big 12345678901", L"big %I64d", 12345678901LL);
  T("big 12345678901", L"big %lld", 12345678901LL);
  T("-5 4294967291", L"%d %u", -5, (unsigned)-5);
  T("(null)", L"%s", (wchar_t*)0);
  wchar_t small[6]; int r=_snwprintf(small,6,L"%s",L"Renegade"); if(r!=-1){printf("FAIL truncation returned %d\n",r);fails++;}
  r=_snwprintf(small,6,L"%s",L"Nod!!"); if(r!=5||small[5]!=0){printf("FAIL fits-with-terminator\n");fails++;}
  r=_snwprintf(small,5,L"%s",L"Nod!!"); if(r!=5){printf("FAIL exact-fit returned %d\n",r);fails++;}
  FILETIME ft; CHECK: if(!DosDateTimeToFileTime((WORD)(((2002-1980)<<9)|(2<<5)|26),(WORD)((13<<11)|(45<<5)|15),&ft)||ft.dwLowDateTime!=20020226||ft.dwHighDateTime!=134530){printf("FAIL dos->filetime\n");fails++;}
  printf(fails? "%d FAILED\n" : "all wide printf tests passed\n", fails); return fails;
}

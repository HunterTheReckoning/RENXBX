#include "xbox_port.h"
#include <stdio.h>
#include <string.h>
static int fails=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
int main(){
  char d[_MAX_DRIVE],dir[_MAX_DIR],f[_MAX_FNAME],e[_MAX_EXT];
  _splitpath("D:\\data\\always.dat",d,dir,f,e);
  CHECK(!strcmp(d,"D:")); CHECK(!strcmp(dir,"\\data\\")); CHECK(!strcmp(f,"always")); CHECK(!strcmp(e,".dat"));
  _splitpath("M01.mix",d,dir,f,e);
  CHECK(!strcmp(d,"")); CHECK(!strcmp(dir,"")); CHECK(!strcmp(f,"M01")); CHECK(!strcmp(e,".mix"));
  _splitpath("data/sub.dir/file",d,dir,f,e);
  CHECK(!strcmp(dir,"data/sub.dir/")); CHECK(!strcmp(f,"file")); CHECK(!strcmp(e,""));
  _splitpath("x.y.z",NULL,NULL,f,e); CHECK(!strcmp(f,"x.y")); CHECK(!strcmp(e,".z"));
  wchar_t w[16]; CHECK(MultiByteToWideChar(CP_ACP,0,"Havoc\xe9",-1,NULL,0)==7);
  CHECK(MultiByteToWideChar(CP_ACP,0,"Havoc\xe9",-1,w,16)==7); CHECK(w[5]==0xe9 && w[6]==0);
  char a[16]; BOOL used=0; wchar_t src[]={'N','o','d',0x4E2D,0};
  CHECK(WideCharToMultiByte(CP_ACP,0,src,-1,NULL,0,NULL,&used)==5 && used);
  CHECK(WideCharToMultiByte(CP_ACP,0,src,-1,a,16,NULL,&used)==5); CHECK(!strcmp(a,"Nod?"));
  CHECK(MultiByteToWideChar(CP_ACP,0,"abc",-1,w,2)==0);  /* buffer too small */
  CHECK(_wcsicmp(L"GDI",L"gdi")==0); CHECK(_wcsicmp(L"a",L"B")<0); CHECK(_wcsicmp(L"ab",L"a")>0);
  char s[]="MiXeD"; CHECK(!strcmp(_strlwr(s),"mixed")); CHECK(!strcmp(_strupr(s),"MIXED"));
  FILETIME ft={0,0}; WORD dd,dt; CHECK(FileTimeToDosDateTime(&ft,&dd,&dt));
  CHECK(dd==(((2002-1980)<<9)|(2<<5)|26)); CHECK(dt==((13<<11)|(45<<5)|15));
  printf(fails? "%d FAILED\n" : "all shim tests passed\n", fails); return fails;
}

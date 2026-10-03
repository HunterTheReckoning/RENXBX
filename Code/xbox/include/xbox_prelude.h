/*
** xbox_prelude.h -- force-included first in every engine file of the Xbox build:
**     nxdk-cxx -include Code/xbox/include/xbox_prelude.h ...
**
** WCHAR: the engine mixes WCHAR, wchar_t and L"..." literals freely, because under MSVC 6
** they were all the same type. nxdk defines WCHAR as unsigned short, while Clang's wchar_t
** is a separate built-in type, so the two no longer mix. Making the engine's WCHAR the
** native wchar_t (as modern Windows SDKs do) restores that. nxdk's own headers keep their
** unsigned short type under another name, so nxdk itself is untouched; both are 16 bits.
*/
#ifndef XBOX_PRELUDE_H
#define XBOX_PRELUDE_H

#if defined(NXDK) && defined(__cplusplus)

#define WCHAR NXDK_WCHAR      /* nxdk's headers declare their own type under this name */
#include <windows.h>
#undef WCHAR
typedef wchar_t WCHAR;        /* the engine's WCHAR: native 16-bit wchar_t */

#include <wchar.h>
#include "../../wwlib/xbox_port.h"   /* port shims, for files that don't include always.h */

static_assert(sizeof(wchar_t) == 2, "Xbox build expects a 16-bit wchar_t");
static_assert(sizeof(NXDK_WCHAR) == sizeof(wchar_t), "nxdk WCHAR size changed");

#endif /* NXDK && __cplusplus */
#endif /* XBOX_PRELUDE_H */

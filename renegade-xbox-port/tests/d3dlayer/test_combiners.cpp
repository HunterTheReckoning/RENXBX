/* Unit tests for xbox_d3d8_combiners.cpp: the NV2A swizzle, and texture stages -> combiners. */
#include "xbox_d3d8_internal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
using namespace XboxD3D;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void Defaults(DWORD st[4][32])
{
	memset(st, 0, sizeof(DWORD) * 4 * 32);
	for (int s = 0; s < 4; s++) {
		st[s][D3DTSS_COLOROP] = s == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE;
		st[s][D3DTSS_COLORARG1] = D3DTA_TEXTURE; st[s][D3DTSS_COLORARG2] = D3DTA_CURRENT;
		st[s][D3DTSS_ALPHAOP] = s == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
		st[s][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE; st[s][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
		st[s][D3DTSS_RESULTARG] = D3DTA_CURRENT;
	}
}

int main()
{
	/* swizzle: x and y bits interleaved, x first, until the smaller dimension runs out */
	CHECK(Swizzle_Offset(1, 0, 4, 4) == 1 && Swizzle_Offset(0, 1, 4, 4) == 2 && Swizzle_Offset(1, 1, 4, 4) == 3);
	CHECK(Swizzle_Offset(2, 0, 4, 4) == 4 && Swizzle_Offset(0, 2, 4, 4) == 8 && Swizzle_Offset(3, 3, 4, 4) == 15);
	CHECK(Swizzle_Offset(2, 0, 8, 2) == 4 && Swizzle_Offset(4, 0, 8, 2) == 8 && Swizzle_Offset(7, 1, 8, 2) == 15);
	CHECK(Swizzle_Offset(0, 4, 2, 8) == 8 && Swizzle_Offset(1, 7, 2, 8) == 15);
	const UINT sizes[][2] = { { 1, 1 }, { 4, 4 }, { 64, 32 }, { 8, 256 }, { 128, 128 } };
	for (auto &sz : sizes) {          /* every pixel to a unique place: a permutation */
		UINT w = sz[0], h = sz[1];
		DWORD *src = (DWORD *)malloc(w * h * 4), *dst = (DWORD *)malloc(w * h * 4);
		for (UINT i = 0; i < w * h; i++) src[i] = i;
		memset(dst, 0xFF, w * h * 4);
		Swizzle_Rect((BYTE *)src, w * 4, (BYTE *)dst, w, h, 4);
		char *seen = (char *)calloc(w * h, 1); bool ok = true;
		for (UINT i = 0; i < w * h; i++) { if (dst[i] >= w * h || seen[dst[i]]) ok = false; else seen[dst[i]] = 1; }
		CHECK(ok);
		CHECK(dst[Swizzle_Offset(w - 1, h - 1, w, h)] == (h - 1) * w + (w - 1));
		free(src); free(dst); free(seen);
	}

	DWORD st[4][32]; bool tex[4] = { true, false, false, false }; CombinerSetup c;
	/* texture * diffuse, alpha from the texture (the most common setup) */
	Defaults(st);
	CHECK(Build_Combiners(st, 4, tex, 0xFFFFFFFF, false, &c));
	CHECK(c.stages == 1 && c.control == 1);
	CHECK(c.color_icw[0] == 0x08040000);        /* A = texture0, B = diffuse */
	CHECK(c.color_ocw[0] == 0x00000C00);        /* A*B + C*D -> spare0 */
	CHECK(c.alpha_icw[0] == 0x18200000);        /* A = texture0 alpha, B = 1 */
	CHECK(c.final_cw0 == 0x00000C00 && c.final_cw1 == 0x00001C00);
	/* every stage disabled: the diffuse color, as the step-2 setup */
	st[0][D3DTSS_COLOROP] = D3DTOP_DISABLE;
	Build_Combiners(st, 4, tex, 0, false, &c);
	CHECK(c.stages == 1 && c.color_icw[0] == 0x04200000);
	/* complement, add, subtract, x2, blend by texture alpha */
	Defaults(st); st[0][D3DTSS_COLOROP] = D3DTOP_SELECTARG1; st[0][D3DTSS_COLORARG1] = D3DTA_TEXTURE | D3DTA_COMPLEMENT;
	Build_Combiners(st, 4, tex, 0, false, &c); CHECK(c.color_icw[0] == 0x28200000);
	st[0][D3DTSS_COLOROP] = D3DTOP_ADD; st[0][D3DTSS_COLORARG1] = D3DTA_TEXTURE; st[0][D3DTSS_COLORARG2] = D3DTA_DIFFUSE;
	Build_Combiners(st, 4, tex, 0, false, &c); CHECK(c.color_icw[0] == 0x08200420);
	st[0][D3DTSS_COLOROP] = D3DTOP_SUBTRACT;
	Build_Combiners(st, 4, tex, 0, false, &c); CHECK(c.color_icw[0] == 0x08200440);   /* D = -1 */
	st[0][D3DTSS_COLOROP] = D3DTOP_MODULATE2X;
	Build_Combiners(st, 4, tex, 0, false, &c); CHECK(c.color_ocw[0] == ((2u << 15) | 0xC00));
	st[0][D3DTSS_COLOROP] = D3DTOP_BLENDTEXTUREALPHA;
	Build_Combiners(st, 4, tex, 0, false, &c); CHECK(c.color_icw[0] == 0x08180438);   /* tex*a + diff*(1-a) */
	/* two stages: stage 1 modulates its texture with the current result */
	Defaults(st); st[1][D3DTSS_COLOROP] = D3DTOP_MODULATE; st[1][D3DTSS_ALPHAOP] = D3DTOP_SELECTARG2;
	tex[1] = true;
	Build_Combiners(st, 4, tex, 0, false, &c);
	CHECK(c.stages == 2 && c.control == 2 && c.color_icw[1] == 0x090C0000 && c.alpha_icw[1] == 0x1C200000);
	/* specular added by the final combiner; untranslated operations reported */
	Build_Combiners(st, 4, tex, 0, true, &c); CHECK(c.final_cw0 == 0x00000C05);
	st[0][D3DTSS_COLOROP] = D3DTOP_LERP;
	CHECK(!Build_Combiners(st, 4, tex, 0, false, &c));
	printf(fails ? "%d FAILED\n" : "all texture swizzle and combiner tests passed\n", fails);
	return fails;
}

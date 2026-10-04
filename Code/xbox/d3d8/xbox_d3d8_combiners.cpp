/*
** xbox_d3d8_combiners.cpp -- pure helpers for the port's Direct3D layer (Xbox build only):
**
**   Swizzle_Rect     the NV2A's swizzled texture layout (Morton order: x and y bits interleaved,
**                    x first, until the smaller dimension runs out)
**   Build_Combiners  Direct3D 8 texture-stage states -> NV2A register combiner settings
**
** No hardware access here, so both are unit-tested natively.
*/
#include "xbox_d3d8_internal.h"

namespace XboxD3D {

bool Is_Power_Of_Two(UINT v)
{
	return v != 0 && (v & (v - 1)) == 0;
}

/* Spreads the low bits of value into the positions set in mask (a software "bit deposit"). */
static UINT Deposit(UINT value, UINT mask)
{
	UINT result = 0;
	for (UINT bit = 1; mask; bit <<= 1) {
		UINT lowest = mask & (0u - mask);
		if (value & bit) result |= lowest;
		mask &= mask - 1;
	}
	return result;
}

/* Address masks for a width x height swizzled image: bit by bit, x then y, while each lasts. */
static void Swizzle_Masks(UINT width, UINT height, UINT *mask_x, UINT *mask_y)
{
	UINT x = 0, y = 0, out_bit = 1;
	for (UINT bit = 1; bit < width || bit < height; bit <<= 1) {
		if (bit < width) { x |= out_bit; out_bit <<= 1; }
		if (bit < height) { y |= out_bit; out_bit <<= 1; }
	}
	*mask_x = x;
	*mask_y = y;
}

UINT Swizzle_Offset(UINT x, UINT y, UINT width, UINT height)
{
	UINT mx, my;
	Swizzle_Masks(width, height, &mx, &my);
	return Deposit(x, mx) | Deposit(y, my);
}

void Swizzle_Rect(const BYTE *src, UINT src_pitch, BYTE *dst, UINT width, UINT height, UINT bpp)
{
	UINT mx, my;
	Swizzle_Masks(width, height, &mx, &my);
	for (UINT y = 0; y < height; y++) {
		const BYTE *row = src + y * src_pitch;
		UINT oy = Deposit(y, my);
		for (UINT x = 0; x < width; x++) {
			memcpy(dst + (Deposit(x, mx) | oy) * bpp, row + x * bpp, bpp);
		}
	}
}

/* --- Texture stages -> register combiners --------------------------------------------------
**
** Each enabled Direct3D stage becomes one combiner stage computing A*B + C*D (with an optional
** output scale or bias) into spare0, which is "current" for the next stage. Register sources:
** 0 zero, 1 factor0 (TFACTOR), 4 vertex diffuse, 5 vertex specular, 8+n texture n, 0xC spare0,
** 0xD spare1 (D3DTA_TEMP). Input maps: 0 identity, 1 1-x, 2 2x-1 (used on zero to make -1).
*/

enum { SRC_ZERO = 0, SRC_FACTOR0 = 1, SRC_DIFFUSE = 4, SRC_SPECULAR = 5, SRC_TEX0 = 8, SRC_SPARE0 = 0xC, SRC_SPARE1 = 0xD };
enum { MAP_IDENTITY = 0, MAP_INVERT = 1, MAP_EXPAND = 2 };
enum { OP_NOSHIFT = 0, OP_NOSHIFT_BIAS = 1, OP_SHIFT1 = 2, OP_SHIFT1_BIAS = 3, OP_SHIFT2 = 4 };

struct Input { DWORD source; bool alpha; DWORD map; };

static Input Zero() { Input i = { SRC_ZERO, false, MAP_IDENTITY }; return i; }
static Input One()  { Input i = { SRC_ZERO, false, MAP_INVERT }; return i; }
static Input Minus_One() { Input i = { SRC_ZERO, false, MAP_EXPAND }; return i; }

/* A D3DTA_* argument for stage `stage`; for_alpha selects the argument's alpha. */
static Input Argument(DWORD arg, int stage, bool for_alpha)
{
	Input in;
	switch (arg & D3DTA_SELECTMASK) {
	case D3DTA_DIFFUSE:  in.source = SRC_DIFFUSE; break;
	case D3DTA_CURRENT:  in.source = stage == 0 ? SRC_DIFFUSE : SRC_SPARE0; break;
	case D3DTA_TEXTURE:  in.source = SRC_TEX0 + stage; break;
	case D3DTA_TFACTOR:  in.source = SRC_FACTOR0; break;
	case D3DTA_SPECULAR: in.source = SRC_SPECULAR; break;
	case D3DTA_TEMP:     in.source = SRC_SPARE1; break;
	default:             in.source = SRC_ZERO; break;
	}
	in.alpha = for_alpha || (arg & D3DTA_ALPHAREPLICATE) != 0;
	in.map = (arg & D3DTA_COMPLEMENT) ? MAP_INVERT : MAP_IDENTITY;
	return in;
}

static Input Alpha_Of(Input in) { in.alpha = true; in.map = MAP_IDENTITY; return in; }
static Input Inverted(Input in) { in.map = (in.map == MAP_INVERT) ? MAP_IDENTITY : MAP_INVERT; return in; }

static DWORD Pack_Input(const Input &in, int shift)
{
	return ((in.map << 5) | (in.alpha ? 0x10 : 0) | (in.source & 0xF)) << shift;
}

/* One stage, color or alpha: fills the input and output words. Returns false for operations
** this layer doesn't translate (the caller logs them). */
static bool Build_Stage(DWORD op, DWORD arg1, DWORD arg2, int stage, bool alpha, DWORD dest,
                        DWORD *icw, DWORD *ocw)
{
	Input a = Zero(), b = Zero(), c = Zero(), d = Zero();
	Input x = Argument(arg1, stage, alpha), y = Argument(arg2, stage, alpha);
	DWORD shift = OP_NOSHIFT;
	bool dot = false, ok = true;
	Input stage_alpha;
	switch (op) {
	case D3DTOP_SELECTARG1:        a = x; b = One(); break;
	case D3DTOP_SELECTARG2:        a = y; b = One(); break;
	case D3DTOP_MODULATE:          a = x; b = y; break;
	case D3DTOP_MODULATE2X:        a = x; b = y; shift = OP_SHIFT1; break;
	case D3DTOP_MODULATE4X:        a = x; b = y; shift = OP_SHIFT2; break;
	case D3DTOP_ADD:               a = x; b = One(); c = y; d = One(); break;
	case D3DTOP_ADDSIGNED:         a = x; b = One(); c = y; d = One(); shift = OP_NOSHIFT_BIAS; break;
	case D3DTOP_ADDSIGNED2X:       a = x; b = One(); c = y; d = One(); shift = OP_SHIFT1_BIAS; break;
	case D3DTOP_SUBTRACT:          a = x; b = One(); c = y; d = Minus_One(); break;
	case D3DTOP_ADDSMOOTH:         a = x; b = One(); c = y; d = Inverted(x); break;
	case D3DTOP_BLENDDIFFUSEALPHA: stage_alpha = Argument(D3DTA_DIFFUSE, stage, true); goto blend;
	case D3DTOP_BLENDTEXTUREALPHA: stage_alpha = Argument(D3DTA_TEXTURE, stage, true); goto blend;
	case D3DTOP_BLENDFACTORALPHA:  stage_alpha = Argument(D3DTA_TFACTOR, stage, true); goto blend;
	case D3DTOP_BLENDCURRENTALPHA: stage_alpha = Argument(D3DTA_CURRENT, stage, true); goto blend;
	blend:                         a = x; b = stage_alpha; c = y; d = Inverted(stage_alpha); break;
	case D3DTOP_BLENDTEXTUREALPHAPM:
		a = x; b = One(); c = y; d = Inverted(Argument(D3DTA_TEXTURE, stage, true)); break;
	case D3DTOP_MODULATEALPHA_ADDCOLOR:    a = x; b = One(); c = Alpha_Of(x); d = y; break;
	case D3DTOP_MODULATECOLOR_ADDALPHA:    a = x; b = y; c = Alpha_Of(x); d = One(); break;
	case D3DTOP_MODULATEINVALPHA_ADDCOLOR: a = x; b = One(); c = Inverted(Alpha_Of(x)); d = y; break;
	case D3DTOP_MODULATEINVCOLOR_ADDALPHA: a = Inverted(x); b = y; c = Alpha_Of(x); d = One(); break;
	case D3DTOP_DOTPRODUCT3:
		if (alpha) { a = x; b = One(); ok = false; break; }   /* color only in Direct3D's pipeline */
		a = x; a.map = MAP_EXPAND; b = y; b.map = MAP_EXPAND; dot = true; break;
	default:                       a = x; b = One(); ok = false; break;   /* untranslated: pass arg1 */
	}
	*icw = Pack_Input(a, 24) | Pack_Input(b, 16) | Pack_Input(c, 8) | Pack_Input(d, 0);
	if (dot) {
		/* the dot product goes to AB; its result is used as the stage's output */
		*ocw = (shift << 15) | (1u << 13) | (dest << 4);
	} else {
		*ocw = (shift << 15) | (dest << 8);    /* SUM_DST: A*B + C*D */
	}
	return ok;
}

bool Build_Combiners(const DWORD (*stage_states)[32], int max_stages, const bool *has_texture,
                     DWORD texture_factor, bool specular_enable, CombinerSetup *out)
{
	(void)has_texture;
	bool all_ok = true;
	memset(out, 0, sizeof(*out));
	int n = 0;
	for (int s = 0; s < max_stages && s < 4; s++) {
		const DWORD *st = stage_states[s];
		if (st[D3DTSS_COLOROP] == D3DTOP_DISABLE) break;
		DWORD dest = st[D3DTSS_RESULTARG] == D3DTA_TEMP ? SRC_SPARE1 : SRC_SPARE0;
		all_ok &= Build_Stage(st[D3DTSS_COLOROP], st[D3DTSS_COLORARG1], st[D3DTSS_COLORARG2], s, false, dest,
		                      &out->color_icw[n], &out->color_ocw[n]);
		DWORD alpha_op = st[D3DTSS_ALPHAOP];
		if (alpha_op == D3DTOP_DISABLE) {
			/* alpha carries through unchanged: current (diffuse for stage 0) */
			Build_Stage(D3DTOP_SELECTARG1, D3DTA_CURRENT, 0, s, true, dest, &out->alpha_icw[n], &out->alpha_ocw[n]);
		} else {
			all_ok &= Build_Stage(alpha_op, st[D3DTSS_ALPHAARG1], st[D3DTSS_ALPHAARG2], s, true, dest,
			                      &out->alpha_icw[n], &out->alpha_ocw[n]);
		}
		n++;
	}
	if (n == 0) {
		/* every stage disabled: output the diffuse color */
		Build_Stage(D3DTOP_SELECTARG1, D3DTA_DIFFUSE, 0, 0, false, SRC_SPARE0, &out->color_icw[0], &out->color_ocw[0]);
		Build_Stage(D3DTOP_SELECTARG1, D3DTA_DIFFUSE, 0, 0, true, SRC_SPARE0, &out->alpha_icw[0], &out->alpha_ocw[0]);
		n = 1;
	}
	out->stages = n;
	out->control = n;           /* iteration count; factor0 shared by all stages */
	out->factor0 = texture_factor;
	/* Final combiner: rgb = A*B + (1-A)*C + D with A=B=0, C=spare0, D=specular when enabled;
	** alpha = G = spare0's alpha. */
	out->final_cw0 = (SRC_SPARE0 << 8) | (specular_enable ? SRC_SPECULAR : SRC_ZERO);
	out->final_cw1 = (1u << 12) | (SRC_SPARE0 << 8);
	return all_ok;
}

} /* namespace XboxD3D */

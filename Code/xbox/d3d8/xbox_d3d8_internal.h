/*
** xbox_d3d8_internal.h -- shared helpers for the port's Direct3D 8 layer (Xbox build only).
**
** Format rules (bytes per pixel, DXT block sizes, row pitch) and GPU-visible memory.
*/
#ifndef XBOX_D3D8_INTERNAL_H
#define XBOX_D3D8_INTERNAL_H

#include <d3d8.h>
#include <d3dx8.h>
#include <string.h>

namespace XboxD3D {

struct FormatInfo {
	bool known;            /* a format this layer can store */
	bool compressed;       /* DXT: stored in 4x4 blocks */
	UINT bytes_per_pixel;  /* uncompressed formats */
	UINT block_bytes;      /* compressed formats: bytes per 4x4 block */
};

inline FormatInfo Format_Info(D3DFORMAT format)
{
	FormatInfo i = { true, false, 0, 0 };
	switch (format) {
	case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: case D3DFMT_D24S8: case D3DFMT_D24X8:
	case D3DFMT_D32: case D3DFMT_X8L8V8U8: case D3DFMT_Q8W8V8U8: case D3DFMT_V16U16:
	case D3DFMT_W11V11U10:
		i.bytes_per_pixel = 4; break;
	case D3DFMT_R8G8B8:
		i.bytes_per_pixel = 3; break;
	case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4:
	case D3DFMT_X4R4G4B4: case D3DFMT_A8R3G3B2: case D3DFMT_A8L8: case D3DFMT_A8P8:
	case D3DFMT_V8U8: case D3DFMT_L6V5U5: case D3DFMT_D16: case D3DFMT_D16_LOCKABLE:
	case D3DFMT_D15S1: case D3DFMT_INDEX16:
		i.bytes_per_pixel = 2; break;
	case D3DFMT_A8: case D3DFMT_L8: case D3DFMT_P8: case D3DFMT_A4L4: case D3DFMT_R3G3B2:
		i.bytes_per_pixel = 1; break;
	case D3DFMT_DXT1:
		i.compressed = true; i.block_bytes = 8; break;
	case D3DFMT_DXT2: case D3DFMT_DXT3: case D3DFMT_DXT4: case D3DFMT_DXT5:
		i.compressed = true; i.block_bytes = 16; break;
	default:
		i.known = false; break;
	}
	return i;
}

/* Bytes per row (for DXT: per row of 4x4 blocks), as Direct3D reports in D3DLOCKED_RECT.Pitch. */
inline UINT Row_Pitch(D3DFORMAT format, UINT width)
{
	FormatInfo i = Format_Info(format);
	if (i.compressed) return ((width + 3) / 4) * i.block_bytes;
	return width * i.bytes_per_pixel;
}

/* Number of rows in memory (for DXT: rows of blocks). */
inline UINT Row_Count(D3DFORMAT format, UINT height)
{
	return Format_Info(format).compressed ? (height + 3) / 4 : height;
}

inline UINT Level_Size(D3DFORMAT format, UINT width, UINT height)
{
	return Row_Pitch(format, width) * Row_Count(format, height);
}

/* Textures: the NV2A's swizzled layout (xbox_d3d8_combiners.cpp) */
bool Is_Power_Of_Two(UINT v);
UINT Swizzle_Offset(UINT x, UINT y, UINT width, UINT height);
void Swizzle_Rect(const BYTE *src, UINT src_pitch, BYTE *dst, UINT width, UINT height, UINT bytes_per_pixel);

/* Direct3D texture-stage states -> register combiner settings (xbox_d3d8_combiners.cpp).
** Returns false if some operation wasn't translated (it then passes its first argument). */
struct CombinerSetup {
	UINT stages;
	DWORD color_icw[4], color_ocw[4], alpha_icw[4], alpha_ocw[4];
	DWORD control, factor0, final_cw0, final_cw1;
};
bool Build_Combiners(const DWORD (*stage_states)[32], int max_stages, const bool *has_texture,
                     DWORD texture_factor, bool specular_enable, CombinerSetup *out);

/* Memory the GPU can read: physically contiguous, write-combined (as nxdk's samples use).
** The GPU addresses physical memory, so textures and vertex data must be contiguous. */
void *Alloc_GPU_Memory(UINT size);
void Free_GPU_Memory(void *memory);

/* Logs a feature the layer does not provide yet, once per call site. */
void Log_Once(bool *done, const char *what);

/* Start-up progress markers: shown on screen when a test program switches PORT_TRACE on
** (see xbox_port.h); nothing in the game. */
void Trace(const char *format, ...);

} /* namespace XboxD3D */

#endif /* XBOX_D3D8_INTERNAL_H */

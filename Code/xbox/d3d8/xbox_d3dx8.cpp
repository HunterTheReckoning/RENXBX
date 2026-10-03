/*
** xbox_d3dx8.cpp -- the D3DX texture helpers for the port's Direct3D layer (Xbox build only).
**
** D3DXLoadSurfaceFromSurface converts between the uncompressed formats through 32-bit ARGB
** and resizes with a box (average) or point filter; this is what the engine uses to build
** mipmaps and convert textures. DXT surfaces are copied when no conversion or resizing is
** needed; anything that would need DXT compression or decompression is not done yet.
*/
#include "xbox_d3d8_internal.h"
#include <stdio.h>

namespace XboxD3D {

/* --- Pixel conversion through A8R8G8B8 ------------------------------------------------------ */

static inline DWORD Expand(DWORD value, int bits)
{
	/* Scale an n-bit channel to 8 bits by repeating its high bits, as hardware does. */
	if (bits == 0) return 0;
	value <<= (8 - bits);
	return value | (value >> bits);
}

static bool Read_Pixel(D3DFORMAT format, const BYTE *p, DWORD *argb)
{
	DWORD v;
	switch (format) {
	case D3DFMT_A8R8G8B8: *argb = *(const DWORD *)p; return true;
	case D3DFMT_X8R8G8B8: *argb = *(const DWORD *)p | 0xFF000000; return true;
	case D3DFMT_R8G8B8:   *argb = 0xFF000000 | ((DWORD)p[2] << 16) | ((DWORD)p[1] << 8) | p[0]; return true;
	case D3DFMT_R5G6B5:
		v = *(const WORD *)p;
		*argb = 0xFF000000 | (Expand((v >> 11) & 31, 5) << 16) | (Expand((v >> 5) & 63, 6) << 8) | Expand(v & 31, 5);
		return true;
	case D3DFMT_X1R5G5B5:
	case D3DFMT_A1R5G5B5:
		v = *(const WORD *)p;
		*argb = ((format == D3DFMT_X1R5G5B5 || (v & 0x8000)) ? 0xFF000000 : 0) |
		        (Expand((v >> 10) & 31, 5) << 16) | (Expand((v >> 5) & 31, 5) << 8) | Expand(v & 31, 5);
		return true;
	case D3DFMT_A4R4G4B4:
		v = *(const WORD *)p;
		*argb = (Expand((v >> 12) & 15, 4) << 24) | (Expand((v >> 8) & 15, 4) << 16) |
		        (Expand((v >> 4) & 15, 4) << 8) | Expand(v & 15, 4);
		return true;
	case D3DFMT_A8: *argb = (DWORD)p[0] << 24; return true;
	case D3DFMT_L8: *argb = 0xFF000000 | ((DWORD)p[0] * 0x010101); return true;
	case D3DFMT_A8L8:
		v = *(const WORD *)p;
		*argb = ((v >> 8) << 24) | ((v & 0xFF) * 0x010101);
		return true;
	default:
		return false;
	}
}

static bool Write_Pixel(D3DFORMAT format, BYTE *p, DWORD argb)
{
	DWORD a = argb >> 24, r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
	switch (format) {
	case D3DFMT_A8R8G8B8: *(DWORD *)p = argb; return true;
	case D3DFMT_X8R8G8B8: *(DWORD *)p = argb | 0xFF000000; return true;
	case D3DFMT_R8G8B8:   p[0] = (BYTE)b; p[1] = (BYTE)g; p[2] = (BYTE)r; return true;
	case D3DFMT_R5G6B5:   *(WORD *)p = (WORD)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); return true;
	case D3DFMT_X1R5G5B5: *(WORD *)p = (WORD)(0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)); return true;
	case D3DFMT_A1R5G5B5: *(WORD *)p = (WORD)((a >= 128 ? 0x8000 : 0) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)); return true;
	case D3DFMT_A4R4G4B4: *(WORD *)p = (WORD)(((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)); return true;
	case D3DFMT_A8:       p[0] = (BYTE)a; return true;
	case D3DFMT_L8:       p[0] = (BYTE)((r * 77 + g * 150 + b * 29) >> 8); return true;      /* luminance */
	case D3DFMT_A8L8:     *(WORD *)p = (WORD)((a << 8) | ((r * 77 + g * 150 + b * 29) >> 8)); return true;
	default:
		return false;
	}
}

static bool Is_Convertible(D3DFORMAT format)
{
	DWORD dummy;
	BYTE zero[4] = { 0, 0, 0, 0 };
	return Read_Pixel(format, zero, &dummy);
}

} /* namespace XboxD3D */

using namespace XboxD3D;

HRESULT D3DXCreateTexture(IDirect3DDevice8 *pDevice, UINT Width, UINT Height, UINT MipLevels, DWORD Usage,
                          D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture8 **ppTexture)
{
	if (!pDevice || !ppTexture) return D3DERR_INVALIDCALL;
	if (Width == D3DX_DEFAULT) Width = 256;
	if (Height == D3DX_DEFAULT) Height = 256;
	if (MipLevels == D3DX_DEFAULT) MipLevels = 0;     /* both mean the full mipmap chain */
	return pDevice->CreateTexture(Width, Height, MipLevels, Usage, Format, Pool, ppTexture);
}

HRESULT D3DXLoadSurfaceFromSurface(IDirect3DSurface8 *pDestSurface, CONST PALETTEENTRY *,
                                   CONST RECT *pDestRect, IDirect3DSurface8 *pSrcSurface,
                                   CONST PALETTEENTRY *, CONST RECT *pSrcRect, DWORD Filter, D3DCOLOR)
{
	if (!pDestSurface || !pSrcSurface) return D3DERR_INVALIDCALL;
	D3DSURFACE_DESC sd, dd;
	pSrcSurface->GetDesc(&sd);
	pDestSurface->GetDesc(&dd);
	RECT src_rect = { 0, 0, (LONG)sd.Width, (LONG)sd.Height };
	RECT dst_rect = { 0, 0, (LONG)dd.Width, (LONG)dd.Height };
	if (pSrcRect) src_rect = *pSrcRect;
	if (pDestRect) dst_rect = *pDestRect;
	int sw = src_rect.right - src_rect.left, sh = src_rect.bottom - src_rect.top;
	int dw = dst_rect.right - dst_rect.left, dh = dst_rect.bottom - dst_rect.top;
	if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return D3DERR_INVALIDCALL;

	/* Same format and size: a plain copy (this also covers DXT surfaces). */
	if (sd.Format == dd.Format && sw == dw && sh == dh) {
		POINT at = { dst_rect.left, dst_rect.top };
		IDirect3DDevice8 *device = NULL;
		pDestSurface->GetDevice(&device);
		HRESULT hr = device ? device->CopyRects(pSrcSurface, &src_rect, 1, pDestSurface, &at) : D3DERR_INVALIDCALL;
		if (device) device->Release();
		return hr;
	}
	if (!Is_Convertible(sd.Format) || !Is_Convertible(dd.Format)) {
		static bool logged;
		Log_Once(&logged, "D3DXLoadSurfaceFromSurface: DXT conversion or resizing not implemented yet");
		return D3DERR_NOTAVAILABLE;
	}

	D3DLOCKED_RECT src, dst;
	if (FAILED(pSrcSurface->LockRect(&src, NULL, D3DLOCK_READONLY))) return D3DERR_INVALIDCALL;
	if (FAILED(pDestSurface->LockRect(&dst, NULL, 0))) {
		pSrcSurface->UnlockRect();
		return D3DERR_INVALIDCALL;
	}
	const UINT sbpp = Format_Info(sd.Format).bytes_per_pixel;
	const UINT dbpp = Format_Info(dd.Format).bytes_per_pixel;
	const bool point = (Filter & 0xFFFF) == D3DX_FILTER_POINT || (Filter & 0xFFFF) == D3DX_FILTER_NONE;

	for (int y = 0; y < dh; y++) {
		/* Source rows covered by this destination row (at least one). */
		int y0 = src_rect.top + (y * sh) / dh;
		int y1 = src_rect.top + ((y + 1) * sh) / dh;
		if (y1 <= y0) y1 = y0 + 1;
		BYTE *drow = (BYTE *)dst.pBits + (dst_rect.top + y) * dst.Pitch;
		for (int x = 0; x < dw; x++) {
			int x0 = src_rect.left + (x * sw) / dw;
			int x1 = src_rect.left + ((x + 1) * sw) / dw;
			if (x1 <= x0) x1 = x0 + 1;
			DWORD out;
			if (point) {
				Read_Pixel(sd.Format, (const BYTE *)src.pBits + y0 * src.Pitch + x0 * sbpp, &out);
			} else {
				/* Box filter: average every source pixel covered by this destination pixel. */
				DWORD sum[4] = { 0, 0, 0, 0 }, count = 0;
				for (int sy = y0; sy < y1; sy++) {
					const BYTE *srow = (const BYTE *)src.pBits + sy * src.Pitch;
					for (int sx = x0; sx < x1; sx++) {
						DWORD c;
						Read_Pixel(sd.Format, srow + sx * sbpp, &c);
						sum[0] += c >> 24; sum[1] += (c >> 16) & 0xFF; sum[2] += (c >> 8) & 0xFF; sum[3] += c & 0xFF;
						count++;
					}
				}
				DWORD half = count / 2;
				out = (((sum[0] + half) / count) << 24) | (((sum[1] + half) / count) << 16) |
				      (((sum[2] + half) / count) << 8) | ((sum[3] + half) / count);
			}
			Write_Pixel(dd.Format, drow + (dst_rect.left + x) * dbpp, out);
		}
	}
	pDestSurface->UnlockRect();
	pSrcSurface->UnlockRect();
	return D3D_OK;
}

/* Fills the levels below SrcLevel from the level above each, as D3DX does. */
HRESULT D3DXFilterTexture(IDirect3DBaseTexture8 *pBaseTexture, CONST PALETTEENTRY *, UINT SrcLevel, DWORD Filter)
{
	if (!pBaseTexture || pBaseTexture->GetType() != D3DRTYPE_TEXTURE) return D3DERR_INVALIDCALL;
	IDirect3DTexture8 *texture = static_cast<IDirect3DTexture8 *>(pBaseTexture);
	if (SrcLevel == D3DX_DEFAULT) SrcLevel = 0;
	if (Filter == D3DX_DEFAULT) Filter = D3DX_FILTER_BOX;
	for (UINT level = SrcLevel + 1; level < texture->GetLevelCount(); level++) {
		IDirect3DSurface8 *src = NULL, *dst = NULL;
		texture->GetSurfaceLevel(level - 1, &src);
		texture->GetSurfaceLevel(level, &dst);
		HRESULT hr = D3DXLoadSurfaceFromSurface(dst, NULL, NULL, src, NULL, NULL, Filter, 0);
		src->Release();
		dst->Release();
		if (FAILED(hr)) return hr;
	}
	return D3D_OK;
}

HRESULT D3DXCreateTextureFromFileExA(IDirect3DDevice8 *, const char *pSrcFile, UINT, UINT, UINT, DWORD,
                                     D3DFORMAT, D3DPOOL, DWORD, DWORD, D3DCOLOR, D3DXIMAGE_INFO *,
                                     PALETTEENTRY *, IDirect3DTexture8 **ppTexture)
{
	/* The engine loads its textures through its own DDS and Targa readers; this D3DX path
	** (loading straight from a file on disk) is not provided yet. */
	static bool logged;
	Log_Once(&logged, "D3DXCreateTextureFromFileExA: not implemented yet");
	(void)pSrcFile;
	if (ppTexture) *ppTexture = NULL;
	return D3DERR_NOTAVAILABLE;
}

HRESULT D3DXGetErrorStringA(HRESULT hr, char *pBuffer, UINT BufferLen)
{
	if (!pBuffer || BufferLen == 0) return D3DERR_INVALIDCALL;
	const char *name = NULL;
	switch (hr) {
	case D3D_OK:                         name = "D3D_OK"; break;
	case D3DERR_WRONGTEXTUREFORMAT:      name = "D3DERR_WRONGTEXTUREFORMAT"; break;
	case D3DERR_UNSUPPORTEDCOLOROPERATION: name = "D3DERR_UNSUPPORTEDCOLOROPERATION"; break;
	case D3DERR_UNSUPPORTEDCOLORARG:     name = "D3DERR_UNSUPPORTEDCOLORARG"; break;
	case D3DERR_UNSUPPORTEDALPHAOPERATION: name = "D3DERR_UNSUPPORTEDALPHAOPERATION"; break;
	case D3DERR_UNSUPPORTEDALPHAARG:     name = "D3DERR_UNSUPPORTEDALPHAARG"; break;
	case D3DERR_TOOMANYOPERATIONS:       name = "D3DERR_TOOMANYOPERATIONS"; break;
	case D3DERR_CONFLICTINGTEXTUREFILTER: name = "D3DERR_CONFLICTINGTEXTUREFILTER"; break;
	case D3DERR_UNSUPPORTEDFACTORVALUE:  name = "D3DERR_UNSUPPORTEDFACTORVALUE"; break;
	case D3DERR_CONFLICTINGRENDERSTATE:  name = "D3DERR_CONFLICTINGRENDERSTATE"; break;
	case D3DERR_UNSUPPORTEDTEXTUREFILTER: name = "D3DERR_UNSUPPORTEDTEXTUREFILTER"; break;
	case D3DERR_CONFLICTINGTEXTUREPALETTE: name = "D3DERR_CONFLICTINGTEXTUREPALETTE"; break;
	case D3DERR_DRIVERINTERNALERROR:     name = "D3DERR_DRIVERINTERNALERROR"; break;
	case D3DERR_NOTFOUND:                name = "D3DERR_NOTFOUND"; break;
	case D3DERR_MOREDATA:                name = "D3DERR_MOREDATA"; break;
	case D3DERR_DEVICELOST:              name = "D3DERR_DEVICELOST"; break;
	case D3DERR_DEVICENOTRESET:          name = "D3DERR_DEVICENOTRESET"; break;
	case D3DERR_NOTAVAILABLE:            name = "D3DERR_NOTAVAILABLE"; break;
	case D3DERR_OUTOFVIDEOMEMORY:        name = "D3DERR_OUTOFVIDEOMEMORY"; break;
	case D3DERR_INVALIDDEVICE:           name = "D3DERR_INVALIDDEVICE"; break;
	case D3DERR_INVALIDCALL:             name = "D3DERR_INVALIDCALL"; break;
	case D3DERR_DRIVERINVALIDCALL:       name = "D3DERR_DRIVERINVALIDCALL"; break;
	case E_OUTOFMEMORY:                  name = "E_OUTOFMEMORY"; break;
	default: break;
	}
	if (name) {
		strncpy(pBuffer, name, BufferLen - 1);
		pBuffer[BufferLen - 1] = 0;
	} else {
		snprintf(pBuffer, BufferLen, "HRESULT 0x%08lX", (unsigned long)hr);
	}
	return D3D_OK;
}

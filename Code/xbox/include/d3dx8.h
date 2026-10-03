/*
** d3dx8.h -- the D3DX helpers Renegade uses (Xbox build only).
**
** The math helpers and D3DXGetFVFVertexSize are pure computation and are implemented here
** in full, with D3DX's conventions (row vectors: v' = v * M). The texture helpers need the
** GPU and are provided by the port's Direct3D layer.
*/
#ifndef D3DX8_H
#define D3DX8_H

#include "d3d8.h"

#ifdef __cplusplus

#define D3DX_DEFAULT ((UINT)-1)

#define D3DX_FILTER_NONE     (1 << 0)
#define D3DX_FILTER_POINT    (2 << 0)
#define D3DX_FILTER_LINEAR   (3 << 0)
#define D3DX_FILTER_TRIANGLE (4 << 0)
#define D3DX_FILTER_BOX      (5 << 0)
#define D3DX_FILTER_MIRROR_U (1 << 16)
#define D3DX_FILTER_MIRROR_V (2 << 16)
#define D3DX_FILTER_DITHER   (8 << 16)

struct D3DXVECTOR3 : public D3DVECTOR {
	D3DXVECTOR3() {}
	operator float *() { return &x; }               /* D3DX vectors index as float arrays */
	operator const float *() const { return &x; }
	D3DXVECTOR3(float fx, float fy, float fz) { x = fx; y = fy; z = fz; }
	D3DXVECTOR3(const D3DVECTOR &v) { x = v.x; y = v.y; z = v.z; }
};

struct D3DXVECTOR4 {
	float x, y, z, w;
	D3DXVECTOR4() {}
	operator float *() { return &x; }
	operator const float *() const { return &x; }
	D3DXVECTOR4(float fx, float fy, float fz, float fw) : x(fx), y(fy), z(fz), w(fw) {}
};

struct D3DXMATRIX : public D3DMATRIX {
	D3DXMATRIX() {}
	D3DXMATRIX(const D3DMATRIX &mat) { *(D3DMATRIX *)this = mat; }
	D3DXMATRIX(const float *f)
	{
		for (int i = 0; i < 16; i++) (&_11)[i] = f[i];
	}
	float &operator()(UINT row, UINT col) { return m[row][col]; }
	float operator()(UINT row, UINT col) const { return m[row][col]; }
	operator float *() { return &_11; }
	operator const float *() const { return &_11; }

	/* Matrix product, as D3DX defines it: (A * B)[i][j] = sum over k of A[i][k] * B[k][j]. */
	D3DXMATRIX operator*(const D3DXMATRIX &b) const
	{
		D3DXMATRIX r;
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				r.m[i][j] = m[i][0] * b.m[0][j] + m[i][1] * b.m[1][j] +
				            m[i][2] * b.m[2][j] + m[i][3] * b.m[3][j];
			}
		}
		return r;
	}
	D3DXMATRIX &operator*=(const D3DXMATRIX &b) { *this = *this * b; return *this; }
};

/* out = transpose(m). Safe when out and m are the same matrix. */
inline D3DXMATRIX *D3DXMatrixTranspose(D3DXMATRIX *pOut, const D3DXMATRIX *pM)
{
	D3DXMATRIX t;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) t.m[i][j] = pM->m[j][i];
	}
	*pOut = t;
	return pOut;
}

/* out = (x, y, z, 1) * M, as a 4-component result (no divide by w). */
inline D3DXVECTOR4 *D3DXVec3Transform(D3DXVECTOR4 *pOut, const D3DXVECTOR3 *pV, const D3DXMATRIX *pM)
{
	const float x = pV->x, y = pV->y, z = pV->z;
	D3DXVECTOR4 r;
	r.x = x * pM->_11 + y * pM->_21 + z * pM->_31 + pM->_41;
	r.y = x * pM->_12 + y * pM->_22 + z * pM->_32 + pM->_42;
	r.z = x * pM->_13 + y * pM->_23 + z * pM->_33 + pM->_43;
	r.w = x * pM->_14 + y * pM->_24 + z * pM->_34 + pM->_44;
	*pOut = r;
	return pOut;
}

/* Size in bytes of one vertex in the given flexible vertex format. */
inline UINT D3DXGetFVFVertexSize(DWORD FVF)
{
	UINT size = 0;
	switch (FVF & D3DFVF_POSITION_MASK) {
	case D3DFVF_XYZ:    size = 12; break;
	case D3DFVF_XYZRHW: size = 16; break;
	case D3DFVF_XYZB1:  size = 16; break;
	case D3DFVF_XYZB2:  size = 20; break;
	case D3DFVF_XYZB3:  size = 24; break;
	case D3DFVF_XYZB4:  size = 28; break;
	case D3DFVF_XYZB5:  size = 32; break;
	default:            break;
	}
	if (FVF & D3DFVF_NORMAL)   size += 12;
	if (FVF & D3DFVF_PSIZE)    size += 4;
	if (FVF & D3DFVF_DIFFUSE)  size += 4;
	if (FVF & D3DFVF_SPECULAR) size += 4;
	UINT tex_count = (FVF & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
	for (UINT i = 0; i < tex_count; i++) {
		switch ((FVF >> (16 + i * 2)) & 3) {
		case D3DFVF_TEXTUREFORMAT1: size += 4;  break;
		case D3DFVF_TEXTUREFORMAT2: size += 8;  break;
		case D3DFVF_TEXTUREFORMAT3: size += 12; break;
		case D3DFVF_TEXTUREFORMAT4: size += 16; break;
		}
	}
	return size;
}

typedef enum _D3DXIMAGE_FILEFORMAT {
	D3DXIFF_BMP = 0,
	D3DXIFF_JPG = 1,
	D3DXIFF_TGA = 2,
	D3DXIFF_PNG = 3,
	D3DXIFF_DDS = 4,
	D3DXIFF_PPM = 5,
	D3DXIFF_DIB = 6,
	D3DXIFF_FORCE_DWORD = 0x7fffffff
} D3DXIMAGE_FILEFORMAT;

typedef struct _D3DXIMAGE_INFO {
	UINT Width;
	UINT Height;
	UINT Depth;
	UINT MipLevels;
	D3DFORMAT Format;
	D3DRESOURCETYPE ResourceType;
	D3DXIMAGE_FILEFORMAT ImageFileFormat;
} D3DXIMAGE_INFO;

/* Texture helpers, provided by the port's Direct3D layer. */
HRESULT D3DXCreateTexture(IDirect3DDevice8 *pDevice, UINT Width, UINT Height, UINT MipLevels, DWORD Usage,
                          D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture8 **ppTexture);
HRESULT D3DXCreateTextureFromFileExA(IDirect3DDevice8 *pDevice, const char *pSrcFile, UINT Width, UINT Height,
                                     UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, DWORD Filter,
                                     DWORD MipFilter, D3DCOLOR ColorKey, D3DXIMAGE_INFO *pSrcInfo,
                                     PALETTEENTRY *pPalette, IDirect3DTexture8 **ppTexture);
HRESULT D3DXLoadSurfaceFromSurface(IDirect3DSurface8 *pDestSurface, CONST PALETTEENTRY *pDestPalette,
                                   CONST RECT *pDestRect, IDirect3DSurface8 *pSrcSurface,
                                   CONST PALETTEENTRY *pSrcPalette, CONST RECT *pSrcRect, DWORD Filter,
                                   D3DCOLOR ColorKey);
HRESULT D3DXFilterTexture(IDirect3DBaseTexture8 *pBaseTexture, CONST PALETTEENTRY *pPalette, UINT SrcLevel,
                          DWORD Filter);
HRESULT D3DXGetErrorStringA(HRESULT hr, char *pBuffer, UINT BufferLen);

#endif /* __cplusplus */

#endif /* D3DX8_H */

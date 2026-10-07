#include "d3dx8.h"
#include <stdio.h>
#include <math.h>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static bool near(float a, float b) { return fabsf(a - b) < 1e-4f; }
int main() {
	float a[16], b[16];
	for (int i = 0; i < 16; i++) { a[i] = (float)(i + 1); b[i] = (float)((i * 7) % 5 - 2); }
	D3DXMATRIX A(a), B(b), C = A * B;
	for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {          /* textbook row-major product */
		float s = 0; for (int k = 0; k < 4; k++) s += a[i*4+k] * b[k*4+j];
		CHECK(near(C.m[i][j], s));
	}
	D3DXMATRIX T = A; D3DXMatrixTranspose(&T, &T);                       /* in place */
	for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) CHECK(T.m[i][j] == A.m[j][i]);
	/* row vector times matrix: translation lives in row 4 (_41.._43) */
	float tr[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 10,20,30,1};
	D3DXMATRIX M(tr); D3DXVECTOR3 v(1, 2, 3); D3DXVECTOR4 out;
	D3DXVec3Transform(&out, &v, &M);
	CHECK(near(out.x, 11) && near(out.y, 22) && near(out.z, 33) && near(out.w, 1));
	D3DXVECTOR4 o2; D3DXVec3Transform(&o2, &v, &C);                      /* general case */
	CHECK(near(o2.x, 1*C._11 + 2*C._21 + 3*C._31 + C._41) && near(o2.w, 1*C._14 + 2*C._24 + 3*C._34 + C._44));
	/* vertex sizes for the formats the engine builds (dx8fvf.h / dx8vertexbuffer.cpp) */
	CHECK(D3DXGetFVFVertexSize(D3DFVF_XYZ | D3DFVF_NORMAL) == 24);
	CHECK(D3DXGetFVFVertexSize(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1) == 32);
	CHECK(D3DXGetFVFVertexSize(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX2) == 40);
	CHECK(D3DXGetFVFVertexSize(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1 | D3DFVF_DIFFUSE) == 36);
	CHECK(D3DXGetFVFVertexSize(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX2 | D3DFVF_DIFFUSE) == 44);
	CHECK(D3DXGetFVFVertexSize(D3DFVF_XYZ | D3DFVF_TEX1 | D3DFVF_DIFFUSE) == 24);
	CHECK(D3DXGetFVFVertexSize(D3DFVF_XYZ | D3DFVF_TEX2 | D3DFVF_DIFFUSE) == 32);
	CHECK(D3DXGetFVFVertexSize(D3DFVF_XYZ | D3DFVF_TEX1) == 20);
	CHECK(D3DXGetFVFVertexSize(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1) == 28);
	CHECK(D3DXGetFVFVertexSize(D3DFVF_XYZ | D3DFVF_TEX2 | D3DFVF_TEXCOORDSIZE1(0) | D3DFVF_TEXCOORDSIZE4(1)) == 12 + 4 + 16);
	CHECK(D3DXGetFVFVertexSize(D3DFVF_XYZB4 | D3DFVF_LASTBETA_UBYTE4 | D3DFVF_NORMAL | D3DFVF_TEX1) == 28 + 12 + 8);
	/* the engine indexes arrays with these: real SDK values, inside the array bounds */
	CHECK(D3DRS_NORMALORDER < 256 && D3DTSS_RESULTARG < 32 && D3DFMT_X8L8V8U8 == 62 && D3DFMT_A8L8 == 51);
	CHECK(D3DTS_WORLD == 256 && D3DTS_TEXTURE0 == 16 && D3DFMT_DXT1 == 0x31545844);
	CHECK(sizeof(D3DMATRIX) == 64 && sizeof(D3DXMATRIX) == 64 && sizeof(D3DXVECTOR3) == 12);
	printf(fails ? "%d FAILED\n" : "all D3DX math tests passed\n", fails);
	return fails;
}

#include "xbox_d3d8_internal.h"
#include "xbox_d3d8_stats.h"
#include <pbkit/pbkit.h>
#include <stdio.h>
#include <string.h>
static const uint32_t FFP_MICROCODE[] = {
#include "xbox_ffp_vs.inl"
};
static const int FFP_WORDS_EXPECTED = sizeof(FFP_MICROCODE) / sizeof(FFP_MICROCODE[0]);
int g_allocs, g_frees, g_video_w, g_video_h, g_video_sets, g_front_shown, g_push_count, g_pb_init, g_pb_reset, g_pb_finished, g_pb_kill, g_vbl;
PushRecord g_push[65536]; uint32_t g_cmd[4096]; uint32_t *g_cmd_start; PB_GAMMA_RAMP g_gamma; uint32_t g_backbuffer[640 * 480];
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static DWORD pushed(DWORD method, int from = 0) { for (int i = g_push_count - 1; i >= from; i--) if (g_push[i].method == method) return g_push[i].value; return 0xDEADBEEF; }

int main() {
	CHECK(Direct3DCreate8(219) == NULL);                                   /* wrong SDK version */
	IDirect3D8 *d3d = Direct3DCreate8(D3D_SDK_VERSION);
	CHECK(d3d && d3d->GetAdapterCount() == 1 && d3d->GetAdapterModeCount(0) == 1);
	D3DDISPLAYMODE mode; CHECK(d3d->EnumAdapterModes(0, 0, &mode) == D3D_OK);
	CHECK(mode.Width == 640 && mode.Height == 480 && mode.Format == D3DFMT_X8R8G8B8);
	D3DADAPTER_IDENTIFIER8 id; d3d->GetAdapterIdentifier(0, 0, &id);
	CHECK(id.VendorId == 0);                                                /* skips the engine's vendor hacks */
	D3DCAPS8 caps; d3d->GetDeviceCaps(0, D3DDEVTYPE_HAL, &caps);
	CHECK(caps.MaxSimultaneousTextures == 4 && caps.MaxVertexIndex == 0xFFFF && (caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT));
	/* the engine's mode search: A8R8G8B8 mode not offered, X8R8G8B8 is; Z: D32 refused, D24S8 accepted */
	CHECK(d3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, D3DFMT_D32) != D3D_OK);
	CHECK(d3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, D3DFMT_D24S8) == D3D_OK);
	CHECK(d3d->CheckDepthStencilMatch(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DFMT_X8R8G8B8, D3DFMT_D24S8) == D3D_OK);
	CHECK(d3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0, D3DRTYPE_TEXTURE, D3DFMT_DXT1) == D3D_OK);
	CHECK(d3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0, D3DRTYPE_TEXTURE, D3DFMT_P8) != D3D_OK);

	D3DPRESENT_PARAMETERS pp; memset(&pp, 0, sizeof(pp));
	pp.BackBufferWidth = 640; pp.BackBufferHeight = 480; pp.BackBufferFormat = D3DFMT_X8R8G8B8;
	pp.EnableAutoDepthStencil = TRUE; pp.AutoDepthStencilFormat = D3DFMT_D24S8;
	IDirect3DDevice8 *dev = NULL;
	pp.Windowed = TRUE; CHECK(d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL, 0, &pp, &dev) != D3D_OK && dev == NULL);
	pp.Windowed = FALSE; CHECK(d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL, 0, &pp, &dev) == D3D_OK && dev);
	CHECK(g_pb_init == 1 && g_video_w == 640 && g_video_h == 480);
	CHECK(g_front_shown == 0);                     /* text screen stays visible until the first frame */
	int sets = g_video_sets;

	/* Clear before BeginScene (as WW3D::Begin_Render does): starts the frame, full viewport */
	CHECK(dev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0xFF336699, 1.0f, 0) == D3D_OK);
	CHECK(g_pb_reset == 1);
	CHECK(pushed(NV097_SET_CLEAR_RECT_HORIZONTAL) == ((639u << 16) | 0) && pushed(NV097_SET_CLEAR_RECT_VERTICAL) == ((479u << 16) | 0));
	CHECK(pushed(NV097_SET_ZSTENCIL_CLEAR_VALUE) == 0xFFFFFF00 && pushed(NV097_SET_COLOR_CLEAR_VALUE) == 0xFF336699);
	CHECK(pushed(NV097_CLEAR_SURFACE) == 0xF3);
	dev->BeginScene(); CHECK(g_pb_reset == 1);                                   /* same frame */
	/* viewport-sized clear, color only, half depth */
	D3DVIEWPORT8 vp = { 10, 20, 100, 50, 0.0f, 1.0f }; dev->SetViewport(&vp);
	int mark = g_push_count;
	dev->Clear(0, NULL, D3DCLEAR_ZBUFFER, 0, 0.5f, 7);
	CHECK(pushed(NV097_SET_CLEAR_RECT_HORIZONTAL, mark) == ((109u << 16) | 10) && pushed(NV097_SET_CLEAR_RECT_VERTICAL, mark) == ((69u << 16) | 20));
	CHECK(pushed(NV097_CLEAR_SURFACE, mark) == NV097_CLEAR_SURFACE_Z);
	CHECK((pushed(NV097_SET_ZSTENCIL_CLEAR_VALUE, mark) >> 8) == (DWORD)(0.5f * 0xFFFFFF) && (pushed(NV097_SET_ZSTENCIL_CLEAR_VALUE, mark) & 0xFF) == 7);
	mark = g_push_count; D3DRECT r = { -5, -5, 2000, 30 };                        /* clipped to the screen */
	dev->Clear(1, &r, D3DCLEAR_TARGET, 0, 1.0f, 0);
	CHECK(pushed(NV097_SET_CLEAR_RECT_HORIZONTAL, mark) == ((639u << 16) | 0) && pushed(NV097_SET_CLEAR_RECT_VERTICAL, mark) == ((29u << 16) | 0));
	dev->EndScene(); dev->Present(NULL, NULL, NULL, NULL);
	CHECK(g_pb_finished == 1 && g_front_shown == 1);
	dev->Present(NULL, NULL, NULL, NULL); CHECK(g_pb_finished == 1);           /* nothing to present */
	dev->BeginScene(); CHECK(g_pb_reset == 2);                                   /* next frame */
	dev->EndScene(); dev->Present(NULL, NULL, NULL, NULL);
	CHECK(g_front_shown == 1);                     /* shown once, not every frame */
	/* a second device in the same mode doesn't switch video mode again (that would wipe text) */
	IDirect3DDevice8 *dev2 = NULL; CHECK(d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL, 0, &pp, &dev2) == D3D_OK);
	CHECK(g_video_sets == sets); dev2->Release(); CHECK(g_pb_kill == 1); g_pb_kill = 0;

	/* drawing: program uploaded at device creation; a 16-bit indexed draw with an odd count */
	int prog_words = 0; for (int i = 0; i < g_push_count; i++) if (g_push[i].method >= NV097_SET_TRANSFORM_PROGRAM && g_push[i].method < NV097_SET_TRANSFORM_PROGRAM + 32 * 4) prog_words++;   /* a bank of 32 registers */
	CHECK(prog_words > 0 && prog_words % FFP_WORDS_EXPECTED == 0);   /* whole program, once per device created */
	{
		IDirect3DVertexBuffer8 *vb = NULL; IDirect3DIndexBuffer8 *ib = NULL;
		DWORD fvf = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;   /* 24-byte vertices */
		CHECK(dev->CreateVertexBuffer(4 * 24, 0, fvf, D3DPOOL_MANAGED, &vb) == D3D_OK);
		CHECK(dev->CreateIndexBuffer(5 * 2, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib) == D3D_OK);
		BYTE *ip; ib->Lock(0, 0, &ip, 0); WORD five[5] = { 0, 1, 2, 3, 2 }; memcpy(ip, five, 10); ib->Unlock();
		dev->SetVertexShader(fvf); dev->SetStreamSource(0, vb, 24); dev->SetIndices(ib, 1);
		D3DMATRIX scale2 = { { { 2,0,0,0, 0,2,0,0, 0,0,2,0, 0,0,0,1 } } };
		dev->SetTransform(D3DTS_WORLD, &scale2);
		D3DVIEWPORT8 full = { 0, 0, 640, 480, 0.0f, 1.0f }; dev->SetViewport(&full);
		dev->SetRenderState(D3DRS_LIGHTING, FALSE);
		mark = g_push_count;
		/* 3 strip triangles = 5 indices; starting at index 1 would read past the 5-index buffer */
		CHECK(dev->DrawIndexedPrimitive(D3DPT_TRIANGLESTRIP, 0, 4, 1, 3) == D3DERR_INVALIDCALL);
		mark = g_push_count;
		CHECK(dev->DrawIndexedPrimitive(D3DPT_TRIANGLESTRIP, 0, 4, 0, 3) == D3D_OK);
		DWORD begin = 0xDEAD, end_seen = 0, pairs[4], npairs = 0, single = 0xDEAD;
		for (int i = mark; i < g_push_count; i++) {
			if (g_push[i].method == NV097_SET_BEGIN_END && g_push[i].value != 0) begin = g_push[i].value;
			if (g_push[i].method == NV097_SET_BEGIN_END && g_push[i].value == 0) end_seen = 1;
			if (g_push[i].method == NV097_ARRAY_ELEMENT16 && npairs < 4) pairs[npairs++] = g_push[i].value;
			if (g_push[i].method == NV097_ARRAY_ELEMENT32) single = g_push[i].value;
		}
		CHECK(begin == NV097_SET_BEGIN_END_OP_TRIANGLE_STRIP && end_seen);
		CHECK(npairs == 2 && pairs[0] == 0x00010000 && pairs[1] == 0x00030002 && single == 2);   /* 0,1 | 2,3 | 2 */
		/* constants: c0 row 0 = world x2 * viewport x scale (320) */
		float c0[4] = { 0, 0, 0, 0 }; int k = -1;
		for (int i = mark; i < g_push_count; i++) {
			if (g_push[i].method == NV097_SET_TRANSFORM_CONSTANT_LOAD) { CHECK(g_push[i].value == 96); k = 0; continue; }
			if (g_push[i].method >= NV097_SET_TRANSFORM_CONSTANT && g_push[i].method < NV097_SET_TRANSFORM_CONSTANT + 32 * 4 && k >= 0 && k < 4) { memcpy(&c0[k], &g_push[i].value, 4); k++; }
		}
		CHECK(c0[0] == 640.0f && c0[1] == 0.0f && c0[2] == 0.0f && c0[3] == 0.0f);
		/* vertex arrays: position at the base vertex (1 * 24 bytes in), diffuse 12 bytes later, stride 24 */
		DWORD pos_fmt = 0, pos_off = 0, dif_fmt = 0, dif_off = 0, tex_fmt = 0;
		for (int i = mark; i < g_push_count; i++) {
			if (g_push[i].method == NV097_SET_VERTEX_DATA_ARRAY_FORMAT + 0) pos_fmt = g_push[i].value;
			if (g_push[i].method == NV097_SET_VERTEX_DATA_ARRAY_OFFSET + 0) pos_off = g_push[i].value;
			if (g_push[i].method == NV097_SET_VERTEX_DATA_ARRAY_FORMAT + 3 * 4) dif_fmt = g_push[i].value;
			if (g_push[i].method == NV097_SET_VERTEX_DATA_ARRAY_OFFSET + 3 * 4) dif_off = g_push[i].value;
			if (g_push[i].method == NV097_SET_VERTEX_DATA_ARRAY_FORMAT + 9 * 4) tex_fmt = g_push[i].value;
		}
		CHECK(pos_fmt == ((24u << 8) | (3u << 4) | NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F));
		CHECK(dif_fmt == ((24u << 8) | (4u << 4) | NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D));
		CHECK(tex_fmt == ((24u << 8) | (2u << 4) | NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F));
		CHECK(dif_off - pos_off == 12);
		/* non-indexed: DRAW_ARRAYS runs of up to 256 vertices */
		mark = g_push_count;
		dev->DrawPrimitive(D3DPT_TRIANGLELIST, 10, 100);     /* 300 vertices from 10: 256 + 44 */
		DWORD runs[3]; int nr = 0;
		for (int i = mark; i < g_push_count; i++) if (g_push[i].method == NV097_DRAW_ARRAYS && nr < 3) runs[nr++] = g_push[i].value;
		CHECK(nr == 2 && runs[0] == ((255u << 24) | 10) && runs[1] == ((43u << 24) | 266));
		dev->SetStreamSource(0, NULL, 0); dev->SetIndices(NULL, 0);
		vb->Release(); ib->Release();
	}

	/* live-object counters follow creation and release; culling follows D3DRS_CULLMODE */
	{
		XboxD3DStats before, during, after;
		XboxD3D_Get_Stats(&before);
		IDirect3DTexture8 *tt = NULL; IDirect3DVertexBuffer8 *vv = NULL; IDirect3DIndexBuffer8 *ii = NULL;
		dev->CreateTexture(16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tt);
		dev->CreateVertexBuffer(96, 0, D3DFVF_XYZ, D3DPOOL_MANAGED, &vv);
		dev->CreateIndexBuffer(12, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ii);
		XboxD3D_Get_Stats(&during);
		CHECK(during.textures == before.textures + 1 && during.texture_bytes >= before.texture_bytes + 16 * 16 * 4);
		CHECK(during.vertex_buffers == before.vertex_buffers + 1 && during.vertex_bytes == before.vertex_bytes + 96);
		CHECK(during.index_buffers == before.index_buffers + 1 && during.index_bytes == before.index_bytes + 12);
		tt->Release(); vv->Release(); ii->Release();
		XboxD3D_Get_Stats(&after);
		CHECK(after.textures == before.textures && after.texture_bytes == before.texture_bytes &&
		      after.vertex_buffers == before.vertex_buffers && after.index_buffers == before.index_buffers);
		IDirect3DVertexBuffer8 *cv = NULL; IDirect3DIndexBuffer8 *ci = NULL; BYTE *cp;
		dev->CreateVertexBuffer(3 * 12, 0, D3DFVF_XYZ, D3DPOOL_MANAGED, &cv);
		dev->CreateIndexBuffer(6, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ci);
		ci->Lock(0, 0, &cp, 0); memset(cp, 0, 6); ci->Unlock();
		dev->SetVertexShader(D3DFVF_XYZ); dev->SetStreamSource(0, cv, 12); dev->SetIndices(ci, 0);
		DWORD modes[3] = { D3DCULL_NONE, D3DCULL_CW, D3DCULL_CCW }, en[3], front[3];
		for (int m = 0; m < 3; m++) {
			dev->SetRenderState(D3DRS_CULLMODE, modes[m]);
			int from = g_push_count; en[m] = 0xDEAD; front[m] = 0;
			dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 3, 0, 1);
			for (int i = from; i < g_push_count; i++) {
				if (g_push[i].method == NV097_SET_CULL_FACE_ENABLE) en[m] = g_push[i].value;
				if (g_push[i].method == NV097_SET_FRONT_FACE) front[m] = g_push[i].value;
			}
		}
		CHECK(en[0] == 0 && en[1] == 1 && en[2] == 1);
		CHECK(front[1] == NV097_SET_FRONT_FACE_V_CCW && front[2] == NV097_SET_FRONT_FACE_V_CW);   /* CW culls clockwise-on-screen */
		/* depth: a z-buffer normally (pbkit leaves the w-buffer on), a w-buffer only for D3DZB_USEW */
		DWORD control[2] = { 0, 0 };
		DWORD zmodes[2] = { D3DZB_TRUE, D3DZB_USEW };
		for (int m = 0; m < 2; m++) {
			dev->SetRenderState(D3DRS_ZENABLE, zmodes[m]);
			int from = g_push_count;
			dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 3, 0, 1);
			for (int i = from; i < g_push_count; i++) if (g_push[i].method == NV097_SET_CONTROL0) control[m] = g_push[i].value;
		}
		CHECK(control[0] == 0x00100001 && control[1] == 0x00110001);
		dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
		/* Texture coordinates (c26-c33): stored sets, generated coordinates, texture matrices. */
		auto constants = [&](float out[35][4]) {
			memset(out, 0, sizeof(float) * 35 * 4);
			int from = g_push_count, k = -1;
			dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 3, 0, 1);
			for (int i = from; i < g_push_count; i++) {
				if (g_push[i].method == NV097_SET_TRANSFORM_CONSTANT_LOAD) { k = 0; continue; }
				if (g_push[i].method >= NV097_SET_TRANSFORM_CONSTANT && g_push[i].method < NV097_SET_TRANSFORM_CONSTANT + 32 * 4 && k >= 0) {
					if (k < 35 * 4) memcpy(&out[k / 4][k % 4], &g_push[i].value, 4);
					k++;
				}
			}
		};
		float c[35][4];
		/* crossed sets (Renegade's UV source): stage 0 reads set 1, stage 1 reads set 3 */
		dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 1);
		dev->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 3);
		constants(c);
		CHECK(c[26][0] == 0 && c[26][1] == 1 && c[27][3] == 1 && c[27][0] == 0);
		CHECK(c[28][0] == 1 && c[29][0] == 1);                     /* both stored, not generated */
		CHECK(c[30][0] == 1 && c[30][1] == 0 && c[32][1] == 1);    /* no matrix: identity columns */
		CHECK(c[34][0] == 1 && c[34][1] == 0 && c[34][2] == 2);    /* the compiler's literal */
		/* an environment mapper: reflection vector, through a matrix (Westwood's offset in row 3) */
		dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
		dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		D3DMATRIX tm; memset(&tm, 0, sizeof(tm));
		tm._11 = 0.5f; tm._22 = 0.5f; tm._31 = 0.25f; tm._32 = 0.75f; tm._33 = 1; tm._44 = 1;
		dev->SetTransform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0), &tm);
		constants(c);
		CHECK(c[28][3] == 1 && c[28][0] == 0);
		CHECK(c[30][0] == 0.5f && c[30][2] == 0.25f && c[32][1] == 0.5f && c[32][2] == 0.75f);
		dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
		dev->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
		dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		CHECK(XboxD3D_Get_Notice(100) == NULL);
		dev->SetStreamSource(0, NULL, 0); dev->SetIndices(NULL, 0); cv->Release(); ci->Release();
	}

	/* textures bound by a draw: format word, packed mip levels, and non-power-of-two left unbound */
	{
		IDirect3DTexture8 *dxt = NULL, *small = NULL, *odd = NULL;
		CHECK(dev->CreateTexture(64, 64, 0, 0, D3DFMT_DXT1, D3DPOOL_MANAGED, &dxt) == D3D_OK);
		D3DLOCKED_RECT l0, l1;
		dxt->LockRect(0, &l0, NULL, 0); dxt->LockRect(1, &l1, NULL, 0);
		CHECK((BYTE *)l1.pBits - (BYTE *)l0.pBits == 16 * 16 * 8);          /* levels packed back to back */
		CHECK(dev->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &small) == D3D_OK);
		CHECK(dev->CreateTexture(48, 48, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &odd) == D3D_OK);
		IDirect3DVertexBuffer8 *tv = NULL; IDirect3DIndexBuffer8 *ti = NULL; BYTE *tp;
		DWORD tfvf = D3DFVF_XYZ | D3DFVF_TEX1;
		dev->CreateVertexBuffer(3 * 20, 0, tfvf, D3DPOOL_MANAGED, &tv);
		dev->CreateIndexBuffer(6, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ti);
		ti->Lock(0, 0, &tp, 0); memset(tp, 0, 6); ti->Unlock();
		dev->SetVertexShader(tfvf); dev->SetStreamSource(0, tv, 20); dev->SetIndices(ti, 0);
		IDirect3DTexture8 *texs[3] = { dxt, small, odd };
		DWORD expect_fmt[3] = { 0x06670C2A, 0x0221062A, 0 };
		for (int t = 0; t < 3; t++) {
			dev->SetTexture(0, texs[t]);
			int from = g_push_count;
			dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 3, 0, 1);
			DWORD fmt = 0, control = 0, program = 0xDEAD;
			for (int i = from; i < g_push_count; i++) {
				if (g_push[i].method == NV097_SET_TEXTURE_FORMAT) fmt = g_push[i].value;
				if (g_push[i].method == NV097_SET_TEXTURE_CONTROL0) control = g_push[i].value;
				if (g_push[i].method == NV097_SET_SHADER_STAGE_PROGRAM) program = g_push[i].value;
			}
			CHECK(fmt == expect_fmt[t]);
			if (t < 2) CHECK((control & (1u << 30)) && program == 1);     /* enabled, 2D projective */
			else CHECK(control == 0x0003ffc0 && program == 0);            /* 48x48: not bound yet */
		}
		dev->SetTexture(0, NULL); dev->SetStreamSource(0, NULL, 0); dev->SetIndices(NULL, 0);
		tv->Release(); ti->Release(); dxt->Release(); small->Release(); odd->Release();
	}

	/* textures: level layout */
	IDirect3DTexture8 *t = NULL;
	CHECK(dev->CreateTexture(64, 32, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t) == D3D_OK);
	CHECK(t->GetLevelCount() == 7);                                              /* 64x32 ... 1x1 */
	D3DSURFACE_DESC desc; t->GetLevelDesc(6, &desc); CHECK(desc.Width == 1 && desc.Height == 1);
	t->GetLevelDesc(2, &desc); CHECK(desc.Width == 16 && desc.Height == 8 && desc.Size == 16 * 8 * 4);
	D3DLOCKED_RECT lr; RECT rc = { 3, 2, 10, 10 };
	D3DLOCKED_RECT base; t->LockRect(0, &base, NULL, 0); t->LockRect(0, &lr, &rc, 0);
	CHECK(lr.Pitch == 256 && (BYTE *)lr.pBits - (BYTE *)base.pBits == 2 * 256 + 3 * 4);
	IDirect3DTexture8 *dxt = NULL;
	CHECK(dev->CreateTexture(64, 64, 0, 0, D3DFMT_DXT1, D3DPOOL_MANAGED, &dxt) == D3D_OK);
	D3DLOCKED_RECT db, dl; RECT drc = { 8, 4, 16, 16 };
	dxt->LockRect(0, &db, NULL, 0); dxt->LockRect(0, &dl, &drc, 0);
	CHECK(db.Pitch == 16 * 8 && (BYTE *)dl.pBits - (BYTE *)db.pBits == 1 * 128 + 2 * 8);
	dxt->GetLevelDesc(6, &desc); CHECK(desc.Width == 1 && desc.Size == 8);           /* one 4x4 block */
	t->Release();
	CHECK(dev->CreateTexture(64, 64, 2, 0, D3DFMT_DXT5, D3DPOOL_MANAGED, &t) == D3D_OK && t->GetLevelCount() == 2); t->Release();

	/* MissingTexture::_Init's sequence: fill level 0, then box-filter each level from the one above */
	IDirect3DTexture8 *mt = NULL;
	CHECK(D3DXCreateTexture(dev, 4, 4, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &mt) == D3D_OK && mt->GetLevelCount() == 3);
	mt->LockRect(0, &lr, NULL, 0);
	DWORD px[16] = { 0xFF000000, 0xFF0000FF, 0x00000000, 0x00000000,  0xFF00FF00, 0xFFFF0000, 0x00000000, 0x00000000,
	                 0x80808080, 0x80808080, 0xFFFFFFFF, 0xFFFFFFFF,   0x80808080, 0x80808080, 0xFFFFFFFF, 0xFFFFFFFF };
	for (int y = 0; y < 4; y++) memcpy((BYTE *)lr.pBits + y * lr.Pitch, px + y * 4, 16);
	mt->UnlockRect(0);
	for (UINT i = 1; i < mt->GetLevelCount(); ++i) {
		IDirect3DSurface8 *src, *dst;
		mt->GetSurfaceLevel(i - 1, &src); mt->GetSurfaceLevel(i, &dst);
		CHECK(D3DXLoadSurfaceFromSurface(dst, NULL, NULL, src, NULL, NULL, D3DX_FILTER_BOX, 0) == D3D_OK);
		src->Release(); dst->Release();
	}
	mt->LockRect(1, &lr, NULL, 0);
	DWORD *l1 = (DWORD *)lr.pBits;
	/* top-left 2x2: A=FF, R=(0+0+0+FF)/4=40, G=(0+0+FF+0)/4=40, B=(0+FF+0+0)/4=40 (rounded) */
	CHECK(l1[0] == 0xFF404040);
	CHECK(l1[1] == 0x00000000);
	DWORD *l1b = (DWORD *)((BYTE *)lr.pBits + lr.Pitch);
	CHECK(l1b[0] == 0x80808080 && l1b[1] == 0xFFFFFFFF);
	mt->LockRect(2, &lr, NULL, 0);                                                 /* 1x1: average of level 1 */
	DWORD avg = *(DWORD *)lr.pBits;
	CHECK((avg >> 24) == ((0xFF + 0 + 0x80 + 0xFF + 2) / 4) && (avg & 0xFF) == ((0x40 + 0 + 0x80 + 0xFF + 2) / 4));
	/* D3DXFilterTexture gives the same result */
	IDirect3DTexture8 *ft = NULL; D3DXCreateTexture(dev, 4, 4, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &ft);
	ft->LockRect(0, &lr, NULL, 0); for (int y = 0; y < 4; y++) memcpy((BYTE *)lr.pBits + y * lr.Pitch, px + y * 4, 16);
	CHECK(D3DXFilterTexture(ft, NULL, 0, D3DX_FILTER_BOX) == D3D_OK);
	D3DLOCKED_RECT a, b; ft->LockRect(2, &a, NULL, 0); mt->LockRect(2, &b, NULL, 0);
	CHECK(*(DWORD *)a.pBits == *(DWORD *)b.pBits);

	/* conversions */
	IDirect3DSurface8 *s565, *s8888, *s4444, *sl8;
	dev->CreateImageSurface(2, 1, D3DFMT_R5G6B5, &s565); dev->CreateImageSurface(2, 1, D3DFMT_A8R8G8B8, &s8888);
	dev->CreateImageSurface(2, 1, D3DFMT_A4R4G4B4, &s4444); dev->CreateImageSurface(2, 1, D3DFMT_L8, &sl8);
	s565->LockRect(&lr, NULL, 0); ((WORD *)lr.pBits)[0] = 0xFFFF; ((WORD *)lr.pBits)[1] = 0xF800;
	CHECK(D3DXLoadSurfaceFromSurface(s8888, NULL, NULL, s565, NULL, NULL, D3DX_FILTER_NONE, 0) == D3D_OK);
	s8888->LockRect(&lr, NULL, 0); CHECK(((DWORD *)lr.pBits)[0] == 0xFFFFFFFF && ((DWORD *)lr.pBits)[1] == 0xFFFF0000);
	((DWORD *)lr.pBits)[0] = 0x80FF8040;
	CHECK(D3DXLoadSurfaceFromSurface(s4444, NULL, NULL, s8888, NULL, NULL, D3DX_FILTER_NONE, 0) == D3D_OK);
	s4444->LockRect(&lr, NULL, 0); CHECK(((WORD *)lr.pBits)[0] == 0x8F84);
	CHECK(D3DXLoadSurfaceFromSurface(sl8, NULL, NULL, s8888, NULL, NULL, D3DX_FILTER_NONE, 0) == D3D_OK);
	sl8->LockRect(&lr, NULL, 0); CHECK(lr.pBits && ((BYTE *)lr.pBits)[1] == (BYTE)((255 * 77) >> 8));     /* pure red */
	/* DXT with no conversion: a copy; DXT needing conversion: reported as not available */
	IDirect3DSurface8 *d0, *d1; IDirect3DTexture8 *dxt2; dev->CreateTexture(64, 64, 1, 0, D3DFMT_DXT1, D3DPOOL_MANAGED, &dxt2);
	dxt->GetSurfaceLevel(0, &d0); dxt2->GetSurfaceLevel(0, &d1);
	dxt->LockRect(0, &lr, NULL, 0); memset(lr.pBits, 0xAB, 512);
	CHECK(D3DXLoadSurfaceFromSurface(d1, NULL, NULL, d0, NULL, NULL, D3DX_FILTER_NONE, 0) == D3D_OK);
	dxt2->LockRect(0, &lr, NULL, 0); CHECK(((BYTE *)lr.pBits)[511] == 0xAB);
	CHECK(D3DXLoadSurfaceFromSurface(s8888, NULL, NULL, d0, NULL, NULL, D3DX_FILTER_BOX, 0) == D3DERR_NOTAVAILABLE);
	/* UpdateTexture copies every level */
	IDirect3DTexture8 *copy = NULL; dev->CreateTexture(4, 4, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &copy);
	CHECK(dev->UpdateTexture(mt, copy) == D3D_OK);
	copy->LockRect(2, &a, NULL, 0); CHECK(*(DWORD *)a.pBits == avg);
	/* gamma: 16-bit ramp to pbkit's 8-bit */
	D3DGAMMARAMP gr; for (int i = 0; i < 256; i++) gr.red[i] = gr.green[i] = gr.blue[i] = (WORD)(65535 - i * 257);
	dev->SetGammaRamp(0, &gr); CHECK(g_gamma.red[0] == 255 && g_gamma.red[255] == 0 && g_gamma.blue[128] == 127);
	/* back buffer lock reads pbkit's current back buffer */
	IDirect3DSurface8 *bb; dev->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb);
	bb->LockRect(&lr, NULL, D3DLOCK_READONLY); CHECK(lr.pBits == (void *)g_backbuffer && lr.Pitch == 2560); bb->Release();
	/* render states and stored defaults */
	DWORD v; dev->GetRenderState(D3DRS_ZFUNC, &v); CHECK(v == D3DCMP_LESSEQUAL);
	dev->SetRenderState(D3DRS_NORMALORDER, 5); dev->GetRenderState(D3DRS_NORMALORDER, &v); CHECK(v == 5);
	CHECK(dev->SetRenderState((D3DRENDERSTATETYPE)300, 1) == D3DERR_INVALIDCALL);
	/* reference counting: surfaces keep their texture alive; everything is freed at the end */
	IDirect3DSurface8 *keep; mt->GetSurfaceLevel(0, &keep);
	CHECK(mt->Release() == 1);                                   /* surface still holds it */
	keep->LockRect(&lr, NULL, 0); CHECK(((DWORD *)lr.pBits)[1] == 0xFF0000FF);
	keep->Release();
	d0->Release(); d1->Release(); dxt2->Release(); dxt->Release(); ft->Release(); copy->Release();
	s565->Release(); s8888->Release(); s4444->Release(); sl8->Release();
	{ IDirect3DTexture8 *t0 = NULL; dev->CreateTexture(64, 32, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t0); t0->Release(); }
	IDirect3DTexture8 *firstt = NULL; (void)firstt;
	dev->Release(); d3d->Release();
	CHECK(g_pb_kill == 1);
	printf("GPU memory: %d allocations, %d frees\n", g_allocs, g_frees);
	CHECK(g_allocs == g_frees);
	printf(fails ? "%d FAILED\n" : "all Direct3D layer tests passed\n", fails);
	return fails;
}

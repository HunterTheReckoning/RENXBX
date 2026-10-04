/*
** xbox_d3d8.cpp -- the port's Direct3D 8 layer on nxdk's pbkit (Xbox build only).
**
** Step 1 (device bring-up): the Direct3D object, device creation on pbkit, Clear, Present,
** viewports and stored render state, plus textures, surfaces and vertex/index buffers in
** GPU-visible memory. Drawing (step 2) and binding textures to the GPU (step 3) come next:
** until then draw calls are accepted and logged once.
**
** The NV2A is reported as an unidentified vendor on purpose: the engine's vendor workarounds
** target 2002 PC drivers (for NVIDIA they would switch off DXT1, which the NV2A decodes
** natively and which most of Renegade's textures use).
*/
#include "xbox_d3d8_internal.h"
#include "xbox_d3d8_stats.h"

#include <hal/video.h>
#include <pbkit/pbkit.h>

/* pbkit defines _11 ... _44 as matrix index macros, which are also the member names of
** Direct3D's D3DMATRIX. This layer doesn't use pbkit's macros, so remove them. */
#undef _11
#undef _12
#undef _13
#undef _14
#undef _21
#undef _22
#undef _23
#undef _24
#undef _31
#undef _32
#undef _33
#undef _34
#undef _41
#undef _42
#undef _43
#undef _44

/* Places a value in a register field, as nxdk's samples do. */
#define MASK(mask, val) (((val) << (__builtin_ffs(mask) - 1)) & (mask))
#include <xboxkrnl/xboxkrnl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#ifndef PORT_TRACE
#define PORT_TRACE(message)
#endif

namespace XboxD3D {

void *Alloc_GPU_Memory(UINT size)
{
	if (size == 0) size = 1;
	return MmAllocateContiguousMemoryEx(size, 0, 0x03FFAFFF, 0x1000, PAGE_READWRITE | PAGE_WRITECOMBINE);
}

void Free_GPU_Memory(void *memory)
{
	if (memory) MmFreeContiguousMemory(memory);
}

static XboxD3DStats Stats;   /* live objects; see xbox_d3d8_stats.h */
static bool DiagnosticW1;    /* see XboxD3D_Set_Diagnostic_W1 */

void Log_Once(bool *done, const char *what)
{
	if (*done) return;
	*done = true;
	DbgPrint("XboxD3D: %s\n", what);
	Trace("%s", what);
}

void Trace(const char *format, ...)
{
	char line[160];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	(void)line;
	PORT_TRACE(line);
}

/* --- Reference counting shared by all objects ------------------------------------------ */

#define XBOXD3D_REFCOUNT_METHODS                                                        \
	HRESULT QueryInterface(REFIID, void **object) override                              \
	{                                                                                   \
		if (object) *object = NULL;                                                     \
		return E_NOINTERFACE;                                                           \
	}                                                                                   \
	ULONG AddRef(void) override { return ++RefCount; }                                  \
	ULONG Release(void) override                                                        \
	{                                                                                   \
		ULONG count = --RefCount;                                                       \
		if (count == 0) delete this;                                                    \
		return count;                                                                   \
	}

class Device;
class Texture;

/* --- Surfaces ------------------------------------------------------------------------- */

/* A 2D image: a texture level, a stand-alone image surface, or the screen's back/depth buffer. */
class Surface : public IDirect3DSurface8 {
public:
	enum Kind { TEXTURE_LEVEL, IMAGE, BACK_BUFFER, DEPTH_BUFFER };

	Surface(Device *device, Kind kind, D3DFORMAT format, UINT width, UINT height, D3DPOOL pool,
	        DWORD usage, BYTE *bits, UINT pitch, Texture *container);
	~Surface() override;
	XBOXD3D_REFCOUNT_METHODS

	HRESULT GetDevice(IDirect3DDevice8 **ppDevice) override;
	HRESULT GetContainer(REFIID, void **ppContainer) override
	{
		if (ppContainer) *ppContainer = NULL;
		return E_NOINTERFACE;
	}
	HRESULT GetDesc(D3DSURFACE_DESC *pDesc) override
	{
		if (!pDesc) return D3DERR_INVALIDCALL;
		pDesc->Format = Format;
		pDesc->Type = D3DRTYPE_SURFACE;
		pDesc->Usage = Usage;
		pDesc->Pool = Pool;
		pDesc->Size = Level_Size(Format, Width, Height);
		pDesc->MultiSampleType = D3DMULTISAMPLE_NONE;
		pDesc->Width = Width;
		pDesc->Height = Height;
		return D3D_OK;
	}
	HRESULT LockRect(D3DLOCKED_RECT *pLockedRect, CONST RECT *pRect, DWORD Flags) override;
	HRESULT UnlockRect(void) override { return D3D_OK; }

	Kind SurfaceKind;
	D3DFORMAT Format;
	UINT Width, Height;
	D3DPOOL Pool;
	DWORD Usage;
	BYTE *Bits;          /* NULL for the back buffer: it rotates, so it is looked up at lock time */
	UINT Pitch;
	bool OwnsBits;
	Texture *Container;  /* holds a reference while this surface exists */
	Device *Owner;
	ULONG RefCount;
};

/* --- Textures ------------------------------------------------------------------------- */

class Texture : public IDirect3DTexture8 {
public:
	enum { MAX_LEVELS = 13 };   /* 4096 x 4096 down to 1 x 1 */

	Texture(Device *device, UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format,
	        D3DPOOL pool);
	~Texture() override
	{
		Stats.textures--;
		Stats.texture_bytes -= MemoryBytes;
		Free_GPU_Memory(Memory);
	}
	bool Valid() const { return Memory != NULL; }
	XBOXD3D_REFCOUNT_METHODS

	HRESULT GetDevice(IDirect3DDevice8 **ppDevice) override;
	DWORD SetPriority(DWORD PriorityNew) override { DWORD old = Priority; Priority = PriorityNew; return old; }
	DWORD GetPriority(void) override { return Priority; }
	void PreLoad(void) override {}
	D3DRESOURCETYPE GetType(void) override { return D3DRTYPE_TEXTURE; }
	DWORD SetLOD(DWORD LODNew) override { DWORD old = LOD; LOD = LODNew; return old; }
	DWORD GetLOD(void) override { return LOD; }
	DWORD GetLevelCount(void) override { return LevelCount; }

	HRESULT GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) override
	{
		if (Level >= LevelCount || !pDesc) return D3DERR_INVALIDCALL;
		pDesc->Format = Format;
		pDesc->Type = D3DRTYPE_SURFACE;
		pDesc->Usage = Usage;
		pDesc->Pool = Pool;
		pDesc->Size = Levels[Level].Size;
		pDesc->MultiSampleType = D3DMULTISAMPLE_NONE;
		pDesc->Width = Levels[Level].Width;
		pDesc->Height = Levels[Level].Height;
		return D3D_OK;
	}
	HRESULT GetSurfaceLevel(UINT Level, IDirect3DSurface8 **ppSurfaceLevel) override;
	HRESULT LockRect(UINT Level, D3DLOCKED_RECT *pLockedRect, CONST RECT *pRect, DWORD Flags) override;
	HRESULT UnlockRect(UINT Level) override { return Level < LevelCount ? D3D_OK : D3DERR_INVALIDCALL; }
	HRESULT AddDirtyRect(CONST RECT *) override { return D3D_OK; }

	struct LevelInfo { UINT Width, Height, Pitch, Size; BYTE *Bits; };

	Device *Owner;
	D3DFORMAT Format;
	DWORD Usage;
	D3DPOOL Pool;
	UINT LevelCount;
	LevelInfo Levels[MAX_LEVELS];
	BYTE *Memory;        /* all levels, one contiguous GPU-visible block */
	UINT MemoryBytes;
	DWORD Priority, LOD;
	ULONG RefCount;
};

/* --- Vertex and index buffers ----------------------------------------------------------- */

class VertexBuffer : public IDirect3DVertexBuffer8 {
public:
	VertexBuffer(Device *device, UINT length, DWORD usage, DWORD fvf, D3DPOOL pool)
		: Owner(device), Length(length), Usage(usage), FVF(fvf), Pool(pool), Priority(0), RefCount(1)
	{
		Memory = (BYTE *)Alloc_GPU_Memory(length);
		Stats.vertex_buffers++;
		Stats.vertex_bytes += Memory ? length : 0;
	}
	~VertexBuffer() override
	{
		Stats.vertex_buffers--;
		Stats.vertex_bytes -= Memory ? Length : 0;
		Free_GPU_Memory(Memory);
	}
	bool Valid() const { return Memory != NULL; }
	XBOXD3D_REFCOUNT_METHODS

	HRESULT GetDevice(IDirect3DDevice8 **ppDevice) override;
	DWORD SetPriority(DWORD PriorityNew) override { DWORD old = Priority; Priority = PriorityNew; return old; }
	DWORD GetPriority(void) override { return Priority; }
	void PreLoad(void) override {}
	D3DRESOURCETYPE GetType(void) override { return D3DRTYPE_VERTEXBUFFER; }
	HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, BYTE **ppbData, DWORD Flags) override;
	HRESULT Unlock(void) override { return D3D_OK; }
	HRESULT GetDesc(D3DVERTEXBUFFER_DESC *pDesc) override
	{
		if (!pDesc) return D3DERR_INVALIDCALL;
		pDesc->Format = D3DFMT_VERTEXDATA;
		pDesc->Type = D3DRTYPE_VERTEXBUFFER;
		pDesc->Usage = Usage;
		pDesc->Pool = Pool;
		pDesc->Size = Length;
		pDesc->FVF = FVF;
		return D3D_OK;
	}

	Device *Owner;
	UINT Length;
	DWORD Usage, FVF;
	D3DPOOL Pool;
	DWORD Priority;
	BYTE *Memory;
	ULONG RefCount;
};

class IndexBuffer : public IDirect3DIndexBuffer8 {
public:
	/* Indices travel to the NV2A inside the command stream, so the CPU reads them on every
	** draw: they live in ordinary cached memory (reading write-combined memory is very slow). */
	IndexBuffer(Device *device, UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool)
		: Owner(device), Length(length), Usage(usage), Format(format), Pool(pool), Priority(0), RefCount(1)
	{
		Memory = (BYTE *)malloc(length ? length : 1);
		Stats.index_buffers++;
		Stats.index_bytes += Memory ? length : 0;
	}
	~IndexBuffer() override
	{
		Stats.index_buffers--;
		Stats.index_bytes -= Memory ? Length : 0;
		free(Memory);
	}
	bool Valid() const { return Memory != NULL; }
	XBOXD3D_REFCOUNT_METHODS

	HRESULT GetDevice(IDirect3DDevice8 **ppDevice) override;
	DWORD SetPriority(DWORD PriorityNew) override { DWORD old = Priority; Priority = PriorityNew; return old; }
	DWORD GetPriority(void) override { return Priority; }
	void PreLoad(void) override {}
	D3DRESOURCETYPE GetType(void) override { return D3DRTYPE_INDEXBUFFER; }
	HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, BYTE **ppbData, DWORD Flags) override;
	HRESULT Unlock(void) override { return D3D_OK; }
	HRESULT GetDesc(D3DINDEXBUFFER_DESC *pDesc) override
	{
		if (!pDesc) return D3DERR_INVALIDCALL;
		pDesc->Format = Format;
		pDesc->Type = D3DRTYPE_INDEXBUFFER;
		pDesc->Usage = Usage;
		pDesc->Pool = Pool;
		pDesc->Size = Length;
		return D3D_OK;
	}

	Device *Owner;
	UINT Length;
	DWORD Usage;
	D3DFORMAT Format;
	D3DPOOL Pool;
	DWORD Priority;
	BYTE *Memory;
	ULONG RefCount;
};

/* --- The Direct3D object ---------------------------------------------------------------- */

/* The one display mode offered for now: 640x480, 32-bit, as nxdk's samples use. */
static const UINT MODE_WIDTH = 640;
static const UINT MODE_HEIGHT = 480;
static const UINT MODE_REFRESH = 60;
static const D3DFORMAT MODE_FORMAT = D3DFMT_X8R8G8B8;

static bool Is_Texture_Format_Supported(D3DFORMAT format)
{
	switch (format) {
	case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5:
	case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4: case D3DFMT_A8: case D3DFMT_L8: case D3DFMT_A8L8:
	case D3DFMT_DXT1: case D3DFMT_DXT2: case D3DFMT_DXT3: case D3DFMT_DXT4: case D3DFMT_DXT5:
		return true;
	default:
		return false;
	}
}

static void Fill_Caps(D3DCAPS8 *caps)
{
	memset(caps, 0, sizeof(*caps));
	caps->DeviceType = D3DDEVTYPE_HAL;
	caps->AdapterOrdinal = 0;
	caps->Caps2 = D3DCAPS2_FULLSCREENGAMMA;
	caps->PresentationIntervals = D3DPRESENT_INTERVAL_ONE | D3DPRESENT_INTERVAL_IMMEDIATE;
	caps->DevCaps = D3DDEVCAPS_HWTRANSFORMANDLIGHT | D3DDEVCAPS_HWRASTERIZATION |
	                D3DDEVCAPS_TEXTUREVIDEOMEMORY | D3DDEVCAPS_DRAWPRIMITIVES2EX;
	caps->RasterCaps = D3DPRASTERCAPS_ZTEST | D3DPRASTERCAPS_FOGVERTEX | D3DPRASTERCAPS_FOGTABLE |
	                   D3DPRASTERCAPS_MIPMAPLODBIAS | D3DPRASTERCAPS_ZBIAS | D3DPRASTERCAPS_ANISOTROPY;
	caps->TextureFilterCaps = D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR |
	                          D3DPTFILTERCAPS_MINFANISOTROPIC | D3DPTFILTERCAPS_MIPFPOINT |
	                          D3DPTFILTERCAPS_MIPFLINEAR | D3DPTFILTERCAPS_MAGFPOINT |
	                          D3DPTFILTERCAPS_MAGFLINEAR | D3DPTFILTERCAPS_MAGFANISOTROPIC;
	caps->TextureOpCaps = D3DTEXOPCAPS_DISABLE | D3DTEXOPCAPS_SELECTARG1 | D3DTEXOPCAPS_SELECTARG2 |
	                      D3DTEXOPCAPS_MODULATE | D3DTEXOPCAPS_MODULATE2X | D3DTEXOPCAPS_MODULATE4X |
	                      D3DTEXOPCAPS_ADD | D3DTEXOPCAPS_ADDSIGNED | D3DTEXOPCAPS_SUBTRACT |
	                      D3DTEXOPCAPS_ADDSMOOTH | D3DTEXOPCAPS_BLENDDIFFUSEALPHA |
	                      D3DTEXOPCAPS_BLENDTEXTUREALPHA | D3DTEXOPCAPS_BLENDFACTORALPHA |
	                      D3DTEXOPCAPS_BLENDCURRENTALPHA | D3DTEXOPCAPS_DOTPRODUCT3;
	caps->MaxTextureWidth = 4096;
	caps->MaxTextureHeight = 4096;
	caps->MaxTextureRepeat = 8192;
	caps->MaxTextureAspectRatio = 4096;
	caps->MaxAnisotropy = 4;
	caps->MaxVertexW = 1.0e10f;
	caps->GuardBandLeft = -32768.0f;
	caps->GuardBandTop = -32768.0f;
	caps->GuardBandRight = 32767.0f;
	caps->GuardBandBottom = 32767.0f;
	caps->MaxTextureBlendStages = 4;
	caps->MaxSimultaneousTextures = 4;
	caps->MaxActiveLights = 8;
	caps->MaxUserClipPlanes = 0;
	caps->MaxVertexBlendMatrices = 4;
	caps->MaxPointSize = 64.0f;
	caps->MaxPrimitiveCount = 0xFFFFF;
	caps->MaxVertexIndex = 0xFFFF;          /* the engine uses 16-bit indices */
	caps->MaxStreams = 16;
	caps->MaxStreamStride = 255;
	caps->VertexShaderVersion = 0;          /* Renegade uses the fixed-function pipeline only */
	caps->PixelShaderVersion = 0;
}

class Direct3D : public IDirect3D8 {
public:
	Direct3D() : RefCount(1) {}
	XBOXD3D_REFCOUNT_METHODS

	UINT GetAdapterCount(void) override { return 1; }
	HRESULT GetAdapterIdentifier(UINT Adapter, DWORD, D3DADAPTER_IDENTIFIER8 *pIdentifier) override
	{
		if (Adapter != 0 || !pIdentifier) return D3DERR_INVALIDCALL;
		memset(pIdentifier, 0, sizeof(*pIdentifier));
		strcpy(pIdentifier->Driver, "nxdk pbkit");
		strcpy(pIdentifier->Description, "Xbox NV2A (Renegade port Direct3D layer)");
		pIdentifier->DriverVersion.QuadPart = 0;
		pIdentifier->VendorId = 0;        /* unidentified on purpose: see the note at the top */
		pIdentifier->DeviceId = 0;
		return D3D_OK;
	}
	UINT GetAdapterModeCount(UINT Adapter) override { return Adapter == 0 ? 1 : 0; }
	HRESULT EnumAdapterModes(UINT Adapter, UINT Mode, D3DDISPLAYMODE *pMode) override
	{
		if (Adapter != 0 || Mode != 0 || !pMode) return D3DERR_INVALIDCALL;
		return GetAdapterDisplayMode(Adapter, pMode);
	}
	HRESULT GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE *pMode) override
	{
		if (Adapter != 0 || !pMode) return D3DERR_INVALIDCALL;
		pMode->Width = MODE_WIDTH;
		pMode->Height = MODE_HEIGHT;
		pMode->RefreshRate = MODE_REFRESH;
		pMode->Format = MODE_FORMAT;
		return D3D_OK;
	}
	HRESULT CheckDeviceType(UINT Adapter, D3DDEVTYPE, D3DFORMAT DisplayFormat, D3DFORMAT BackBufferFormat,
	                        BOOL Windowed) override
	{
		if (Adapter != 0 || Windowed) return D3DERR_NOTAVAILABLE;
		bool ok = DisplayFormat == MODE_FORMAT &&
		          (BackBufferFormat == D3DFMT_X8R8G8B8 || BackBufferFormat == D3DFMT_A8R8G8B8);
		return ok ? D3D_OK : D3DERR_NOTAVAILABLE;
	}
	HRESULT CheckDeviceFormat(UINT Adapter, D3DDEVTYPE, D3DFORMAT, DWORD Usage, D3DRESOURCETYPE RType,
	                          D3DFORMAT CheckFormat) override
	{
		if (Adapter != 0) return D3DERR_INVALIDCALL;
		if (Usage & D3DUSAGE_DEPTHSTENCIL) {
			return CheckFormat == D3DFMT_D24S8 ? D3D_OK : D3DERR_NOTAVAILABLE;   /* pbkit's depth buffer */
		}
		if (Usage & D3DUSAGE_RENDERTARGET) {
			/* The back buffer only, until render-to-texture is implemented. */
			bool ok = RType == D3DRTYPE_SURFACE &&
			          (CheckFormat == D3DFMT_X8R8G8B8 || CheckFormat == D3DFMT_A8R8G8B8);
			return ok ? D3D_OK : D3DERR_NOTAVAILABLE;
		}
		return Is_Texture_Format_Supported(CheckFormat) ? D3D_OK : D3DERR_NOTAVAILABLE;
	}
	HRESULT CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE, D3DFORMAT, D3DFORMAT RenderTargetFormat,
	                               D3DFORMAT DepthStencilFormat) override
	{
		if (Adapter != 0) return D3DERR_INVALIDCALL;
		bool ok = DepthStencilFormat == D3DFMT_D24S8 &&
		          (RenderTargetFormat == D3DFMT_X8R8G8B8 || RenderTargetFormat == D3DFMT_A8R8G8B8);
		return ok ? D3D_OK : D3DERR_NOTAVAILABLE;
	}
	HRESULT GetDeviceCaps(UINT Adapter, D3DDEVTYPE, D3DCAPS8 *pCaps) override
	{
		if (Adapter != 0 || !pCaps) return D3DERR_INVALIDCALL;
		Fill_Caps(pCaps);
		return D3D_OK;
	}
	HMONITOR GetAdapterMonitor(UINT) override { return NULL; }
	HRESULT CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
	                     D3DPRESENT_PARAMETERS *pPresentationParameters,
	                     IDirect3DDevice8 **ppReturnedDeviceInterface) override;

	ULONG RefCount;
};

/* --- The device ------------------------------------------------------------------------- */

class Device : public IDirect3DDevice8 {
public:
	enum { MAX_STAGES = 4, MAX_LIGHTS = 8, MAX_STREAMS = 16, RENDER_STATE_COUNT = 256, TSS_COUNT = 32,
	       TRANSFORM_COUNT = 512 };

	Device(Direct3D *d3d, const D3DPRESENT_PARAMETERS &params);
	~Device() override;
	bool Init_Hardware();
	XBOXD3D_REFCOUNT_METHODS

	HRESULT TestCooperativeLevel(void) override { return D3D_OK; }       /* never lost on Xbox */
	UINT GetAvailableTextureMem(void) override
	{
		MM_STATISTICS stats;
		memset(&stats, 0, sizeof(stats));
		stats.Length = sizeof(stats);
		MmQueryStatistics(&stats);
		return stats.AvailablePages * 4096;
	}
	HRESULT ResourceManagerDiscardBytes(DWORD) override { return D3D_OK; }
	HRESULT GetDirect3D(IDirect3D8 **ppD3D8) override
	{
		if (!ppD3D8) return D3DERR_INVALIDCALL;
		D3D->AddRef();
		*ppD3D8 = D3D;
		return D3D_OK;
	}
	HRESULT GetDeviceCaps(D3DCAPS8 *pCaps) override { return D3D->GetDeviceCaps(0, D3DDEVTYPE_HAL, pCaps); }
	HRESULT GetDisplayMode(D3DDISPLAYMODE *pMode) override { return D3D->GetAdapterDisplayMode(0, pMode); }
	HRESULT CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *, IDirect3DSwapChain8 **pSwapChain) override
	{
		if (pSwapChain) *pSwapChain = NULL;
		return D3DERR_NOTAVAILABLE;
	}
	HRESULT Reset(D3DPRESENT_PARAMETERS *pPresentationParameters) override
	{
		/* Only the one mode exists, so a reset to it is a no-op. */
		if (!pPresentationParameters) return D3DERR_INVALIDCALL;
		bool same = (pPresentationParameters->BackBufferWidth == 0 ||
		             pPresentationParameters->BackBufferWidth == Width) &&
		            (pPresentationParameters->BackBufferHeight == 0 ||
		             pPresentationParameters->BackBufferHeight == Height);
		return same ? D3D_OK : D3DERR_NOTAVAILABLE;
	}
	HRESULT Present(CONST RECT *, CONST RECT *, HWND, CONST RGNDATA *) override;
	HRESULT GetBackBuffer(UINT BackBuffer, D3DBACKBUFFER_TYPE, IDirect3DSurface8 **ppBackBuffer) override
	{
		if (BackBuffer != 0 || !ppBackBuffer) return D3DERR_INVALIDCALL;
		BackBufferSurface->AddRef();
		*ppBackBuffer = BackBufferSurface;
		return D3D_OK;
	}
	void SetGammaRamp(DWORD, CONST D3DGAMMARAMP *pRamp) override
	{
		if (!pRamp) return;
		Gamma = *pRamp;
		PB_GAMMA_RAMP ramp;
		for (int i = 0; i < 256; i++) {
			ramp.red[i] = (uint8_t)(pRamp->red[i] >> 8);
			ramp.green[i] = (uint8_t)(pRamp->green[i] >> 8);
			ramp.blue[i] = (uint8_t)(pRamp->blue[i] >> 8);
		}
		pb_set_gamma_ramp(&ramp);
	}
	void GetGammaRamp(D3DGAMMARAMP *pRamp) override { if (pRamp) *pRamp = Gamma; }

	HRESULT CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
	                      IDirect3DTexture8 **ppTexture) override;
	HRESULT CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool,
	                           IDirect3DVertexBuffer8 **ppVertexBuffer) override;
	HRESULT CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
	                          IDirect3DIndexBuffer8 **ppIndexBuffer) override;
	HRESULT CreateRenderTarget(UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, BOOL, IDirect3DSurface8 **ppSurface) override
	{
		static bool logged;
		Log_Once(&logged, "CreateRenderTarget: render-to-texture not implemented yet");
		if (ppSurface) *ppSurface = NULL;
		return D3DERR_NOTAVAILABLE;
	}
	HRESULT CreateDepthStencilSurface(UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, IDirect3DSurface8 **ppSurface) override
	{
		if (ppSurface) *ppSurface = NULL;
		return D3DERR_NOTAVAILABLE;
	}
	HRESULT CreateImageSurface(UINT Width, UINT Height, D3DFORMAT Format, IDirect3DSurface8 **ppSurface) override;
	HRESULT CopyRects(IDirect3DSurface8 *pSourceSurface, CONST RECT *pSourceRectsArray, UINT cRects,
	                  IDirect3DSurface8 *pDestinationSurface, CONST POINT *pDestPointsArray) override;
	HRESULT UpdateTexture(IDirect3DBaseTexture8 *pSourceTexture, IDirect3DBaseTexture8 *pDestinationTexture) override;
	HRESULT GetFrontBuffer(IDirect3DSurface8 *) override
	{
		static bool logged;
		Log_Once(&logged, "GetFrontBuffer: not implemented yet");
		return D3DERR_NOTAVAILABLE;
	}
	HRESULT SetRenderTarget(IDirect3DSurface8 *pRenderTarget, IDirect3DSurface8 *pNewZStencil) override
	{
		bool back = (pRenderTarget == NULL || pRenderTarget == BackBufferSurface) &&
		            (pNewZStencil == NULL || pNewZStencil == DepthSurface);
		if (back) return D3D_OK;
		static bool logged;
		Log_Once(&logged, "SetRenderTarget: render-to-texture not implemented yet");
		return D3DERR_NOTAVAILABLE;
	}
	HRESULT GetRenderTarget(IDirect3DSurface8 **ppRenderTarget) override { return GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, ppRenderTarget); }
	HRESULT GetDepthStencilSurface(IDirect3DSurface8 **ppZStencilSurface) override
	{
		if (!ppZStencilSurface) return D3DERR_INVALIDCALL;
		DepthSurface->AddRef();
		*ppZStencilSurface = DepthSurface;
		return D3D_OK;
	}
	HRESULT BeginScene(void) override { Begin_Frame_If_Needed(); InScene = true; return D3D_OK; }
	HRESULT EndScene(void) override { InScene = false; return D3D_OK; }
	HRESULT Clear(DWORD Count, CONST D3DRECT *pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) override;

	HRESULT SetTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX *pMatrix) override
	{
		if ((UINT)State >= TRANSFORM_COUNT || !pMatrix) return D3DERR_INVALIDCALL;
		Transforms[State] = *pMatrix;
		return D3D_OK;
	}
	HRESULT GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX *pMatrix) override
	{
		if ((UINT)State >= TRANSFORM_COUNT || !pMatrix) return D3DERR_INVALIDCALL;
		*pMatrix = Transforms[State];
		return D3D_OK;
	}
	HRESULT SetViewport(CONST D3DVIEWPORT8 *pViewport) override
	{
		if (!pViewport) return D3DERR_INVALIDCALL;
		Viewport = *pViewport;
		return D3D_OK;
	}
	HRESULT GetViewport(D3DVIEWPORT8 *pViewport) override
	{
		if (!pViewport) return D3DERR_INVALIDCALL;
		*pViewport = Viewport;
		return D3D_OK;
	}
	HRESULT SetMaterial(CONST D3DMATERIAL8 *pMaterial) override
	{
		if (!pMaterial) return D3DERR_INVALIDCALL;
		Material = *pMaterial;
		return D3D_OK;
	}
	HRESULT GetMaterial(D3DMATERIAL8 *pMaterial) override
	{
		if (!pMaterial) return D3DERR_INVALIDCALL;
		*pMaterial = Material;
		return D3D_OK;
	}
	HRESULT SetLight(DWORD Index, CONST D3DLIGHT8 *pLight) override
	{
		if (Index >= MAX_LIGHTS || !pLight) return D3DERR_INVALIDCALL;
		Lights[Index] = *pLight;
		return D3D_OK;
	}
	HRESULT GetLight(DWORD Index, D3DLIGHT8 *pLight) override
	{
		if (Index >= MAX_LIGHTS || !pLight) return D3DERR_INVALIDCALL;
		*pLight = Lights[Index];
		return D3D_OK;
	}
	HRESULT LightEnable(DWORD Index, BOOL Enable) override
	{
		if (Index >= MAX_LIGHTS) return D3DERR_INVALIDCALL;
		LightEnabled[Index] = Enable ? TRUE : FALSE;
		return D3D_OK;
	}
	HRESULT GetLightEnable(DWORD Index, BOOL *pEnable) override
	{
		if (Index >= MAX_LIGHTS || !pEnable) return D3DERR_INVALIDCALL;
		*pEnable = LightEnabled[Index];
		return D3D_OK;
	}
	HRESULT SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) override
	{
		if ((UINT)State >= RENDER_STATE_COUNT) return D3DERR_INVALIDCALL;
		RenderStates[State] = Value;
		return D3D_OK;
	}
	HRESULT GetRenderState(D3DRENDERSTATETYPE State, DWORD *pValue) override
	{
		if ((UINT)State >= RENDER_STATE_COUNT || !pValue) return D3DERR_INVALIDCALL;
		*pValue = RenderStates[State];
		return D3D_OK;
	}
	HRESULT GetTexture(DWORD Stage, IDirect3DBaseTexture8 **ppTexture) override
	{
		if (Stage >= MAX_STAGES || !ppTexture) return D3DERR_INVALIDCALL;
		*ppTexture = Textures[Stage];
		if (*ppTexture) (*ppTexture)->AddRef();
		return D3D_OK;
	}
	HRESULT SetTexture(DWORD Stage, IDirect3DBaseTexture8 *pTexture) override
	{
		if (Stage >= MAX_STAGES) return D3DERR_INVALIDCALL;
		if (pTexture) pTexture->AddRef();
		if (Textures[Stage]) Textures[Stage]->Release();
		Textures[Stage] = pTexture;
		return D3D_OK;
	}
	HRESULT GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD *pValue) override
	{
		if (Stage >= MAX_STAGES || (UINT)Type >= TSS_COUNT || !pValue) return D3DERR_INVALIDCALL;
		*pValue = StageStates[Stage][Type];
		return D3D_OK;
	}
	HRESULT SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) override
	{
		if (Stage >= MAX_STAGES || (UINT)Type >= TSS_COUNT) return D3DERR_INVALIDCALL;
		StageStates[Stage][Type] = Value;
		return D3D_OK;
	}
	HRESULT ValidateDevice(DWORD *pNumPasses) override
	{
		if (pNumPasses) *pNumPasses = 1;
		return D3D_OK;
	}
	HRESULT DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) override;
	HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT minIndex, UINT NumVertices,
	                             UINT startIndex, UINT primCount) override;
	HRESULT SetVertexShader(DWORD Handle) override { VertexShader = Handle; return D3D_OK; }
	HRESULT GetVertexShader(DWORD *pHandle) override
	{
		if (!pHandle) return D3DERR_INVALIDCALL;
		*pHandle = VertexShader;
		return D3D_OK;
	}
	HRESULT SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer8 *pStreamData, UINT Stride) override
	{
		if (StreamNumber >= MAX_STREAMS) return D3DERR_INVALIDCALL;
		if (pStreamData) pStreamData->AddRef();
		if (Streams[StreamNumber]) Streams[StreamNumber]->Release();
		Streams[StreamNumber] = pStreamData;
		StreamStrides[StreamNumber] = Stride;
		return D3D_OK;
	}
	HRESULT GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer8 **ppStreamData, UINT *pStride) override
	{
		if (StreamNumber >= MAX_STREAMS || !ppStreamData || !pStride) return D3DERR_INVALIDCALL;
		*ppStreamData = Streams[StreamNumber];
		if (*ppStreamData) (*ppStreamData)->AddRef();
		*pStride = StreamStrides[StreamNumber];
		return D3D_OK;
	}
	HRESULT SetIndices(IDirect3DIndexBuffer8 *pIndexData, UINT BaseVertexIndex) override
	{
		if (pIndexData) pIndexData->AddRef();
		if (Indices) Indices->Release();
		Indices = pIndexData;
		BaseVertex = BaseVertexIndex;
		return D3D_OK;
	}
	HRESULT GetIndices(IDirect3DIndexBuffer8 **ppIndexData, UINT *pBaseVertexIndex) override
	{
		if (!ppIndexData || !pBaseVertexIndex) return D3DERR_INVALIDCALL;
		*ppIndexData = Indices;
		if (Indices) Indices->AddRef();
		*pBaseVertexIndex = BaseVertex;
		return D3D_OK;
	}

	void Begin_Frame_If_Needed();
	void Init_Pipeline();
	bool Prepare_Draw(UINT base_vertex);

	Direct3D *D3D;
	D3DPRESENT_PARAMETERS Params;
	UINT Width, Height;
	bool HardwareReady;
	bool ShowingFrames;    /* pb_show_front_screen done (at the first Present) */
	unsigned FramesPresented;
	unsigned ClearsTraced;
	bool FrameOpen;
	bool InScene;
	Surface *BackBufferSurface;
	Surface *DepthSurface;
	D3DVIEWPORT8 Viewport;
	D3DMATERIAL8 Material;
	D3DLIGHT8 Lights[MAX_LIGHTS];
	BOOL LightEnabled[MAX_LIGHTS];
	DWORD RenderStates[RENDER_STATE_COUNT];
	DWORD StageStates[MAX_STAGES][TSS_COUNT];
	D3DMATRIX Transforms[TRANSFORM_COUNT];
	IDirect3DBaseTexture8 *Textures[MAX_STAGES];
	IDirect3DVertexBuffer8 *Streams[MAX_STREAMS];
	UINT StreamStrides[MAX_STREAMS];
	IDirect3DIndexBuffer8 *Indices;
	UINT BaseVertex;
	DWORD VertexShader;
	D3DGAMMARAMP Gamma;
	ULONG RefCount;
};

/* --- Surface implementation --------------------------------------------------------------- */

Surface::Surface(Device *device, Kind kind, D3DFORMAT format, UINT width, UINT height, D3DPOOL pool,
                 DWORD usage, BYTE *bits, UINT pitch, Texture *container)
	: SurfaceKind(kind), Format(format), Width(width), Height(height), Pool(pool), Usage(usage),
	  Bits(bits), Pitch(pitch), OwnsBits(false), Container(container), Owner(device), RefCount(1)
{
	if (Container) Container->AddRef();
	Stats.surfaces++;
}

Surface::~Surface()
{
	Stats.surfaces--;
	if (OwnsBits) Free_GPU_Memory(Bits);
	if (Container) Container->Release();
}

HRESULT Surface::GetDevice(IDirect3DDevice8 **ppDevice)
{
	if (!ppDevice) return D3DERR_INVALIDCALL;
	Owner->AddRef();
	*ppDevice = Owner;
	return D3D_OK;
}

HRESULT Surface::LockRect(D3DLOCKED_RECT *pLockedRect, CONST RECT *pRect, DWORD)
{
	if (!pLockedRect) return D3DERR_INVALIDCALL;
	BYTE *bits = Bits;
	UINT pitch = Pitch;
	if (SurfaceKind == BACK_BUFFER) {
		/* The back buffer rotates each frame, so find the current one, after the GPU is done. */
		while (pb_busy()) {
		}
		bits = (BYTE *)pb_back_buffer();
		pitch = pb_back_buffer_pitch();
	}
	if (!bits) return D3DERR_NOTAVAILABLE;          /* the depth buffer is not readable yet */
	if (pRect) {
		FormatInfo info = Format_Info(Format);
		if (info.compressed) {
			bits += (pRect->top / 4) * pitch + (pRect->left / 4) * info.block_bytes;
		} else {
			bits += pRect->top * pitch + pRect->left * info.bytes_per_pixel;
		}
	}
	pLockedRect->pBits = bits;
	pLockedRect->Pitch = (INT)pitch;
	return D3D_OK;
}

/* --- Texture implementation --------------------------------------------------------------- */

Texture::Texture(Device *device, UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool)
	: Owner(device), Format(format), Usage(usage), Pool(pool), LevelCount(0), Memory(NULL), Priority(0),
	  LOD(0), RefCount(1)
{
	/* Level count: 0 means the full chain down to 1 x 1, as in Direct3D. */
	UINT full = 1;
	for (UINT w = width, h = height; (w > 1 || h > 1) && full < MAX_LEVELS; full++) {
		w = w > 1 ? w / 2 : 1;
		h = h > 1 ? h / 2 : 1;
	}
	LevelCount = (levels == 0 || levels > full) ? full : levels;

	UINT total = 0;
	UINT w = width, h = height;
	for (UINT i = 0; i < LevelCount; i++) {
		Levels[i].Width = w;
		Levels[i].Height = h;
		Levels[i].Pitch = Row_Pitch(format, w);
		Levels[i].Size = Level_Size(format, w, h);
		total += (Levels[i].Size + 127) & ~127u;   /* keep every level 128-byte aligned for the GPU */
		w = w > 1 ? w / 2 : 1;
		h = h > 1 ? h / 2 : 1;
	}
	Memory = (BYTE *)Alloc_GPU_Memory(total);
	MemoryBytes = Memory ? total : 0;
	Stats.textures++;
	Stats.texture_bytes += MemoryBytes;
	UINT offset = 0;
	for (UINT i = 0; i < LevelCount; i++) {
		Levels[i].Bits = Memory ? Memory + offset : NULL;
		offset += (Levels[i].Size + 127) & ~127u;
	}
}

HRESULT Texture::GetDevice(IDirect3DDevice8 **ppDevice)
{
	if (!ppDevice) return D3DERR_INVALIDCALL;
	Owner->AddRef();
	*ppDevice = Owner;
	return D3D_OK;
}

HRESULT Texture::GetSurfaceLevel(UINT Level, IDirect3DSurface8 **ppSurfaceLevel)
{
	if (Level >= LevelCount || !ppSurfaceLevel) return D3DERR_INVALIDCALL;
	*ppSurfaceLevel = new Surface(Owner, Surface::TEXTURE_LEVEL, Format, Levels[Level].Width,
	                              Levels[Level].Height, Pool, Usage, Levels[Level].Bits,
	                              Levels[Level].Pitch, this);
	return D3D_OK;
}

HRESULT Texture::LockRect(UINT Level, D3DLOCKED_RECT *pLockedRect, CONST RECT *pRect, DWORD)
{
	if (Level >= LevelCount || !pLockedRect) return D3DERR_INVALIDCALL;
	BYTE *bits = Levels[Level].Bits;
	UINT pitch = Levels[Level].Pitch;
	if (pRect) {
		FormatInfo info = Format_Info(Format);
		if (info.compressed) {
			bits += (pRect->top / 4) * pitch + (pRect->left / 4) * info.block_bytes;
		} else {
			bits += pRect->top * pitch + pRect->left * info.bytes_per_pixel;
		}
	}
	pLockedRect->pBits = bits;
	pLockedRect->Pitch = (INT)pitch;
	return D3D_OK;
}

/* --- Buffer implementation ----------------------------------------------------------------- */

HRESULT VertexBuffer::GetDevice(IDirect3DDevice8 **ppDevice)
{
	if (!ppDevice) return D3DERR_INVALIDCALL;
	Owner->AddRef();
	*ppDevice = Owner;
	return D3D_OK;
}

HRESULT VertexBuffer::Lock(UINT OffsetToLock, UINT SizeToLock, BYTE **ppbData, DWORD)
{
	if (!ppbData || OffsetToLock > Length || SizeToLock > Length - OffsetToLock) return D3DERR_INVALIDCALL;
	*ppbData = Memory + OffsetToLock;
	return D3D_OK;
}

HRESULT IndexBuffer::GetDevice(IDirect3DDevice8 **ppDevice)
{
	if (!ppDevice) return D3DERR_INVALIDCALL;
	Owner->AddRef();
	*ppDevice = Owner;
	return D3D_OK;
}

HRESULT IndexBuffer::Lock(UINT OffsetToLock, UINT SizeToLock, BYTE **ppbData, DWORD)
{
	if (!ppbData || OffsetToLock > Length || SizeToLock > Length - OffsetToLock) return D3DERR_INVALIDCALL;
	*ppbData = Memory + OffsetToLock;
	return D3D_OK;
}

/* --- Device implementation ----------------------------------------------------------------- */

Device::Device(Direct3D *d3d, const D3DPRESENT_PARAMETERS &params)
	: D3D(d3d), Params(params), HardwareReady(false), ShowingFrames(false), FramesPresented(0),
	  ClearsTraced(0), FrameOpen(false), InScene(false),
	  BackBufferSurface(NULL), DepthSurface(NULL), Indices(NULL), BaseVertex(0), VertexShader(0), RefCount(1)
{
	D3D->AddRef();
	Width = params.BackBufferWidth ? params.BackBufferWidth : MODE_WIDTH;
	Height = params.BackBufferHeight ? params.BackBufferHeight : MODE_HEIGHT;

	memset(&Material, 0, sizeof(Material));
	memset(Lights, 0, sizeof(Lights));
	memset(LightEnabled, 0, sizeof(LightEnabled));
	memset(RenderStates, 0, sizeof(RenderStates));
	memset(StageStates, 0, sizeof(StageStates));
	memset(Textures, 0, sizeof(Textures));
	memset(Streams, 0, sizeof(Streams));
	memset(StreamStrides, 0, sizeof(StreamStrides));
	for (int i = 0; i < 256; i++) {
		Gamma.red[i] = Gamma.green[i] = Gamma.blue[i] = (WORD)(i * 257);
	}

	/* Direct3D's documented defaults for the states the engine relies on. */
	static const D3DMATRIX identity = { { { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 } } };
	for (int i = 0; i < TRANSFORM_COUNT; i++) Transforms[i] = identity;
	RenderStates[D3DRS_ZENABLE] = D3DZB_TRUE;
	RenderStates[D3DRS_FILLMODE] = D3DFILL_SOLID;
	RenderStates[D3DRS_SHADEMODE] = D3DSHADE_GOURAUD;
	RenderStates[D3DRS_ZWRITEENABLE] = TRUE;
	RenderStates[D3DRS_LASTPIXEL] = TRUE;
	RenderStates[D3DRS_SRCBLEND] = D3DBLEND_ONE;
	RenderStates[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
	RenderStates[D3DRS_CULLMODE] = D3DCULL_CCW;
	RenderStates[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
	RenderStates[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
	RenderStates[D3DRS_LIGHTING] = TRUE;
	RenderStates[D3DRS_CLIPPING] = TRUE;
	RenderStates[D3DRS_COLORVERTEX] = TRUE;
	RenderStates[D3DRS_DIFFUSEMATERIALSOURCE] = D3DMCS_COLOR1;
	RenderStates[D3DRS_SPECULARMATERIALSOURCE] = D3DMCS_COLOR2;
	RenderStates[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
	RenderStates[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
	RenderStates[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
	RenderStates[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
	RenderStates[D3DRS_STENCILMASK] = 0xFFFFFFFF;
	RenderStates[D3DRS_STENCILWRITEMASK] = 0xFFFFFFFF;
	RenderStates[D3DRS_TEXTUREFACTOR] = 0xFFFFFFFF;
	RenderStates[D3DRS_COLORWRITEENABLE] = 0x0000000F;
	RenderStates[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
	RenderStates[D3DRS_MULTISAMPLEMASK] = 0xFFFFFFFF;
	for (int s = 0; s < MAX_STAGES; s++) {
		StageStates[s][D3DTSS_COLOROP] = s == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE;
		StageStates[s][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
		StageStates[s][D3DTSS_COLORARG2] = D3DTA_CURRENT;
		StageStates[s][D3DTSS_ALPHAOP] = s == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
		StageStates[s][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
		StageStates[s][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
		StageStates[s][D3DTSS_TEXCOORDINDEX] = s;
		StageStates[s][D3DTSS_ADDRESSU] = D3DTADDRESS_WRAP;
		StageStates[s][D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
		StageStates[s][D3DTSS_ADDRESSW] = D3DTADDRESS_WRAP;
		StageStates[s][D3DTSS_MAGFILTER] = D3DTEXF_POINT;
		StageStates[s][D3DTSS_MINFILTER] = D3DTEXF_POINT;
		StageStates[s][D3DTSS_MIPFILTER] = D3DTEXF_NONE;
		StageStates[s][D3DTSS_MAXANISOTROPY] = 1;
		StageStates[s][D3DTSS_COLORARG0] = D3DTA_CURRENT;
		StageStates[s][D3DTSS_ALPHAARG0] = D3DTA_CURRENT;
		StageStates[s][D3DTSS_RESULTARG] = D3DTA_CURRENT;
	}
	Viewport.X = 0;
	Viewport.Y = 0;
	Viewport.Width = Width;
	Viewport.Height = Height;
	Viewport.MinZ = 0.0f;
	Viewport.MaxZ = 1.0f;
}

Device::~Device()
{
	for (int s = 0; s < MAX_STAGES; s++) if (Textures[s]) Textures[s]->Release();
	for (int s = 0; s < MAX_STREAMS; s++) if (Streams[s]) Streams[s]->Release();
	if (Indices) Indices->Release();
	if (BackBufferSurface) BackBufferSurface->Release();
	if (DepthSurface) DepthSurface->Release();
	if (HardwareReady) pb_kill();
	D3D->Release();
}

bool Device::Init_Hardware()
{
	if (Width != MODE_WIDTH || Height != MODE_HEIGHT) {
		Trace("d3d: CreateDevice: %ux%u is not offered", Width, Height);
		return false;
	}
	/* Switch mode only if needed: switching again would wipe the text screen. */
	VIDEO_MODE current = XVideoGetMode();
	if (current.width != (int)Width || current.height != (int)Height || current.bpp != 32) {
		Trace("d3d: setting video mode %ux%u, 32-bit", Width, Height);
		if (!XVideoSetMode((int)Width, (int)Height, 32, REFRESH_DEFAULT)) return false;
	}
	Trace("d3d: pb_init ...");
	int status = pb_init();
	if (status != 0) {
		Trace("d3d: pb_init failed (%d)", status);
		return false;
	}
	/* pb_init ends by showing its own (blank) front buffer, which would hide the text screen
	** until our first Present; switch back so start-up messages stay visible. */
	pb_show_debug_screen();
	Init_Pipeline();
	Trace("d3d: pb_init ok, back buffer %ux%u, pitch %u", (unsigned)pb_back_buffer_width(),
	      (unsigned)pb_back_buffer_height(), (unsigned)pb_back_buffer_pitch());
	/* The text screen stays visible until the first frame is presented (see Present), so
	** anything that goes wrong before then can still be seen. */
	HardwareReady = true;
	BackBufferSurface = new Surface(this, Surface::BACK_BUFFER, D3DFMT_X8R8G8B8, Width, Height,
	                                D3DPOOL_DEFAULT, D3DUSAGE_RENDERTARGET, NULL, 0, NULL);
	DepthSurface = new Surface(this, Surface::DEPTH_BUFFER, D3DFMT_D24S8, Width, Height,
	                           D3DPOOL_DEFAULT, D3DUSAGE_DEPTHSTENCIL, NULL, 0, NULL);
	return true;
}

/* A frame starts with whichever comes first: Clear (the engine clears before BeginScene) or
** BeginScene. It ends at Present. */
void Device::Begin_Frame_If_Needed()
{
	if (FrameOpen) return;
	bool trace = FramesPresented < 2;
	if (trace) Trace("d3d: frame %u: waiting for vertical blank", FramesPresented);
	pb_wait_for_vbl();
	pb_reset();
	pb_target_back_buffer();
	if (trace) Trace("d3d: frame %u: started", FramesPresented);
	FrameOpen = true;
}

HRESULT Device::Clear(DWORD Count, CONST D3DRECT *pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil)
{
	Begin_Frame_If_Needed();

	DWORD clear = 0;
	if (Flags & D3DCLEAR_TARGET) clear |= NV097_CLEAR_SURFACE_COLOR;
	if (Flags & D3DCLEAR_ZBUFFER) clear |= NV097_CLEAR_SURFACE_Z;
	if (Flags & D3DCLEAR_STENCIL) clear |= NV097_CLEAR_SURFACE_STENCIL;
	if (!clear) return D3D_OK;
	if (ClearsTraced < 2) {
		ClearsTraced++;
		Trace("d3d: Clear flags 0x%lx color 0x%08lx z %d/1000", (unsigned long)Flags, (unsigned long)Color,
		      (int)(Z * 1000.0f));
	}

	if (Z < 0.0f) Z = 0.0f;
	if (Z > 1.0f) Z = 1.0f;
	DWORD depth = (DWORD)(Z * (float)0x00FFFFFF);   /* 24-bit depth */
	DWORD zstencil = (depth << 8) | (Stencil & 0xFF);

	/* With no rectangles, Direct3D clears the current viewport. */
	D3DRECT whole;
	if (Count == 0 || !pRects) {
		whole.x1 = (LONG)Viewport.X;
		whole.y1 = (LONG)Viewport.Y;
		whole.x2 = (LONG)(Viewport.X + Viewport.Width);
		whole.y2 = (LONG)(Viewport.Y + Viewport.Height);
		pRects = &whole;
		Count = 1;
	}
	for (DWORD i = 0; i < Count; i++) {
		LONG x1 = pRects[i].x1 < 0 ? 0 : pRects[i].x1;
		LONG y1 = pRects[i].y1 < 0 ? 0 : pRects[i].y1;
		LONG x2 = pRects[i].x2 > (LONG)Width ? (LONG)Width : pRects[i].x2;
		LONG y2 = pRects[i].y2 > (LONG)Height ? (LONG)Height : pRects[i].y2;
		if (x2 <= x1 || y2 <= y1) continue;
		uint32_t *p = pb_begin();
		p = pb_push1(p, NV097_SET_CLEAR_RECT_HORIZONTAL, ((DWORD)(x2 - 1) << 16) | (DWORD)x1);
		p = pb_push1(p, NV097_SET_CLEAR_RECT_VERTICAL, ((DWORD)(y2 - 1) << 16) | (DWORD)y1);
		p = pb_push1(p, NV097_SET_ZSTENCIL_CLEAR_VALUE, zstencil);
		p = pb_push1(p, NV097_SET_COLOR_CLEAR_VALUE, Color);
		p = pb_push1(p, NV097_CLEAR_SURFACE, clear);
		pb_end(p);
	}
	return D3D_OK;
}

HRESULT Device::Present(CONST RECT *, CONST RECT *, HWND, CONST RGNDATA *)
{
	if (!FrameOpen) return D3D_OK;
	bool trace = FramesPresented < 2;
	if (trace) Trace("d3d: frame %u: Present: waiting for the GPU", FramesPresented);
	while (pb_busy()) {
	}
	if (!ShowingFrames) {
		Trace("d3d: first frame done; switching the screen to the GPU's output");
		pb_show_front_screen();
		ShowingFrames = true;
	}
	if (trace) Trace("d3d: frame %u: Present: queueing the swap", FramesPresented);
	while (pb_finished()) {
		/* the previous frame's swap hasn't happened yet */
	}
	if (trace) Trace("d3d: frame %u: presented", FramesPresented);
	FramesPresented++;
	FrameOpen = false;
	return D3D_OK;
}

HRESULT Device::CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                              IDirect3DTexture8 **ppTexture)
{
	if (!ppTexture) return D3DERR_INVALIDCALL;
	*ppTexture = NULL;
	if (Width == 0 || Height == 0 || !Is_Texture_Format_Supported(Format)) return D3DERR_INVALIDCALL;
	if (Usage & D3DUSAGE_RENDERTARGET) {
		static bool logged;
		Log_Once(&logged, "CreateTexture: render-target textures not implemented yet");
		return D3DERR_NOTAVAILABLE;
	}
	static unsigned traced;
	if (traced < 4) {
		traced++;
		Trace("d3d: CreateTexture %ux%u, %u levels, format 0x%lx", Width, Height, Levels, (unsigned long)Format);
	}
	Texture *texture = new Texture(this, Width, Height, Levels, Usage, Format, Pool);
	if (!texture->Valid()) {
		texture->Release();
		return D3DERR_OUTOFVIDEOMEMORY;
	}
	*ppTexture = texture;
	return D3D_OK;
}

HRESULT Device::CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool,
                                   IDirect3DVertexBuffer8 **ppVertexBuffer)
{
	if (!ppVertexBuffer) return D3DERR_INVALIDCALL;
	*ppVertexBuffer = NULL;
	if (Length == 0) return D3DERR_INVALIDCALL;
	VertexBuffer *buffer = new VertexBuffer(this, Length, Usage, FVF, Pool);
	if (!buffer->Valid()) {
		buffer->Release();
		return D3DERR_OUTOFVIDEOMEMORY;
	}
	*ppVertexBuffer = buffer;
	return D3D_OK;
}

HRESULT Device::CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                  IDirect3DIndexBuffer8 **ppIndexBuffer)
{
	if (!ppIndexBuffer) return D3DERR_INVALIDCALL;
	*ppIndexBuffer = NULL;
	if (Length == 0 || (Format != D3DFMT_INDEX16 && Format != D3DFMT_INDEX32)) return D3DERR_INVALIDCALL;
	IndexBuffer *buffer = new IndexBuffer(this, Length, Usage, Format, Pool);
	if (!buffer->Valid()) {
		buffer->Release();
		return D3DERR_OUTOFVIDEOMEMORY;
	}
	*ppIndexBuffer = buffer;
	return D3D_OK;
}

HRESULT Device::CreateImageSurface(UINT Width, UINT Height, D3DFORMAT Format, IDirect3DSurface8 **ppSurface)
{
	if (!ppSurface) return D3DERR_INVALIDCALL;
	*ppSurface = NULL;
	if (Width == 0 || Height == 0 || !Format_Info(Format).known) return D3DERR_INVALIDCALL;
	UINT pitch = Row_Pitch(Format, Width);
	BYTE *bits = (BYTE *)Alloc_GPU_Memory(Level_Size(Format, Width, Height));
	if (!bits) return D3DERR_OUTOFVIDEOMEMORY;
	Surface *surface = new Surface(this, Surface::IMAGE, Format, Width, Height, D3DPOOL_SYSTEMMEM, 0,
	                               bits, pitch, NULL);
	surface->OwnsBits = true;
	*ppSurface = surface;
	return D3D_OK;
}

/* Copies rectangles between surfaces of the same format, without conversion or scaling. */
HRESULT Device::CopyRects(IDirect3DSurface8 *pSourceSurface, CONST RECT *pSourceRectsArray, UINT cRects,
                          IDirect3DSurface8 *pDestinationSurface, CONST POINT *pDestPointsArray)
{
	if (!pSourceSurface || !pDestinationSurface) return D3DERR_INVALIDCALL;
	D3DSURFACE_DESC sd, dd;
	pSourceSurface->GetDesc(&sd);
	pDestinationSurface->GetDesc(&dd);
	if (sd.Format != dd.Format) return D3DERR_INVALIDCALL;
	FormatInfo info = Format_Info(sd.Format);
	UINT unit = info.compressed ? 4 : 1;                          /* DXT copies whole blocks */
	UINT unit_bytes = info.compressed ? info.block_bytes : info.bytes_per_pixel;

	RECT whole = { 0, 0, (LONG)sd.Width, (LONG)sd.Height };
	if (cRects == 0 || !pSourceRectsArray) {
		pSourceRectsArray = &whole;
		cRects = 1;
	}
	D3DLOCKED_RECT src, dst;
	if (FAILED(pSourceSurface->LockRect(&src, NULL, D3DLOCK_READONLY))) return D3DERR_INVALIDCALL;
	if (FAILED(pDestinationSurface->LockRect(&dst, NULL, 0))) {
		pSourceSurface->UnlockRect();
		return D3DERR_INVALIDCALL;
	}
	for (UINT r = 0; r < cRects; r++) {
		const RECT &sr = pSourceRectsArray[r];
		LONG dx = pDestPointsArray ? pDestPointsArray[r].x : sr.left;
		LONG dy = pDestPointsArray ? pDestPointsArray[r].y : sr.top;
		UINT w = (UINT)(sr.right - sr.left), h = (UINT)(sr.bottom - sr.top);
		if ((UINT)dx + w > dd.Width || (UINT)dy + h > dd.Height) continue;
		UINT row_bytes = ((w + unit - 1) / unit) * unit_bytes;
		UINT rows = (h + unit - 1) / unit;
		for (UINT y = 0; y < rows; y++) {
			const BYTE *s = (const BYTE *)src.pBits + (sr.top / unit + y) * src.Pitch + (sr.left / unit) * unit_bytes;
			BYTE *d = (BYTE *)dst.pBits + (dy / unit + y) * dst.Pitch + (dx / unit) * unit_bytes;
			memcpy(d, s, row_bytes);
		}
	}
	pDestinationSurface->UnlockRect();
	pSourceSurface->UnlockRect();
	return D3D_OK;
}

/* Copies every level of one texture into another of the same format and size. */
HRESULT Device::UpdateTexture(IDirect3DBaseTexture8 *pSourceTexture, IDirect3DBaseTexture8 *pDestinationTexture)
{
	if (!pSourceTexture || !pDestinationTexture) return D3DERR_INVALIDCALL;
	if (pSourceTexture->GetType() != D3DRTYPE_TEXTURE || pDestinationTexture->GetType() != D3DRTYPE_TEXTURE) {
		return D3DERR_INVALIDCALL;
	}
	Texture *src = static_cast<Texture *>(static_cast<IDirect3DTexture8 *>(pSourceTexture));
	Texture *dst = static_cast<Texture *>(static_cast<IDirect3DTexture8 *>(pDestinationTexture));
	if (src->Format != dst->Format) return D3DERR_INVALIDCALL;
	UINT count = src->LevelCount < dst->LevelCount ? src->LevelCount : dst->LevelCount;
	for (UINT i = 0; i < count; i++) {
		if (src->Levels[i].Size != dst->Levels[i].Size) return D3DERR_INVALIDCALL;
		memcpy(dst->Levels[i].Bits, src->Levels[i].Bits, src->Levels[i].Size);
	}
	return D3D_OK;
}

/* --- Drawing (step 2) -------------------------------------------------------------------
**
** Vertices go through xbox_ffp.vs.cg, a vertex program reproducing Direct3D 8's fixed-function
** transform and (directional) lighting. Pixels go through the NV2A's register combiners; for
** now they output the lit vertex color (textures come in step 3). The program's constant
** layout, as nxdk's Cg compiler reported it, is mirrored by the CONST_* values below.
*/

static const uint32_t FFP_PROGRAM[] = {
#include "xbox_ffp_vs.inl"
};

enum {
	CONST_BASE = 96,            /* program constant c[i] is uploaded at slot 96 + i */
	CONST_M_SCREEN = 0,         /* c0-c3   world * view * projection * viewport */
	CONST_M_WORLD = 4,          /* c4-c7 */
	CONST_MAT_DIFFUSE = 8,
	CONST_MAT_AMBIENT = 9,
	CONST_MAT_EMISSIVE = 10,
	CONST_AMBIENT_GLOBAL = 11,
	CONST_FLAGS = 12,
	CONST_FLAGS2 = 13,
	CONST_LIGHT_DIR = 14,       /* c14-c17 */
	CONST_LIGHT_DIFFUSE = 18,   /* c18-c21 */
	CONST_LITERAL = 22,         /* "#const c[22] = 1 0" from the compiler */
	CONST_COUNT = 23
};

/* NV2A vertex attribute slots used by the program (texture coordinates land in 9 and 10). */
enum { ATTR_POSITION = 0, ATTR_NORMAL = 2, ATTR_DIFFUSE = 3, ATTR_SPECULAR = 4, ATTR_TEX0 = 9, ATTR_TEX1 = 10 };

static inline DWORD Physical(const void *p) { return (DWORD)((uintptr_t)p & 0x03FFFFFF); }

/* Row-vector product, as Direct3D: out = a * b. */
static void Matrix_Multiply(D3DMATRIX &out, const D3DMATRIX &a, const D3DMATRIX &b)
{
	D3DMATRIX r;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
		}
	}
	out = r;
}

static void Color_To_Floats(DWORD argb, float out[4])
{
	out[0] = ((argb >> 16) & 0xFF) / 255.0f;
	out[1] = ((argb >> 8) & 0xFF) / 255.0f;
	out[2] = (argb & 0xFF) / 255.0f;
	out[3] = (argb >> 24) / 255.0f;
}

/* Direct3D's compare functions are in the same order as the GL values the NV2A takes. */
static DWORD Compare_To_NV(DWORD d3dcmp)
{
	if (d3dcmp < D3DCMP_NEVER || d3dcmp > D3DCMP_ALWAYS) d3dcmp = D3DCMP_ALWAYS;
	return 0x200 + (d3dcmp - D3DCMP_NEVER);
}

static DWORD Blend_To_NV(DWORD d3dblend)
{
	switch (d3dblend) {
	case D3DBLEND_ZERO:         return 0x0000;
	case D3DBLEND_ONE:          return 0x0001;
	case D3DBLEND_SRCCOLOR:     return 0x0300;
	case D3DBLEND_INVSRCCOLOR:  return 0x0301;
	case D3DBLEND_SRCALPHA:     return 0x0302;
	case D3DBLEND_INVSRCALPHA:  return 0x0303;
	case D3DBLEND_DESTALPHA:    return 0x0304;
	case D3DBLEND_INVDESTALPHA: return 0x0305;
	case D3DBLEND_DESTCOLOR:    return 0x0306;
	case D3DBLEND_INVDESTCOLOR: return 0x0307;
	case D3DBLEND_SRCALPHASAT:  return 0x0308;
	default:                    return 0x0001;
	}
}

/* Loads the vertex program and sets up the pixel combiners: called once, after pb_init. */
void Device::Init_Pipeline()
{
	uint32_t *p = pb_begin();
	p = pb_push1(p, NV097_SET_TRANSFORM_PROGRAM_START, 0);
	p = pb_push1(p, NV097_SET_TRANSFORM_EXECUTION_MODE,
	             MASK(NV097_SET_TRANSFORM_EXECUTION_MODE_MODE, NV097_SET_TRANSFORM_EXECUTION_MODE_MODE_PROGRAM) |
	             MASK(NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE, NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE_PRIV));
	p = pb_push1(p, NV097_SET_TRANSFORM_PROGRAM_CXT_WRITE_EN, 0);
	p = pb_push1(p, NV097_SET_TRANSFORM_PROGRAM_LOAD, 0);
	pb_end(p);
	for (unsigned i = 0; i < sizeof(FFP_PROGRAM) / sizeof(FFP_PROGRAM[0]); i += 4) {
		p = pb_begin();
		pb_push(p++, NV097_SET_TRANSFORM_PROGRAM, 4);
		memcpy(p, &FFP_PROGRAM[i], 4 * 4);
		p += 4;
		pb_end(p);
	}

	/* Pixels: stage 0 computes diffuse * 1 into spare0 (color and alpha); the final combiner
	** outputs spare0. Register sources: 0 zero, 4 vertex diffuse, 0xC spare0. */
	p = pb_begin();
	p = pb_push1(p, NV097_SET_SHADER_STAGE_PROGRAM, 0);               /* no texture shader stages */
	p = pb_push1(p, NV097_SET_SHADER_OTHER_STAGE_INPUT, 0);
	p = pb_push1(p, NV097_SET_COMBINER_COLOR_ICW, (4u << 24) | (1u << 21));          /* A=diffuse, B=1 */
	p = pb_push1(p, NV097_SET_COMBINER_COLOR_OCW, 0xCu << 4);                         /* AB -> spare0 */
	p = pb_push1(p, NV097_SET_COMBINER_ALPHA_ICW, (1u << 28) | (4u << 24) | (1u << 21)); /* A=diffuse.a, B=1 */
	p = pb_push1(p, NV097_SET_COMBINER_ALPHA_OCW, 0xCu << 4);
	p = pb_push1(p, NV097_SET_COMBINER_CONTROL, 1);                                   /* one stage */
	p = pb_push1(p, NV097_SET_COMBINER_SPECULAR_FOG_CW0, 0xCu << 8);                  /* rgb = C = spare0 */
	p = pb_push1(p, NV097_SET_COMBINER_SPECULAR_FOG_CW1, (1u << 12) | (0xCu << 8));   /* alpha = G = spare0.a */
	for (int i = 0; i < 4; i++) {
		p = pb_push1(p, NV20_TCL_PRIMITIVE_3D_TX_ENABLE(i), 0x0003ffc0);           /* textures off (step 3) */
	}
	pb_end(p);
}

/* Shader constants, render states and vertex arrays for the next draw. */
bool Device::Prepare_Draw(UINT base_vertex)
{
	DWORD fvf = VertexShader;
	VertexBuffer *vb = static_cast<VertexBuffer *>(Streams[0]);
	if (!vb || fvf == 0 || fvf > 0xFFFF) return false;
	if ((fvf & D3DFVF_POSITION_MASK) != D3DFVF_XYZ) {
		static bool logged;
		Log_Once(&logged, "draw: only untransformed (XYZ) vertices are drawn so far");
		return false;
	}
	UINT stride = StreamStrides[0] ? StreamStrides[0] : D3DXGetFVFVertexSize(fvf);

	/* Constants */
	float c[CONST_COUNT][4];
	memset(c, 0, sizeof(c));
	D3DMATRIX viewport;
	memset(&viewport, 0, sizeof(viewport));
	viewport._11 = Viewport.Width / 2.0f;
	viewport._22 = Viewport.Height / -2.0f;
	viewport._33 = (Viewport.MaxZ - Viewport.MinZ) * (float)0x00FFFFFF;   /* 24-bit depth buffer */
	viewport._41 = Viewport.X + Viewport.Width / 2.0f;
	viewport._42 = Viewport.Y + Viewport.Height / 2.0f;
	viewport._43 = Viewport.MinZ * (float)0x00FFFFFF;
	viewport._44 = 1.0f;
	D3DMATRIX screen;
	Matrix_Multiply(screen, Transforms[D3DTS_WORLD], Transforms[D3DTS_VIEW]);
	Matrix_Multiply(screen, screen, Transforms[D3DTS_PROJECTION]);
	Matrix_Multiply(screen, screen, viewport);
	memcpy(c[CONST_M_SCREEN], &screen, sizeof(screen));
	memcpy(c[CONST_M_WORLD], &Transforms[D3DTS_WORLD], sizeof(D3DMATRIX));
	memcpy(c[CONST_MAT_DIFFUSE], &Material.Diffuse, 16);
	memcpy(c[CONST_MAT_AMBIENT], &Material.Ambient, 16);
	memcpy(c[CONST_MAT_EMISSIVE], &Material.Emissive, 16);
	Color_To_Floats(RenderStates[D3DRS_AMBIENT], c[CONST_AMBIENT_GLOBAL]);
	bool color_vertex = RenderStates[D3DRS_COLORVERTEX] != 0 && (fvf & D3DFVF_DIFFUSE);
	c[CONST_FLAGS][0] = RenderStates[D3DRS_LIGHTING] ? 1.0f : 0.0f;
	c[CONST_FLAGS][1] = (color_vertex && RenderStates[D3DRS_DIFFUSEMATERIALSOURCE] == D3DMCS_COLOR1) ? 1.0f : 0.0f;
	c[CONST_FLAGS][2] = (color_vertex && RenderStates[D3DRS_AMBIENTMATERIALSOURCE] == D3DMCS_COLOR1) ? 1.0f : 0.0f;
	c[CONST_FLAGS][3] = (color_vertex && RenderStates[D3DRS_EMISSIVEMATERIALSOURCE] == D3DMCS_COLOR1) ? 1.0f : 0.0f;
	c[CONST_FLAGS2][0] = (fvf & D3DFVF_DIFFUSE) ? 1.0f : 0.0f;
	c[CONST_FLAGS2][1] = (fvf & D3DFVF_SPECULAR) ? 1.0f : 0.0f;
	c[CONST_FLAGS2][2] = DiagnosticW1 ? 1.0f : 0.0f;
	for (int i = 0; i < 4; i++) {
		if (!LightEnabled[i]) continue;
		if (Lights[i].Type != D3DLIGHT_DIRECTIONAL) {
			static bool logged;
			Log_Once(&logged, "draw: point and spot lights are not used by Renegade's meshes; ignored");
			continue;
		}
		c[CONST_LIGHT_DIR + i][0] = Lights[i].Direction.x;
		c[CONST_LIGHT_DIR + i][1] = Lights[i].Direction.y;
		c[CONST_LIGHT_DIR + i][2] = Lights[i].Direction.z;
		c[CONST_LIGHT_DIFFUSE + i][0] = Lights[i].Diffuse.r;
		c[CONST_LIGHT_DIFFUSE + i][1] = Lights[i].Diffuse.g;
		c[CONST_LIGHT_DIFFUSE + i][2] = Lights[i].Diffuse.b;
	}
	c[CONST_LITERAL][0] = 1.0f;
	c[CONST_LITERAL][1] = 0.0f;

	uint32_t *p = pb_begin();
	p = pb_push1(p, NV097_SET_TRANSFORM_CONSTANT_LOAD, CONST_BASE);
	pb_end(p);
	const float *cf = &c[0][0];
	for (unsigned i = 0; i < CONST_COUNT * 4; i += 16) {
		unsigned n = CONST_COUNT * 4 - i < 16 ? CONST_COUNT * 4 - i : 16;
		p = pb_begin();
		pb_push(p++, NV097_SET_TRANSFORM_CONSTANT, n);
		memcpy(p, cf + i, n * 4);
		p += n;
		pb_end(p);
	}

	/* Render states.
	** Culling: our screen coordinates have y pointing down, so a triangle that looks clockwise
	** on screen has a positive signed area in raw coordinates, which the NV2A (following
	** OpenGL) calls counterclockwise. So Direct3D's "cull clockwise" is front face = CW with the
	** back culled, and "cull counterclockwise" is front face = CCW. (Renegade culls CW.) */
	DWORD color_mask = RenderStates[D3DRS_COLORWRITEENABLE];
	DWORD cull = RenderStates[D3DRS_CULLMODE];
	p = pb_begin();
	p = pb_push1(p, NV097_SET_CULL_FACE_ENABLE, cull == D3DCULL_NONE ? 0 : 1);
	if (cull != D3DCULL_NONE) {
		p = pb_push1(p, NV097_SET_FRONT_FACE, cull == D3DCULL_CW ? NV097_SET_FRONT_FACE_V_CW : NV097_SET_FRONT_FACE_V_CCW);
		p = pb_push1(p, NV097_SET_CULL_FACE, NV097_SET_CULL_FACE_V_BACK);
	}
	p = pb_push1(p, NV097_SET_DEPTH_TEST_ENABLE, RenderStates[D3DRS_ZENABLE] ? 1 : 0);
	p = pb_push1(p, NV097_SET_DEPTH_FUNC, Compare_To_NV(RenderStates[D3DRS_ZFUNC]));
	p = pb_push1(p, NV097_SET_DEPTH_MASK, RenderStates[D3DRS_ZWRITEENABLE] ? 1 : 0);
	p = pb_push1(p, NV097_SET_ALPHA_TEST_ENABLE, RenderStates[D3DRS_ALPHATESTENABLE] ? 1 : 0);
	p = pb_push1(p, NV097_SET_ALPHA_FUNC, Compare_To_NV(RenderStates[D3DRS_ALPHAFUNC]));
	p = pb_push1(p, NV097_SET_ALPHA_REF, RenderStates[D3DRS_ALPHAREF] & 0xFF);
	p = pb_push1(p, NV097_SET_BLEND_ENABLE, RenderStates[D3DRS_ALPHABLENDENABLE] ? 1 : 0);
	p = pb_push1(p, NV097_SET_BLEND_FUNC_SFACTOR, Blend_To_NV(RenderStates[D3DRS_SRCBLEND]));
	p = pb_push1(p, NV097_SET_BLEND_FUNC_DFACTOR, Blend_To_NV(RenderStates[D3DRS_DESTBLEND]));
	p = pb_push1(p, NV097_SET_COLOR_MASK,
	             ((color_mask & D3DCOLORWRITEENABLE_ALPHA) ? 0x01000000 : 0) |
	             ((color_mask & D3DCOLORWRITEENABLE_RED) ? 0x00010000 : 0) |
	             ((color_mask & D3DCOLORWRITEENABLE_GREEN) ? 0x00000100 : 0) |
	             ((color_mask & D3DCOLORWRITEENABLE_BLUE) ? 0x00000001 : 0));
	pb_end(p);

	/* Vertex arrays, straight from the Direct3D vertex buffer (base vertex applied here) */
	const BYTE *base = vb->Memory + base_vertex * stride;
	UINT offset = 0;
	p = pb_begin();
	pb_push(p++, NV097_SET_VERTEX_DATA_ARRAY_FORMAT, 16);
	for (int i = 0; i < 16; i++) *(p++) = NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F;   /* all off */
	pb_end(p);
	struct Attr { int slot; DWORD type; UINT size; UINT bytes; };
	Attr attrs[8];
	int count = 0;
	attrs[count++] = { ATTR_POSITION, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 3, 12 };
	if (fvf & D3DFVF_NORMAL)   attrs[count++] = { ATTR_NORMAL, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 3, 12 };
	if (fvf & D3DFVF_PSIZE)    attrs[count++] = { -1, 0, 0, 4 };
	if (fvf & D3DFVF_DIFFUSE)  attrs[count++] = { ATTR_DIFFUSE, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D, 4, 4 };
	if (fvf & D3DFVF_SPECULAR) attrs[count++] = { ATTR_SPECULAR, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D, 4, 4 };
	UINT tex_count = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
	for (UINT t = 0; t < tex_count; t++) {
		static const UINT sizes[4] = { 2, 3, 4, 1 };   /* by D3DFVF_TEXTUREFORMAT value */
		UINT size = sizes[(fvf >> (16 + t * 2)) & 3];
		int slot = t == 0 ? ATTR_TEX0 : (t == 1 ? ATTR_TEX1 : -1);
		attrs[count++] = { slot, NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, size, size * 4 };
	}
	p = pb_begin();
	for (int i = 0; i < count; i++) {
		if (attrs[i].slot >= 0) {
			p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + attrs[i].slot * 4,
			             MASK(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE, attrs[i].type) |
			             MASK(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_SIZE, attrs[i].size) |
			             MASK(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_STRIDE, stride));
			p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + attrs[i].slot * 4, Physical(base + offset));
		}
		offset += attrs[i].bytes;
	}
	pb_end(p);
	return true;
}

static bool Primitive_To_NV(D3DPRIMITIVETYPE type, UINT prims, DWORD *op, UINT *vertices)
{
	switch (type) {
	case D3DPT_POINTLIST:     *op = NV097_SET_BEGIN_END_OP_POINTS;         *vertices = prims;     return true;
	case D3DPT_LINELIST:      *op = NV097_SET_BEGIN_END_OP_LINES;          *vertices = prims * 2; return true;
	case D3DPT_LINESTRIP:     *op = NV097_SET_BEGIN_END_OP_LINE_STRIP;     *vertices = prims + 1; return true;
	case D3DPT_TRIANGLELIST:  *op = NV097_SET_BEGIN_END_OP_TRIANGLES;      *vertices = prims * 3; return true;
	case D3DPT_TRIANGLESTRIP: *op = NV097_SET_BEGIN_END_OP_TRIANGLE_STRIP; *vertices = prims + 2; return true;
	case D3DPT_TRIANGLEFAN:   *op = NV097_SET_BEGIN_END_OP_TRIANGLE_FAN;   *vertices = prims + 2; return true;
	default: return false;
	}
}

enum { MAX_PUSH_WORDS = 100 };   /* keep each command block small, as nxdk's samples do */

HRESULT Device::DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT, UINT, UINT startIndex, UINT primCount)
{
	Begin_Frame_If_Needed();
	DWORD op;
	UINT count;
	IndexBuffer *ib = static_cast<IndexBuffer *>(Indices);
	if (!ib || primCount == 0 || !Primitive_To_NV(PrimitiveType, primCount, &op, &count)) return D3DERR_INVALIDCALL;
	if (!Prepare_Draw(BaseVertex)) return D3D_OK;
	bool wide = ib->Format == D3DFMT_INDEX32;
	if ((startIndex + count) * (wide ? 4u : 2u) > ib->Length) return D3DERR_INVALIDCALL;

	uint32_t *p = pb_begin();
	p = pb_push1(p, NV097_SET_BEGIN_END, op);
	pb_end(p);
	if (wide) {
		const DWORD *idx = (const DWORD *)ib->Memory + startIndex;
		for (UINT done = 0; done < count;) {
			UINT n = count - done < MAX_PUSH_WORDS ? count - done : MAX_PUSH_WORDS;
			p = pb_begin();
			pb_push(p++, 0x40000000 | NV097_ARRAY_ELEMENT32, n);
			memcpy(p, idx + done, n * 4);
			p += n;
			pb_end(p);
			done += n;
		}
	} else {
		/* 16-bit indices go two per word; an odd last one goes alone as a 32-bit element. */
		const WORD *idx = (const WORD *)ib->Memory + startIndex;
		UINT pairs = count / 2;
		for (UINT done = 0; done < pairs;) {
			UINT n = pairs - done < MAX_PUSH_WORDS ? pairs - done : MAX_PUSH_WORDS;
			p = pb_begin();
			pb_push(p++, 0x40000000 | NV097_ARRAY_ELEMENT16, n);
			for (UINT i = 0; i < n; i++) {
				*(p++) = (DWORD)idx[(done + i) * 2] | ((DWORD)idx[(done + i) * 2 + 1] << 16);
			}
			pb_end(p);
			done += n;
		}
		if (count & 1) {
			p = pb_begin();
			p = pb_push1(p, NV097_ARRAY_ELEMENT32, idx[count - 1]);
			pb_end(p);
		}
	}
	p = pb_begin();
	p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
	pb_end(p);
	return D3D_OK;
}

HRESULT Device::DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount)
{
	Begin_Frame_If_Needed();
	DWORD op;
	UINT count;
	if (PrimitiveCount == 0 || !Primitive_To_NV(PrimitiveType, PrimitiveCount, &op, &count)) return D3DERR_INVALIDCALL;
	if (!Prepare_Draw(0)) return D3D_OK;
	uint32_t *p = pb_begin();
	p = pb_push1(p, NV097_SET_BEGIN_END, op);
	pb_end(p);
	/* DRAW_ARRAYS takes runs of up to 256 vertices: (count - 1) << 24 | first. */
	for (UINT done = 0; done < count;) {
		UINT n = count - done < 256 ? count - done : 256;
		p = pb_begin();
		p = pb_push1(p, NV097_DRAW_ARRAYS, ((n - 1) << 24) | (StartVertex + done));
		pb_end(p);
		done += n;
	}
	p = pb_begin();
	p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
	pb_end(p);
	return D3D_OK;
}

HRESULT Direct3D::CreateDevice(UINT Adapter, D3DDEVTYPE, HWND, DWORD,
                               D3DPRESENT_PARAMETERS *pPresentationParameters,
                               IDirect3DDevice8 **ppReturnedDeviceInterface)
{
	if (Adapter != 0 || !pPresentationParameters || !ppReturnedDeviceInterface) return D3DERR_INVALIDCALL;
	*ppReturnedDeviceInterface = NULL;
	if (pPresentationParameters->Windowed) return D3DERR_NOTAVAILABLE;
	Trace("d3d: CreateDevice %ux%u, back buffer format 0x%lx, depth format 0x%lx",
	      pPresentationParameters->BackBufferWidth, pPresentationParameters->BackBufferHeight,
	      (unsigned long)pPresentationParameters->BackBufferFormat,
	      (unsigned long)pPresentationParameters->AutoDepthStencilFormat);
	Device *device = new Device(this, *pPresentationParameters);
	if (!device->Init_Hardware()) {
		device->Release();
		return D3DERR_NOTAVAILABLE;
	}
	Trace("d3d: CreateDevice ok");
	*ppReturnedDeviceInterface = device;
	return D3D_OK;
}

} /* namespace XboxD3D */

void XboxD3D_Get_Stats(XboxD3DStats *out)
{
	if (out) *out = XboxD3D::Stats;
}

void XboxD3D_Set_Diagnostic_W1(bool on)
{
	XboxD3D::DiagnosticW1 = on;
}

IDirect3D8 *Direct3DCreate8(UINT SDKVersion)
{
	if (SDKVersion != D3D_SDK_VERSION) return NULL;
	XboxD3D::Trace("d3d: Direct3DCreate8");
	return new XboxD3D::Direct3D();
}

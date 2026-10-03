/*
** d3d8.h -- the Direct3D 8 interface, as far as Renegade uses it (Xbox build only).
**
** Names, signatures, constants and error codes match Microsoft's DirectX 8 SDK, so the
** engine's Direct3D code compiles unchanged. The interfaces are plain C++ classes with
** COM-style reference counting; the port's Direct3D layer (built on nxdk's pbkit) provides
** the implementations. Only methods Renegade calls, and their natural pairs, are declared:
** a call to anything else fails to compile, which flags it rather than hiding it.
*/
#ifndef D3D8_H
#define D3D8_H

#ifndef DIRECT3D_VERSION
#define DIRECT3D_VERSION 0x0800
#endif

#include "d3d8types.h"
#include "d3d8caps.h"

#define D3D_SDK_VERSION 220

/* CreateDevice behaviour flags */
#define D3DCREATE_FPU_PRESERVE               0x00000002L
#define D3DCREATE_MULTITHREADED              0x00000004L
#define D3DCREATE_PUREDEVICE                 0x00000010L
#define D3DCREATE_SOFTWARE_VERTEXPROCESSING  0x00000020L
#define D3DCREATE_HARDWARE_VERTEXPROCESSING  0x00000040L
#define D3DCREATE_MIXED_VERTEXPROCESSING     0x00000080L

#define D3DADAPTER_DEFAULT    0
#define D3DENUM_NO_WHQL_LEVEL 0x00000002L

/* Error codes */
#define _FACD3D 0x876
#define MAKE_D3DHRESULT(code) MAKE_HRESULT(1, _FACD3D, code)

#define D3D_OK S_OK
#define D3DERR_WRONGTEXTUREFORMAT         MAKE_D3DHRESULT(2072)
#define D3DERR_UNSUPPORTEDCOLOROPERATION  MAKE_D3DHRESULT(2073)
#define D3DERR_UNSUPPORTEDCOLORARG        MAKE_D3DHRESULT(2074)
#define D3DERR_UNSUPPORTEDALPHAOPERATION  MAKE_D3DHRESULT(2075)
#define D3DERR_UNSUPPORTEDALPHAARG        MAKE_D3DHRESULT(2076)
#define D3DERR_TOOMANYOPERATIONS          MAKE_D3DHRESULT(2077)
#define D3DERR_CONFLICTINGTEXTUREFILTER   MAKE_D3DHRESULT(2078)
#define D3DERR_UNSUPPORTEDFACTORVALUE     MAKE_D3DHRESULT(2079)
#define D3DERR_CONFLICTINGRENDERSTATE     MAKE_D3DHRESULT(2081)
#define D3DERR_UNSUPPORTEDTEXTUREFILTER   MAKE_D3DHRESULT(2082)
#define D3DERR_CONFLICTINGTEXTUREPALETTE  MAKE_D3DHRESULT(2086)
#define D3DERR_DRIVERINTERNALERROR        MAKE_D3DHRESULT(2087)
#define D3DERR_NOTFOUND                   MAKE_D3DHRESULT(2150)
#define D3DERR_MOREDATA                   MAKE_D3DHRESULT(2151)
#define D3DERR_DEVICELOST                 MAKE_D3DHRESULT(2152)
#define D3DERR_DEVICENOTRESET             MAKE_D3DHRESULT(2153)
#define D3DERR_NOTAVAILABLE               MAKE_D3DHRESULT(2154)
#define D3DERR_OUTOFVIDEOMEMORY           MAKE_D3DHRESULT(380)
#define D3DERR_INVALIDDEVICE              MAKE_D3DHRESULT(2155)
#define D3DERR_INVALIDCALL                MAKE_D3DHRESULT(2156)
#define D3DERR_DRIVERINVALIDCALL          MAKE_D3DHRESULT(2157)

#ifdef __cplusplus

struct IDirect3D8;
struct IDirect3DDevice8;
struct IDirect3DResource8;
struct IDirect3DBaseTexture8;
struct IDirect3DTexture8;
struct IDirect3DSurface8;
struct IDirect3DVertexBuffer8;
struct IDirect3DIndexBuffer8;
struct IDirect3DSwapChain8;

typedef IDirect3D8 *LPDIRECT3D8, *PDIRECT3D8;
typedef IDirect3DDevice8 *LPDIRECT3DDEVICE8, *PDIRECT3DDEVICE8;
typedef IDirect3DResource8 *LPDIRECT3DRESOURCE8, *PDIRECT3DRESOURCE8;
typedef IDirect3DBaseTexture8 *LPDIRECT3DBASETEXTURE8, *PDIRECT3DBASETEXTURE8;
typedef IDirect3DTexture8 *LPDIRECT3DTEXTURE8, *PDIRECT3DTEXTURE8;
typedef IDirect3DSurface8 *LPDIRECT3DSURFACE8, *PDIRECT3DSURFACE8;
typedef IDirect3DVertexBuffer8 *LPDIRECT3DVERTEXBUFFER8, *PDIRECT3DVERTEXBUFFER8;
typedef IDirect3DIndexBuffer8 *LPDIRECT3DINDEXBUFFER8, *PDIRECT3DINDEXBUFFER8;
typedef IDirect3DSwapChain8 *LPDIRECT3DSWAPCHAIN8, *PDIRECT3DSWAPCHAIN8;

struct IDirect3D8 : public IUnknown {
	virtual UINT GetAdapterCount(void) = 0;
	virtual HRESULT GetAdapterIdentifier(UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER8 *pIdentifier) = 0;
	virtual UINT GetAdapterModeCount(UINT Adapter) = 0;
	virtual HRESULT EnumAdapterModes(UINT Adapter, UINT Mode, D3DDISPLAYMODE *pMode) = 0;
	virtual HRESULT GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE *pMode) = 0;
	virtual HRESULT CheckDeviceType(UINT Adapter, D3DDEVTYPE CheckType, D3DFORMAT DisplayFormat,
	                                D3DFORMAT BackBufferFormat, BOOL Windowed) = 0;
	virtual HRESULT CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat,
	                                  DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) = 0;
	virtual HRESULT CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat,
	                                       D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) = 0;
	virtual HRESULT GetDeviceCaps(UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS8 *pCaps) = 0;
	virtual HMONITOR GetAdapterMonitor(UINT Adapter) = 0;
	virtual HRESULT CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
	                             DWORD BehaviorFlags, D3DPRESENT_PARAMETERS *pPresentationParameters,
	                             IDirect3DDevice8 **ppReturnedDeviceInterface) = 0;
};

struct IDirect3DDevice8 : public IUnknown {
	virtual HRESULT TestCooperativeLevel(void) = 0;
	virtual UINT GetAvailableTextureMem(void) = 0;
	virtual HRESULT ResourceManagerDiscardBytes(DWORD Bytes) = 0;
	virtual HRESULT GetDirect3D(IDirect3D8 **ppD3D8) = 0;
	virtual HRESULT GetDeviceCaps(D3DCAPS8 *pCaps) = 0;
	virtual HRESULT GetDisplayMode(D3DDISPLAYMODE *pMode) = 0;
	virtual HRESULT CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *pPresentationParameters,
	                                          IDirect3DSwapChain8 **pSwapChain) = 0;
	virtual HRESULT Reset(D3DPRESENT_PARAMETERS *pPresentationParameters) = 0;
	virtual HRESULT Present(CONST RECT *pSourceRect, CONST RECT *pDestRect, HWND hDestWindowOverride,
	                        CONST RGNDATA *pDirtyRegion) = 0;
	virtual HRESULT GetBackBuffer(UINT BackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface8 **ppBackBuffer) = 0;
	virtual void SetGammaRamp(DWORD Flags, CONST D3DGAMMARAMP *pRamp) = 0;
	virtual void GetGammaRamp(D3DGAMMARAMP *pRamp) = 0;
	virtual HRESULT CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format,
	                              D3DPOOL Pool, IDirect3DTexture8 **ppTexture) = 0;
	virtual HRESULT CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool,
	                                   IDirect3DVertexBuffer8 **ppVertexBuffer) = 0;
	virtual HRESULT CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
	                                  IDirect3DIndexBuffer8 **ppIndexBuffer) = 0;
	virtual HRESULT CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
	                                   BOOL Lockable, IDirect3DSurface8 **ppSurface) = 0;
	virtual HRESULT CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format,
	                                          D3DMULTISAMPLE_TYPE MultiSample, IDirect3DSurface8 **ppSurface) = 0;
	virtual HRESULT CreateImageSurface(UINT Width, UINT Height, D3DFORMAT Format, IDirect3DSurface8 **ppSurface) = 0;
	virtual HRESULT CopyRects(IDirect3DSurface8 *pSourceSurface, CONST RECT *pSourceRectsArray, UINT cRects,
	                          IDirect3DSurface8 *pDestinationSurface, CONST POINT *pDestPointsArray) = 0;
	virtual HRESULT UpdateTexture(IDirect3DBaseTexture8 *pSourceTexture, IDirect3DBaseTexture8 *pDestinationTexture) = 0;
	virtual HRESULT GetFrontBuffer(IDirect3DSurface8 *pDestSurface) = 0;
	virtual HRESULT SetRenderTarget(IDirect3DSurface8 *pRenderTarget, IDirect3DSurface8 *pNewZStencil) = 0;
	virtual HRESULT GetRenderTarget(IDirect3DSurface8 **ppRenderTarget) = 0;
	virtual HRESULT GetDepthStencilSurface(IDirect3DSurface8 **ppZStencilSurface) = 0;
	virtual HRESULT BeginScene(void) = 0;
	virtual HRESULT EndScene(void) = 0;
	virtual HRESULT Clear(DWORD Count, CONST D3DRECT *pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) = 0;
	virtual HRESULT SetTransform(D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX *pMatrix) = 0;
	virtual HRESULT GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX *pMatrix) = 0;
	virtual HRESULT SetViewport(CONST D3DVIEWPORT8 *pViewport) = 0;
	virtual HRESULT GetViewport(D3DVIEWPORT8 *pViewport) = 0;
	virtual HRESULT SetMaterial(CONST D3DMATERIAL8 *pMaterial) = 0;
	virtual HRESULT GetMaterial(D3DMATERIAL8 *pMaterial) = 0;
	virtual HRESULT SetLight(DWORD Index, CONST D3DLIGHT8 *pLight) = 0;
	virtual HRESULT GetLight(DWORD Index, D3DLIGHT8 *pLight) = 0;
	virtual HRESULT LightEnable(DWORD Index, BOOL Enable) = 0;
	virtual HRESULT GetLightEnable(DWORD Index, BOOL *pEnable) = 0;
	virtual HRESULT SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) = 0;
	virtual HRESULT GetRenderState(D3DRENDERSTATETYPE State, DWORD *pValue) = 0;
	virtual HRESULT GetTexture(DWORD Stage, IDirect3DBaseTexture8 **ppTexture) = 0;
	virtual HRESULT SetTexture(DWORD Stage, IDirect3DBaseTexture8 *pTexture) = 0;
	virtual HRESULT GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD *pValue) = 0;
	virtual HRESULT SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) = 0;
	virtual HRESULT ValidateDevice(DWORD *pNumPasses) = 0;
	virtual HRESULT DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) = 0;
	virtual HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT minIndex, UINT NumVertices,
	                                     UINT startIndex, UINT primCount) = 0;
	virtual HRESULT SetVertexShader(DWORD Handle) = 0;
	virtual HRESULT GetVertexShader(DWORD *pHandle) = 0;
	virtual HRESULT SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer8 *pStreamData, UINT Stride) = 0;
	virtual HRESULT GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer8 **ppStreamData, UINT *pStride) = 0;
	virtual HRESULT SetIndices(IDirect3DIndexBuffer8 *pIndexData, UINT BaseVertexIndex) = 0;
	virtual HRESULT GetIndices(IDirect3DIndexBuffer8 **ppIndexData, UINT *pBaseVertexIndex) = 0;
};

struct IDirect3DResource8 : public IUnknown {
	virtual HRESULT GetDevice(IDirect3DDevice8 **ppDevice) = 0;
	virtual DWORD SetPriority(DWORD PriorityNew) = 0;
	virtual DWORD GetPriority(void) = 0;
	virtual void PreLoad(void) = 0;
	virtual D3DRESOURCETYPE GetType(void) = 0;
};

struct IDirect3DBaseTexture8 : public IDirect3DResource8 {
	virtual DWORD SetLOD(DWORD LODNew) = 0;
	virtual DWORD GetLOD(void) = 0;
	virtual DWORD GetLevelCount(void) = 0;
};

struct IDirect3DTexture8 : public IDirect3DBaseTexture8 {
	virtual HRESULT GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) = 0;
	virtual HRESULT GetSurfaceLevel(UINT Level, IDirect3DSurface8 **ppSurfaceLevel) = 0;
	virtual HRESULT LockRect(UINT Level, D3DLOCKED_RECT *pLockedRect, CONST RECT *pRect, DWORD Flags) = 0;
	virtual HRESULT UnlockRect(UINT Level) = 0;
	virtual HRESULT AddDirtyRect(CONST RECT *pDirtyRect) = 0;
};

/* As in Direct3D 8, a surface is not a resource: it derives directly from IUnknown. */
struct IDirect3DSurface8 : public IUnknown {
	virtual HRESULT GetDevice(IDirect3DDevice8 **ppDevice) = 0;
	virtual HRESULT GetContainer(REFIID riid, void **ppContainer) = 0;
	virtual HRESULT GetDesc(D3DSURFACE_DESC *pDesc) = 0;
	virtual HRESULT LockRect(D3DLOCKED_RECT *pLockedRect, CONST RECT *pRect, DWORD Flags) = 0;
	virtual HRESULT UnlockRect(void) = 0;
};

struct IDirect3DVertexBuffer8 : public IDirect3DResource8 {
	virtual HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, BYTE **ppbData, DWORD Flags) = 0;
	virtual HRESULT Unlock(void) = 0;
	virtual HRESULT GetDesc(D3DVERTEXBUFFER_DESC *pDesc) = 0;
};

struct IDirect3DIndexBuffer8 : public IDirect3DResource8 {
	virtual HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, BYTE **ppbData, DWORD Flags) = 0;
	virtual HRESULT Unlock(void) = 0;
	virtual HRESULT GetDesc(D3DINDEXBUFFER_DESC *pDesc) = 0;
};

struct IDirect3DSwapChain8 : public IUnknown {
	virtual HRESULT Present(CONST RECT *pSourceRect, CONST RECT *pDestRect, HWND hDestWindowOverride,
	                        CONST RGNDATA *pDirtyRegion) = 0;
	virtual HRESULT GetBackBuffer(UINT BackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface8 **ppBackBuffer) = 0;
};

/* Creates the Direct3D object (provided by the port's Direct3D layer). */
IDirect3D8 *Direct3DCreate8(UINT SDKVersion);

#endif /* __cplusplus */

#endif /* D3D8_H */

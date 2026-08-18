#pragma once

#include <d3d9.h>
#include <d3dx9.h>

// D3D8 to D3D9 Type Mappings using macros to support legacy forward declarations
#define IDirect3D8 IDirect3D9
#define IDirect3DDevice8 IDirect3DDevice9
#define IDirect3DTexture8 IDirect3DTexture9
#define IDirect3DVolumeTexture8 IDirect3DVolumeTexture9
#define IDirect3DCubeTexture8 IDirect3DCubeTexture9
#define IDirect3DVolume8 IDirect3DVolume9
#define IDirect3DSurface8 IDirect3DSurface9
#define IDirect3DSwapChain8 IDirect3DSwapChain9
#define IDirect3DVertexBuffer8 IDirect3DVertexBuffer9
#define IDirect3DIndexBuffer8 IDirect3DIndexBuffer9
#define IDirect3DBaseTexture8 IDirect3DBaseTexture9
#define D3DCAPS8 D3DCAPS9
#define D3DVIEWPORT8 D3DVIEWPORT9
#define D3DMATERIAL8 D3DMATERIAL9
#define D3DLIGHT8 D3DLIGHT9
#define D3DADAPTER_IDENTIFIER8 D3DADAPTER_IDENTIFIER9

// Pointer type macros
#define LPDIRECT3D8 IDirect3D9*
#define LPDIRECT3DDEVICE8 IDirect3DDevice9*
#define LPDIRECT3DTEXTURE8 IDirect3DTexture9*
#define LPDIRECT3DVOLUMETEXTURE8 IDirect3DVolumeTexture9*
#define LPDIRECT3DCUBETEXTURE8 IDirect3DCubeTexture9*
#define LPDIRECT3DVOLUME8 IDirect3DVolume9*
#define LPDIRECT3DSURFACE8 IDirect3DSurface9*

#define LPDIRECT3DSWAPCHAIN8 IDirect3DSwapChain9*
#define LPDIRECT3DVERTEXBUFFER8 IDirect3DVertexBuffer9*
#define LPDIRECT3DINDEXBUFFER8 IDirect3DIndexBuffer9*
#define LPDIRECT3DBASETEXTURE8 IDirect3DBaseTexture9*

// Legacy texture stage states that became sampler states in D3D9
#define D3DTSS_ADDRESSU ((D3DTEXTURESTAGESTATETYPE)13)
#define D3DTSS_ADDRESSV ((D3DTEXTURESTAGESTATETYPE)14)
#define D3DTSS_BORDERCOLOR ((D3DTEXTURESTAGESTATETYPE)15)
#define D3DTSS_MAGFILTER ((D3DTEXTURESTAGESTATETYPE)16)
#define D3DTSS_MINFILTER ((D3DTEXTURESTAGESTATETYPE)17)
#define D3DTSS_MIPFILTER ((D3DTEXTURESTAGESTATETYPE)18)
#define D3DTSS_MIPMAPLODBIAS ((D3DTEXTURESTAGESTATETYPE)19)
#define D3DTSS_MAXMIPLEVEL ((D3DTEXTURESTAGESTATETYPE)20)
#define D3DTSS_MAXANISOTROPY ((D3DTEXTURESTAGESTATETYPE)21)
#define D3DTSS_ADDRESSW ((D3DTEXTURESTAGESTATETYPE)25)

// Missing D3D8 render states mapped to safe dummy values within RenderStates[256]
#define D3DRS_LINEPATTERN               ((D3DRENDERSTATETYPE)220)
#define D3DRS_SOFTWAREVERTEXPROCESSING  ((D3DRENDERSTATETYPE)221)
#define D3DRS_ZVISIBLE                  ((D3DRENDERSTATETYPE)222)
#define D3DRS_PATCHSEGMENTS             ((D3DRENDERSTATETYPE)223)
#define D3DRS_ZBIAS                     ((D3DRENDERSTATETYPE)224)
#define D3DRS_EDGEANTIALIAS             ((D3DRENDERSTATETYPE)225)
#define D3DRS_PATCHEDGESTYLE            ((D3DRENDERSTATETYPE)226)

// Legacy Direct3D 8 Vertex Shader Declaration Token Macros
#define D3DVSD_TOKEN_STREAM             0
#define D3DVSD_TOKEN_STREAMDATA         1
#define D3DVSD_TOKEN_TESSELLATOR        2
#define D3DVSD_TOKEN_CONSTMEM           3
#define D3DVSD_TOKENTYPE_SHIFT          29
#define D3DVSD_MAKETOKENTYPE(tokenType) ((tokenType) << D3DVSD_TOKENTYPE_SHIFT)
#define D3DVSD_DATATYPESHIFT            16
#define D3DVSD_REG(reg, type) (D3DVSD_MAKETOKENTYPE(D3DVSD_TOKEN_STREAMDATA) | ((type) << D3DVSD_DATATYPESHIFT) | (reg))
#define D3DVSD_STREAM(stream) (D3DVSD_MAKETOKENTYPE(D3DVSD_TOKEN_STREAM) | (stream))
#define D3DVSD_END()                    0xFFFFFFFF

enum D3DVSD_DATATYPE
{
    D3DVSDT_FLOAT1      = 0,
    D3DVSDT_FLOAT2      = 1,
    D3DVSDT_FLOAT3      = 2,
    D3DVSDT_FLOAT4      = 3,
    D3DVSDT_D3DCOLOR    = 4,
    D3DVSDT_UBYTE4      = 5,
    D3DVSDT_SHORT2      = 6,
    D3DVSDT_SHORT4      = 7,
};

// Missing D3D8 texture filter modes
#define D3DTEXF_FLATCUBIC               ((D3DTEXTUREFILTERTYPE)4)
#define D3DTEXF_GAUSSIANCUBIC           ((D3DTEXTUREFILTERTYPE)5)


// D3D8 to D3D9 Resource Creation mapping (bridges the missing pSharedHandle / Quality / Discard arguments)
#define CreateIndexBuffer(len, usage, format, pool, ppIB) CreateIndexBuffer(len, usage, format, pool, ppIB, nullptr)
#define CreateVertexBuffer(len, usage, fvf, pool, ppVB) CreateVertexBuffer(len, usage, fvf, pool, ppVB, nullptr)
#define CreateTexture(w, h, l, u, f, p, pp) CreateTexture(w, h, l, u, f, p, pp, nullptr)
#define CreateCubeTexture(e, l, u, f, p, pp) CreateCubeTexture(e, l, u, f, p, pp, nullptr)
#define CreateVolumeTexture(w, h, d, l, u, f, p, pp) CreateVolumeTexture(w, h, d, l, u, f, p, pp, nullptr)
#define CreateRenderTarget(w, h, f, ms, l, pp) CreateRenderTarget(w, h, f, ms, 0, l, pp, nullptr)
#define CreateDepthStencilSurface(w, h, f, ms, pp) CreateDepthStencilSurface(w, h, f, ms, 0, TRUE, pp, nullptr)

// Macro mappings
#define FullScreen_PresentationInterval PresentationInterval

#ifndef D3DSWAPEFFECT_COPY_VSYNC
#define D3DSWAPEFFECT_COPY_VSYNC D3DSWAPEFFECT_COPY
#endif

// Direct3D 8 to Direct3D 9 mapping for stream & index buffer bindings and drawing
extern INT g_D3D9_BaseVertexIndex;
#define SetStreamSource(stream, data, stride) SetStreamSource(stream, data, 0, stride)
#define SetIndices(pIndexData, BaseVertexIndex) SetIndices((g_D3D9_BaseVertexIndex = (BaseVertexIndex), pIndexData))
#define DrawIndexedPrimitive(type, minIndex, numVerts, startIndex, primCount) \
	DrawIndexedPrimitive(type, g_D3D9_BaseVertexIndex, minIndex, numVerts, startIndex, primCount)
#define D3DENUM_NO_WHQL_LEVEL 0
#define DX8_LOCK_CAST(x) (void**)(x)

// Direct3D 8 to Direct3D 9 compatibility wrappers for shader creation, binding, constants, and destruction
#define CreateVertexShader(decl, func, handle, usage) CreateVertexShader(func, reinterpret_cast<IDirect3DVertexShader9**>(handle))
#define CreatePixelShader(func, handle) CreatePixelShader(func, reinterpret_cast<IDirect3DPixelShader9**>(handle))
#define D3DXAssembleShader(src, len, flags, consts, ppShader, ppErrors) D3DXAssembleShader(src, len, nullptr, nullptr, flags, ppShader, ppErrors)
#define DeleteVertexShader(handle) TestCooperativeLevel(), (handle ? ((IUnknown*)(handle))->Release() : 0)
#define DeletePixelShader(handle) TestCooperativeLevel(), (handle ? ((IUnknown*)(handle))->Release() : 0)
#define SetVertexShaderConstant(reg, data, count) SetVertexShaderConstantF(reg, (const float*)(data), count)
#define SetPixelShaderConstant(reg, data, count) SetPixelShaderConstantF(reg, (const float*)(data), count)

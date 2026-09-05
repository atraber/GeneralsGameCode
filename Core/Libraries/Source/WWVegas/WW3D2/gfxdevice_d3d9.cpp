/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "gfxdevice_d3d9.h"
#include "dx8wrapper.h"
#include "formconv.h"
#include "WWLib/DbgHelpGuard.h"

// Defined in dx8wrapper.cpp: decodes an HRESULT and logs it without asserting.
extern void Non_Fatal_Log_DX8_ErrorCode(unsigned res, const char * file, int line);

// What DX8CALL used to do at the call site, now that the call site is one seam away:
// check the result and count it. Counting here rather than in the wrapper is the only
// way the count can be right, because one interface call is not always one device call
// -- binding a vertex format is two in D3D9 and would be one anywhere else.
#define D3DCALL(x) DX8_ErrorCode(m_device->x); DX8Wrapper::Increment_DX8_CallCount()

// ---------------------------------------------------------------------------
// The absorbed-write experiment. Debug only, off unless asked for.
//
// The D3D11 backend silently drops every state write that has no D3D11 meaning and counts
// them: per 600-frame window on civ_buildings, 5 render-state writes, 4502 texture-stage
// writes and every one of 1582 transforms. Those are assumed harmless and had never been
// tested, and the only test possible is to make *this* backend drop exactly the same
// writes and diff D3D9-with against D3D9-without. Nothing else can answer it, and once the
// D3D9 backend is removed nothing ever will.
//
//   W3D_D3D9_DROP_ABSORBED=rs,tss,xf     (any subset, or "all")
//
// The predicate is GfxAbsorb's, shared with the D3D11 backend, so this cannot quietly
// become an experiment about two different sets -- and the D3D11 backend counts and prints
// any disagreement between that predicate and what its own switch does.
//
// The transform case mirrors D3D11 exactly: the write to the fixed-function pipeline is
// dropped and the value is *kept*, because D3D11 keeps it too and Bind_Ui_Shader_World
// reads VIEW and PROJECTION back off the device.
//
// Default off. The phase's control was run with it off.
// ---------------------------------------------------------------------------

#ifdef RTS_DEBUG
namespace
{
	enum { DROP_RS = 1, DROP_TSS = 2, DROP_XF = 4 };

	unsigned Absorb_Drop_Mask()
	{
		static int cached = -1;
		if (cached >= 0) return (unsigned)cached;
		cached = 0;
		const char * env = getenv("W3D_D3D9_DROP_ABSORBED");
		if (env != nullptr && *env != 0) {
			if (strstr(env, "all") != nullptr) cached = DROP_RS | DROP_TSS | DROP_XF;
			if (strstr(env, "rs") != nullptr) cached |= DROP_RS;
			if (strstr(env, "tss") != nullptr) cached |= DROP_TSS;
			if (strstr(env, "xf") != nullptr) cached |= DROP_XF;
			WWDEBUG_SAY(("D3D9 DROP ABSORBED: '%s' -> mask 0x%x. This backend is deliberately "
				"dropping the writes D3D11 has no meaning for. It is an experiment, not a "
				"configuration, and any pixel difference it produces is the result.",
				env, (unsigned)cached));
		}
		return (unsigned)cached;
	}

	// What D3D11 keeps so that a caller can read it back. Only touched when xf is being
	// dropped, so the ordinary path is exactly what it was.
	float s_dropped_transforms[512][16];
	bool  s_dropped_transform_valid[512];

	// What the drop switch actually dropped, per group.
	//
	// The whole point of the experiment is that D3D9 draws an identical frame with these
	// writes gone, and a zero pixel difference means nothing unless something says the
	// writes really were gone. The device-state audit is that control for two of the three
	// groups -- with rs dropped it reads 122 wrong words instead of its usual 2, with tss
	// 1122 -- but it is blind to the transform group by construction, because that group
	// keeps a shadow copy and Get_Transform answers out of it exactly as D3D11 does. So the
	// transforms need a counter of their own, and the other two get one for symmetry.
	unsigned s_dropped_rs = 0;
	unsigned s_dropped_tss = 0;
	unsigned s_dropped_xf = 0;
}
#endif

namespace
{
	// Ten of the D3D8 texture stage states became sampler states in D3D9 and are
	// reached through a different entry point there. This is the whole of that
	// difference, and it is a fact about D3D9, so it lives on this side of the seam.
	// d3d9_compat.h keeps the D3DTSS_ spellings alive as their old D3D8 numbers.
	bool Sampler_Remap(unsigned state, D3DSAMPLERSTATETYPE & out)
	{
		switch (state) {
			case D3DTSS_ADDRESSU:      out = D3DSAMP_ADDRESSU;      return true;
			case D3DTSS_ADDRESSV:      out = D3DSAMP_ADDRESSV;      return true;
			case D3DTSS_ADDRESSW:      out = D3DSAMP_ADDRESSW;      return true;
			case D3DTSS_BORDERCOLOR:   out = D3DSAMP_BORDERCOLOR;   return true;
			case D3DTSS_MAGFILTER:     out = D3DSAMP_MAGFILTER;     return true;
			case D3DTSS_MINFILTER:     out = D3DSAMP_MINFILTER;     return true;
			case D3DTSS_MIPFILTER:     out = D3DSAMP_MIPFILTER;     return true;
			case D3DTSS_MIPMAPLODBIAS: out = D3DSAMP_MIPMAPLODBIAS; return true;
			case D3DTSS_MAXMIPLEVEL:   out = D3DSAMP_MAXMIPLEVEL;   return true;
			case D3DTSS_MAXANISOTROPY: out = D3DSAMP_MAXANISOTROPY; return true;
			default: return false;
		}
	}
}

// ----------------------------------------------------------------------------
// Frame
// ----------------------------------------------------------------------------

void GfxDeviceD3D9::Begin_Scene()
{
	D3DCALL(BeginScene());
}

void GfxDeviceD3D9::End_Scene()
{
	D3DCALL(EndScene());
}

GfxDeviceStatus GfxDeviceD3D9::Present()
{
	const HRESULT hr = m_device->Present(nullptr, nullptr, nullptr, nullptr);
	if (SUCCEEDED(hr)) return GFX_DEVICE_OK;
	if (hr == D3DERR_DEVICELOST) return GFX_DEVICE_LOST;
	DX8_ErrorCode(hr);
	return GFX_DEVICE_ERROR;
}

GfxDeviceStatus GfxDeviceD3D9::Get_Device_Status()
{
	const HRESULT hr = m_device->TestCooperativeLevel();
	if (SUCCEEDED(hr)) return GFX_DEVICE_OK;
	if (hr == D3DERR_DEVICENOTRESET) return GFX_DEVICE_NEEDS_RESET;
	if (hr == D3DERR_DEVICELOST) return GFX_DEVICE_LOST;
	return GFX_DEVICE_ERROR;
}

void GfxDeviceD3D9::Clear(bool clear_color, bool clear_z, bool clear_stencil,
	unsigned argb, float z, unsigned stencil)
{
	DWORD flags = 0;
	if (clear_color) flags |= D3DCLEAR_TARGET;
	if (clear_z) flags |= D3DCLEAR_ZBUFFER;
	if (clear_stencil) flags |= D3DCLEAR_STENCIL;
	if (!flags) return;
	D3DCALL(Clear(0, nullptr, flags, (D3DCOLOR)argb, z, stencil));
}

bool GfxDeviceD3D9::Has_Stencil_Target()
{
	// Asking for a stencil clear when the bound depth buffer has no stencil bits
	// fails the whole Clear, colour included, so the answer has to come from the
	// surface that is actually bound rather than from the presentation parameters.
	IDirect3DSurface8 * depthbuffer = nullptr;
	m_device->GetDepthStencilSurface(&depthbuffer);
	if (depthbuffer == nullptr) return false;

	D3DSURFACE_DESC desc;
	const bool ok = SUCCEEDED(depthbuffer->GetDesc(&desc));
	depthbuffer->Release();
	if (!ok) return false;

	return desc.Format == D3DFMT_D15S1 ||
		desc.Format == D3DFMT_D24S8 ||
		desc.Format == D3DFMT_D24X4S4;
}

// ----------------------------------------------------------------------------
// Render state
// ----------------------------------------------------------------------------

void GfxDeviceD3D9::Set_Render_State(unsigned state, unsigned value)
{
#ifdef RTS_DEBUG
	if ((Absorb_Drop_Mask() & DROP_RS) != 0 && GfxAbsorb::Render_State_Is_Absorbed(state))
		{ ++s_dropped_rs; return; }
#endif
	if (state == D3DRS_SOFTWAREVERTEXPROCESSING) {
		// Not a render state in D3D9 at all; d3d9_compat.h keeps the number alive as a
		// dummy slot so the engine's tracked array still has somewhere to put it.
		D3DCALL(SetSoftwareVertexProcessing(value));
		return;
	}
	if (state == D3DRS_ZBIAS) {
		// D3D8's ZBIAS was an integer 0..16; D3D9 wants a float in depth-buffer units.
		// This is the one place that knows the conversion, and a raw read of the state
		// off a D3D9 device does not return anything like it -- which is what made
		// half-converting a save/restore pair cost a regression once already.
		const float bias = (float)value * -0.000005f;
		D3DCALL(SetRenderState(D3DRS_DEPTHBIAS, *(const DWORD*)&bias));
		return;
	}
	if (state == D3DRS_LINEPATTERN || state == D3DRS_ZVISIBLE ||
		state == D3DRS_PATCHSEGMENTS || state == D3DRS_EDGEANTIALIAS ||
		state == D3DRS_PATCHEDGESTYLE) {
		// D3D8 states D3D9 dropped outright. Absorbed here rather than at the caller,
		// so the engine never has to know which of its state words this API kept.
		return;
	}
	D3DCALL(SetRenderState((D3DRENDERSTATETYPE)state, value));
}

void GfxDeviceD3D9::Set_Texture_Stage_State(unsigned stage, unsigned state, unsigned value)
{
#ifdef RTS_DEBUG
	if ((Absorb_Drop_Mask() & DROP_TSS) != 0 && GfxAbsorb::Stage_State_Is_Absorbed(state))
		{ ++s_dropped_tss; return; }
#endif
	D3DSAMPLERSTATETYPE sampler_state;
	if (Sampler_Remap(state, sampler_state)) {
		D3DCALL(SetSamplerState(stage, sampler_state, value));
	} else {
		D3DCALL(SetTextureStageState(stage, (D3DTEXTURESTAGESTATETYPE)state, value));
	}
}

void GfxDeviceD3D9::Set_Clip_Plane(unsigned index, const float * plane)
{
	D3DCALL(SetClipPlane(index, plane));
}

bool GfxDeviceD3D9::Get_Render_State(unsigned state, unsigned & value)
{
	DWORD actual = 0;
	if (FAILED(m_device->GetRenderState((D3DRENDERSTATETYPE)state, &actual))) return false;
	value = (unsigned)actual;
	return true;
}

bool GfxDeviceD3D9::Get_Texture_Stage_State(unsigned stage, unsigned state, unsigned & value)
{
	DWORD actual = 0;
	D3DSAMPLERSTATETYPE sampler_state;
	if (Sampler_Remap(state, sampler_state)) {
		if (FAILED(m_device->GetSamplerState(stage, sampler_state, &actual))) return false;
	} else {
		if (FAILED(m_device->GetTextureStageState(stage, (D3DTEXTURESTAGESTATETYPE)state, &actual))) return false;
	}
	value = (unsigned)actual;
	return true;
}


// ----------------------------------------------------------------------------
// Fixed-function residue
// ----------------------------------------------------------------------------




void GfxDeviceD3D9::Set_Transform(unsigned which, const float * matrix4x4)
{
#ifdef RTS_DEBUG
	if ((Absorb_Drop_Mask() & DROP_XF) != 0) {
		// Exactly what D3D11 does: the fixed-function pipeline never sees it, and the
		// value is kept so that a reader still gets an answer.
		if (which < 512 && matrix4x4 != nullptr) {
			memcpy(s_dropped_transforms[which], matrix4x4, 16 * sizeof(float));
			s_dropped_transform_valid[which] = true;
		}
		++s_dropped_xf;
		return;
	}
#endif
	D3DCALL(SetTransform((D3DTRANSFORMSTATETYPE)which, (const D3DMATRIX*)matrix4x4));
}

bool GfxDeviceD3D9::Get_Transform(unsigned which, float * matrix4x4)
{
#ifdef RTS_DEBUG
	if ((Absorb_Drop_Mask() & DROP_XF) != 0) {
		if (which >= 512 || !s_dropped_transform_valid[which]) return false;
		memcpy(matrix4x4, s_dropped_transforms[which], 16 * sizeof(float));
		return true;
	}
#endif
	return SUCCEEDED(m_device->GetTransform((D3DTRANSFORMSTATETYPE)which, (D3DMATRIX*)matrix4x4));
}

// ----------------------------------------------------------------------------
// Bindings
// ----------------------------------------------------------------------------

void GfxDeviceD3D9::Set_Texture(unsigned stage, GfxTexture * texture)
{
	D3DCALL(SetTexture(stage, (IDirect3DBaseTexture8*)texture));
}

void GfxDeviceD3D9::Set_Vertex_Shader(GfxShaderHandle shader)
{
	// A handle below 0x10000 is an FVF code, not a compiled shader. In D3D8 those were
	// the same thing; in D3D9 they are two calls, and setting an FVF leaves a vertex
	// shader bound unless it is explicitly cleared. Callers do not have to know that.
	if (shader < 0x10000) {
		D3DCALL(SetFVF((DWORD)shader));
		D3DCALL(SetVertexShader(nullptr));
	} else {
		D3DCALL(SetVertexShader(reinterpret_cast<IDirect3DVertexShader9*>(shader)));
	}
}

void GfxDeviceD3D9::Set_Pixel_Shader(GfxShaderHandle shader)
{
	D3DCALL(SetPixelShader(reinterpret_cast<IDirect3DPixelShader9*>(shader)));
}

GfxShaderHandle GfxDeviceD3D9::Create_Vertex_Shader(const void * bytecode, unsigned size)
{
	// D3D9 takes no length: the bytecode ends in an END token and the runtime reads to it.
	(void)size;
	IDirect3DVertexShader9 * shader = nullptr;
	if (FAILED(m_device->CreateVertexShader((const DWORD*)bytecode, &shader))) return 0;
	return (GfxShaderHandle)shader;
}

GfxShaderHandle GfxDeviceD3D9::Create_Pixel_Shader(const void * bytecode, unsigned size)
{
	(void)size;
	IDirect3DPixelShader9 * shader = nullptr;
	if (FAILED(m_device->CreatePixelShader((const DWORD*)bytecode, &shader))) return 0;
	return (GfxShaderHandle)shader;
}

void GfxDeviceD3D9::Release_Vertex_Shader(GfxShaderHandle shader)
{
	if (shader == 0) return;
	// An FVF code is not an object and there is nothing to release. Set_Vertex_Shader
	// draws the same line at the same place.
	if (shader < 0x10000) return;
	reinterpret_cast<IDirect3DVertexShader9*>(shader)->Release();
}

void GfxDeviceD3D9::Release_Pixel_Shader(GfxShaderHandle shader)
{
	if (shader == 0) return;
	reinterpret_cast<IDirect3DPixelShader9*>(shader)->Release();
}

void GfxDeviceD3D9::Set_Vertex_Shader_Constants(unsigned reg, const float * data, unsigned vec4_count)
{
	D3DCALL(SetVertexShaderConstantF(reg, data, vec4_count));
}

void GfxDeviceD3D9::Set_Pixel_Shader_Constants(unsigned reg, const float * data, unsigned vec4_count)
{
	D3DCALL(SetPixelShaderConstantF(reg, data, vec4_count));
}

void GfxDeviceD3D9::Set_Vertex_Stream(unsigned stream, GfxVertexBuffer * buffer, unsigned stride)
{
	D3DCALL(SetStreamSource(stream, (IDirect3DVertexBuffer8*)buffer, stride));
}

bool GfxDeviceD3D9::Get_Vertex_Stream(unsigned stream, GfxVertexBuffer ** buffer,
	unsigned * offset, unsigned * stride)
{
	IDirect3DVertexBuffer9 * bound = nullptr;
	UINT off = 0;
	UINT str = 0;
	if (FAILED(m_device->GetStreamSource(stream, &bound, &off, &str))) return false;
	if (buffer) *buffer = (GfxVertexBuffer*)bound;
	if (offset) *offset = off;
	if (stride) *stride = str;
	return true;
}

void GfxDeviceD3D9::Set_Index_Buffer(GfxIndexBuffer * buffer, int base_vertex_index)
{
	// The two-argument spelling is d3d9_compat.h's: D3D8 carried the base vertex index
	// on the index binding, D3D9 carries it on the draw, and the macro bridges the two
	// through g_D3D9_BaseVertexIndex. Keeping that global in step matters because the
	// direct drawers that have not been routed through here yet still read it.
	D3DCALL(SetIndices((IDirect3DIndexBuffer8*)buffer, base_vertex_index));
}

// ----------------------------------------------------------------------------
// Draws
// ----------------------------------------------------------------------------

void GfxDeviceD3D9::Draw_Indexed(unsigned primitive_type, int base_vertex_index,
	unsigned min_vertex_index, unsigned vertex_count,
	unsigned start_index, unsigned primitive_count)
{
	g_D3D9_BaseVertexIndex = base_vertex_index;
	D3DCALL(DrawIndexedPrimitive((D3DPRIMITIVETYPE)primitive_type,
		min_vertex_index, vertex_count, start_index, primitive_count));
}

void GfxDeviceD3D9::Draw(unsigned primitive_type, unsigned start_vertex, unsigned primitive_count)
{
	D3DCALL(DrawPrimitive((D3DPRIMITIVETYPE)primitive_type, start_vertex, primitive_count));
}

void GfxDeviceD3D9::Draw_Up(unsigned primitive_type, unsigned primitive_count,
	const void * vertex_data, unsigned vertex_stride)
{
	D3DCALL(DrawPrimitiveUP((D3DPRIMITIVETYPE)primitive_type, primitive_count,
		vertex_data, vertex_stride));
}

// ----------------------------------------------------------------------------
// Targets
// ----------------------------------------------------------------------------

bool GfxDeviceD3D9::Set_Render_Target(GfxSurface * color, GfxSurface * depth)
{
	if (FAILED(m_device->SetRenderTarget(0, (IDirect3DSurface8*)color))) return false;
	return SUCCEEDED(m_device->SetDepthStencilSurface((IDirect3DSurface8*)depth));
}

GfxSurface * GfxDeviceD3D9::Get_Render_Target(unsigned index)
{
	IDirect3DSurface8 * rt = nullptr;
	if (FAILED(m_device->GetRenderTarget(index, &rt))) return nullptr;
	return (GfxSurface*)rt;
}

GfxSurface * GfxDeviceD3D9::Get_Depth_Target()
{
	IDirect3DSurface8 * ds = nullptr;
	if (FAILED(m_device->GetDepthStencilSurface(&ds))) return nullptr;
	return (GfxSurface*)ds;
}

GfxSurface * GfxDeviceD3D9::Get_Back_Buffer(unsigned index)
{
	IDirect3DSurface8 * bb = nullptr;
	if (FAILED(m_device->GetBackBuffer(0, index, D3DBACKBUFFER_TYPE_MONO, &bb))) return nullptr;
	return (GfxSurface*)bb;
}

void GfxDeviceD3D9::Set_Viewport(const GfxViewport & viewport)
{
	D3DVIEWPORT9 vp;
	vp.X = viewport.X;
	vp.Y = viewport.Y;
	vp.Width = viewport.Width;
	vp.Height = viewport.Height;
	vp.MinZ = viewport.MinZ;
	vp.MaxZ = viewport.MaxZ;
#ifdef RTS_DEBUG
	// The same line the D3D11 backend prints, in the same words, so the two runs' logs
	// can be compared without translating either. Said once per distinct viewport.
	{
		static D3DVIEWPORT9 s_last = { 0xffffffff, 0xffffffff, 0, 0, -1.0f, -1.0f };
		if (memcmp(&s_last, &vp, sizeof(vp)) != 0) {
			s_last = vp;
			WWDEBUG_SAY(("D3D9 VIEWPORT: x %g y %g w %g h %g minZ %g maxZ %g",
				(double)vp.X, (double)vp.Y, (double)vp.Width, (double)vp.Height,
				(double)vp.MinZ, (double)vp.MaxZ));
		}
	}
#endif
	D3DCALL(SetViewport(&vp));
}

bool GfxDeviceD3D9::Get_Viewport(GfxViewport & viewport)
{
	D3DVIEWPORT9 vp;
	if (FAILED(m_device->GetViewport(&vp))) return false;
	viewport.X = vp.X;
	viewport.Y = vp.Y;
	viewport.Width = vp.Width;
	viewport.Height = vp.Height;
	viewport.MinZ = vp.MinZ;
	viewport.MaxZ = vp.MaxZ;
	return true;
}

// ----------------------------------------------------------------------------
// Transfers and queries
// ----------------------------------------------------------------------------

bool GfxDeviceD3D9::Copy_Surface(GfxSurface * source, const GfxRect * source_rect,
	GfxSurface * dest, const GfxRect * dest_rect)
{
	IDirect3DSurface8 * src = (IDirect3DSurface8*)source;
	IDirect3DSurface8 * dst = (IDirect3DSurface8*)dest;

	// Three ways of doing one thing, tried in order of how much the driver is allowed
	// to charge for it. UpdateSurface refuses anything but a plain same-format copy
	// between the right pools, StretchRect refuses some pool combinations of its own,
	// and D3DX will do it on the CPU if it has to.
	const RECT * srcRect = reinterpret_cast<const RECT*>(source_rect);
	const RECT * dstRect = reinterpret_cast<const RECT*>(dest_rect);

	POINT destPoint;
	const POINT * destPointPtr = nullptr;
	if (dest_rect != nullptr) {
		destPoint.x = dest_rect->left;
		destPoint.y = dest_rect->top;
		destPointPtr = &destPoint;
	}

	HRESULT hr = m_device->UpdateSurface(src, srcRect, dst, destPointPtr);
	if (FAILED(hr)) {
		hr = m_device->StretchRect(src, srcRect, dst, dstRect, D3DTEXF_NONE);
	}
	if (FAILED(hr)) {
		hr = D3DXLoadSurfaceFromSurface(dst, nullptr, dstRect, src, nullptr, srcRect,
			D3DX_FILTER_NONE, 0);
	}
	return SUCCEEDED(hr);
}

bool GfxDeviceD3D9::Copy_Surface_Rect(GfxSurface * source, const GfxRect * source_rect,
	GfxSurface * dest, const GfxRect * dest_rect, GfxCopyFilter filter)
{
	// Deliberately not the ladder in Copy_Surface above. Both callers here have already
	// decided that the result matters more than the route, and one of them is a scale
	// that must filter -- StretchRect would take it with point sampling and produce a
	// different image.
	HRESULT hr = D3DXLoadSurfaceFromSurface(
		(IDirect3DSurface8*)dest, nullptr, reinterpret_cast<const RECT*>(dest_rect),
		(IDirect3DSurface8*)source, nullptr, reinterpret_cast<const RECT*>(source_rect),
		filter == GFX_COPY_RESAMPLE ? D3DX_FILTER_TRIANGLE
			: (filter == GFX_COPY_HALVE ? D3DX_FILTER_BOX : D3DX_FILTER_NONE), 0);
	DX8_ErrorCode(hr);
	return SUCCEEDED(hr);
}

bool GfxDeviceD3D9::Update_Texture(GfxTexture * source, GfxTexture * dest)
{
	HRESULT hr = m_device->UpdateTexture((IDirect3DBaseTexture8*)source,
		(IDirect3DBaseTexture8*)dest);
	DX8_ErrorCode(hr);
	return SUCCEEDED(hr);
}

bool GfxDeviceD3D9::Generate_Mips(GfxTexture * texture, unsigned base_level)
{
	if (texture == nullptr) return false;
	// D3DX_DEFAULT is D3DX_FILTER_BOX here, which is what the one caller that passed it
	// meant and what the other five asked for outright.
	HRESULT hr = D3DXFilterTexture((IDirect3DBaseTexture8*)texture, nullptr, base_level,
		D3DX_FILTER_BOX);
	DX8_ErrorCode(hr);
	return SUCCEEDED(hr);
}

void GfxDeviceD3D9::Set_Texture_Detail_Level(GfxTexture * texture, unsigned skip_levels)
{
	if (texture != nullptr) ((IDirect3DBaseTexture8*)texture)->SetLOD(skip_levels);
}

bool GfxDeviceD3D9::Capture_Front_Buffer(GfxSurface * dest)
{
	HRESULT hr = m_device->GetFrontBufferData(0, (IDirect3DSurface8*)dest);
	DX8_ErrorCode(hr);
	return SUCCEEDED(hr);
}

// ----------------------------------------------------------------------------
// Buffers
// ----------------------------------------------------------------------------

// D3DUSAGE_WRITEONLY is not in the neutral vocabulary because every buffer the engine
// makes is write-only -- nothing reads one back -- so it is a property of this backend
// rather than a choice a caller makes. Software vertex processing is likewise forced on
// hardware without transform and lighting whether or not the caller asked for it.
static unsigned Usage_To_D3D(unsigned usage)
{
	unsigned flags = D3DUSAGE_WRITEONLY;
	if (usage & GFX_USAGE_DYNAMIC) flags |= D3DUSAGE_DYNAMIC;
	if (usage & GFX_USAGE_NPATCHES) flags |= D3DUSAGE_NPATCHES;
	if (usage & GFX_USAGE_SOFTWARE_PROCESSING) flags |= D3DUSAGE_SOFTWAREPROCESSING;
	if (usage & GFX_USAGE_POINT_SPRITES) flags |= D3DUSAGE_POINTS;
	if (!DX8Wrapper::Get_Current_Caps()->Support_TnL()) flags |= D3DUSAGE_SOFTWAREPROCESSING;
	return flags;
}

static D3DPOOL Usage_To_D3D_Pool(unsigned usage)
{
	return (usage & GFX_USAGE_DYNAMIC) ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED;
}

static DWORD Map_Mode_To_D3D(GfxMapMode mode)
{
	switch (mode) {
	case GFX_MAP_WRITE_DISCARD:			return D3DLOCK_DISCARD;
	case GFX_MAP_WRITE_NO_OVERWRITE:	return D3DLOCK_NOOVERWRITE;
	case GFX_MAP_READ:					return D3DLOCK_READONLY;
	default:							return 0;
	}
}

#ifdef RTS_DEBUG
// D3D9 lets a discard or no-overwrite map through against a buffer that was not created
// dynamic; it ignores the flag. D3D11's Map does not -- it fails the call. So the
// mismatch is invisible here and fatal there, which makes it worth counting while the
// only backend is still the forgiving one.
static unsigned s_nondynamic_vb_discards = 0;
static unsigned s_nondynamic_ib_discards = 0;
// The positive control. A zero above is only a reading if these are not zero: without
// them a discard audit that never runs and a discard audit that finds nothing print the
// same line.
static unsigned s_vb_discards = 0;
static unsigned s_ib_discards = 0;

static bool Is_Discarding(GfxMapMode mode)
{
	return mode == GFX_MAP_WRITE_DISCARD || mode == GFX_MAP_WRITE_NO_OVERWRITE;
}
#endif

void GfxDeviceD3D9::Report_Nondynamic_Discards()
{
#ifdef RTS_DEBUG
	// The same 600-frame window every other census here uses, so the figures can be read
	// beside them.
	static unsigned frames = 0;
	if (++frames < 600) return;
	frames = 0;
	WWDEBUG_SAY(("DISCARD AUDIT over 600 frames: %u of %u vertex-buffer and %u of %u "
		"index-buffer discard/append maps were against a buffer that was not created "
		"dynamic. D3D9 ignores the flag and the map succeeds; D3D11's Map would fail the "
		"call. The totals are the control: a zero beside a zero total is the audit not "
		"running, not the engine being right.",
		s_nondynamic_vb_discards, s_vb_discards, s_nondynamic_ib_discards, s_ib_discards));
	if (Absorb_Drop_Mask() != 0) {
		WWDEBUG_SAY(("D3D9 DROP ABSORBED over 600 frames: %u render-state, %u texture-stage "
			"and %u transform writes were dropped before they reached the device. This is "
			"the control on the experiment: a frame that is unchanged with these numbers at "
			"zero says nothing at all.",
			s_dropped_rs, s_dropped_tss, s_dropped_xf));
		s_dropped_rs = 0;
		s_dropped_tss = 0;
		s_dropped_xf = 0;
	}
	s_nondynamic_vb_discards = 0;
	s_nondynamic_ib_discards = 0;
	s_vb_discards = 0;
	s_ib_discards = 0;
#endif
}

GfxVertexBuffer * GfxDeviceD3D9::Create_Vertex_Buffer(unsigned size_in_bytes, unsigned fvf,
	unsigned usage)
{
	IDirect3DVertexBuffer8 * buffer = nullptr;
	if (FAILED(m_device->CreateVertexBuffer(size_in_bytes, Usage_To_D3D(usage), fvf,
			Usage_To_D3D_Pool(usage), &buffer))) {
		return nullptr;
	}
	return (GfxVertexBuffer*)buffer;
}

GfxIndexBuffer * GfxDeviceD3D9::Create_Index_Buffer(unsigned index_count, unsigned usage)
{
	IDirect3DIndexBuffer8 * buffer = nullptr;
	// Sixteen-bit indices, which is the only width the engine has ever asked for: every
	// index it hands to a draw is an unsigned short.
	if (FAILED(m_device->CreateIndexBuffer(sizeof(unsigned short) * index_count,
			Usage_To_D3D(usage), D3DFMT_INDEX16, Usage_To_D3D_Pool(usage), &buffer))) {
		return nullptr;
	}
	return (GfxIndexBuffer*)buffer;
}

void GfxDeviceD3D9::Release_Vertex_Buffer(GfxVertexBuffer * buffer)
{
	if (buffer != nullptr) ((IDirect3DVertexBuffer8*)buffer)->Release();
}

void GfxDeviceD3D9::Release_Index_Buffer(GfxIndexBuffer * buffer)
{
	if (buffer != nullptr) ((IDirect3DIndexBuffer8*)buffer)->Release();
}

bool GfxDeviceD3D9::Map_Vertex_Buffer(GfxVertexBuffer * buffer, unsigned offset_in_bytes,
	unsigned size_in_bytes, GfxMapMode mode, void ** data)
{
	if (buffer == nullptr || data == nullptr) return false;
	IDirect3DVertexBuffer8 * vb = (IDirect3DVertexBuffer8*)buffer;
#ifdef RTS_DEBUG
	if (Is_Discarding(mode)) {
		s_vb_discards++;
		D3DVERTEXBUFFER_DESC vbd;
		if (SUCCEEDED(vb->GetDesc(&vbd)) && (vbd.Usage & D3DUSAGE_DYNAMIC) == 0) {
			s_nondynamic_vb_discards++;
		}
	}
#endif
	HRESULT hr = vb->Lock(offset_in_bytes, size_in_bytes, DX8_LOCK_CAST(data),
		Map_Mode_To_D3D(mode));
	DX8_ErrorCode(hr);
	return SUCCEEDED(hr);
}

void GfxDeviceD3D9::Unmap_Vertex_Buffer(GfxVertexBuffer * buffer)
{
	if (buffer != nullptr) DX8_ErrorCode(((IDirect3DVertexBuffer8*)buffer)->Unlock());
}

bool GfxDeviceD3D9::Map_Index_Buffer(GfxIndexBuffer * buffer, unsigned offset_in_bytes,
	unsigned size_in_bytes, GfxMapMode mode, void ** data)
{
	if (buffer == nullptr || data == nullptr) return false;
	IDirect3DIndexBuffer8 * ib = (IDirect3DIndexBuffer8*)buffer;
#ifdef RTS_DEBUG
	if (Is_Discarding(mode)) {
		s_ib_discards++;
		D3DINDEXBUFFER_DESC ibd;
		if (SUCCEEDED(ib->GetDesc(&ibd)) && (ibd.Usage & D3DUSAGE_DYNAMIC) == 0) {
			s_nondynamic_ib_discards++;
		}
	}
#endif
	HRESULT hr = ib->Lock(offset_in_bytes, size_in_bytes, DX8_LOCK_CAST(data),
		Map_Mode_To_D3D(mode));
	DX8_ErrorCode(hr);
	return SUCCEEDED(hr);
}

void GfxDeviceD3D9::Unmap_Index_Buffer(GfxIndexBuffer * buffer)
{
	if (buffer != nullptr) DX8_ErrorCode(((IDirect3DIndexBuffer8*)buffer)->Unlock());
}

// ----------------------------------------------------------------------------
// Textures and surfaces
// ----------------------------------------------------------------------------

// What a texture's usage bits become. Separate from the buffer version above and not
// a call to it, because the two share only their names: D3DUSAGE_WRITEONLY is illegal
// on a texture, and a render target must be created in the default pool whatever else
// it was asked for.
static unsigned Texture_Usage_To_D3D(unsigned usage)
{
	unsigned flags = 0;
	if (usage & GFX_USAGE_RENDER_TARGET)   flags |= D3DUSAGE_RENDERTARGET;
	if (usage & GFX_USAGE_DYNAMIC_TEXTURE) flags |= D3DUSAGE_DYNAMIC;
	return flags;
}

static D3DPOOL Texture_Usage_To_D3D_Pool(unsigned usage)
{
	// The placement bits are asked first and answered exactly, because the engine's
	// texture path distinguishes all three pools and the pairing that matters --
	// UpdateTexture from system memory into device memory -- fails if either end lands
	// somewhere else. Only when the caller has said nothing about placement do the
	// usage bits decide, and then a render target or a dynamic texture has to be in
	// the default pool because D3D9 has nowhere else to put one.
	if (usage & GFX_USAGE_STAGING) return D3DPOOL_SYSTEMMEM;
	if (usage & GFX_USAGE_GPU_RESIDENT) return D3DPOOL_DEFAULT;
	if (usage & (GFX_USAGE_RENDER_TARGET | GFX_USAGE_DYNAMIC_TEXTURE)) return D3DPOOL_DEFAULT;
	// Managed, which is what the engine has always asked for and what has no D3D11
	// equivalent -- see the note at GfxResourceUsage.
	return D3DPOOL_MANAGED;
}

GfxTexture * GfxDeviceD3D9::Create_Texture(unsigned width, unsigned height, unsigned levels,
	WW3DFormat format, unsigned usage)
{
	IDirect3DTexture8 * texture = nullptr;
	HRESULT hr = m_device->CreateTexture(width, height, levels, Texture_Usage_To_D3D(usage),
		WW3DFormat_To_D3DFormat(format), Texture_Usage_To_D3D_Pool(usage), &texture);
	if (FAILED(hr)) return nullptr;
	return (GfxTexture*)texture;
}

GfxTexture * GfxDeviceD3D9::Create_Cube_Texture(unsigned edge_length, unsigned levels,
	WW3DFormat format, unsigned usage)
{
	IDirect3DCubeTexture8 * texture = nullptr;
	HRESULT hr = m_device->CreateCubeTexture(edge_length, levels, Texture_Usage_To_D3D(usage),
		WW3DFormat_To_D3DFormat(format), Texture_Usage_To_D3D_Pool(usage), &texture);
	if (FAILED(hr)) return nullptr;
	return (GfxTexture*)texture;
}

GfxTexture * GfxDeviceD3D9::Create_Volume_Texture(unsigned width, unsigned height,
	unsigned depth, unsigned levels, WW3DFormat format, unsigned usage)
{
	IDirect3DVolumeTexture8 * texture = nullptr;
	HRESULT hr = m_device->CreateVolumeTexture(width, height, depth, levels,
		Texture_Usage_To_D3D(usage), WW3DFormat_To_D3DFormat(format),
		Texture_Usage_To_D3D_Pool(usage), &texture);
	if (FAILED(hr)) return nullptr;
	return (GfxTexture*)texture;
}

GfxTexture * GfxDeviceD3D9::Create_Depth_Texture(unsigned width, unsigned height,
	unsigned levels, WW3DZFormat format, unsigned usage)
{
	IDirect3DTexture8 * texture = nullptr;
	// D3DUSAGE_DEPTHSTENCIL rather than the usage bits' own flags: a depth texture is
	// written by the depth test and not by a draw, and D3D9 wants to be told which.
	HRESULT hr = m_device->CreateTexture(width, height, levels, D3DUSAGE_DEPTHSTENCIL,
		WW3DZFormat_To_D3DFormat(format), Texture_Usage_To_D3D_Pool(usage), &texture);
	if (FAILED(hr)) return nullptr;
	return (GfxTexture*)texture;
}

void GfxDeviceD3D9::Release_Texture(GfxTexture * texture)
{
	if (texture != nullptr) ((IDirect3DBaseTexture8*)texture)->Release();
}

void GfxDeviceD3D9::Reference_Texture(GfxTexture * texture)
{
	if (texture != nullptr) ((IDirect3DBaseTexture8*)texture)->AddRef();
}

GfxSurface * GfxDeviceD3D9::Create_Render_Target_Surface(unsigned width, unsigned height,
	WW3DFormat format, WW3DMultiSampleType multisample)
{
	IDirect3DSurface8 * surface = nullptr;
	// The last argument is D3D9's "lockable"; a render target the CPU can read is a
	// different and much slower thing, and nothing here asks for one.
	HRESULT hr = m_device->CreateRenderTarget(width, height, WW3DFormat_To_D3DFormat(format),
		WW3DMultiSample_To_D3DMultiSample(multisample), FALSE, &surface);
	if (FAILED(hr)) return nullptr;
	return (GfxSurface*)surface;
}

GfxSurface * GfxDeviceD3D9::Create_Depth_Stencil_Surface(unsigned width, unsigned height,
	WW3DZFormat format, WW3DMultiSampleType multisample)
{
	IDirect3DSurface8 * surface = nullptr;
	HRESULT hr = m_device->CreateDepthStencilSurface(width, height,
		WW3DZFormat_To_D3DFormat(format), WW3DMultiSample_To_D3DMultiSample(multisample),
		&surface);
	if (FAILED(hr)) return nullptr;
	return (GfxSurface*)surface;
}

// The two-pool ladder D3D9 needs: system memory first, because a surface there can be
// the source of an UpdateSurface, and scratch only if the driver refuses the format
// there. Scratch accepts any format but can be the source of nothing.
//
// This was public API on DX8Wrapper taking an IDirect3DDevice9 and handing back an
// IDirect3DSurface9. It has always belonged here.
static HRESULT Create_Image_Surface(IDirect3DDevice8 * device, unsigned width,
	unsigned height, D3DFORMAT format, IDirect3DSurface8 ** surface)
{
	HRESULT hr = device->CreateOffscreenPlainSurface(width, height, format,
		D3DPOOL_SYSTEMMEM, surface, nullptr);
	if (FAILED(hr)) {
		hr = device->CreateOffscreenPlainSurface(width, height, format,
			D3DPOOL_SCRATCH, surface, nullptr);
	}
	return hr;
}

GfxSurface * GfxDeviceD3D9::Create_Offscreen_Surface(unsigned width, unsigned height,
	WW3DFormat format)
{
	IDirect3DSurface8 * surface = nullptr;
	HRESULT hr = Create_Image_Surface(m_device, width, height,
		WW3DFormat_To_D3DFormat(format), &surface);
	if (FAILED(hr)) return nullptr;
	return (GfxSurface*)surface;
}

void GfxDeviceD3D9::Release_Surface(GfxSurface * surface)
{
	if (surface != nullptr) ((IDirect3DSurface8*)surface)->Release();
}

void GfxDeviceD3D9::Reference_Surface(GfxSurface * surface)
{
	if (surface != nullptr) ((IDirect3DSurface8*)surface)->AddRef();
}

unsigned GfxDeviceD3D9::Get_Texture_Level_Count(GfxTexture * texture)
{
	if (texture == nullptr) return 0;
	return ((IDirect3DBaseTexture8*)texture)->GetLevelCount();
}

GfxSurface * GfxDeviceD3D9::Get_Texture_Surface_Level(GfxTexture * texture, unsigned level)
{
	if (texture == nullptr) return nullptr;
	// Same reasoning as Describe_Texture_Level: the handle does not say which kind of
	// texture it is, and GetSurfaceLevel exists only on the 2-D one.
	IDirect3DBaseTexture8 * base = (IDirect3DBaseTexture8*)texture;
	if (base->GetType() != D3DRTYPE_TEXTURE) return nullptr;
	IDirect3DSurface8 * surface = nullptr;
	if (FAILED(((IDirect3DTexture8*)base)->GetSurfaceLevel(level, &surface))) return nullptr;
	return (GfxSurface*)surface;
}

bool GfxDeviceD3D9::Map_Texture(GfxTexture * texture, unsigned level, const GfxRect * rect,
	GfxMapMode mode, GfxMappedRect & mapped)
{
	mapped.Data = nullptr;
	mapped.Pitch = 0;
	if (texture == nullptr) return false;
	IDirect3DBaseTexture8 * base = (IDirect3DBaseTexture8*)texture;
	if (base->GetType() != D3DRTYPE_TEXTURE) return false;
	D3DLOCKED_RECT lr;
	HRESULT hr = ((IDirect3DTexture8*)base)->LockRect(level, &lr,
		reinterpret_cast<const RECT*>(rect), Map_Mode_To_D3D(mode));
	DX8_ErrorCode(hr);
	if (FAILED(hr)) return false;
	mapped.Data = lr.pBits;
	mapped.Pitch = lr.Pitch;
	return true;
}

void GfxDeviceD3D9::Unmap_Texture(GfxTexture * texture, unsigned level)
{
	if (texture == nullptr) return;
	IDirect3DBaseTexture8 * base = (IDirect3DBaseTexture8*)texture;
	if (base->GetType() != D3DRTYPE_TEXTURE) return;
	DX8_ErrorCode(((IDirect3DTexture8*)base)->UnlockRect(level));
}

bool GfxDeviceD3D9::Map_Surface(GfxSurface * surface, const GfxRect * rect,
	GfxMapMode mode, GfxMappedRect & mapped)
{
	mapped.Data = nullptr;
	mapped.Pitch = 0;
	if (surface == nullptr) return false;
	D3DLOCKED_RECT lr;
	HRESULT hr = ((IDirect3DSurface8*)surface)->LockRect(&lr,
		reinterpret_cast<const RECT*>(rect), Map_Mode_To_D3D(mode));
	DX8_ErrorCode(hr);
	if (FAILED(hr)) return false;
	mapped.Data = lr.pBits;
	mapped.Pitch = lr.Pitch;
	return true;
}

void GfxDeviceD3D9::Unmap_Surface(GfxSurface * surface)
{
	if (surface != nullptr) DX8_ErrorCode(((IDirect3DSurface8*)surface)->UnlockRect());
}

bool GfxDeviceD3D9::Map_Volume_Texture(GfxTexture * texture, unsigned level,
	GfxMapMode mode, GfxMappedBox & mapped)
{
	mapped.Data = nullptr;
	mapped.RowPitch = 0;
	mapped.SlicePitch = 0;
	if (texture == nullptr) return false;
	IDirect3DBaseTexture8 * base = (IDirect3DBaseTexture8*)texture;
	if (base->GetType() != D3DRTYPE_VOLUMETEXTURE) return false;
	D3DLOCKED_BOX lb;
	HRESULT hr = ((IDirect3DVolumeTexture8*)base)->LockBox(level, &lb, nullptr,
		Map_Mode_To_D3D(mode));
	DX8_ErrorCode(hr);
	if (FAILED(hr)) return false;
	mapped.Data = lb.pBits;
	mapped.RowPitch = lb.RowPitch;
	mapped.SlicePitch = lb.SlicePitch;
	return true;
}

void GfxDeviceD3D9::Unmap_Volume_Texture(GfxTexture * texture, unsigned level)
{
	if (texture == nullptr) return;
	IDirect3DBaseTexture8 * base = (IDirect3DBaseTexture8*)texture;
	if (base->GetType() != D3DRTYPE_VOLUMETEXTURE) return;
	DX8_ErrorCode(((IDirect3DVolumeTexture8*)base)->UnlockBox(level));
}

bool GfxDeviceD3D9::Map_Cube_Texture(GfxTexture * texture, unsigned face, unsigned level,
	const GfxRect * rect, GfxMapMode mode, GfxMappedRect & mapped)
{
	mapped.Data = nullptr;
	mapped.Pitch = 0;
	if (texture == nullptr) return false;
	IDirect3DBaseTexture8 * base = (IDirect3DBaseTexture8*)texture;
	if (base->GetType() != D3DRTYPE_CUBETEXTURE) return false;
	D3DLOCKED_RECT lr;
	HRESULT hr = ((IDirect3DCubeTexture8*)base)->LockRect((D3DCUBEMAP_FACES)face, level, &lr,
		reinterpret_cast<const RECT*>(rect), Map_Mode_To_D3D(mode));
	DX8_ErrorCode(hr);
	if (FAILED(hr)) return false;
	mapped.Data = lr.pBits;
	mapped.Pitch = lr.Pitch;
	return true;
}

void GfxDeviceD3D9::Unmap_Cube_Texture(GfxTexture * texture, unsigned face, unsigned level)
{
	if (texture == nullptr) return;
	IDirect3DBaseTexture8 * base = (IDirect3DBaseTexture8*)texture;
	if (base->GetType() != D3DRTYPE_CUBETEXTURE) return;
	DX8_ErrorCode(((IDirect3DCubeTexture8*)base)->UnlockRect((D3DCUBEMAP_FACES)face, level));
}

bool GfxDeviceD3D9::Describe_Depth_Texture_Level(GfxTexture * texture, unsigned level,
	WW3DZFormat & format)
{
	format = WW3D_ZFORMAT_UNKNOWN;
	if (texture == nullptr) return false;
	IDirect3DBaseTexture8 * base = (IDirect3DBaseTexture8*)texture;
	if (base->GetType() != D3DRTYPE_TEXTURE) return false;
	D3DSURFACE_DESC sd;
	if (FAILED(((IDirect3DTexture8*)base)->GetLevelDesc(level, &sd))) return false;
	format = D3DFormat_To_WW3DZFormat(sd.Format);
	return true;
}

bool GfxDeviceD3D9::Describe_Surface(GfxSurface * surface, WW3DSurfaceDescription & desc)
{
	if (surface == nullptr) return false;
	D3DSURFACE_DESC sd;
	if (FAILED(((IDirect3DSurface8*)surface)->GetDesc(&sd))) return false;
	desc.Width = sd.Width;
	desc.Height = sd.Height;
	desc.Format = D3DFormat_To_WW3DFormat(sd.Format);
	desc.MultiSample = D3DMultiSample_To_WW3DMultiSample(sd.MultiSampleType);
	return true;
}

bool GfxDeviceD3D9::Describe_Texture_Level(GfxTexture * texture, unsigned level,
	WW3DSurfaceDescription & desc)
{
	if (texture == nullptr) return false;
	// GetLevelDesc lives on the 2-D texture interface, and the handle this side of the
	// seam does not say which kind of texture it is -- so ask before casting rather
	// than calling a method the object may not have.
	IDirect3DBaseTexture8 * base = (IDirect3DBaseTexture8*)texture;
	D3DSURFACE_DESC sd;
	switch (base->GetType()) {
		case D3DRTYPE_TEXTURE:
			if (FAILED(((IDirect3DTexture8*)base)->GetLevelDesc(level, &sd))) return false;
			break;
		case D3DRTYPE_CUBETEXTURE:
			// Every face of a cube level has the same description, so face 0 answers for
			// the level -- which is what the one caller wants to know.
			if (FAILED(((IDirect3DCubeTexture8*)base)->GetLevelDesc(level, &sd))) return false;
			break;
		default:
			return false;
	}
	desc.Width = sd.Width;
	desc.Height = sd.Height;
	desc.Format = D3DFormat_To_WW3DFormat(sd.Format);
	desc.MultiSample = D3DMultiSample_To_WW3DMultiSample(sd.MultiSampleType);
	return true;
}

bool GfxDeviceD3D9::Describe_Volume_Level(GfxTexture * texture, unsigned level,
	WW3DSurfaceDescription & desc, unsigned & depth)
{
	depth = 0;
	if (texture == nullptr) return false;
	IDirect3DBaseTexture8 * base = (IDirect3DBaseTexture8*)texture;
	if (base->GetType() != D3DRTYPE_VOLUMETEXTURE) return false;
	D3DVOLUME_DESC vd;
	if (FAILED(((IDirect3DVolumeTexture8*)base)->GetLevelDesc(level, &vd))) return false;
	desc.Width = vd.Width;
	desc.Height = vd.Height;
	desc.Format = D3DFormat_To_WW3DFormat(vd.Format);
	// A volume has no multisampling; saying so is better than leaving the field unset.
	desc.MultiSample = WW3D_MULTISAMPLE_NONE;
	depth = vd.Depth;
	return true;
}

bool GfxDeviceD3D9::Get_Display_Mode(unsigned & width, unsigned & height, WW3DFormat & format)
{
	D3DDISPLAYMODE mode;
	HRESULT hr = m_device->GetDisplayMode(0, &mode);
	DX8_ErrorCode(hr);
	if (FAILED(hr)) return false;
	width = mode.Width;
	height = mode.Height;
	format = D3DFormat_To_WW3DFormat(mode.Format);
	return true;
}

unsigned GfxDeviceD3D9::Get_Available_Texture_Memory()
{
	return m_device->GetAvailableTextureMem();
}

void GfxDeviceD3D9::Trim_Resource_Memory()
{
	D3DCALL(EvictManagedResources());
}

void GfxDeviceD3D9::Set_Gamma_Ramp(const void * ramp, bool calibrate)
{
	m_device->SetGammaRamp(0, calibrate ? D3DSGR_CALIBRATE : D3DSGR_NO_CALIBRATION,
		(const D3DGAMMARAMP*)ramp);
}

bool GfxDeviceD3D9::Set_Hardware_Cursor(GfxSurface * image, unsigned hot_x, unsigned hot_y)
{
	HRESULT hr = m_device->SetCursorProperties(hot_x, hot_y, (IDirect3DSurface8*)image);
	return SUCCEEDED(hr);
}

void GfxDeviceD3D9::Show_Hardware_Cursor(bool show)
{
	m_device->ShowCursor(show ? TRUE : FALSE);
}

void GfxDeviceD3D9::Set_Hardware_Cursor_Position(unsigned x, unsigned y)
{
	// D3DCURSOR_IMMEDIATE_UPDATE: move it now rather than at the next present, which is
	// what a cursor following the mouse has to do.
	m_device->SetCursorPosition(x, y, D3DCURSOR_IMMEDIATE_UPDATE);
}

bool GfxDeviceD3D9::Save_Surface_To_File(const char * path, GfxSurface * surface)
{
	if (surface == nullptr) return false;
	HRESULT hr = D3DXSaveSurfaceToFileA(path, D3DXIFF_PNG, (IDirect3DSurface8*)surface,
		nullptr, nullptr);
	return SUCCEEDED(hr);
}

// ---------------------------------------------------------------------------
// The adapter.
//
// Everything here runs before a device exists -- enumerating what the machine has, asking
// each adapter what it supports, and finally creating one. See gfxdevice.h for why it is a
// separate object from the device rather than part of it.
// ---------------------------------------------------------------------------

// Set from the command line (-preserveFPU). A D3D9 wart: the driver is otherwise allowed to
// leave the FPU control word changed, which the simulation notices.
extern int DX8Wrapper_PreserveFPU;

// The device type every call here asks about. There has only ever been one.
#define GFX_D3D9_DEVTYPE D3DDEVTYPE_HAL

typedef IDirect3D9* (WINAPI *Direct3DCreate9Type)(UINT SDKVersion);

GfxAdapterD3D9::GfxAdapterD3D9()
	: m_library(nullptr), m_d3d(nullptr)
{
	m_library = LoadLibrary("d3d9.dll");
	if (m_library == nullptr)
		return;

	Direct3DCreate9Type create = (Direct3DCreate9Type)GetProcAddress(m_library, "Direct3DCreate9");
	if (create == nullptr)
		return;

	// TheSuperHackers @bugfix xezon 13/06/2025 Front load the system dbghelp.dll to prevent
	// the graphics driver from potentially loading the old game dbghelp.dll and then crashing
	// the game process.
	DbgHelpGuard dbgHelpGuard;
	m_d3d = create(D3D_SDK_VERSION);
}

GfxAdapterD3D9::~GfxAdapterD3D9()
{
	if (m_d3d != nullptr) {
		m_d3d->Release();
		m_d3d = nullptr;
	}
	if (m_library != nullptr) {
		FreeLibrary(m_library);
		m_library = nullptr;
	}
}

unsigned GfxAdapterD3D9::Get_Adapter_Count()
{
	if (m_d3d == nullptr) return 0;
	return (unsigned)m_d3d->GetAdapterCount();
}

bool GfxAdapterD3D9::Get_Adapter_Info(unsigned adapter, GfxAdapterInfo & info)
{
	memset(&info, 0, sizeof(info));
	if (m_d3d == nullptr) return false;

	D3DADAPTER_IDENTIFIER8 id;
	::ZeroMemory(&id, sizeof(id));
	if (FAILED(m_d3d->GetAdapterIdentifier(adapter, D3DENUM_NO_WHQL_LEVEL, &id)))
		return false;

	strncpy(info.Description, id.Description, sizeof(info.Description) - 1);
	strncpy(info.Driver, id.Driver, sizeof(info.Driver) - 1);
	info.DriverProduct       = HIWORD(id.DriverVersion.HighPart);
	info.DriverVersionNumber = LOWORD(id.DriverVersion.HighPart);
	info.DriverSubVersion    = HIWORD(id.DriverVersion.LowPart);
	info.DriverBuildVersion  = LOWORD(id.DriverVersion.LowPart);
	sprintf(info.DriverVersion, "%d.%d.%d.%d",
		info.DriverProduct, info.DriverVersionNumber,
		info.DriverSubVersion, info.DriverBuildVersion);
	sprintf(info.DeviceIdentifier, "%08X-%04X-%04X-%02X%02X%02X%02X%02X%02X%02X%02X",
		id.DeviceIdentifier.Data1, id.DeviceIdentifier.Data2, id.DeviceIdentifier.Data3,
		id.DeviceIdentifier.Data4[0], id.DeviceIdentifier.Data4[1],
		id.DeviceIdentifier.Data4[2], id.DeviceIdentifier.Data4[3],
		id.DeviceIdentifier.Data4[4], id.DeviceIdentifier.Data4[5],
		id.DeviceIdentifier.Data4[6], id.DeviceIdentifier.Data4[7]);
	info.VendorId = id.VendorId;
	info.DeviceId = id.DeviceId;
	info.SubSystemId = id.SubSysId;
	info.Revision = id.Revision;
	return true;
}

bool GfxAdapterD3D9::Get_Current_Display_Mode(unsigned adapter, GfxDisplayMode & mode)
{
	memset(&mode, 0, sizeof(mode));
	mode.Format = WW3D_FORMAT_UNKNOWN;
	if (m_d3d == nullptr) return false;

	D3DDISPLAYMODE d3dmode;
	::ZeroMemory(&d3dmode, sizeof(d3dmode));
	if (FAILED(m_d3d->GetAdapterDisplayMode(adapter, &d3dmode)))
		return false;

	mode.Width = d3dmode.Width;
	mode.Height = d3dmode.Height;
	mode.RefreshRate = d3dmode.RefreshRate;
	mode.Format = D3DFormat_To_WW3DFormat(d3dmode.Format);
	return true;
}

unsigned GfxAdapterD3D9::Get_Display_Mode_Count(unsigned adapter, WW3DFormat format)
{
	if (m_d3d == nullptr) return 0;
	return (unsigned)m_d3d->GetAdapterModeCount(adapter, WW3DFormat_To_D3DFormat(format));
}

bool GfxAdapterD3D9::Get_Display_Mode(unsigned adapter, WW3DFormat format,
	unsigned index, GfxDisplayMode & mode)
{
	memset(&mode, 0, sizeof(mode));
	mode.Format = WW3D_FORMAT_UNKNOWN;
	if (m_d3d == nullptr) return false;

	D3DDISPLAYMODE d3dmode;
	::ZeroMemory(&d3dmode, sizeof(d3dmode));
	if (FAILED(m_d3d->EnumAdapterModes(adapter, WW3DFormat_To_D3DFormat(format), index, &d3dmode)))
		return false;

	mode.Width = d3dmode.Width;
	mode.Height = d3dmode.Height;
	mode.RefreshRate = d3dmode.RefreshRate;
	mode.Format = D3DFormat_To_WW3DFormat(d3dmode.Format);
	return true;
}

bool GfxAdapterD3D9::Supports_Display_Format(unsigned adapter, WW3DFormat display,
	WW3DFormat back_buffer, bool windowed)
{
	if (m_d3d == nullptr) return false;
	return m_d3d->CheckDeviceType(adapter, GFX_D3D9_DEVTYPE,
		WW3DFormat_To_D3DFormat(display), WW3DFormat_To_D3DFormat(back_buffer),
		windowed ? TRUE : FALSE) == D3D_OK;
}

bool GfxAdapterD3D9::Supports_Depth_Stencil_Format(unsigned adapter, WW3DFormat display,
	WW3DFormat back_buffer, WW3DZFormat depth)
{
	if (m_d3d == nullptr) return false;

	const D3DFORMAT d3d_display = WW3DFormat_To_D3DFormat(display);
	const D3DFORMAT d3d_depth   = WW3DZFormat_To_D3DFormat(depth);

	// Two separate questions and both have to be yes: can the adapter make a depth buffer
	// in this format at all, and can that depth buffer be used with this back buffer.
	if (FAILED(m_d3d->CheckDeviceFormat(adapter, GFX_D3D9_DEVTYPE, d3d_display,
			D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, d3d_depth)))
		return false;

	if (FAILED(m_d3d->CheckDepthStencilMatch(adapter, GFX_D3D9_DEVTYPE, d3d_display,
			WW3DFormat_To_D3DFormat(back_buffer), d3d_depth)))
		return false;

	return true;
}

bool GfxAdapterD3D9::Supports_Multisample(unsigned adapter, WW3DFormat format,
	bool windowed, WW3DMultiSampleType samples)
{
	if (m_d3d == nullptr) return false;
	return SUCCEEDED(m_d3d->CheckDeviceMultiSampleType(adapter, GFX_D3D9_DEVTYPE,
		WW3DFormat_To_D3DFormat(format), windowed ? TRUE : FALSE,
		WW3DMultiSample_To_D3DMultiSample(samples), nullptr));
}

bool GfxAdapterD3D9::Supports_Depth_Multisample(unsigned adapter, WW3DZFormat format,
	bool windowed, WW3DMultiSampleType samples)
{
	if (m_d3d == nullptr) return false;
	return SUCCEEDED(m_d3d->CheckDeviceMultiSampleType(adapter, GFX_D3D9_DEVTYPE,
		WW3DZFormat_To_D3DFormat(format), windowed ? TRUE : FALSE,
		WW3DMultiSample_To_D3DMultiSample(samples), nullptr));
}

bool GfxAdapterD3D9::Supports_Hardware_Transform_And_Lighting(unsigned adapter)
{
	if (m_d3d == nullptr) return false;
	D3DCAPS8 caps;
	::ZeroMemory(&caps, sizeof(caps));
	if (FAILED(m_d3d->GetDeviceCaps(adapter, GFX_D3D9_DEVTYPE, &caps)))
		return false;
	return (caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) != 0;
}

// Translate the API's capability struct into the sixteen things the engine asks about.
static void Fill_Device_Caps(const D3DCAPS8 & d3d, GfxDeviceCaps & caps)
{
	memset(&caps, 0, sizeof(caps));
	caps.AdapterOrdinal = d3d.AdapterOrdinal;

	caps.HardwareTransformAndLighting = (d3d.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) != 0;
	caps.NPatches            = (d3d.DevCaps & D3DDEVCAPS_NPATCHES) != 0;
	caps.FullScreenGamma     = (d3d.Caps2 & D3DCAPS2_FULLSCREENGAMMA) != 0;
	caps.CubeMaps            = (d3d.TextureCaps & D3DPTEXTURECAPS_CUBEMAP) != 0;
	caps.ColorWriteEnable    = (d3d.PrimitiveMiscCaps & D3DPMISCCAPS_COLORWRITEENABLE) != 0;
	caps.BumpEnvmap          = (d3d.TextureOpCaps & D3DTEXOPCAPS_BUMPENVMAP) != 0;
	caps.BumpEnvmapLuminance = (d3d.TextureOpCaps & D3DTEXOPCAPS_BUMPENVMAPLUMINANCE) != 0;
	caps.ModulateAlphaAddColor = (d3d.TextureOpCaps & D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR) != 0;
	caps.DotProduct3         = (d3d.TextureOpCaps & D3DTEXOPCAPS_DOTPRODUCT3) != 0;
	caps.PointSprites        = d3d.MaxPointSize > 1.0f;

	caps.LinearFilter        = (d3d.TextureFilterCaps & D3DPTFILTERCAPS_MINFLINEAR) != 0 &&
	                           (d3d.TextureFilterCaps & D3DPTFILTERCAPS_MAGFLINEAR) != 0;
	caps.MipLinearFilter     = (d3d.TextureFilterCaps & D3DPTFILTERCAPS_MIPFLINEAR) != 0;
	caps.AnisotropicFilter   = (d3d.TextureFilterCaps & D3DPTFILTERCAPS_MAGFANISOTROPIC) != 0 &&
	                           (d3d.TextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC) != 0;

	caps.MaxTextureWidth        = d3d.MaxTextureWidth;
	caps.MaxTextureHeight       = d3d.MaxTextureHeight;
	caps.MaxVolumeExtent        = d3d.MaxVolumeExtent;
	caps.MaxTextureAspectRatio  = d3d.MaxTextureAspectRatio;
	caps.MaxSimultaneousTextures = d3d.MaxSimultaneousTextures;

	caps.VertexShaderVersion = d3d.VertexShaderVersion & 0xffff;
	caps.PixelShaderVersion  = d3d.PixelShaderVersion & 0xffff;

	caps.FixedFunctionCombineOps = d3d.TextureOpCaps;
}

bool GfxAdapterD3D9::Query_Capabilities(unsigned adapter, GfxDeviceCaps & caps)
{
	memset(&caps, 0, sizeof(caps));
	if (m_d3d == nullptr) return false;

	D3DCAPS8 d3d;
	::ZeroMemory(&d3d, sizeof(d3d));
	if (FAILED(m_d3d->GetDeviceCaps(adapter, GFX_D3D9_DEVTYPE, &d3d)))
		return false;

	Fill_Device_Caps(d3d, caps);
	return true;
}

bool GfxAdapterD3D9::Supports_Texture_Format(unsigned adapter, WW3DFormat display,
	WW3DFormat format, GfxFormatCapability capability)
{
	if (m_d3d == nullptr) return false;

	DWORD usage;
	switch (capability) {
	case GFX_FORMAT_RENDER_TARGET: usage = D3DUSAGE_RENDERTARGET; break;
	case GFX_FORMAT_BLENDABLE:     usage = D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING; break;
	case GFX_FORMAT_FILTERABLE:    usage = D3DUSAGE_QUERY_FILTER; break;
	default:                       usage = 0; break;
	}

	return SUCCEEDED(m_d3d->CheckDeviceFormat(adapter, GFX_D3D9_DEVTYPE,
		WW3DFormat_To_D3DFormat(display), usage,
		D3DRTYPE_TEXTURE, WW3DFormat_To_D3DFormat(format)));
}

bool GfxAdapterD3D9::Supports_Depth_Texture_Format(unsigned adapter, WW3DFormat display,
	WW3DZFormat format)
{
	if (m_d3d == nullptr) return false;
	return SUCCEEDED(m_d3d->CheckDeviceFormat(adapter, GFX_D3D9_DEVTYPE,
		WW3DFormat_To_D3DFormat(display), D3DUSAGE_DEPTHSTENCIL,
		D3DRTYPE_TEXTURE, WW3DZFormat_To_D3DFormat(format)));
}

bool GfxDeviceD3D9::Query_Capabilities(GfxDeviceCaps & caps)
{
	memset(&caps, 0, sizeof(caps));

	// What the device reports depends on which vertex-processing mode it is in, so ask
	// twice: once forced to software, and again in hardware if the first answer says there
	// is hardware transform and lighting to ask about. The engine wants the second answer.
	//
	// Through DX8Wrapper::Set_DX8_Render_State rather than at the device, so the wrapper's
	// record of what the device holds stays true across a probe that runs before the first
	// frame.
	D3DCAPS8 d3d;
	::ZeroMemory(&d3d, sizeof(d3d));

	DX8Wrapper::Set_DX8_Render_State(D3DRS_SOFTWAREVERTEXPROCESSING, TRUE);
	if (FAILED(m_device->GetDeviceCaps(&d3d)))
		return false;

	if ((d3d.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) == D3DDEVCAPS_HWTRANSFORMANDLIGHT) {
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SOFTWAREVERTEXPROCESSING, FALSE);
		if (FAILED(m_device->GetDeviceCaps(&d3d)))
			return false;
	}

	Fill_Device_Caps(d3d, caps);
	return true;
}

// Fill in the API's own creation structure from what the engine asked for.
static void Fill_Present_Parameters(const GfxSwapChainDesc & desc, D3DPRESENT_PARAMETERS & pp)
{
	::ZeroMemory(&pp, sizeof(pp));
	pp.BackBufferWidth = desc.Width;
	pp.BackBufferHeight = desc.Height;
	pp.BackBufferCount = desc.BackBufferCount;
	pp.BackBufferFormat = WW3DFormat_To_D3DFormat(desc.BackBufferFormat);
	// Discard even in fullscreen: it is the most efficient, and nothing here reads a
	// previous frame back out of the chain.
	pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp.hDeviceWindow = (HWND)desc.Window;
	pp.Windowed = desc.Windowed ? TRUE : FALSE;
	pp.EnableAutoDepthStencil = TRUE;
	pp.AutoDepthStencilFormat = WW3DZFormat_To_D3DFormat(desc.DepthStencilFormat);
	pp.MultiSampleType = WW3DMultiSample_To_D3DMultiSample(desc.MultiSample);
	pp.Flags = 0;								// the back buffer is never locked
	pp.FullScreen_RefreshRateInHz = desc.RefreshRate;
	switch (desc.SwapInterval) {
	case 0:  pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE; break;
	case 1:  pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_ONE; break;
	case 2:  pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_TWO; break;
	case 3:  pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_THREE; break;
	default: pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT; break;
	}
}

GfxDeviceClass * GfxAdapterD3D9::Create_Device(unsigned adapter, GfxSwapChainDesc & desc)
{
	if (m_d3d == nullptr) return nullptr;

	// How vertices are processed, how the FPU is left and whether the device is
	// multithreaded are all questions only this API asks, so they are answered here and
	// are not in the description the engine wrote.
	DWORD behavior = Supports_Hardware_Transform_And_Lighting(adapter)
		? D3DCREATE_MIXED_VERTEXPROCESSING : D3DCREATE_SOFTWARE_VERTEXPROCESSING;

#ifdef CREATE_DX8_MULTI_THREADED
	behavior |= D3DCREATE_MULTITHREADED;
	_DX8SingleThreaded = false;
#else
	_DX8SingleThreaded = true;
#endif

	if (DX8Wrapper_PreserveFPU)
		behavior |= D3DCREATE_FPU_PRESERVE;

#ifdef CREATE_DX8_FPU_PRESERVE
	behavior |= D3DCREATE_FPU_PRESERVE;
#endif

	D3DPRESENT_PARAMETERS pp;
	Fill_Present_Parameters(desc, pp);

	// TheSuperHackers @bugfix xezon 13/06/2025 Front load the system dbghelp.dll to prevent
	// the graphics driver from potentially loading the old game dbghelp.dll and then crashing
	// the game process.
	DbgHelpGuard dbgHelpGuard;

	IDirect3DDevice8 * device = nullptr;
	HRESULT hr = m_d3d->CreateDevice(adapter, GFX_D3D9_DEVTYPE, (HWND)desc.Window,
		behavior, &pp, &device);

	if (FAILED(hr)) {
		// The adapter may have claimed a 32-bit depth buffer it cannot actually pair with a
		// 16-bit display. Drop to 16-bit depth and try once more, and tell the caller what
		// it ended up with -- Has_Stencil and the shadow path both read that back.
		const bool sixteen_bit_colour =
			pp.BackBufferFormat == D3DFMT_R5G6B5 ||
			pp.BackBufferFormat == D3DFMT_X1R5G5B5 ||
			pp.BackBufferFormat == D3DFMT_A1R5G5B5;
		const bool deep_depth =
			pp.AutoDepthStencilFormat == D3DFMT_D32 ||
			pp.AutoDepthStencilFormat == D3DFMT_D24S8 ||
			pp.AutoDepthStencilFormat == D3DFMT_D24X8;

		if (!sixteen_bit_colour || !deep_depth)
			return nullptr;

		desc.DepthStencilFormat = WW3D_ZFORMAT_D16;
		pp.AutoDepthStencilFormat = D3DFMT_D16;
		hr = m_d3d->CreateDevice(adapter, GFX_D3D9_DEVTYPE, (HWND)desc.Window,
			behavior, &pp, &device);
		if (FAILED(hr))
			return nullptr;
	}

	dbgHelpGuard.deactivate();
	return new GfxDeviceD3D9(device, pp);
}

// Named rather than anonymous now that there are two of these. The choice between them is
// in gfxdevice_create.cpp; this is only the D3D9 half of it.
GfxAdapterClass * Gfx_Create_Adapter_D3D9()
{
	GfxAdapterD3D9 * adapter = new GfxAdapterD3D9;
	if (!adapter->Is_Valid()) {
		delete adapter;
		return nullptr;
	}
	return adapter;
}

bool GfxDeviceD3D9::Reset_Swap_Chain(GfxSwapChainDesc & desc)
{
	Fill_Present_Parameters(desc, m_present);

	// A device create or a mode switch commonly leaves the device transiently lost for a
	// few frames. Wait for the OS to hand it back rather than giving up immediately, which
	// intermittently left a dead device -- black screen or hang -- on startup.
	HRESULT hr = m_device->TestCooperativeLevel();
	WWDEBUG_SAY(("Reset_Swap_Chain: TestCooperativeLevel -> 0x%08x", hr));
	int attempts = 0;
	while (hr == D3DERR_DEVICELOST && attempts < 100) {
		::Sleep(50);
		hr = m_device->TestCooperativeLevel();
		++attempts;
	}
	if (attempts > 0)
		WWDEBUG_SAY(("Reset_Swap_Chain: waited %d x50ms; TestCooperativeLevel -> 0x%08x", attempts, hr));
	if (hr == D3DERR_DEVICELOST) {
		WWDEBUG_SAY(("Reset_Swap_Chain: device still lost after wait; giving up this attempt."));
		return false;
	}

	// D3D_OK and D3DERR_DEVICENOTRESET are both resettable. Reset is called directly rather
	// than through the error-checking macro: that routes failures into an assert, which in
	// fullscreen pops an invisible dialog and hangs the app. Log and return false so the
	// caller retries next frame.
	hr = m_device->Reset(&m_present);
	WWDEBUG_SAY(("Reset_Swap_Chain: Reset() -> 0x%08x", hr));
	if (hr != D3D_OK) {
		Non_Fatal_Log_DX8_ErrorCode(hr, __FILE__, __LINE__);
		return false;
	}
	return true;
}

GfxQuery * GfxDeviceD3D9::Create_Query(GfxQueryType type)
{
	D3DQUERYTYPE d3dType;
	switch (type) {
	case GFX_QUERY_TIMESTAMP:           d3dType = D3DQUERYTYPE_TIMESTAMP; break;
	case GFX_QUERY_TIMESTAMP_FREQUENCY: d3dType = D3DQUERYTYPE_TIMESTAMPFREQ; break;
	case GFX_QUERY_TIMESTAMP_DISJOINT:  d3dType = D3DQUERYTYPE_TIMESTAMPDISJOINT; break;
	default: return nullptr;
	}
	IDirect3DQuery9 * query = nullptr;
	// Not an error worth shouting about: plenty of hardware and every reference
	// rasterizer declines timestamps. The caller goes quiet on a null.
	if (FAILED(m_device->CreateQuery(d3dType, &query)))
		return nullptr;
	return (GfxQuery *)query;
}

void GfxDeviceD3D9::Release_Query(GfxQuery * query)
{
	if (query == nullptr) return;
	((IDirect3DQuery9 *)query)->Release();
}

void GfxDeviceD3D9::Begin_Query(GfxQuery * query)
{
	if (query == nullptr) return;
	((IDirect3DQuery9 *)query)->Issue(D3DISSUE_BEGIN);
}

void GfxDeviceD3D9::End_Query(GfxQuery * query)
{
	if (query == nullptr) return;
	((IDirect3DQuery9 *)query)->Issue(D3DISSUE_END);
}

bool GfxDeviceD3D9::Get_Query_Data(GfxQuery * query, void * dest, unsigned size)
{
	if (query == nullptr) return false;
	// D3DGETDATA_FLUSH is deliberately not passed: it would push the command buffer to
	// get an answer sooner, which is exactly the interference this is built to avoid.
	return ((IDirect3DQuery9 *)query)->GetData(dest, (DWORD)size, 0) == S_OK;
}

bool GfxDeviceD3D9::Validate_Draw_State(unsigned & passes)
{
	DWORD n = 0;
	const HRESULT hr = m_device->ValidateDevice(&n);
	passes = (unsigned)n;
	return SUCCEEDED(hr);
}

// Straight out of the device. D3D9 keeps the constant registers itself and hands them
// back, so there is nothing to stage and nothing to guess at.
bool GfxDeviceD3D9::Debug_Read_Vertex_Constants(unsigned first_register, unsigned count,
	float * out)
{
	if (out == nullptr || count == 0) return false;
	return SUCCEEDED(m_device->GetVertexShaderConstantF((UINT)first_register, out,
		(UINT)count));
}

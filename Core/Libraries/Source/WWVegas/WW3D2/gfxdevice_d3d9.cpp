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

// What DX8CALL used to do at the call site, now that the call site is one seam away:
// check the result and count it. Counting here rather than in the wrapper is the only
// way the count can be right, because one interface call is not always one device call
// -- binding a vertex format is two in D3D9 and would be one anywhere else.
#define D3DCALL(x) DX8_ErrorCode(m_device->x); DX8Wrapper::Increment_DX8_CallCount()

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

bool GfxDeviceD3D9::Get_Material(void * material_desc)
{
	return SUCCEEDED(m_device->GetMaterial((D3DMATERIAL8*)material_desc));
}

// ----------------------------------------------------------------------------
// Fixed-function residue
// ----------------------------------------------------------------------------

void GfxDeviceD3D9::Set_Light(unsigned index, const void * light_desc)
{
	D3DCALL(SetLight(index, (const D3DLIGHT8*)light_desc));
	D3DCALL(LightEnable(index, TRUE));
}

void GfxDeviceD3D9::Disable_Light(unsigned index)
{
	D3DCALL(LightEnable(index, FALSE));
}

void GfxDeviceD3D9::Set_Material(const void * material_desc)
{
	D3DCALL(SetMaterial((const D3DMATERIAL8*)material_desc));
}

void GfxDeviceD3D9::Set_Transform(unsigned which, const float * matrix4x4)
{
	D3DCALL(SetTransform((D3DTRANSFORMSTATETYPE)which, (const D3DMATRIX*)matrix4x4));
}

bool GfxDeviceD3D9::Get_Transform(unsigned which, float * matrix4x4)
{
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

bool GfxDeviceD3D9::Update_Texture(GfxTexture * source, GfxTexture * dest)
{
	HRESULT hr = m_device->UpdateTexture((IDirect3DBaseTexture8*)source,
		(IDirect3DBaseTexture8*)dest);
	DX8_ErrorCode(hr);
	return SUCCEEDED(hr);
}

bool GfxDeviceD3D9::Capture_Front_Buffer(GfxSurface * dest)
{
	HRESULT hr = m_device->GetFrontBufferData(0, (IDirect3DSurface8*)dest);
	DX8_ErrorCode(hr);
	return SUCCEEDED(hr);
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

bool GfxDeviceD3D9::Validate_Draw_State(unsigned & passes)
{
	DWORD n = 0;
	const HRESULT hr = m_device->ValidateDevice(&n);
	passes = (unsigned)n;
	return SUCCEEDED(hr);
}

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

// The D3D9 implementation of the graphics backend interface.
//
// This is the only file besides dx8wrapper's own that is allowed to know the game
// draws through Direct3D 9. It does not own the device: DX8Wrapper creates one,
// hands it here, and destroys it, because device creation and mode selection are
// still on the wrapper's side of the seam (see the note in gfxdevice.h).
//
// Every method here is the body that used to sit inline at a DX8CALL, moved rather
// than rewritten. The three translations that came with them -- D3DRS_ZBIAS into a
// real depth bias, the D3D8 stage states that became D3D9 sampler states, and the
// legacy render states D3D9 dropped -- belong to this backend and not to the
// engine, so they moved here too.

#pragma once

#include "gfxdevice.h"
#include "d3d9_compat.h"

/*
** The D3D9 adapter. Owns the IDirect3D9 interface and the d3d9.dll it came out of,
** and is the only thing in the engine that creates a device.
*/
class GfxAdapterD3D9 : public GfxAdapterClass
{
public:
	GfxAdapterD3D9();
	virtual ~GfxAdapterD3D9();

	/// False if d3d9.dll is missing or refused to hand back an interface.
	bool					Is_Valid() const { return m_d3d != nullptr; }

	virtual unsigned		Get_Adapter_Count();
	virtual bool			Get_Adapter_Info(unsigned adapter, GfxAdapterInfo & info);
	virtual bool			Get_Current_Display_Mode(unsigned adapter, GfxDisplayMode & mode);
	virtual unsigned		Get_Display_Mode_Count(unsigned adapter, WW3DFormat format);
	virtual bool			Get_Display_Mode(unsigned adapter, WW3DFormat format,
								unsigned index, GfxDisplayMode & mode);
	virtual bool			Supports_Display_Format(unsigned adapter, WW3DFormat display,
								WW3DFormat back_buffer, bool windowed);
	virtual bool			Supports_Depth_Stencil_Format(unsigned adapter, WW3DFormat display,
								WW3DFormat back_buffer, WW3DZFormat depth);
	virtual bool			Supports_Multisample(unsigned adapter, WW3DFormat format,
								bool windowed, WW3DMultiSampleType samples);
	virtual bool			Supports_Depth_Multisample(unsigned adapter, WW3DZFormat format,
								bool windowed, WW3DMultiSampleType samples);
	virtual bool			Supports_Hardware_Transform_And_Lighting(unsigned adapter);
	virtual bool			Supports_Texture_Format(unsigned adapter, WW3DFormat display,
								WW3DFormat format, GfxFormatCapability capability);
	virtual bool			Supports_Depth_Texture_Format(unsigned adapter, WW3DFormat display,
								WW3DZFormat format);
	virtual bool			Query_Capabilities(unsigned adapter, GfxDeviceCaps & caps);
	virtual GfxDeviceClass * Create_Device(unsigned adapter, GfxSwapChainDesc & desc);

	/// The raw interface, for the D3D9-only capability probe in dx8caps.
	IDirect3D8 *			Peek_D3D() const { return m_d3d; }

private:
	HMODULE			m_library;
	IDirect3D8 *	m_d3d;
};

/*
** Make the D3D9 adapter, or null if d3d9.dll is missing. gfxdevice_create.cpp calls
** this; nothing else does.
*/
GfxAdapterClass * Gfx_Create_Adapter_D3D9();

class GfxDeviceD3D9 : public GfxDeviceClass
{
public:
	GfxDeviceD3D9(IDirect3DDevice8 * device, const D3DPRESENT_PARAMETERS & pp)
		: m_device(device), m_present(pp) {}
	// Owns the reference Create_Device took out. DX8Wrapper used to hold a second one
	// and release it just before deleting this; it no longer holds a device at all.
	virtual ~GfxDeviceD3D9() { if (m_device != nullptr) m_device->Release(); }

	// ---- frame -----------------------------------------------------------

	virtual void			Begin_Scene();
	virtual void			End_Scene();
	virtual GfxDeviceStatus	Present();
	virtual GfxDeviceStatus	Get_Device_Status();
	virtual void			Clear(bool clear_color, bool clear_z, bool clear_stencil,
								unsigned argb, float z, unsigned stencil);
	virtual bool			Has_Stencil_Target();

	// ---- render state ----------------------------------------------------

	virtual void			Set_Render_State(unsigned state, unsigned value);
	virtual void			Set_Texture_Stage_State(unsigned stage, unsigned state, unsigned value);
	virtual void			Set_Clip_Plane(unsigned index, const float * plane);

	virtual bool			Get_Render_State(unsigned state, unsigned & value);
	virtual bool			Get_Texture_Stage_State(unsigned stage, unsigned state, unsigned & value);

	// ---- fixed-function residue ------------------------------------------

	virtual void			Set_Transform(unsigned which, const float * matrix4x4);
	virtual bool			Get_Transform(unsigned which, float * matrix4x4);

	// ---- bindings --------------------------------------------------------

	virtual void			Set_Texture(unsigned stage, GfxTexture * texture);
	virtual void			Set_Vertex_Shader(GfxShaderHandle shader);
	virtual void			Set_Pixel_Shader(GfxShaderHandle shader);
	virtual GfxShaderHandle	Create_Vertex_Shader(const void * bytecode, unsigned size);
	virtual GfxShaderHandle	Create_Pixel_Shader(const void * bytecode, unsigned size);
	virtual void			Release_Vertex_Shader(GfxShaderHandle shader);
	virtual void			Release_Pixel_Shader(GfxShaderHandle shader);
	virtual void			Set_Vertex_Shader_Constants(unsigned reg, const float * data, unsigned vec4_count);
	virtual void			Set_Pixel_Shader_Constants(unsigned reg, const float * data, unsigned vec4_count);
	virtual void			Set_Vertex_Stream(unsigned stream, GfxVertexBuffer * buffer, unsigned stride);
	virtual bool			Get_Vertex_Stream(unsigned stream, GfxVertexBuffer ** buffer,
								unsigned * offset, unsigned * stride);
	virtual void			Set_Index_Buffer(GfxIndexBuffer * buffer, int base_vertex_index);

	// ---- draws -----------------------------------------------------------

	virtual void			Draw_Indexed(unsigned primitive_type, int base_vertex_index,
								unsigned min_vertex_index, unsigned vertex_count,
								unsigned start_index, unsigned primitive_count);
	virtual void			Draw(unsigned primitive_type, unsigned start_vertex,
								unsigned primitive_count);
	virtual void			Draw_Up(unsigned primitive_type, unsigned primitive_count,
								const void * vertex_data, unsigned vertex_stride);

	// ---- targets ---------------------------------------------------------

	virtual bool			Set_Render_Target(GfxSurface * color, GfxSurface * depth);
	virtual GfxSurface *	Get_Render_Target(unsigned index);
	virtual GfxSurface *	Get_Depth_Target();
	virtual GfxSurface *	Get_Back_Buffer(unsigned index);
	virtual void			Set_Viewport(const GfxViewport & viewport);
	virtual bool			Get_Viewport(GfxViewport & viewport);

	// ---- transfers and queries -------------------------------------------

	virtual bool			Copy_Surface(GfxSurface * source, const GfxRect * source_rect,
								GfxSurface * dest, const GfxRect * dest_rect);
	virtual bool			Copy_Surface_Rect(GfxSurface * source, const GfxRect * source_rect,
								GfxSurface * dest, const GfxRect * dest_rect,
								GfxCopyFilter filter);
	virtual bool			Update_Texture(GfxTexture * source, GfxTexture * dest);
	virtual bool			Generate_Mips(GfxTexture * texture, unsigned base_level);
	virtual void			Set_Texture_Detail_Level(GfxTexture * texture, unsigned skip_levels);
	virtual bool			Capture_Front_Buffer(GfxSurface * dest);
	virtual GfxVertexBuffer * Create_Vertex_Buffer(unsigned size_in_bytes, unsigned fvf,
								unsigned usage);
	virtual GfxIndexBuffer *  Create_Index_Buffer(unsigned index_count, unsigned usage);
	virtual void			Release_Vertex_Buffer(GfxVertexBuffer * buffer);
	virtual void			Release_Index_Buffer(GfxIndexBuffer * buffer);

	virtual bool			Map_Vertex_Buffer(GfxVertexBuffer * buffer, unsigned offset_in_bytes,
								unsigned size_in_bytes, GfxMapMode mode, void ** data);
	virtual void			Unmap_Vertex_Buffer(GfxVertexBuffer * buffer);
	virtual bool			Map_Index_Buffer(GfxIndexBuffer * buffer, unsigned offset_in_bytes,
								unsigned size_in_bytes, GfxMapMode mode, void ** data);
	virtual void			Unmap_Index_Buffer(GfxIndexBuffer * buffer);

	// Debug only. Says how many maps asked to discard or append against a buffer that
	// was not created dynamic -- legal under D3D9, a failed Map under D3D11.
	static void				Report_Nondynamic_Discards();

	// ---- textures and surfaces -------------------------------------------

	virtual GfxTexture *	Create_Texture(unsigned width, unsigned height, unsigned levels,
								WW3DFormat format, unsigned usage);
	virtual GfxTexture *	Create_Cube_Texture(unsigned edge_length, unsigned levels,
								WW3DFormat format, unsigned usage);
	virtual GfxTexture *	Create_Volume_Texture(unsigned width, unsigned height,
								unsigned depth, unsigned levels, WW3DFormat format,
								unsigned usage);
	virtual GfxTexture *	Create_Depth_Texture(unsigned width, unsigned height,
								unsigned levels, WW3DZFormat format, unsigned usage);
	virtual void			Release_Texture(GfxTexture * texture);
	virtual void			Reference_Texture(GfxTexture * texture);
	virtual GfxSurface *	Create_Render_Target_Surface(unsigned width, unsigned height,
								WW3DFormat format, WW3DMultiSampleType multisample);
	virtual GfxSurface *	Create_Depth_Stencil_Surface(unsigned width, unsigned height,
								WW3DZFormat format, WW3DMultiSampleType multisample);
	virtual GfxSurface *	Create_Offscreen_Surface(unsigned width, unsigned height,
								WW3DFormat format);
	virtual void			Release_Surface(GfxSurface * surface);
	virtual void			Reference_Surface(GfxSurface * surface);
	virtual unsigned		Get_Texture_Level_Count(GfxTexture * texture);
	virtual GfxSurface *	Get_Texture_Surface_Level(GfxTexture * texture, unsigned level);
	virtual bool			Map_Texture(GfxTexture * texture, unsigned level,
								const GfxRect * rect, GfxMapMode mode, GfxMappedRect & mapped);
	virtual void			Unmap_Texture(GfxTexture * texture, unsigned level);
	virtual bool			Map_Surface(GfxSurface * surface, const GfxRect * rect,
								GfxMapMode mode, GfxMappedRect & mapped);
	virtual void			Unmap_Surface(GfxSurface * surface);
	virtual bool			Map_Volume_Texture(GfxTexture * texture, unsigned level,
								GfxMapMode mode, GfxMappedBox & mapped);
	virtual void			Unmap_Volume_Texture(GfxTexture * texture, unsigned level);
	virtual bool			Map_Cube_Texture(GfxTexture * texture, unsigned face, unsigned level,
								const GfxRect * rect, GfxMapMode mode, GfxMappedRect & mapped);
	virtual void			Unmap_Cube_Texture(GfxTexture * texture, unsigned face, unsigned level);
	virtual bool			Describe_Depth_Texture_Level(GfxTexture * texture, unsigned level,
								WW3DZFormat & format);

	virtual bool			Describe_Surface(GfxSurface * surface, WW3DSurfaceDescription & desc);
	virtual bool			Describe_Texture_Level(GfxTexture * texture, unsigned level,
								WW3DSurfaceDescription & desc);
	virtual bool			Describe_Volume_Level(GfxTexture * texture, unsigned level,
								WW3DSurfaceDescription & desc, unsigned & depth);

	virtual bool			Get_Display_Mode(unsigned & width, unsigned & height, WW3DFormat & format);
	virtual unsigned		Get_Available_Texture_Memory();
	virtual void			Trim_Resource_Memory();
	virtual void			Set_Gamma_Ramp(const void * ramp, bool calibrate);
	virtual bool			Set_Hardware_Cursor(GfxSurface * image, unsigned hot_x, unsigned hot_y);
	virtual void			Show_Hardware_Cursor(bool show);
	virtual void			Set_Hardware_Cursor_Position(unsigned x, unsigned y);
	virtual bool			Save_Surface_To_File(const char * path, GfxSurface * surface);
	virtual GfxQuery *		Create_Query(GfxQueryType type);
	virtual void			Release_Query(GfxQuery * query);
	virtual void			Begin_Query(GfxQuery * query);
	virtual void			End_Query(GfxQuery * query);
	virtual bool			Get_Query_Data(GfxQuery * query, void * dest, unsigned size);

	virtual bool			Query_Capabilities(GfxDeviceCaps & caps);

	/// The raw device, for the out-of-engine interop described at the base class. The
	/// engine itself no longer has one: everything it does to a device goes through the
	/// virtuals above.
	virtual void *			Peek_Native_Device() { return m_device; }
	virtual bool			Reset_Swap_Chain(GfxSwapChainDesc & desc);
	virtual bool			Validate_Draw_State(unsigned & passes);
	virtual bool			Debug_Read_Vertex_Constants(unsigned first_register,
								unsigned count, float * out);

private:
	IDirect3DDevice8 *		m_device;
	// The parameters the swap chain was made with. Reset needs them again, and the
	// engine no longer keeps a copy in D3D9 vocabulary for it to pass back.
	D3DPRESENT_PARAMETERS	m_present;
};

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

// The Direct3D 11 implementation of the graphics backend interface.
//
// The second backend. It implements exactly the same 15 + 106 virtuals the D3D9 one does,
// and the engine above the seam cannot tell which of them it is talking to -- that was the
// point of the nine phases before this one.
//
// This header names no D3D11 type. Everything with a size lives in one pimpl defined in
// the .cpp, which is the only translation unit in the tree that includes <d3d11.h>. That
// matters for one caller in particular: gfxdevice_create.cpp picks between the two
// backends and so includes both of their headers, and it must be able to do that without
// pulling either graphics API in.
//
// What is different about this backend, stated once here rather than repeated at each
// method:
//
//   * D3D9 sets render state one word at a time; D3D11 has four immutable state objects.
//     Set_Render_State and Set_Texture_Stage_State therefore only record. The objects are
//     materialised at draw time and cached on a hash of the words that feed them.
//   * There is no fixed-function pipeline, so Set_Transform's fixed-function half and the
//     D3DTSS combiner words have nowhere to go. They are swallowed, and counted under
//     RTS_DEBUG so that what was swallowed can be read rather than assumed.
//   * A draw with no vertex shader cannot be made and is dropped with a count.
//   * There is no front buffer, no hardware cursor and no D3DX, so Capture_Front_Buffer
//     reads the back buffer, Set_Hardware_Cursor answers false (W3DMouse then takes its
//     software path) and Save_Surface_To_File writes a PNG through stb_image_write.

#pragma once

#include "gfxdevice.h"

struct GfxD3D11Impl;

/*
** The D3D11 adapter. Owns the DXGI factory and the two libraries it came out of, and is
** the only thing here that creates a device.
*/
class GfxAdapterD3D11 : public GfxAdapterClass
{
public:
	GfxAdapterD3D11();
	virtual ~GfxAdapterD3D11();

	/// False if d3d11.dll or dxgi.dll is missing, or DXGI refused a factory.
	bool					Is_Valid() const;

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

private:
	// A device kept only to answer capability questions -- see the definition. Declared
	// with a forward reference rather than by including d3d11.h, which this header must
	// not do: d3d9_compat.h #defines CreateTexture and friends as fixed-arity macros and
	// would rewrite the D3D11 calls of the same name in anything that included both.
	struct ID3D11Device *	Caps_Device(unsigned adapter);

	struct Impl;
	Impl *	m_impl;
};

/*
** Make the D3D11 adapter, or null if this machine has no D3D11. gfxdevice_create.cpp calls
** this; nothing else does.
*/
GfxAdapterClass * Gfx_Create_Adapter_D3D11();

class GfxDeviceD3D11 : public GfxDeviceClass
{
public:
	explicit GfxDeviceD3D11(GfxD3D11Impl * impl) : m_impl(impl) {}
	virtual ~GfxDeviceD3D11();

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

	// ---- buffers ---------------------------------------------------------

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

	// ---- describing a resource -------------------------------------------

	virtual bool			Describe_Surface(GfxSurface * surface, WW3DSurfaceDescription & desc);
	virtual bool			Describe_Texture_Level(GfxTexture * texture, unsigned level,
								WW3DSurfaceDescription & desc);
	virtual bool			Describe_Volume_Level(GfxTexture * texture, unsigned level,
								WW3DSurfaceDescription & desc, unsigned & depth);
	virtual bool			Describe_Depth_Texture_Level(GfxTexture * texture, unsigned level,
								WW3DZFormat & format);

	// ---- the rest --------------------------------------------------------

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
	virtual bool			Reset_Swap_Chain(GfxSwapChainDesc & desc);
	virtual bool			Validate_Draw_State(unsigned & passes);
	virtual bool			Debug_Read_Vertex_Constants(unsigned first_register,
								unsigned count, float * out);

	/// Null, and deliberately. DX8WebBrowser hands the device to an ActiveX control that
	/// wants a D3D9 device or nothing; the caller turns the in-game browser off.
	virtual void *			Peek_Native_Device() { return nullptr; }

	/// Debug only: what this backend had to swallow, and what it had to drop. Printed on
	/// the same 600-frame window as every other census here so the figures read beside them.
	static void				Report_Absorbed_State();

private:
	GfxD3D11Impl *	m_impl;
};

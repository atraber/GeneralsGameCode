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

class GfxDeviceD3D9 : public GfxDeviceClass
{
public:
	explicit GfxDeviceD3D9(IDirect3DDevice8 * device) : m_device(device) {}
	virtual ~GfxDeviceD3D9() {}

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
	virtual bool			Update_Texture(GfxTexture * source, GfxTexture * dest);
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

	virtual bool			Describe_Surface(GfxSurface * surface, WW3DSurfaceDescription & desc);
	virtual bool			Describe_Texture_Level(GfxTexture * texture, unsigned level,
								WW3DSurfaceDescription & desc);

	virtual bool			Get_Display_Mode(unsigned & width, unsigned & height, WW3DFormat & format);
	virtual unsigned		Get_Available_Texture_Memory();
	virtual void			Trim_Resource_Memory();
	virtual void			Set_Gamma_Ramp(const void * ramp, bool calibrate);
	virtual bool			Validate_Draw_State(unsigned & passes);

private:
	IDirect3DDevice8 *		m_device;
};

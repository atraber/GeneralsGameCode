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

// The graphics backend interface.
//
// Everything DX8Wrapper does to a graphics device on the render path is declared
// here and nowhere else. There is exactly one implementation -- GfxDeviceD3D9 --
// and this header is deliberately written so that it could have a second: it
// includes no graphics API header, names no API type, and hands back no API
// pointer the caller can call a method on.
//
// It follows the shape ww3dformat.h / formconv.h already established in this tree:
// a neutral vocabulary in the header, conversion at the boundary, and the API's own
// types confined to the implementation. DX8Wrapper is that boundary. Its public API
// does not change at all -- every one of its static entry points keeps its
// signature and its body, and simply ends in a call through here -- which is what
// makes the seam verifiable by "nothing moved".
//
// What is NOT behind this interface, and why:
//
//   * Resource creation, and the resource handles themselves. TextureClass,
//     SurfaceClass, DX8VertexBufferClass and DX8IndexBufferClass all put D3D
//     pointers in their public API, and W3DShaderManager round-trips a
//     D3DSURFACE_DESC it got back from D3D straight into CreateTexture. A neutral
//     creation call cannot be written until those change, so the handle types
//     below are opaque tags that only travel from the engine to the backend and
//     back again unchanged. That is the resource seam, and it is still open.
//
//   * Device creation, mode enumeration and Reset. Those run once at startup and
//     on a window change, which is exactly the code path the replay harness never
//     executes -- so moving them could not be verified by the thing that verifies
//     everything else here.
//
// The render-state vocabulary is still D3D9-numbered: Set_Render_State takes the
// same state word the engine has always written, because that word IS the engine's
// tracked-state IR -- DX8Wrapper::RenderStates[256] is indexed by it -- and it is
// named at some 800 call sites. Turning it into a neutral enum is a translation
// table inside a backend, not a change to the engine, so it is left as one.

#pragma once

#include "WWLib/always.h"
#include "ww3dformat.h"

/*
** Opaque resource handles.
**
** These are never dereferenced on this side of the seam. A backend hands out its
** own pointers cast to these types and casts them back on the way in; nothing in
** this header lets a caller do anything else with one.
*/
struct GfxTexture;
struct GfxSurface;
struct GfxVertexBuffer;
struct GfxIndexBuffer;

/*
** A bound shader, as the engine has always carried one: an opaque word that is
** either a vertex-format code or a compiled-shader handle. Set_Vertex_Shader below
** is what makes the difference invisible to the caller.
*/
typedef unsigned long GfxShaderHandle;

/*
** Neutral spellings of the two small structs that cross the seam by value.
*/
struct GfxViewport
{
	unsigned	X;
	unsigned	Y;
	unsigned	Width;
	unsigned	Height;
	float		MinZ;
	float		MaxZ;
};

struct GfxRect
{
	long	left;
	long	top;
	long	right;
	long	bottom;
};

/*
** What Get_Device_Status reports. D3D9's TestCooperativeLevel is three outcomes
** wearing one HRESULT; this names them, and a backend with no device-lost concept
** simply always answers GFX_DEVICE_OK.
*/
enum GfxDeviceStatus
{
	GFX_DEVICE_OK = 0,
	GFX_DEVICE_LOST,			// unusable, and not recoverable yet
	GFX_DEVICE_NEEDS_RESET,		// recoverable now
	GFX_DEVICE_ERROR			// the call failed for some other reason
};

class GfxDeviceClass
{
public:
	virtual ~GfxDeviceClass() {}

	// ---- frame -----------------------------------------------------------

	virtual void			Begin_Scene() = 0;
	virtual void			End_Scene() = 0;
	// GFX_DEVICE_OK, GFX_DEVICE_LOST or GFX_DEVICE_ERROR. The two failures are kept
	// apart because the caller does different things with them, and folding them
	// together would make a driver hiccup look like an alt-tab.
	virtual GfxDeviceStatus	Present() = 0;
	virtual GfxDeviceStatus	Get_Device_Status() = 0;
	virtual void			Clear(bool clear_color, bool clear_z, bool clear_stencil,
								unsigned argb, float z, unsigned stencil) = 0;
	// Whether the bound depth target carries stencil bits. Asking for a stencil clear
	// when it does not fails the whole clear, so the caller has to know.
	virtual bool			Has_Stencil_Target() = 0;

	// ---- render state ----------------------------------------------------
	//
	// The state words are the engine's own; see the note at the top of this file.
	// A backend is expected to translate them, and to absorb the ones its API has
	// no equivalent for rather than making the caller know which those are.

	virtual void			Set_Render_State(unsigned state, unsigned value) = 0;
	// One call for both halves of what D3D9 split in two. Ten of the D3D8 stage
	// states became sampler states in D3D9 and need a different entry point there;
	// which those are is a fact about that API, so the remap lives in the backend and
	// SetSamplerState does not appear in this interface.
	virtual void			Set_Texture_Stage_State(unsigned stage, unsigned state, unsigned value) = 0;
	virtual void			Set_Clip_Plane(unsigned index, const float * plane) = 0;

	// The read-back half. These exist for the device-state audit, which is the one
	// instrument that can tell whether the tracked state and the device have drifted
	// apart -- so it has to be able to ask the device rather than the wrapper. They
	// return false when the backend cannot answer, and a backend that keeps no device
	// state of its own is entitled to say so.
	virtual bool			Get_Render_State(unsigned state, unsigned & value) = 0;
	virtual bool			Get_Texture_Stage_State(unsigned stage, unsigned state, unsigned & value) = 0;
	// The render path no longer reads a transform back -- it reads the matrix the wrapper
	// sent. This is the audit's read-back and only that, which is why it sits with the
	// other three rather than with Set_Transform: it asks the backend what it is holding,
	// and a backend holding no transform state of its own answers false.
	virtual bool			Get_Transform(unsigned which, float * matrix4x4) = 0;

	// ---- fixed-function residue ------------------------------------------
	//
	// What is left of it. Lights and the material went with the lighting stage they fed:
	// no draw in either shadow configuration reaches the device with D3DRS_LIGHTING
	// enabled, so nothing they described could reach a pixel.
	//
	// Set_Transform stays because it is not residue. The shadow volumes are genuinely
	// fixed-function draws -- an FVF, no pixel shader, a stencil pass -- and they position
	// themselves with D3DTS_WORLD. A backend that cannot draw from an FVF has no fixed
	// function to feed, and can implement this as the matrix half of whatever it puts in
	// place of one. The matrix passes as sixteen floats rather than an API type for the
	// same reason the rest of this header names no API type.
	virtual void			Set_Transform(unsigned which, const float * matrix4x4) = 0;

	// ---- bindings --------------------------------------------------------

	virtual void			Set_Texture(unsigned stage, GfxTexture * texture) = 0;

	// Binds either a compiled vertex shader or a vertex-format code, whichever the
	// handle is. D3D9 needs two different calls for that, and one of them clears
	// the other; which of those is true is the backend's business, not the
	// caller's, so SetFVF does not appear in this interface at all.
	virtual void			Set_Vertex_Shader(GfxShaderHandle shader) = 0;
	virtual void			Set_Pixel_Shader(GfxShaderHandle shader) = 0;

	virtual void			Set_Vertex_Shader_Constants(unsigned reg, const float * data, unsigned vec4_count) = 0;
	virtual void			Set_Pixel_Shader_Constants(unsigned reg, const float * data, unsigned vec4_count) = 0;

	virtual void			Set_Vertex_Stream(unsigned stream, GfxVertexBuffer * buffer, unsigned stride) = 0;
	virtual bool			Get_Vertex_Stream(unsigned stream, GfxVertexBuffer ** buffer,
								unsigned * offset, unsigned * stride) = 0;
	virtual void			Set_Index_Buffer(GfxIndexBuffer * buffer, int base_vertex_index) = 0;

	// ---- draws -----------------------------------------------------------

	virtual void			Draw_Indexed(unsigned primitive_type, int base_vertex_index,
								unsigned min_vertex_index, unsigned vertex_count,
								unsigned start_index, unsigned primitive_count) = 0;
	virtual void			Draw(unsigned primitive_type, unsigned start_vertex,
								unsigned primitive_count) = 0;
	virtual void			Draw_Up(unsigned primitive_type, unsigned primitive_count,
								const void * vertex_data, unsigned vertex_stride) = 0;

	// ---- targets ---------------------------------------------------------
	//
	// The three Get_ calls hand back a reference the caller must release, exactly
	// as they always have. That is the resource seam showing through: releasing it
	// means calling a method on a handle this interface calls opaque.

	virtual bool			Set_Render_Target(GfxSurface * color, GfxSurface * depth) = 0;
	virtual GfxSurface *	Get_Render_Target(unsigned index) = 0;
	virtual GfxSurface *	Get_Depth_Target() = 0;
	virtual GfxSurface *	Get_Back_Buffer(unsigned index) = 0;

	virtual void			Set_Viewport(const GfxViewport & viewport) = 0;
	virtual bool			Get_Viewport(GfxViewport & viewport) = 0;

	// ---- transfers and queries -------------------------------------------

	// One call for D3D9's UpdateSurface and StretchRect: the difference between
	// them is whether the rectangles are the same size, which the backend can see
	// for itself.
	virtual bool			Copy_Surface(GfxSurface * source, const GfxRect * source_rect,
								GfxSurface * dest, const GfxRect * dest_rect) = 0;
	virtual bool			Update_Texture(GfxTexture * source, GfxTexture * dest) = 0;
	virtual bool			Capture_Front_Buffer(GfxSurface * dest) = 0;

	virtual bool			Get_Display_Mode(unsigned & width, unsigned & height, WW3DFormat & format) = 0;
	virtual unsigned		Get_Available_Texture_Memory() = 0;

	// A hint that the backend may drop whatever it is holding for resources nothing
	// has asked for lately. D3D9 evicts its managed pool; a backend with no such
	// pool does nothing.
	virtual void			Trim_Resource_Memory() = 0;

	virtual void			Set_Gamma_Ramp(const void * ramp, bool calibrate) = 0;

	// Debug only: asks whether the current state can be drawn in one pass. There is
	// no obligation to answer -- a backend that cannot returns false.
	virtual bool			Validate_Draw_State(unsigned & passes) = 0;
};

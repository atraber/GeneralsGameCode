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
** What the engine will do with a resource's memory.
**
** A backend chooses its own storage from this; the engine never names one. This is
** deliberately not a pool argument: D3DPOOL_MANAGED -- what GFX_USAGE_STATIC becomes
** under D3D9 -- has no D3D11 equivalent at all, so a pool is exactly the thing that
** cannot cross a seam. The bit values match DX8VertexBufferClass::UsageType and
** DX8IndexBufferClass::UsageType, which are defined in terms of them.
*/
enum GfxResourceUsage
{
	GFX_USAGE_STATIC = 0,				// filled at creation, then read by the GPU
	GFX_USAGE_DYNAMIC = 1,				// rewritten by the CPU every frame or oftener
	GFX_USAGE_SOFTWARE_PROCESSING = 2,	// vertices are transformed on the CPU
	GFX_USAGE_NPATCHES = 4,				// will be fed to a tessellator
	GFX_USAGE_POINT_SPRITES = 8,		// each vertex is expanded into a screen-facing quad
	GFX_USAGE_RENDER_TARGET = 16,		// the GPU will draw into it
	GFX_USAGE_DYNAMIC_TEXTURE = 32		// the CPU rewrites its pixels while the GPU reads them
};

/*
** What a caller intends to do with mapped memory, and what it promises about the part
** it is not writing.
**
** D3D9's Lock takes these as hints it is free to ignore; D3D11's Map takes them as
** rules, and a discard map against a resource that was not created dynamic fails at
** runtime rather than at compile time. Stating the intent here is what lets a backend
** enforce -- or at least notice -- the pairing, and the D3D9 backend does notice: see
** the discard audit in Map_Vertex_Buffer.
**
** D3D9's D3DLOCK_NOSYSLOCK has no counterpart here and is deliberately absent. It says
** whether the driver may hold the system lock, it changes nothing about what is drawn,
** and there is nothing for a second backend to do with it.
*/
enum GfxMapMode
{
	GFX_MAP_WRITE = 0,			// write; whatever is not written stays
	GFX_MAP_WRITE_DISCARD,		// write all of it; the previous contents are not wanted
	GFX_MAP_WRITE_NO_OVERWRITE,	// append; nothing the GPU may still be reading is touched
	GFX_MAP_READ,
	GFX_MAP_READ_WRITE
};

/*
** A mapped region of a texture or a surface.
**
** Two dimensions, not one, because a mapped image is not a flat run of bytes: the
** distance from one row to the next is the driver's business and is routinely larger
** than the row itself. Every caller in this engine already reasoned in exactly these
** two numbers -- D3DLOCKED_RECT's pBits and Pitch -- so this is the same fact in a
** name a second backend can also answer to. D3D11's MAPPED_SUBRESOURCE says RowPitch
** and means the same thing.
*/
struct GfxMappedRect
{
	void *	Data;
	int		Pitch;		// bytes from the start of one row to the start of the next
};

/*
** The same for a volume texture, which has a second stride between slices.
*/
struct GfxMappedBox
{
	void *	Data;
	int		RowPitch;
	int		SlicePitch;
};

/*
** What Get_Device_Status reports. D3D9's TestCooperativeLevel is three outcomes
** wearing one HRESULT; this names them, and a backend with no device-lost concept
** simply always answers GFX_DEVICE_OK.
*/
/*
** Whether a rectangle copy may resample.
*/
enum GfxCopyFilter
{
	GFX_COPY_NO_FILTER = 0,		// take the pixels as they are
	GFX_COPY_RESAMPLE,			// scale them, filtering as it goes
	GFX_COPY_HALVE				// build the next mip down from this one
};

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

	// ---- buffers ---------------------------------------------------------
	//
	// Creation says what the buffer is for, not where to put it. The retry ladder the
	// engine runs when creation fails -- drop old textures, flush the mesh cache, try
	// again -- stays on the engine side, because what it frees is the engine's.
	//
	// Map hands back a pointer into the buffer, and takes the caller's intent with it.

	virtual GfxVertexBuffer * Create_Vertex_Buffer(unsigned size_in_bytes, unsigned fvf,
								unsigned usage) = 0;
	virtual GfxIndexBuffer *  Create_Index_Buffer(unsigned index_count, unsigned usage) = 0;
	virtual void			Release_Vertex_Buffer(GfxVertexBuffer * buffer) = 0;
	virtual void			Release_Index_Buffer(GfxIndexBuffer * buffer) = 0;

	virtual bool			Map_Vertex_Buffer(GfxVertexBuffer * buffer, unsigned offset_in_bytes,
								unsigned size_in_bytes, GfxMapMode mode, void ** data) = 0;
	virtual void			Unmap_Vertex_Buffer(GfxVertexBuffer * buffer) = 0;
	virtual bool			Map_Index_Buffer(GfxIndexBuffer * buffer, unsigned offset_in_bytes,
								unsigned size_in_bytes, GfxMapMode mode, void ** data) = 0;
	virtual void			Unmap_Index_Buffer(GfxIndexBuffer * buffer) = 0;

	// ---- textures and surfaces -------------------------------------------
	//
	// The same shape as the buffer half above, and for the same reason: creation says
	// what the resource is for and what is in it, never where the API should put it.
	// D3DPOOL_MANAGED, which is what a plain static texture becomes under D3D9, has no
	// D3D11 equivalent at all -- so a pool is exactly the argument that cannot cross a
	// seam, and the backend picks one from the usage bits.
	//
	// Every one of these returns nullptr on failure rather than an HRESULT. The retry
	// ladders the engine runs when a creation fails -- free old textures, flush the mesh
	// cache, try a smaller format -- stay on the engine side, because what they free is
	// the engine's.
	//
	// Levels is a count, and zero still means "all the way down to 1x1", which is what
	// both APIs mean by it.
	virtual GfxTexture *	Create_Texture(unsigned width, unsigned height, unsigned levels,
								WW3DFormat format, unsigned usage) = 0;
	virtual GfxTexture *	Create_Cube_Texture(unsigned edge_length, unsigned levels,
								WW3DFormat format, unsigned usage) = 0;
	virtual void			Release_Texture(GfxTexture * texture) = 0;
	// The wrapper keeps its own reference to whatever is bound at each stage, so that an
	// engine-side owner going away does not free a texture the device is still pointing
	// at. That is not a COM detail leaking through -- a backend that hands out handles
	// has to be told when a second holder appears, whatever it counts them with -- so it
	// is stated here as a pair with Release_Texture rather than as an AddRef.
	virtual void			Reference_Texture(GfxTexture * texture) = 0;

	// A render target and a depth target are surfaces rather than textures because
	// that is what the engine binds: Set_Render_Target above takes two of them. Where
	// it wants to sample the result afterwards it creates a texture with
	// GFX_USAGE_RENDER_TARGET and asks for level 0 below.
	virtual GfxSurface *	Create_Render_Target_Surface(unsigned width, unsigned height,
								WW3DFormat format, WW3DMultiSampleType multisample) = 0;
	virtual GfxSurface *	Create_Depth_Stencil_Surface(unsigned width, unsigned height,
								WW3DZFormat format, WW3DMultiSampleType multisample) = 0;
	// CPU-side pixels: the staging surface a readback lands in and the shroud is built
	// in. Nothing draws from one. The two-step fallback D3D9 needs when the first pool
	// refuses is the backend's problem, not a caller's.
	virtual GfxSurface *	Create_Offscreen_Surface(unsigned width, unsigned height,
								WW3DFormat format) = 0;
	virtual void			Release_Surface(GfxSurface * surface) = 0;
	// The counterpart of Release_Surface, for the same reason Reference_Texture exists:
	// the wrapper holds the render target and the depth target it swapped out across a
	// redirected pass, and something has to know there are two holders.
	virtual void			Reference_Surface(GfxSurface * surface) = 0;

	// How many mip levels a texture actually has, and the surface for one of them.
	// Get_Texture_Surface_Level hands back a reference the caller must give to
	// Release_Surface -- the same borrowing rule the three Get_ target calls above use,
	// and the reason those three are documented as showing the resource seam through.
	virtual unsigned		Get_Texture_Level_Count(GfxTexture * texture) = 0;
	virtual GfxSurface *	Get_Texture_Surface_Level(GfxTexture * texture, unsigned level) = 0;

	// Mapping, with the caller's intent stated the way the buffer half states it.
	//
	// A null rect means the whole level, which is what every caller that passes one
	// means by it. GFX_MAP_READ is the mode that matters here and did not matter for
	// buffers: the staging reads in the smudge and the surface copies genuinely read
	// pixels back, and D3D11 will not let a map read from a resource that was not
	// created for it.
	//
	// D3D9's D3DLOCK_NO_DIRTY_UPDATE is absorbed here rather than named, alongside
	// D3DLOCK_NOSYSLOCK above. It suppresses the dirty-region bookkeeping D3D9 keeps
	// for managed textures so that an UpdateTexture does not re-upload what was
	// touched -- and its one caller in this engine passes it to an *offscreen plain
	// system-memory surface*, which has no managed copy, no dirty region and nothing to
	// update. It has never done anything, and there is nothing for a second backend to
	// do with it either.
	virtual bool			Map_Texture(GfxTexture * texture, unsigned level,
								const GfxRect * rect, GfxMapMode mode,
								GfxMappedRect & mapped) = 0;
	virtual void			Unmap_Texture(GfxTexture * texture, unsigned level) = 0;
	virtual bool			Map_Surface(GfxSurface * surface, const GfxRect * rect,
								GfxMapMode mode, GfxMappedRect & mapped) = 0;
	virtual void			Unmap_Surface(GfxSurface * surface) = 0;
	// The volume texture is one caller -- the mip upload for a 3-D texture -- and it
	// needs the second stride. Kept apart from Map_Texture rather than folded into it
	// because a caller that has a volume knows it has one, and a GfxMappedRect that
	// silently dropped the slice pitch would be a correct-looking wrong answer.
	virtual bool			Map_Volume_Texture(GfxTexture * texture, unsigned level,
								GfxMapMode mode, GfxMappedBox & mapped) = 0;
	virtual void			Unmap_Volume_Texture(GfxTexture * texture, unsigned level) = 0;
	// A cube face is addressed by index 0..5 in the order +X -X +Y -Y +Z -Z, which is
	// the order both APIs use and the order the engine's own loader writes them in.
	virtual bool			Map_Cube_Texture(GfxTexture * texture, unsigned face, unsigned level,
								const GfxRect * rect, GfxMapMode mode, GfxMappedRect & mapped) = 0;
	virtual void			Unmap_Cube_Texture(GfxTexture * texture, unsigned face, unsigned level) = 0;

	// ---- describing a resource -------------------------------------------
	//
	// What a surface is, said in the neutral vocabulary. These are what break the
	// D3DSURFACE_DESC round trip: the engine used to read a description off a surface
	// the API had handed it and feed the Format and MultiSampleType fields straight
	// back into a creation call, so its idea of "this format" was whatever D3D9 had
	// written in a struct. No second backend can implement that. It can implement
	// these.
	//
	// Describe_Texture_Level answers for a 2-D texture and for a cube face, both of
	// which have exactly this shape. A volume level has a third dimension and gets its
	// own call rather than a Depth field nothing else would ever set.
	virtual bool			Describe_Surface(GfxSurface * surface, WW3DSurfaceDescription & desc) = 0;
	virtual bool			Describe_Texture_Level(GfxTexture * texture, unsigned level,
								WW3DSurfaceDescription & desc) = 0;
	virtual bool			Describe_Volume_Level(GfxTexture * texture, unsigned level,
								WW3DSurfaceDescription & desc, unsigned & depth) = 0;
	// A depth texture's level, whose format is a depth format and so has no WW3DFormat
	// spelling at all. Asking Describe_Texture_Level for one and reading the answer as a
	// colour format is how a Z texture comes back as WW3D_FORMAT_UNKNOWN.
	virtual bool			Describe_Depth_Texture_Level(GfxTexture * texture, unsigned level,
								WW3DZFormat & format) = 0;

	// ---- transfers and queries -------------------------------------------

	// One call for D3D9's UpdateSurface and StretchRect: the difference between
	// them is whether the rectangles are the same size, which the backend can see
	// for itself.
	virtual bool			Copy_Surface(GfxSurface * source, const GfxRect * source_rect,
								GfxSurface * dest, const GfxRect * dest_rect) = 0;
	// A rectangle-to-rectangle copy, with the caller saying whether the pixels may be
	// resampled on the way. Copy_Surface above is the fast path and will take whatever
	// route the API offers; this one is the two callers that care about the answer --
	// SurfaceClass::Copy must not filter, SurfaceClass::Stretch_Copy must -- and folding
	// them together would silently change what one of them produces.
	//
	// Under D3D9 both are D3DXLoadSurfaceFromSurface, which is one of the D3DX services
	// with no D3D11 counterpart: over there this becomes a shader pass or a CPU convert.
	// Naming the intent here is what makes that a decision a backend gets to make.
	virtual bool			Copy_Surface_Rect(GfxSurface * source, const GfxRect * source_rect,
								GfxSurface * dest, const GfxRect * dest_rect,
								GfxCopyFilter filter) = 0;
	virtual bool			Update_Texture(GfxTexture * source, GfxTexture * dest) = 0;
	// Fill in every mip below base_level from the level above it. Five callers build a
	// texture atlas a tile at a time and then ask for this; under D3D9 it is D3DXFilterTexture
	// with a box filter, which is what every one of them asked for by name, and under
	// D3D11 it is GenerateMips with its own rules. Naming the intent is what lets those
	// be different.
	virtual bool			Generate_Mips(GfxTexture * texture, unsigned base_level) = 0;
	// How many of the largest mip levels to leave unused. This is the texture-reduction
	// setting, and it is a property of a texture rather than of a draw.
	virtual void			Set_Texture_Detail_Level(GfxTexture * texture, unsigned skip_levels) = 0;
	virtual bool			Capture_Front_Buffer(GfxSurface * dest) = 0;

	virtual bool			Get_Display_Mode(unsigned & width, unsigned & height, WW3DFormat & format) = 0;
	virtual unsigned		Get_Available_Texture_Memory() = 0;

	// A hint that the backend may drop whatever it is holding for resources nothing
	// has asked for lately. D3D9 evicts its managed pool; a backend with no such
	// pool does nothing.
	virtual void			Trim_Resource_Memory() = 0;

	virtual void			Set_Gamma_Ramp(const void * ramp, bool calibrate) = 0;

	// The hardware cursor: an image, and the point in it the pointer actually is.
	//
	// This is the least portable thing left in this interface and it is here so that a
	// backend can say no. D3D9 draws a cursor; D3D11 has no cursor concept at all, and
	// what replaces it is either the Win32 cursor or a quad the game draws itself.
	// W3DMouse already has a software path -- RM_POLYGON -- so a backend answering
	// false here is not a missing feature, it is the other path.
	virtual bool			Set_Hardware_Cursor(GfxSurface * image, unsigned hot_x, unsigned hot_y) = 0;
	virtual void			Show_Hardware_Cursor(bool show) = 0;
	virtual void			Set_Hardware_Cursor_Position(unsigned x, unsigned y) = 0;

	// Write a surface out as an image file, which is what the screenshot and the frame
	// dump are built on. Every measurement in this port has come through here.
	virtual bool			Save_Surface_To_File(const char * path, GfxSurface * surface) = 0;

	// Debug only: asks whether the current state can be drawn in one pass. There is
	// no obligation to answer -- a backend that cannot returns false.
	virtual bool			Validate_Draw_State(unsigned & passes) = 0;
};

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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : ww3d                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/dx8wrapper.h                           $*
 *                                                                                             *
 *              Original Author:: Jani Penttinen                                               *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               *
 *                                                                                             *
 *                     $Modtime:: 08/05/02 2:40p                                              $*
 *                                                                                             *
 *                    $Revision:: 92                                                          $*
 *                                                                                             *
 * 06/26/02 KM Matrix name change to avoid MAX conflicts                                       *
 * 06/27/02 KM Render to shadow buffer texture support														*
 * 08/05/02 KM Texture class redesign
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once

#include "WWLib/always.h"
#include "dllist.h"
#include "d3d9_compat.h"
#include "gfxdevice.h"
#include "WWMath/matrix4.h"
#include "statistics.h"
#include "WWLib/wwstring.h"
#include "WW3D2/lightenvironment.h"
#include "WW3D2/debugvis.h"
#include "WW3D2/meshtechnique.h"
#include "WW3D2/shader.h"
#include "WWMath/vector4.h"
#include "WWLib/cpudetect.h"
#include "dx8caps.h"

#include "texture.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "WW3D2/vertmaterial.h"

/*
** Registry value names
*/
#define	VALUE_NAME_RENDER_DEVICE_NAME					"RenderDeviceName"
#define	VALUE_NAME_RENDER_DEVICE_WIDTH				"RenderDeviceWidth"
#define	VALUE_NAME_RENDER_DEVICE_HEIGHT				"RenderDeviceHeight"
#define	VALUE_NAME_RENDER_DEVICE_DEPTH				"RenderDeviceDepth"
#define	VALUE_NAME_RENDER_DEVICE_WINDOWED			"RenderDeviceWindowed"
#define	VALUE_NAME_RENDER_DEVICE_TEXTURE_DEPTH		"RenderDeviceTextureDepth"

const unsigned MAX_TEXTURE_STAGES=8;
const unsigned MAX_VERTEX_STREAMS=2;
const unsigned MAX_VERTEX_SHADER_CONSTANTS=96;
// The PBR pixel shader uses c0-c11 (lights, ambient, camera, material, opacity), c12-c15
// (sun view-projection), c16 (shadow bias/strength), c17 (SSR params), c18-c21 (camera
// view-projection, for the SSR reprojection) and c23 (shadow normal offset + mesh
// bias). This is the shadow-cache size;
// keep it above the highest register any pixel shader writes, or
// Set_Pixel_Shader_Constant memcpys past the array and corrupts adjacent statics.
const unsigned MAX_PIXEL_SHADER_CONSTANTS=32;

// Alpha test reference and compare direction, for the shaders that do their own cutout.
// Written once per draw by DX8Wrapper::Draw out of the tracked render states; read by
// every pixel shader that includes alphatest.hlsli, which declares the matching
// register. The two have to agree, so change them together.
//
// c28 was chosen because it is one of only three registers (c28, c29, c30) that no
// pixel shader already claims: unit_pbr_ps reaches c25, water_ps c27 and debugtint_ps
// c31, counting the four registers a float4x4 occupies rather than the one it is
// declared at. It is inside MAX_PIXEL_SHADER_CONSTANTS, which is what keeps the shadow
// cache from being written past -- see the warning above.
const unsigned ALPHA_TEST_PS_CONSTANT=28;

const unsigned MAX_SHADOW_MAPS=1;

enum {
	BUFFER_TYPE_DX8,
	BUFFER_TYPE_SORTING,
	BUFFER_TYPE_DYNAMIC_DX8,
	BUFFER_TYPE_DYNAMIC_SORTING,
	BUFFER_TYPE_INVALID
};

class VertexMaterialClass;
class CameraClass;
class LightEnvironmentClass;
class RenderDeviceDescClass;
class VertexBufferClass;
class DynamicVBAccessClass;
class IndexBufferClass;
class DynamicIBAccessClass;
class TextureClass;
class LightClass;
class SurfaceClass;

struct DX8FrameStatistics
{
	DX8FrameStatistics() :
		matrix_changes(0),
		material_changes(0),
		vertex_buffer_changes(0),
		index_buffer_changes(0),
		texture_changes(0),
		render_state_changes(0),
		texture_stage_state_changes(0),
		dx8_calls(0),
		draw_calls(0)
	{
	}

	unsigned matrix_changes;
	unsigned material_changes;
	unsigned vertex_buffer_changes;
	unsigned index_buffer_changes;
	unsigned texture_changes;
	unsigned render_state_changes;
	unsigned texture_stage_state_changes;
	unsigned dx8_calls;
	unsigned draw_calls;
};

#define DX8_RECORD_MATRIX_CHANGE()				FrameStatistics.matrix_changes++
#define DX8_RECORD_MATERIAL_CHANGE()			FrameStatistics.material_changes++
#define DX8_RECORD_VERTEX_BUFFER_CHANGE()		FrameStatistics.vertex_buffer_changes++
#define DX8_RECORD_INDEX_BUFFER_CHANGE()		FrameStatistics.index_buffer_changes++
#define DX8_RECORD_TEXTURE_CHANGE()				FrameStatistics.texture_changes++
#define DX8_RECORD_RENDER_STATE_CHANGE()		FrameStatistics.render_state_changes++
#define DX8_RECORD_TEXTURE_STAGE_STATE_CHANGE() FrameStatistics.texture_stage_state_changes++
#define DX8_RECORD_DX8_CALLS()					FrameStatistics.dx8_calls++
#define DX8_RECORD_DRAW_CALLS()					FrameStatistics.draw_calls++

extern bool _DX8SingleThreaded;

void DX8_Assert();
void Log_DX8_ErrorCode(unsigned res);

WWINLINE void DX8_ErrorCode(unsigned res)
{
	if (res==D3D_OK) return;
	Log_DX8_ErrorCode(res);
}

// DX8CALL, DX8CALL_HRES and DX8CALL_D3D used to be here: "call this method on the
// device", with the error check and the call count wrapped around it. They are gone
// because there is no device on this side of the seam to call one on -- GFXCALL below
// is what replaced them, and the last DX8CALL in the tree went with the additional
// swap chain.
#ifdef WWDEBUG
#define DX8_THREAD_ASSERT() if (_DX8SingleThreaded) { WWASSERT_PRINT(DX8Wrapper::_Get_Main_Thread_ID()==ThreadClass::_Get_Current_Thread_ID(),"DX8Wrapper::DX8 calls must be called from the main thread!"); }
#else
#define DX8_THREAD_ASSERT() ;
#endif

// The same shape as DX8CALL, one indirection further out: the call goes to whatever
// backend is bound instead of straight at a D3D9 device. The error check and the call
// count both moved into the backend, which is the only side that knows how many device
// calls one of these turns into -- binding a vertex format is two in D3D9 and one
// anywhere else.
#ifdef WWDEBUG
#define GFXCALL(x) DX8_Assert(); DX8Wrapper::Gfx->x;
#else
#define GFXCALL(x) DX8Wrapper::Gfx->x;
#endif




// This virtual interface was added for the Generals RTS.
// It is called before resetting the dx8 device to ensure
// that all dx8 resources are released.  Otherwise reset fails. jba.
class DX8_CleanupHook
{
public:
	virtual void ReleaseResources()=0;
	virtual void ReAcquireResources()=0;
};


struct RenderStateStruct
{
	ShaderClass shader;
	VertexMaterialClass* material;
	TextureBaseClass * Textures[MAX_TEXTURE_STAGES];
	D3DLIGHT8 Lights[4];
	bool LightEnable[4];
	D3DMATRIX world;
	D3DMATRIX view;
	unsigned vertex_buffer_types[MAX_VERTEX_STREAMS];
	unsigned index_buffer_type;
	unsigned short vba_offset;
	unsigned short vba_count;
	unsigned short iba_offset;
	VertexBufferClass* vertex_buffers[MAX_VERTEX_STREAMS];
	IndexBufferClass* index_buffer;
	unsigned short index_base_offset;

	RenderStateStruct();
	~RenderStateStruct();

	RenderStateStruct& operator= (const RenderStateStruct& src);
};

/**
** DX8Wrapper
**
** DX8 interface wrapper class.  This encapsulates the DX8 interface; adding redundant state
** detection, stat tracking, etc etc.  In general, we will wrap all DX8 calls with at least
** an WWINLINE function so that we can add stat tracking, etc if needed.  Direct access to the
** D3D device will require "friend" status and should be granted only in extreme circumstances :-)
*/
class DX8Wrapper
{
	enum ChangedStates {
		WORLD_CHANGED	=	1<<0,
		VIEW_CHANGED	=	1<<1,
		LIGHT0_CHANGED	=	1<<2,
		LIGHT1_CHANGED	=	1<<3,
		LIGHT2_CHANGED	=	1<<4,
		LIGHT3_CHANGED	=	1<<5,
		TEXTURE0_CHANGED=	1<<6,
		TEXTURE1_CHANGED=	1<<7,
		TEXTURE2_CHANGED=	1<<8,
		TEXTURE3_CHANGED=	1<<9,
		MATERIAL_CHANGED=	1<<14,
		SHADER_CHANGED	=	1<<15,
		VERTEX_BUFFER_CHANGED = 1<<16,
		INDEX_BUFFER_CHANGED = 1 << 17,
		WORLD_IDENTITY=	1<<18,
		VIEW_IDENTITY=		1<<19,
		// A texture coordinate source, texture transform flag or texture matrix has been
		// written straight into the tracked state, outside render_state.
		//
		// Those writes are inputs to the routing block, which reproduces the fixed-function
		// texgen in the vertex shader -- but nothing used to mark them, so a decision taken
		// before them stayed in force. A caller that applies its state, then writes its
		// texgen, then draws (ShroudTextureShader::set does exactly that, and must, so the
		// material's own Apply cannot overwrite it again) had its shader constants chosen
		// while the texgen was still invisible: TexGenCtl went up as zero and the projection
		// was dropped, leaving the shader sampling with the mesh's own UVs.
		//
		// Nothing else re-runs on this bit. Every sub-apply above the routing block is gated
		// on its own flag, so the material and the shader are not re-applied and cannot
		// clobber the very state that set this.
		TEXGEN_STATE_CHANGED = 1<<20,

		TEXTURES_CHANGED=
			TEXTURE0_CHANGED|TEXTURE1_CHANGED|TEXTURE2_CHANGED|TEXTURE3_CHANGED,
		LIGHTS_CHANGED=
			LIGHT0_CHANGED|LIGHT1_CHANGED|LIGHT2_CHANGED|LIGHT3_CHANGED,
	};

	static void Draw_Sorting_IB_VB(
		unsigned primitive_type,
		unsigned short start_index,
		unsigned short polygon_count,
		unsigned short min_vertex_index,
		unsigned short vertex_count);

	static void Draw(
		unsigned primitive_type,
		unsigned short start_index,
		unsigned short polygon_count,
		unsigned short min_vertex_index=0,
		unsigned short vertex_count=0);

public:

	static bool Init(void * hwnd, bool lite = false);
	static void Shutdown();

	static void SetCleanupHook(DX8_CleanupHook *pCleanupHook) {m_pCleanupHook = pCleanupHook;};
	/*
	** Some WW3D sub-systems need to be initialized after the device is created and shutdown
	** before the device is released.
	*/
	static void	Do_Onetime_Device_Dependent_Inits();
	static void Do_Onetime_Device_Dependent_Shutdowns();

	static bool Is_Device_Lost() { return IsDeviceLost; }
	static bool Is_Initted() { return IsInitted; }

	static bool Has_Stencil ();
	static void Get_Format_Name(unsigned int format, StringClass *tex_format);
	// How many bytes a surface of this shape occupies. Said in the engine's own format
	// vocabulary: the two callers both have a WW3DSurfaceDescription now, and the answer
	// is arithmetic about a pixel layout rather than about an API.
	static unsigned int Get_Surface_Size(const WW3DSurfaceDescription& desc);

	/*
	** Rendering
	*/
	static void Begin_Scene();
	static void End_Scene(bool flip_frame = true);

	// ---------------------------------------------------------------------------
	// In-game debug visualization (see debugvis.h). The mode is held here because it
	// is applied by Apply_Render_State_Changes, which nothing above this layer can
	// reach into, while the game layer switches it through Display::
	// cycleDebugVisualization. One owner, so the two cannot disagree about which
	// mode is on.
	//
	// Set_Debug_Vis_Mode invalidates the cached shader states, and it must:
	// DEBUG_VIS_OVERDRAW writes texture-stage state that ShaderClass believes it has
	// already applied, so without the invalidate the flat colour survives the mode
	// being switched off, for as long as the same shader stays current.
	static DebugVisMode					m_debugVisMode;
	static void Set_Debug_Vis_Mode(DebugVisMode mode);
	static DebugVisMode Get_Debug_Vis_Mode() { return m_debugVisMode; }
	// The flat-shading pixel shader the tinting modes bind in place of whatever a
	// programmable draw would otherwise have used. Zero if it failed to load, in which
	// case programmable draws are left alone and only fixed-function ones are tinted.
	static DWORD						m_dwDebugTintPS;
	// DEBUG_VIS_NORMALS substitutes this pair for unit_vs/unit_ps. A vertex shader of
	// its own is unavoidable: unit_vs lights per vertex and forwards the result, so the
	// normal no longer exists by the pixel stage. See debugnormal_vs.hlsl, which
	// deliberately declares the same matrix registers as unit_vs so the substitution
	// needs no upload.
	static DWORD						m_dwDebugNormalVS;
	static DWORD						m_dwDebugNormalPS;
	// Constant register the tint colour is handed over in. Well above every register any
	// real pixel shader here declares, so setting it cannot disturb a shader that is
	// about to be replaced anyway -- or one that is not, when the tint shader is missing.
	enum { DEBUG_TINT_PS_REGISTER = 31 };
	// Replace this draw's shading to say something about the draw. Called at the very end
	// of Apply_Render_State_Changes, after routing has bound everything, so that what it
	// reports is what the frame is really about to do rather than what it was asked to
	// do -- and so that ShaderClass::Apply, which runs earlier inside that function,
	// cannot overwrite the blend and depth state these modes depend on.
	//
	// `fixedFunction` is the routing's own verdict for this draw; `hasNormal` and
	// `onMeshPath` are what DEBUG_VIS_NORMALS needs to decide whether it can say
	// anything about this draw at all.
	static void Apply_Debug_Draw_Override(bool fixedFunction, bool hasNormal,
										  unsigned routeBit);
	// Make one draw come out a single flat colour on whichever pipeline is drawing it.
	// 0xAARRGGBB; the alpha reaches the shader and is left to the draw's own blend.
	static void Debug_Flat_Shade(unsigned color, bool fixedFunction);

	// TheSuperHackers @feature andytraber 17/08/2026
	// A callback run once, at the one instant in the frame where the finished back buffer
	// can be read: after EndScene, so the surface is no longer being rendered into and
	// copies off it are legal, and before Present, because the swap effect is
	// D3DSWAPEFFECT_DISCARD and the contents are undefined the moment the frame is
	// presented. The game layer has no point of its own between the two -- WW3D::End_Render
	// does both -- so it asks for the callback instead, and this stays free of any knowledge
	// of what the caller intends to do with the pixels.
	typedef void (*PostSceneCallbackFunc)(void* userData);
	static void Request_Post_Scene_Callback(PostSceneCallbackFunc func, void* userData);

	// Writes the sun's shadow map beside a frame dump. Deliberately available: a back
	// buffer with no visible shadows has two causes -- the map is empty, or the receivers
	// are not sampling it -- and they are indistinguishable from the back buffer alone.
	static bool Dump_Shadow_Map(const char* pathname);

	// Flip until the primary buffer is visible.
	static void Flip_To_Primary();

	static void Clear(bool clear_color, bool clear_z_stencil, const Vector3 &color, float dest_alpha=0.0f, float z=1.0f, unsigned int stencil=0);

	static void	Set_Viewport(CONST D3DVIEWPORT8* pViewport);

	static void Set_Vertex_Buffer(const VertexBufferClass* vb, unsigned stream=0);
	static void Set_Vertex_Buffer(const DynamicVBAccessClass& vba);
	static void Set_Index_Buffer(const IndexBufferClass* ib,unsigned short index_base_offset);
	static void Set_Index_Buffer(const DynamicIBAccessClass& iba,unsigned short index_base_offset);
	static void Set_Index_Buffer_Index_Offset(unsigned offset);

	static void Get_Render_State(RenderStateStruct& state);
	// One tracked light, without copying the whole render state and its texture
	// references. Used by the sorted-draw lighting census.
	static bool Peek_Light(unsigned index, D3DLIGHT8& light);
	static void Set_Render_State(const RenderStateStruct& state);
	static void Release_Render_State();

	static void Set_DX8_Material(const D3DMATERIAL8* mat);

	static void Set_Gamma(float gamma,float bright,float contrast,bool calibrate=true,bool uselimit=true);

	// Set_ and Get_Transform() functions take the matrix in Westwood convention format.

	static void Set_Projection_Transform_With_Z_Bias(const Matrix4x4& matrix,float znear, float zfar);	// pointer to 16 matrices

	static void Set_Transform(D3DTRANSFORMSTATETYPE transform,const Matrix4x4& m);
	static void Set_Transform(D3DTRANSFORMSTATETYPE transform,const Matrix3D& m);
	// Same, for a caller that already holds a D3DMATRIX -- deferred draws replay one they
	// captured. The distinction from _Set_DX8_Transform is not the argument type: this
	// updates the *tracked* world/view, which is where the programmable path reads the
	// matrices it concatenates on the CPU, while _Set_DX8_Transform reaches past it to the
	// device and so is visible only to the fixed-function pipeline.
	static void Set_Transform(D3DTRANSFORMSTATETYPE transform,const D3DMATRIX& m);
	static void Get_Transform(D3DTRANSFORMSTATETYPE transform, Matrix4x4& m);
	static void Set_World_Identity();
	static void Set_View_Identity();
	static bool Is_World_Identity();
	static bool Is_View_Identity();

	// Note that *_DX8_Transform() functions take the matrix in DX8 format - transposed from Westwood convention.

	static void _Set_DX8_Transform(D3DTRANSFORMSTATETYPE transform, const D3DMATRIX& m);
	static void _Get_DX8_Transform(D3DTRANSFORMSTATETYPE transform, D3DMATRIX& m);

	// The one place a matrix is handed to the device. Every path that writes a transform
	// goes through here -- _Set_DX8_Transform, the three Set_Transform overloads and
	// Set_Projection_Transform_With_Z_Bias -- so there is a single point at which what was
	// sent can be recorded, which is what the device-state audit compares against.
	static void Send_Transform_To_Device(unsigned which, const D3DMATRIX& m);
	// Which deferred slot a transform belongs in, and back again. -1 for a transform the
	// engine never writes -- D3DTS_WORLD1..3 and the rest of D3D's 256 world matrices --
	// which is sent straight through rather than given a slot it would never reuse.
	static int      FF_Transform_Slot(unsigned which);
	static unsigned FF_Transform_Which(unsigned slot);

	// Raise TEXGEN_STATE_CHANGED when the matrix just written was a texture stage's.
	//
	// _Set_DX8_Transform does this itself, but the three Set_Transform overloads reach the
	// device directly for anything that is not world/view/projection -- and that default
	// branch is how *every* mapper writes its texture matrix (mapper.cpp, matrixmapper.cpp).
	// The routing block uploads that matrix to the vertex shader, so writing one must
	// invalidate a decision already taken, or a draw keeps constants chosen before the
	// matrix existed. See TEXGEN_STATE_CHANGED.
	static void Note_Texture_Transform_Write(D3DTRANSFORMSTATETYPE transform);

	static void Set_DX8_Render_State(D3DRENDERSTATETYPE state, unsigned value);
	static void Set_DX8_Clip_Plane(DWORD Index, CONST float* pPlane);
	static void Set_DX8_Texture_Stage_State(unsigned stage, D3DTEXTURESTAGESTATETYPE state, unsigned value);
	// The same write with no sampler guard. Set_Sampler decomposes a description through
	// this, and the initial-state loop states the two addressing defaults through it; no
	// third caller should exist.
	static void Set_DX8_Stage_State_Unguarded(unsigned stage, D3DTEXTURESTAGESTATETYPE state, unsigned value);
	// The seven stage states the sampler description owns. Writing one of them directly is
	// how the engine used to say "linear, clamped", and is what Set_Sampler replaced.
	static bool Is_Sampler_Stage_State(unsigned state);
	static void Set_DX8_Texture(unsigned int stage, GfxTexture* texture);

	/*
	** How the texture at a slot is sampled, as one description rather than ten indexed
	** state words.
	**
	** Set_Texture says *which* texture; this says *how*. They were the same call under
	** D3D9 because a texture stage is both, and separating them is the piece of work that
	** has to happen in the engine before a backend can bind an SRV at t# and a sampler
	** object at s#. The slot number is shared by convention, not by necessity.
	**
	** Callers state a whole sampler, and the ordinary way to build one is to read the
	** slot's current description and change what this pass means to change:
	**
	**     SamplerStateClass s = DX8Wrapper::Get_Sampler(stage);
	**     s.Set_Filter(SamplerStateClass::FILTER_LINEAR, SamplerStateClass::FILTER_LINEAR);
	**     s.Set_Address(SamplerStateClass::ADDRESS_CLAMP, SamplerStateClass::ADDRESS_CLAMP);
	**     DX8Wrapper::Set_Sampler(stage, s);
	**
	** That is deliberate rather than convenient. Filtering is inherited state in this
	** engine -- a pass that sets minification and magnification and says nothing about the
	** mip filter really does mean "leave the mip filter as it is" -- and a description
	** built from nothing would silently overwrite what the previous pass established.
	** Reading first makes the inheritance explicit and keeps it exact.
	*/
	static const SamplerStateClass & Get_Sampler(unsigned stage);
	static void Set_Sampler(unsigned stage, const SamplerStateClass & sampler);

	/*
	** The direct drawers' entry points.
	**
	** A handful of subsystems build their own vertex and index buffers and submit them
	** themselves -- the water grid, the shadow volumes, the shadow decal batch, the
	** screen-space filters, the snow. They announce it with Prepare_Direct_Draw, and they
	** are not going to stop, because what they draw does not fit the mesh path. What they
	** must not do is reach around the wrapper to a device, which is a hole in the seam and
	** is also how the base-vertex-index and stream-binding regressions happened. These are
	** the same submissions, through the same backend as every other draw in the game.
	*/
	static void Set_DX8_Stream_Source(unsigned stream, GfxVertexBuffer* buffer, unsigned stride);
	static void Set_DX8_Indices(GfxIndexBuffer* buffer, int base_vertex_index);
	static void Draw_DX8_Indexed_Primitive(unsigned primitive_type, int base_vertex_index,
					unsigned min_vertex_index, unsigned vertex_count,
					unsigned start_index, unsigned primitive_count);
	static void Draw_DX8_Primitive(unsigned primitive_type, unsigned start_vertex,
					unsigned primitive_count);
	static void Draw_DX8_Primitive_UP(unsigned primitive_type, unsigned primitive_count,
					const void* vertex_data, unsigned vertex_stride);

	/*
	** Device reads for the callers that genuinely need one. A tracked read is not a device
	** read -- see the colour-write and ZBIAS traps in device-escape-classification -- so
	** these stay reads, they just stop being reads of a D3D device in particular.
	*/
	static bool Get_DX8_Render_State(unsigned state, unsigned& value);
	static GfxSurface* Get_DX8_Render_Target_Surface(unsigned index);
	static GfxSurface* Get_DX8_Depth_Target_Surface();
	static bool Get_DX8_Viewport(D3DVIEWPORT8& viewport);
	static bool Copy_DX8_Surface(GfxSurface* source, GfxSurface* dest);
	// Rectangle to rectangle, taking whatever route the backend has for it.
	static bool Copy_DX8_Surface(GfxSurface* source, const GfxRect* source_rect,
		GfxSurface* dest, const GfxRect* dest_rect);

	/*
	** What a surface is, in the engine's own vocabulary rather than in D3D9's. Callers
	** that used to read a D3DSURFACE_DESC off a surface and hand its Format straight back
	** into a creation call go through these instead: see the note at Describe_Surface in
	** gfxdevice.h for why a round trip through an API struct is the thing that stops a
	** second backend existing.
	*/
	/*
	** Buffers. Creation says what the buffer is for -- see GfxResourceUsage -- and never
	** where to put it, because a pool is the one thing that cannot cross a seam. Mapping
	** states the caller's intent, which is a hint under D3D9 and a rule under D3D11.
	*/
	static GfxVertexBuffer* Create_DX8_Vertex_Buffer(unsigned size_in_bytes,
		unsigned fvf, unsigned usage);
	static GfxIndexBuffer* Create_DX8_Index_Buffer(unsigned index_count, unsigned usage);
	static void Release_DX8_Vertex_Buffer(GfxVertexBuffer* buffer);
	static void Release_DX8_Index_Buffer(GfxIndexBuffer* buffer);

	static bool Map_DX8_Vertex_Buffer(GfxVertexBuffer* buffer, unsigned offset_in_bytes,
		unsigned size_in_bytes, GfxMapMode mode, void** data);
	static void Unmap_DX8_Vertex_Buffer(GfxVertexBuffer* buffer);
	static bool Map_DX8_Index_Buffer(GfxIndexBuffer* buffer, unsigned offset_in_bytes,
		unsigned size_in_bytes, GfxMapMode mode, void** data);
	static void Unmap_DX8_Index_Buffer(GfxIndexBuffer* buffer);

	static bool Describe_DX8_Surface(GfxSurface* surface, WW3DSurfaceDescription& desc);
	static bool Describe_DX8_Texture_Level(GfxTexture* texture, unsigned level,
		WW3DSurfaceDescription& desc);
	static bool Describe_DX8_Volume_Level(GfxTexture* texture, unsigned level,
		WW3DSurfaceDescription& desc, unsigned& depth);

	/*
	** Textures and surfaces, on the same terms as the buffers above: what it is for and
	** what is in it, never which pool. These are declared in the opaque handle types
	** rather than in D3D9's, because a caller that has to name GfxTexture to hold
	** the result has not actually stopped depending on D3D9 -- and naming it in a header
	** is what makes the dependency a class contract instead of a call site.
	*/
	static void Reference_DX8_Texture(GfxTexture* texture);
	static GfxTexture* Create_DX8_Texture_Resource(unsigned width, unsigned height,
		unsigned levels, WW3DFormat format, unsigned usage);
	static GfxTexture* Create_DX8_Cube_Texture_Resource(unsigned edge_length, unsigned levels,
		WW3DFormat format, unsigned usage);
	static void Release_DX8_Texture_Resource(GfxTexture* texture);
	/*
	** Release and forget, in one call that picks the right one by the handle's type.
	** Every one of these resources was being freed through a SAFE_RELEASE that called
	** Release() on it -- which is a method, and an opaque handle has none. Two overloads
	** rather than one macro so that handing a texture to the surface path is a compile
	** error rather than a wrong vtable slot.
	*/
	static void Release_DX8_Resource(GfxTexture*& texture);
	static void Release_DX8_Resource(GfxSurface*& surface);
	static GfxSurface* Create_DX8_Render_Target_Surface(unsigned width, unsigned height,
		WW3DFormat format, WW3DMultiSampleType multisample);
	static GfxSurface* Create_DX8_Depth_Stencil_Surface(unsigned width, unsigned height,
		WW3DZFormat format, WW3DMultiSampleType multisample);
	static GfxSurface* Create_DX8_Offscreen_Surface(unsigned width, unsigned height,
		WW3DFormat format);
	static void Release_DX8_Surface_Resource(GfxSurface* surface);
	static void Reference_DX8_Surface(GfxSurface* surface);
	static bool Copy_DX8_Surface_Rect(GfxSurface* source, const GfxRect* source_rect,
		GfxSurface* dest, const GfxRect* dest_rect, GfxCopyFilter filter);
	static unsigned Get_DX8_Texture_Level_Count(GfxTexture* texture);
	static bool Generate_DX8_Mips(GfxTexture* texture, unsigned base_level);
	static void Set_DX8_Texture_Detail_Level(GfxTexture* texture, unsigned skip_levels);
	static bool Set_DX8_Hardware_Cursor(GfxSurface* image, unsigned hot_x, unsigned hot_y);
	static void Show_DX8_Hardware_Cursor(bool show);
	static void Set_DX8_Hardware_Cursor_Position(unsigned x, unsigned y);
	static bool Save_DX8_Surface_To_File(const char* path, GfxSurface* surface);
	// Hands back a reference; give it to Release_DX8_Surface_Resource.
	static GfxSurface* Get_DX8_Texture_Surface_Level(GfxTexture* texture, unsigned level);
	static bool Map_DX8_Texture(GfxTexture* texture, unsigned level, const GfxRect* rect,
		GfxMapMode mode, GfxMappedRect& mapped);
	static void Unmap_DX8_Texture(GfxTexture* texture, unsigned level);
	static bool Map_DX8_Surface(GfxSurface* surface, const GfxRect* rect, GfxMapMode mode,
		GfxMappedRect& mapped);
	static void Unmap_DX8_Surface(GfxSurface* surface);
	static bool Map_DX8_Volume_Texture(GfxTexture* texture, unsigned level, GfxMapMode mode,
		GfxMappedBox& mapped);
	static void Unmap_DX8_Volume_Texture(GfxTexture* texture, unsigned level);
	static bool Map_DX8_Cube_Texture(GfxTexture* texture, unsigned face, unsigned level,
		const GfxRect* rect, GfxMapMode mode, GfxMappedRect& mapped);
	static void Unmap_DX8_Cube_Texture(GfxTexture* texture, unsigned face, unsigned level);
	static bool Describe_DX8_Depth_Texture_Level(GfxTexture* texture, unsigned level,
		WW3DZFormat& format);

	/*
	** Whether the backend is in a state to be drawn to. Four subsystems check this before
	** touching the terrain, the shroud or a view, and all four used to ask a D3D9 device
	** whether it had been taken away by another application. That question has a
	** D3D9-shaped answer and a general one, and this is the general one: false means do
	** not draw this frame.
	*/
	static bool Is_Device_Ready() { return Gfx != nullptr && Gfx->Get_Device_Status() == GFX_DEVICE_OK; }
	static void Set_Light_Environment(LightEnvironmentClass* light_env);
	static LightEnvironmentClass* Get_Light_Environment() { return Light_Environment; }
	static void Set_Fog(bool enable, const Vector3 &color, float start, float end);

	// Deferred

	static void Set_Shader(const ShaderClass& shader);
	static void Get_Shader(ShaderClass& shader);
	static void Set_Texture(unsigned stage,TextureBaseClass* texture);
	static void Set_Material(const VertexMaterialClass* material);
	static void Set_Light(unsigned index,const D3DLIGHT8* light);
	static void Set_Light(unsigned index,const LightClass &light);

	static void Apply_Render_State_Changes();	// Apply deferred render state changes (will be called automatically by Draw...)

	static void Draw_Triangles(
		unsigned buffer_type,
		unsigned short start_index,
		unsigned short polygon_count,
		unsigned short min_vertex_index,
		unsigned short vertex_count);
	static void Draw_Triangles(
		unsigned short start_index,
		unsigned short polygon_count,
		unsigned short min_vertex_index,
		unsigned short vertex_count);
	static void Draw_Strip(
		unsigned short start_index,
		unsigned short index_count,
		unsigned short min_vertex_index,
		unsigned short vertex_count);

	/*
	** Resources
	*/

	static GfxTexture* _Create_DX8_Volume_Texture
	(
		unsigned int width,
		unsigned int height,
		unsigned int depth,
		WW3DFormat format,
		MipCountType mip_level_count,
		D3DPOOL pool=D3DPOOL_MANAGED
	);

	static GfxTexture* _Create_DX8_Cube_Texture
	(
		unsigned int width,
		unsigned int height,
		WW3DFormat format,
		MipCountType mip_level_count,
		D3DPOOL pool=D3DPOOL_MANAGED,
		bool rendertarget=false
	);


	static GfxTexture* _Create_DX8_ZTexture
	(
		unsigned int width,
		unsigned int height,
		WW3DZFormat zformat,
		MipCountType mip_level_count,
		D3DPOOL pool=D3DPOOL_MANAGED
	);


	static GfxTexture * _Create_DX8_Texture
	(
		unsigned int width,
		unsigned int height,
		WW3DFormat format,
		MipCountType mip_level_count,
		D3DPOOL pool=D3DPOOL_MANAGED,
		bool rendertarget=false
	);
	static GfxTexture * _Create_DX8_Texture(const char *filename, MipCountType mip_level_count);
	static GfxTexture * _Create_DX8_Texture(GfxSurface *surface, MipCountType mip_level_count);

	static GfxSurface * _Create_DX8_Surface(unsigned int width, unsigned int height, WW3DFormat format);
	static GfxSurface * _Create_DX8_Surface(const char *filename);
	static GfxSurface * _Get_DX8_Front_Buffer();
	static SurfaceClass * _Get_DX8_Back_Buffer(unsigned int num=0);
	// The colour surface being drawn into right now, which is the back buffer only when
	// nothing has redirected the scene. Anything reading back what the frame has drawn so
	// far wants this and not the back buffer -- see W3DSmudgeManager::render.
	static SurfaceClass * _Get_DX8_Render_Target();

	static HRESULT _Copy_DX8_Rects(
			GfxSurface* pSourceSurface,
			CONST RECT* pSourceRectsArray,
			UINT cRects,
			GfxSurface* pDestinationSurface,
			CONST POINT* pDestPointsArray
	);


	static HRESULT Set_DX8_Render_Target(
			GfxSurface* pRenderTarget,
			GfxSurface* pNewZStencil
	);

	static void _Update_Texture(TextureClass *system, TextureClass *video);
	static void Flush_DX8_Resource_Manager(unsigned int bytes=0);
	static unsigned int Get_Free_Texture_RAM();

	static unsigned _Get_Main_Thread_ID() { return _MainThreadID; }

	/*
	** Statistics
	*/
	static void Begin_Statistics();
	static void End_Statistics();
	static const DX8FrameStatistics& Get_Last_Frame_Statistics();
	static unsigned long Get_FrameCount();
	static void Increment_DX8_CallCount() { DX8_RECORD_DX8_CALLS(); }

	// Needed by shader class
	static bool						Get_Fog_Enable() { return FogEnable; }
	static D3DCOLOR				Get_Fog_Color() { return FogColor; }

	// Utilities
	static Vector4 Convert_Color(unsigned color);
	static unsigned int Convert_Color(const Vector4& color);
	static unsigned int Convert_Color(const Vector3& color, const float alpha);
	static void Clamp_Color(Vector4& color);
	static unsigned int Convert_Color_Clamp(const Vector4& color);

	static void			  Set_Alpha (const float alpha, unsigned int &color);

	static void _Enable_Triangle_Draw(bool enable) { _EnableTriangleDraw=enable; }
	static bool _Is_Triangle_Draw_Enabled() { return _EnableTriangleDraw; }

	/*
	** Render target interface. If render target format is WW3D_FORMAT_UNKNOWN, current display format is used.
	*/
	static TextureClass *	Create_Render_Target (int width, int height, WW3DFormat format = WW3D_FORMAT_UNKNOWN);

	static void					Set_Render_Target (GfxSurface *render_target, bool use_default_depth_buffer = false);
	static void					Set_Render_Target (GfxSurface* render_target, GfxSurface* dpeth_buffer);

	static bool					Is_Render_To_Texture() { return IsRenderToTexture; }

	// for depth map support KJM V
	static void Create_Render_Target
	(
		int width,
		int height,
		WW3DFormat format,
		WW3DZFormat zformat,
		TextureClass** target,
		ZTextureClass** depth_buffer
	);
	static void					Set_Render_Target_With_Z (TextureClass * texture, ZTextureClass* ztexture=nullptr);

	static void Set_Shadow_Map(int idx, ZTextureClass* ztex) { Shadow_Map[idx]=ztex; }
	static ZTextureClass* Get_Shadow_Map(int idx) { return Shadow_Map[idx]; }
	// for depth map support KJM ^

	// shader system updates KJM v
	static void Apply_Default_State();

	static void Set_Vertex_Shader(DWORD vertex_shader);
	static void Set_Pixel_Shader(DWORD pixel_shader);

	// Compiled bytecode in, a bindable handle out; 0 means the device refused it. The
	// subsystems that own a shader of their own -- the water, the trees, the profiler
	// capture, the shader manager's loader -- went to the device for these, and they were
	// the last device calls left in those files that were not caps or queries.
	static DWORD Create_Vertex_Shader(const void * bytecode, unsigned size);
	static DWORD Create_Pixel_Shader(const void * bytecode, unsigned size);
	static void Release_Vertex_Shader(DWORD vertex_shader);
	static void Release_Pixel_Shader(DWORD pixel_shader);
	// What is bound right now. For the callers that draw straight on the device after
	// Apply_Render_State_Changes: whatever it left standing is what rasterises them.
	static DWORD Get_Vertex_Shader() { return Vertex_Shader; }
	static DWORD Get_Pixel_Shader()  { return Pixel_Shader; }
	// Call immediately before drawing on the device yourself. Setting your own FVF does
	// not unbind the pixel shader; this does. See the definition for what it cost.
	static void Force_Fixed_Function_Pipeline();

	static void Set_Vertex_Shader_Constant(int reg, const void* data, int count);
	static void Set_Pixel_Shader_Constant(int reg, const void* data, int count);


	// Needed by scene lighting class
	static void						Set_Ambient(const Vector3& color);
	static const Vector3&		Get_Ambient() { return Ambient_Color; }
	// shader system updates KJM ^




	/*
	** The graphics backend. Every device call on the render path goes through this;
	** see gfxdevice.h for what is behind the seam and what is not. It is created in
	** Create_Device and destroyed in Release_Device, so it is null before Init and
	** after Shutdown. It is also the only thing in this tree that knows which graphics
	** API this is: no D3D interface type is named anywhere in this class any more, and
	** a second backend is a new implementation of gfxdevice.h plus a line in the
	** adapter factory, not a change to this file.
	*/
	static GfxDeviceClass * Gfx;

	/*
	** Is there a device to draw on?
	**
	** Almost every caller that ever took the device into a local only compared it
	** against null -- 35 of the 36 outside this file, in eight files -- and none of them
	** wanted a D3D pointer for that. Asking here instead keeps the question and drops
	** the API type, which is the whole difference between a subsystem that can be
	** compiled against a second backend and one that cannot. The device itself is now
	** reachable only through Gfx, and only as an opaque word (Peek_Native_Device).
	*/
	static bool Has_Device() { return Gfx != nullptr; }

	/*
	** The adapter this device was made on, and which one of them it is.
	**
	** Everything that used to reach for the IDirect3D9 interface -- device enumeration,
	** mode search, format checks, creation itself -- asks this instead. It outlives the
	** device: it is made in Init and destroyed in Shutdown, whereas Gfx comes and goes
	** with each device.
	*/
	static GfxAdapterClass * Get_Adapter() { return Adapter; }
	static unsigned Get_Adapter_Index() { return (CurRenderDevice >= 0) ? (unsigned)CurRenderDevice : 0u; }
	/// Returns the display format - added by TR for video playback - not part of W3D
	static WW3DFormat	getBackBufferFormat();
	static bool Reset_Device(bool reload_assets=true);

	static const DX8Caps*	Get_Current_Caps() { WWASSERT(CurrentCaps); return CurrentCaps; }

	static bool Registry_Save_Render_Device( const char * sub_key );
	static bool Registry_Load_Render_Device( const char * sub_key, bool resize_window );

	static const char* Get_DX8_Render_State_Name(D3DRENDERSTATETYPE state);
	static const char* Get_DX8_Texture_Stage_State_Name(D3DTEXTURESTAGESTATETYPE state);
	static unsigned Get_DX8_Render_State(D3DRENDERSTATETYPE state) { return RenderStates[state]; }

	// Fixed-function state that has been tracked but not sent.
	//
	// Nothing in this renderer draws with the fixed-function pipeline any more -- the
	// census says 0 of 612237 draws over a window -- but a great deal of code still
	// *describes* itself through fixed-function state, and the routing block reads that
	// description back to decide which shader a draw wants. So the writes cannot simply be
	// deleted: TextureStageStates is the intermediate representation, and a combine that
	// never reaches D3D still tells the router this pass wanted a detail blend.
	//
	// What can go is the D3D call. Colour and alpha ops, texgen, lighting, the material --
	// none of it has any effect while a vertex and pixel shader are bound, so it is written
	// to the tracked arrays and stops there.
	//
	// Correct rather than merely fast, because the deferral is lazy and not a suppression:
	// any draw that really does go out on fixed function flushes the pending words first,
	// so a pass that has no programmable path yet still renders from the state its caller
	// asked for. Flushed from two places, which between them cover every way to draw:
	// Draw(), for everything that goes through the wrapper, and Prepare_Direct_Draw for the
	// handful of subsystems that talk to the device themselves.
	static bool Has_Pending_Fixed_Function_State() { return FFStatePending || FFTransformPending!=0; }
	static void Flush_Fixed_Function_State();
	static bool Is_Deferred_FF_Stage_State(unsigned state);
	static bool Is_Deferred_FF_Render_State(unsigned state);

	// Which half of the pipeline a draw is about to use, read off what is bound.
	//
	// Separate questions with separate answers: D3D9 lets a pixel shader pair with a
	// fixed-function vertex format, and every screen-space quad in the frame does exactly
	// that. Asking only about the pixel shader -- which the direct-draw census used to do --
	// reports those as fully programmable when their vertex side is not merely
	// fixed-function but skipped, the position arriving already in screen space.
	//
	// Either one being true means the fixed-function pipeline is live for this draw and any
	// deferred state it depends on has to be on the device first.
	static bool Is_Fixed_Function_Vertex_Draw()
	{
		// Below 0x10000 the handle is an FVF code rather than a vertex shader.
		return Vertex_Shader < 0x10000;
	}
	static bool Is_Fixed_Function_Pixel_Draw() { return Pixel_Shader == 0; }
	static bool Is_Fixed_Function_Draw()
	{
		return Is_Fixed_Function_Pixel_Draw() || Is_Fixed_Function_Vertex_Draw();
	}
	// Declare a draw the wrapper will not see. Flushes any deferred fixed-function state,
	// because a direct-device drawer binds nothing and may well be running fixed function
	// -- and in a debug build, counts it, since nothing else can.
	static void Prepare_Direct_Draw(const char * site);

	// Put the 2D interface shader on an untextured screen-space quad drawn straight at the
	// device, and build the pixels-to-clip matrix it needs.
	//
	// For the direct-device drawers, which never reach the routing block and so cannot be
	// given a shader by it. Without this they are the last genuinely fixed-function draws
	// in the frame -- and one fixed-function draw is enough to make every deferred state
	// word real again, because the pipeline it renders from has to be reassembled from
	// whatever the tracked state has accumulated since the previous one. Six such draws a
	// frame were holding roughly 740 state words a frame at the device.
	//
	// Takes screen pixel coordinates because that is what these callers already compute;
	// it replaces D3DFVF_XYZRHW, which said the same thing to the fixed-function pipeline.
	// Returns false if the shader is unavailable, in which case the caller must keep its
	// fixed-function path -- so this can be adopted one drawer at a time.
	//
	// sampleColour / sampleAlpha say whether the combine reads the bound texture, matching
	// the stage-0 COLORARG/ALPHAARG the caller would otherwise have written. Both default
	// off, which is a flat vertex-diffuse quad.
	static bool Bind_Screen_Space_Shader(bool sampleColour = false, bool sampleAlpha = false);

	// The same pair with a transform the caller supplies, for direct-device drawers whose
	// geometry is in world space rather than screen space (the shadow decals, the projected
	// terrain shadow). Screen space is one caller of this.
	//
	// The matrix is 16 floats in row-major order -- the same vocabulary Gfx::Set_Transform
	// and Gfx::Get_Transform use across the seam, and for the same reason. It used to be a
	// D3DXMATRIX, which put a D3D9-only type in this class's public API and made every
	// caller include d3dx9.h to say anything to it. It is deliberately NOT WWMath's
	// Matrix4x4: that is column-major, so the conversion is a transpose, and these values
	// go straight out as vertex-shader constants where a transpose does not draw the
	// geometry wrong, it draws nothing at all.
	static bool Bind_Ui_Shader_Direct(const float * wvp, bool sampleColour, bool sampleAlpha);
	// As above, but concatenating the caller's world matrix with the view and projection the
	// device currently holds -- which for these callers is the pair Apply_Render_State_Changes
	// just put there.
	static bool Bind_Ui_Shader_World(const float * world, bool sampleColour, bool sampleAlpha);

	// Bind the screen-space quad *vertex* shader and give it the pixels-to-clip matrix,
	// leaving the pixel shader alone for the caller to set. The FVF must already be set --
	// Set_Vertex_Shader clears the bound shader when handed one, so the declaration has to
	// come first -- and must be XYZ | DIFFUSE | TEX2 to match the declaration in
	// screenquad_vs.hlsl. Returns false if the shader is unavailable, leaving the caller on
	// whatever it had.
	static bool Bind_Screen_Quad_Shader();
	// The pixels-to-clip matrix itself, built from the current viewport. Shared by both
	// binds above; exposed because a caller that builds its own constant set still needs
	// exactly this mapping and must not reinvent the sign convention.
	static bool Build_Pixels_To_Clip(float * out);
	// Names of the specific values of render states and texture stage states
	static void Get_DX8_Texture_Stage_State_Value_Name(StringClass& name, D3DTEXTURESTAGESTATETYPE state, unsigned value);
	static void Get_DX8_Render_State_Value_Name(StringClass& name, D3DRENDERSTATETYPE state, unsigned value);

	static const char* Get_DX8_Texture_Address_Name(unsigned value);
	static const char* Get_DX8_Texture_Filter_Name(unsigned value);
	static const char* Get_DX8_Texture_Arg_Name(unsigned value);
	static const char* Get_DX8_Texture_Op_Name(unsigned value);
	static const char* Get_DX8_Texture_Transform_Flag_Name(unsigned value);
	static const char* Get_DX8_ZBuffer_Type_Name(unsigned value);
	static const char* Get_DX8_Fill_Mode_Name(unsigned value);
	static const char* Get_DX8_Shade_Mode_Name(unsigned value);
	static const char* Get_DX8_Blend_Name(unsigned value);
	static const char* Get_DX8_Cull_Mode_Name(unsigned value);
	static const char* Get_DX8_Cmp_Func_Name(unsigned value);
	static const char* Get_DX8_Fog_Mode_Name(unsigned value);
	static const char* Get_DX8_Stencil_Op_Name(unsigned value);
	static const char* Get_DX8_Material_Source_Name(unsigned value);
	static const char* Get_DX8_Vertex_Blend_Flag_Name(unsigned value);
	static const char* Get_DX8_Patch_Edge_Style_Name(unsigned value);
	static const char* Get_DX8_Debug_Monitor_Token_Name(unsigned value);
	static const char* Get_DX8_Blend_Op_Name(unsigned value);

	// Forget everything the wrapper knows about the device, because the device no longer
	// holds it: it has been created, reset, or written straight at by something that does
	// not go through here. Nothing in the render path qualifies -- see the note above the
	// definition, and Invalidate_Cached_Shader below for what those callers actually want.
	//
	// The site name is the audit's; it says which caller asked to forget, so "was anything
	// actually written behind the wrapper's back here" can be answered per caller rather
	// than in aggregate. See Debug_Audit_Invalidation.
	static void Invalidate_Cached_Render_States(const char * site = nullptr);

	// The caller has written render states that ShaderClass believes it owns -- a
	// W3DShaderManager custom shader, a post-process filter's own z and blend, the shadow
	// depth pass's colour mask -- so render_state.shader no longer describes the device.
	// Re-describe it on the next draw.
	//
	// This is the narrow half of what the render path used to ask
	// Invalidate_Cached_Render_States for, and the only half of it that was ever load
	// bearing there. ShaderClass keeps a cache of its own (CurrentShader), so re-setting
	// the same shader value is otherwise skipped and the custom states stay on the device;
	// that cache is a level above the tracked render states and no device read-back can
	// see it going stale. The wide half -- forgetting what the *device* holds -- was
	// warranted at none of those sites: measured over gla_midgame in both shadow
	// configurations, 28650 invalidations found the device disagreeing with the wrapper
	// zero times.
	static void Invalidate_Cached_Shader();

	static void Set_Draw_Polygon_Low_Bound_Limit(unsigned n) { DrawPolygonLowBoundLimit=n; }

protected:

	static bool	Create_Device();
	static void Release_Device();

	/*
	** What D3DXCreateTexture did before it reached CreateTexture: bring a requested
	** size, format and mip count inside what the device can actually make. It lives on
	** this side of the seam because every question it asks -- how big a texture may be,
	** what aspect ratio is allowed, which formats exist -- is one DX8Caps already
	** answers, and because there is no D3DX11 to defer it into.
	*/
	static void Adjust_Texture_Requirements(unsigned & width, unsigned & height,
					WW3DFormat & format, unsigned & levels);
	/// The engine's three texture homes, said in the seam's vocabulary. The pool stays
	/// in these helpers' public signatures because 30 call sites across the tree name
	/// one; it stops here.
	static unsigned Texture_Pool_To_Usage(D3DPOOL pool, bool rendertarget);

	static void Reset_Statistics();
	static void Enumerate_Devices();
	static void Set_Default_Global_Render_States();

	/*
	** Device Selection Code.
	** For backward compatibility, the public interface for these functions is in the ww3d.
	** header file.  These functions are protected so that we aren't exposing two interfaces.
	*/
	static bool Set_Any_Render_Device();
	static bool	Set_Render_Device(const char * dev_name,int width=-1,int height=-1,int bits=-1,int windowed=-1,bool resize_window=false);
	static bool	Set_Render_Device(int dev=-1,int resx=-1,int resy=-1,int bits=-1,int windowed=-1,bool resize_window = false, bool reset_device = false, bool restore_assets=true);
	static bool Set_Next_Render_Device();
	static bool Toggle_Windowed();

	static int	Get_Render_Device_Count();
	static int	Get_Render_Device();
	static const RenderDeviceDescClass & Get_Render_Device_Desc(int deviceidx);
	static const char * Get_Render_Device_Name(int device_index);
	static bool Set_Device_Resolution(int width=-1,int height=-1,int bits=-1,int windowed=-1, bool resize_window=false);
	static void Get_Device_Resolution(int & set_w,int & set_h,int & set_bits,bool & set_windowed);
	static void Get_Render_Target_Resolution(int & set_w,int & set_h,int & set_bits,bool & set_windowed);
	static int	Get_Device_Resolution_Width() { return ResolutionWidth; }
	static int	Get_Device_Resolution_Height() { return ResolutionHeight; }

	static bool Registry_Save_Render_Device( const char *sub_key, int device, int width, int height, int depth, bool windowed, int texture_depth);
	static bool Registry_Load_Render_Device( const char * sub_key, char *device, int device_len, int &width, int &height, int &depth, int &windowed, int &texture_depth);
	static bool Is_Windowed() { return IsWindowed; }

	static void	Set_Texture_Bitdepth(int depth)	{ WWASSERT(depth==16 || depth==32); TextureBitDepth = depth; }
	static int	Get_Texture_Bitdepth()			{ return TextureBitDepth; }

	static void Set_MSAA_Mode(WW3DMultiSampleType mode) { MultiSampleAntiAliasing = mode; }
	static WW3DMultiSampleType Get_MSAA_Mode() { return MultiSampleAntiAliasing; }

	static void	Set_Swap_Interval(int swap);
	static int	Get_Swap_Interval();
	static void Set_Polygon_Mode(int mode);

	/*
	** Internal functions
	*/
	static void Resize_And_Position_Window();
	static bool Find_Color_And_Z_Mode(int resx,int resy,int bitdepth,WW3DFormat * set_colorbuffer,WW3DFormat * set_backbuffer, WW3DZFormat * set_zmode);
	static bool Find_Color_Mode(WW3DFormat colorbuffer, int resx, int resy, unsigned *mode);
	static bool Find_Z_Mode(WW3DFormat colorbuffer,WW3DFormat backbuffer, WW3DZFormat *zmode);
	static bool Test_Z_Mode(WW3DFormat colorbuffer,WW3DFormat backbuffer, WW3DZFormat zmode);
	static void Compute_Caps(WW3DFormat display_format);

	/*
	** Protected Member Variables
	*/

	static DX8_CleanupHook *m_pCleanupHook;

	static RenderStateStruct			render_state;
	static unsigned						render_state_changed;
	static D3DMATRIX						DX8Transforms[D3DTS_WORLD+1];

	static bool								IsInitted;
	static bool								IsDeviceLost;
	static void *							Hwnd;
	static unsigned						_MainThreadID;

	static bool								_EnableTriangleDraw;

	static int								CurRenderDevice;
	static int								ResolutionWidth;
	static int								ResolutionHeight;
	static int								BitDepth;
	static int								TextureBitDepth;
	static bool								IsWindowed;
	static WW3DFormat					DisplayFormat;
	static WW3DMultiSampleType	MultiSampleAntiAliasing;


	// shader system updates KJM v
	static DWORD							Vertex_Shader;
	static DWORD							Pixel_Shader;

#ifdef RTS_DEBUG
	// The vertex *declaration* standing at the device, which Vertex_Shader stops being the
	// moment a real shader is bound over it.
	//
	// Set_Vertex_Shader overloads one word for two things: below 0x10000 it is an FVF and
	// the backend calls SetFVF, above it a compiled shader and the backend calls
	// SetVertexShader. SetFVF is sticky and SetVertexShader does not clear it, so a
	// programmable draw still reads its attributes through the last FVF anybody set -- and
	// that FVF, not the shader handle, is what a D3D11 input layout has to describe.
	// Apply_Render_State_Changes sets it from the bound vertex buffer and the routing block
	// then binds a shader over the top, so by the time a draw happens the two are routinely
	// different values and Vertex_Shader only remembers the second.
	static DWORD							Debug_Vertex_FVF;
#endif

	static Vector4							Vertex_Shader_Constants[MAX_VERTEX_SHADER_CONSTANTS];
	static Vector4							Pixel_Shader_Constants[MAX_PIXEL_SHADER_CONSTANTS];

	static LightEnvironmentClass*		Light_Environment;


	static ZTextureClass*				Shadow_Map[MAX_SHADOW_MAPS];

	static Vector3							Ambient_Color;
	// shader system updates KJM ^

	static bool								world_identity;
	static unsigned						RenderStates[256];
	static unsigned						TextureStageStates[MAX_TEXTURE_STAGES][32];
	static SamplerStateClass			Samplers[MAX_TEXTURE_STAGES];
	static GfxTexture *	Textures[MAX_TEXTURE_STAGES];

	// Deferred fixed-function state. See Flush_Fixed_Function_State in dx8wrapper.cpp for
	// why these words stop at the arrays above instead of going on to the device.
	//
	// One bit per pending state word: stage states are all below 32, and the fixed-function
	// render states this defers top out at D3DRS_EMISSIVEMATERIALSOURCE (148).
	static unsigned						FFStagePending[MAX_TEXTURE_STAGES];
	static unsigned						FFRenderPending[8];
	static bool								FFStatePending;
	// What the device was actually last told, so a flush writes only the words that would
	// change something. Without it the flush is not incremental and the deferral trades a
	// steady trickle of writes for a periodic flood: invalidation marks every deferred word
	// pending, and each of the frame's direct draws would then re-push the lot. Measured at
	// 420744 words a window that way, against 336289 asked for -- worse than not deferring.
	static unsigned						FFDeviceStage[MAX_TEXTURE_STAGES][32];
	static unsigned						FFDeviceRender[256];
	// Transforms, deferred the same way. Eleven slots rather than a 257-entry array:
	// D3DTS_WORLD is 256 and the rest are 2, 3 and 16..23, so they are packed into one
	// bitmask by FF_Transform_Slot. FFDeviceTransformValid says which the device has ever
	// actually been handed -- the equivalent of the 0x12345678 sentinel for a matrix, which
	// has no spare bit pattern to spare.
	enum { FF_TRANSFORM_SLOTS = 11 };
	static D3DMATRIX					FFDeviceTransform[FF_TRANSFORM_SLOTS];
	static unsigned						FFTransformPending;
	static unsigned						FFDeviceTransformValid;
	// The material is tracked rather than bit-flagged because there is no array behind it
	// to defer into -- this copy *is* the tracked state. The routing block reads it to
	// recover the house-colour tint and the stealth opacity, which it used to fetch back
	// out of the device with GetMaterial once per draw.
	static D3DMATERIAL8					CurrentMaterial;

	// These fog settings are constant for all objects in a given scene,
	// unlike the matching renderstates which vary based on shader settings.
	static bool								FogEnable;
	static D3DCOLOR						FogColor;

	static DX8FrameStatistics			FrameStatistics;

	static unsigned long FrameCount;

	static DX8Caps*						CurrentCaps;


	// The adapter, and the swap chain the device was created with. Both neutral; see
	// gfxdevice.h. The API's own present parameters live inside the backend now.
	static GfxAdapterClass *			Adapter;
	static GfxSwapChainDesc				SwapChain;

	static GfxSurface *			CurrentRenderTarget;
	static GfxSurface *			CurrentDepthBuffer;
	static GfxSurface *			DefaultRenderTarget;
	static GfxSurface *			DefaultDepthBuffer;

	static unsigned							DrawPolygonLowBoundLimit;

	static bool								IsRenderToTexture;

	static int								ZBias;
	static float							ZNear;
	static float							ZFar;

public:
	// Programmable (D3D9) unit render path. The handles are populated by
	// W3DShaderManager once the device exists; the render code binds them for
	// object meshes in place of the fixed-function pipeline. The mesh FVF serves
	// as the vertex declaration, so no explicit declaration is needed.
	static DWORD						m_dwUnitVS;
	static DWORD						m_dwUnitPrelitVS;   // meshes with no NORMAL (roads, tracks)
	static DWORD						m_dwUnitUv2VS;      // stage 1 on the mesh's 2nd UV set
	static DWORD						m_dwUnitPS;
	static DWORD						m_dwUnitDetailPS;   // base + detail (stage 1) variant

	// Which draw categories the programmable path is allowed to claim, selectable from
	// options.ini ("ShaderRouting") so the alternatives can be compared in game without a
	// rebuild. DETAIL|TEXGEN is the default: between them they keep every pass of a mesh
	// on one pipeline, which is what stops coincident passes of a mesh from being drawn
	// with differing depth and z-fighting. Zero selects neither, i.e. the routing before
	// those categories existed. PBR is not in the default: it changes how meshes are
	// shaded rather than which pipeline they are drawn on, so it is opt-in
	// ("ShaderRouting = 35" for the default categories plus PBR).
	enum ShaderRoutingFlags
	{
		SHADER_ROUTE_BASELINE      = 0,
		SHADER_ROUTE_DETAIL        = 1 << 0,   // multi-texture (stage 1) detail passes
		SHADER_ROUTE_TEXGEN        = 1 << 1,   // camera-space texture coordinate generation
		SHADER_ROUTE_SORTING       = 1 << 2,   // sorted (no-FVF) buffers -- not implemented
		SHADER_ROUTE_EVERYTHING    = 1 << 3,   // drop every restriction (diagnostic)
		SHADER_ROUTE_OFF           = 1 << 4,   // no mesh routing at all (fixed function)
		SHADER_ROUTE_ADDITIVE      = 1 << 6,   // additive effect passes too (diagnostic; see below)
		SHADER_ROUTE_PBR           = 1 << 5,   // metallic-roughness shading for object meshes
		// PBR on house-coloured meshes with their *authored* ORM map. Normally they are
		// held back from it, because a procedurally generated ORM reads their bright white
		// base texture as near-metallic and PBR then renders dark metal where a team tint
		// belongs. That reasoning is about *generated* maps: an authored ORM whose metallic
		// is deliberate wants to be obeyed, and since every player-owned unit carries a team
		// tint, the exclusion otherwise keeps the authored maps off all of them.
		//
		// Note this no longer decides whether a house-coloured mesh is shaded by PBR at
		// all -- without this flag it still is, on the neutral default map, where there is
		// no metallic channel to misread. It decides only whose ORM data it uses.
		SHADER_ROUTE_PBR_TEAMCOLOR = 1 << 7,
		// Restore the pre-migration behaviour: PBR only for meshes that actually ship an
		// <name>_orm map, everything else on the M3 lit shader. Kept as an opt-out so the
		// two can still be compared in game -- the shipping behaviour is that every
		// eligible mesh is shaded by PBR, falling back to m_defaultOrmMap.
		SHADER_ROUTE_PBR_AUTHORED_ONLY = 1 << 8,
	};
	static DWORD						m_shaderRoutingMask;
	// PBR (metallic-roughness, SM3) variant of the unit shader. Bound in place of the
	// plain unit shader for every eligible object mesh -- on its own <name>_orm map
	// where one is authored, on m_defaultOrmMap where none is.
	static DWORD						m_dwUnitPbrVS;
	static DWORD						m_dwUnitPbrPS;
	// Shared environment cubemap sampled by the PBR shader for reflections. Bound on
	// texture stage 4 (0=albedo, 1=ORM, 2/3=terrain overlays are already spoken for).
	static GfxTexture*		m_envCubeMap;
	// Mean colour of the baked cubemap. The PBR shader divides its irradiance tap by
	// this so the directional ambient it derives averages to 1.0, letting it redistribute
	// the engine's ambient by direction without changing the overall exposure.
	static float						m_envAverage[4];
	// Put texture stage 1 back after a PBR draw bound its ORM map straight to it.
	static void Restore_Stage1_After_Pbr();
	static void Restore_Stage5_After_Shadow();
	static void Restore_Pbr_Extra_Stages();
	// Neutral 1x1 ORM map, bound to stage 1 for meshes that ship no <name>_orm of their
	// own so they can still be shaded by PBR. Its channels hold the values that make the
	// metallic-roughness BRDF land closest to what the fixed-function pipeline drew:
	// unoccluded (AO 1), fully dielectric (metallic 0), and rough enough that the
	// specular lobe is a broad sheen rather than a highlight the original never had.
	// Built by W3DShaderManager::initDefaultOrmMap.
	static GfxTexture*		m_defaultOrmMap;
	// Resolver (installed by the game layer) that maps a base texture to its ORM
	// sibling texture (<name>_orm), or nullptr when the unit ships no PBR maps.
	typedef TextureBaseClass* (*OrmResolverFunc)(TextureBaseClass* baseTexture);
	static OrmResolverFunc				s_ormResolver;
	static void Set_Orm_Resolver(OrmResolverFunc fn) { s_ormResolver = fn; }
	// Programmable terrain path. HeightMap flags a terrain tile pass and the
	// render code binds these instead of the mesh shader for those draws.
	static DWORD						m_dwTerrainVS;
	static DWORD						m_dwTerrainPS;
	static bool							m_bUnitShaderBound;   // a programmable shader is currently bound
	static bool							m_bTerrainShaderPass; // current draws are terrain tiles
	static void Set_Terrain_Shader_Pass(bool active) { m_bTerrainShaderPass = active; }
	static bool Has_Terrain_Shader() { return m_dwTerrainVS != 0 && m_dwTerrainPS != 0; }
#ifdef RTS_DEBUG
	// Split-pipeline watchdog. A mesh drawn by more than one pipeline z-fights with
	// itself -- fixed function and the vertex shader do not compute identical depth, so
	// coincident passes of the same mesh disagree in the last bits and the later one is
	// rejected in a camera-dependent pattern. That is what makes civilian buildings
	// flicker, and it has arrived twice now: first when milestone 3 left some passes
	// behind, then when an exclusion meant for effect geometry was written as a blend
	// test that ordinary blended surface passes match just as well.
	//
	// The routing gate is decided per pass, so nothing in its structure prevents a third
	// time. Watch the invariant itself and name the offender the first time it breaks.
	//
	// Keyed on the model name, so two objects sharing an asset but drawn in genuinely
	// different states -- one of them stealthed, say -- can report a split that is not
	// one. Treat a report as a place to look rather than as a verdict.
	static const char*					s_debugMeshName;
	static void Set_Debug_Mesh_Name(const char* n) { s_debugMeshName = n; }
	static void Debug_Note_Mesh_Routing(unsigned pipelineBit, unsigned ffReason);
	static void Debug_Check_Mesh_Routing_Split();
	// Per-pipeline draw census, reported over a window of frames. Says which pipeline
	// claimed how much of the frame -- the check that PBR really did widen to every
	// eligible mesh, and that the exclusions around it still hold.
	static void Debug_Note_Routing_Census(unsigned category);
	static void Debug_Report_Routing_Census();
	// Draws reaching the routing block with no declared technique, grouped by texture.
	// These are the callers that bypass the mesh renderer and the last users of the
	// inference; this is the survey of what they are. Temporary, for stage 6.
	static void Debug_Note_Unclassified_Draw(
		TextureBaseClass* tex0, unsigned fvf, bool wentToShader, bool blended, bool softOverlay);
	static void Debug_Report_Unclassified_Draws();
	// Every draw that still leaves Apply_Render_State_Changes on the fixed-function
	// pipeline, attributed to whatever identity it has -- declaration site, mesh name,
	// or failing both, the frame pass it was drawn in plus its texture and vertex
	// format. The routing census says how much fixed function is left; this says who
	// it belongs to, which is what an order of work has to be built from.
	static void Debug_Note_FF_Draw(TextureBaseClass* tex0, unsigned fvf,
								   bool viewIdentity, unsigned ffReason);
	static void Debug_Note_Routed_Draw();   // the control: a draw a shader claimed
	static void Debug_Note_Suppressed_Draw();   // the routing block declined a caster
	static void Debug_Note_Unsubmitted_Draw();  // ...and a draw call was not made
	// Which pass of the frame is being drawn, for a draw that declared no technique
	// scope of its own. Same fallback chain the fixed-function draw census uses, so the
	// two tables name the same things.
	static const char* Debug_Current_Pass_Name();
	static void Debug_Report_FF_Draws();
	// The blind spot in the census above, made visible. A caller that applies state and
	// then issues DrawIndexedPrimitive/DrawPrimitiveUP against the device itself never
	// reaches DX8Wrapper::Draw, so nothing attributes it and nothing binds a shader for
	// it either -- it renders with whatever the previous draw happened to leave bound.
	// These are the last drawers not accounted for, so each one names itself here.
	static void Debug_Note_Direct_Draw(const char * site);
	static void Debug_Report_Direct_Draws();
	// (vertex format x vertex shader) per drawer -- the one question no other census here
	// answers, and the table a D3D11 backend is built from.
	//
	// D3D11 has no FVF and no implicit declaration: an input layout is created from a
	// vertex layout *and* a compiled vertex shader's input signature together, and the set
	// of layouts a backend must create is therefore the set of distinct pairs, not the set
	// of formats and not the set of shaders. Nothing else here reports the pair. The
	// routing census reports the block's verdict; the lighting census reports one example
	// format per drawer and only for fixed-function draws through Draw(); the direct-device
	// census reports which half was programmable and no format at all.
	//
	// Fed from both ways to draw, so a drawer that goes straight at the device is in the
	// same table as one that does not, and marked with which it was. `submitted` is false
	// for a draw Draw() drops before it reaches a device -- those need no layout, and
	// counting them in would put the depth pass at the top of a table meant to size work.
	static void Debug_Note_Vertex_Layout(const char * site, bool direct, bool submitted);
	static void Debug_Report_Vertex_Layouts();
	// The first wrapper draw after a direct-device drawer, measured at the moment Draw()
	// takes the device's bindings back -- before the repair, because after it there is
	// nothing left to see. Says how many of those draws would have used the wrong base
	// vertex index or somebody else's vertex stream, grouped by the drawer that left them.
	static void Debug_Note_Foreign_Bindings(int expectedBase, int inheritedBase, bool streamWrong);
	// Frame time over the census window: mean, median, p95 and worst, so a cost can be
	// judged on its distribution rather than its average.
	static void Debug_Report_Frame_Timing();
	// Declared technique (meshtechnique.h) against the per-draw routing block's own
	// answer. Must be quiet before anything is allowed to read the declared value.
	static void Debug_Note_Technique_Agreement(MeshTechnique declared, bool prelitGain);
	static void Debug_Note_Technique_Mismatch(
		MeshTechnique declared, MeshTechnique live, TextureBaseClass* tex0, unsigned fvf);
	static void Debug_Report_Technique_Check();

	// Particle shadow casting, counted at both ends of the path, because either end
	// failing looks exactly like the other from the outside. The manager reports what it
	// selected and submitted; the routing block reports what actually reached the sprite
	// depth shaders. Mesh casters are counted alongside as the control: if both are zero
	// the depth pass itself is not running, which is a different bug entirely.
	static void Debug_Note_Particle_Shadow_Submit(unsigned systemsSeen, unsigned systemsCast,
												  unsigned particles);
	static void Debug_Note_Particle_Shadow_Sprite(float size, float alpha);
	static void Debug_Note_Shadow_Caster_Draw(bool particleVariant);
	static void Debug_Report_Particle_Shadows();

	// Fixed-function *call sites*, as opposed to fixed-function draws.
	//
	// Debug_Report_FF_Draws says nobody still draws with fixed function. It does not say
	// nobody still writes it: a caller can set a whole colour/alpha combine and then have
	// its draw claimed by a pixel shader that ignores every word of it. Those writes are
	// what is left to delete, and the count of them is not the count of draws.
	//
	// Each emitting function names itself with FFSiteScope. Two numbers per site: how often
	// the function ran, and how many fixed-function-only state words it actually pushed to
	// the device from there. A site with calls but no writes is asking for state that was
	// already set -- redundant, and safe to delete on its own. A site with writes is live
	// churn, and needs its subsystem routed onto a shader before the block can go.
	//
	// Counted at the device, past the redundancy check, and deliberately so: the tracked
	// TextureStageStates array is *not* dead even where the device state is, because the
	// routing block reads texgen intent back out of it. The array is the intermediate
	// representation of what the caller wanted; only the D3D call is waste.
	static void Debug_Set_FF_Site(const char* site);
	static const char* Debug_Get_FF_Site();
	static void Debug_Note_FF_State_Write(unsigned isTextureStage, unsigned state);
	static void Debug_Report_FF_Sites();
	// Does the wrapper's model of the device still match the device? Reads every word it
	// claims to know back off D3D and counts the ones it has wrong. Returns that count.
	// Called on a timer, and from what is left of Invalidate_Cached_Render_States.
	static unsigned Debug_Audit_Invalidation(const char * site);
	// The once-a-frame check, plus the positive control that keeps its zero meaningful.
	static void Debug_Audit_Frame_End();
	static void Debug_Report_Invalidations();

	// Transforms and lights: the two categories the audit above does not cover.
	//
	// It compares the device against the wrapper's tracked arrays, and neither of these
	// has one it can use. DX8Transforms[] looks like the array for transforms and is not:
	// _Set_DX8_Transform writes it, but the three Set_Transform overloads and
	// Set_Projection_Transform_With_Z_Bias reach the device without touching it, so a
	// projection or texture matrix set through those is on the device and not in the
	// array. What the audit needs is what was actually sent, so that is what this records,
	// at the device call itself.
	//
	// Lights are asked a different question, because "does anything write one behind our
	// back" is not the interesting one -- nothing does. The interesting one is whether any
	// draw still *consumes* fixed-function lighting, and that is a per-draw property, so
	// it is counted in Draw() beside the alpha-test census rather than read back here.
	static void Debug_Note_Device_Transform(unsigned which, const float * matrix4x4);
	static void Debug_Note_Lighting_Draw();
	static void Debug_Report_Lighting();

	// Alpha test and fog: the two fixed-function *stages* that have no D3D11 equivalent
	// at all. Both run after the pixel shader, so a shader can be entirely correct and
	// still lose them, and both fail silently -- soft-edged foliage, an unfogged scene.
	//
	// This counts the draws each stage is actually live on before anything is ported.
	// Alpha test is grouped by the (compare function, reference, pixel shader) triple,
	// which is at once the design input for the shader-side constant and the list of
	// shaders that need a clip(). Fog is a straight count, because the question there is
	// only whether it does anything at all.
	//
	// Each number carries its own control -- the draws where the stage was off -- so a
	// zero reads as "nobody asked for it" rather than "the instrument was not reached".
	static void Debug_Note_Alpha_Fog_Draw();
	static void Debug_Report_Alpha_Fog();
	// Handle -> name for every shader loaded through W3DShaderManager, so the census
	// above can say "tree_ps" instead of a device handle. Shaders created by a direct
	// CreatePixelShader call are not in the table and report as unregistered.
	static void Debug_Register_Shader_Name(unsigned handle, const char* path);
	static const char* Debug_Shader_Name(unsigned handle);
	static void Debug_Report_Shader_Names();
	// What a texture was asked for against what was actually made.
	//
	// D3DXCreateTexture ran D3DXCheckTextureRequirements before CreateTexture -- clamping
	// the size to the device's limits, rounding to a power of two where the device needs
	// one, and substituting a format it does not support -- and that adjustment is the
	// one thing in the texture path that can change what the frame looks like. There is
	// no D3DX11, so the check has to be written on this side of the seam; this census is
	// what says whether the two agree, by measuring the same thing before and after.
	//
	// Cumulative, not windowed: textures are created at load and almost never after, so a
	// 600-frame window would report an empty table.
	static void Debug_Note_Texture_Made(unsigned req_w, unsigned req_h, WW3DFormat req_fmt,
					unsigned req_levels, GfxTexture * made);
	static void Debug_Note_Texture_Made_Other(const char * kind);
	static void Debug_Report_Texture_Requirements();

#endif
	// Programmable road path. Roads are decals on the terrain and want the terrain's
	// shading -- cloud, noise and, the reason this exists, cast shadows. Their own
	// fixed-function path could not sample the shadow map, so a road stayed at full
	// brightness through a shadow the ground around it was in. Flagged by W3DRoadBuffer
	// the same way HeightMap flags a terrain pass.
	static DWORD						m_dwRoadVS;
	static DWORD						m_dwRoadPS;
	static bool							m_bRoadShaderPass;    // current draws are road segments
	static void Set_Road_Shader_Pass(bool active) { m_bRoadShaderPass = active; }
	static bool Has_Road_Shader() { return m_dwRoadVS != 0 && m_dwRoadPS != 0; }

	// 2D interface. Declared by Render2DClass around its own draws, for the same reason the
	// roads and the water are: it is not a mesh, so there is no technique to read off it.
	//
	// Deliberately not keyed on VIEW_IDENTITY, which is the flag the routing uses to *decline*
	// 2D. An identity view means the vertices are already in camera space, which the interface
	// is an instance of rather than a synonym for -- dazzle.cpp sets it too, and a
	// camera-relative 3D pass would as well. Declining on it is safe because it only ever
	// leaves a draw on the pipeline it already had; claiming on it would not be.
	//
	// m_uiGreyscale desaturates, replacing the two-stage DOT3 combine Render2DClass used to
	// assemble by hand for disabled buttons.
	static DWORD						m_dwUiVS;
	static DWORD						m_dwUiPS;
	static bool							m_bUiPass;        // current draws are 2D interface
	static bool							m_uiGreyscale;
	static void Set_Ui_Pass(bool active) { m_bUiPass = active; }
	static void Set_Ui_Greyscale(bool on) { m_uiGreyscale = on; }
	static bool Has_Ui_Shader() { return m_dwUiVS != 0 && m_dwUiPS != 0; }

	// The vertex half of a screen-space quad, for the post-process chain: the bloom passes,
	// the tone map, the screen filters, the smudge, the profiler capture. Each of those
	// brings its own pixel shader and wants only the vertex side replaced, which is what
	// separates this from the ui pair -- and it carries two texture coordinate sets, which
	// ui_vs does not, because the bloom composite samples scene and bloom with different
	// coordinates in one draw.
	static DWORD						m_dwScreenQuadVS;
	static bool Has_Screen_Quad_Shader() { return m_dwScreenQuadVS != 0; }

	// The projected alpha mask, declared by W3DMaskMaterialPassClass. Unlike every other
	// declared pass this one is not a *kind of geometry* -- it is the whole scene, drawn a
	// second time with colour writes off, so that each pixel leaves the mask's alpha behind
	// for the cross-fade or the wireframe preview to composite against. Which is exactly why
	// it has to be declared rather than inferred: the draws are ordinary meshes and terrain,
	// indistinguishable from their real pass by anything the routing block can see.
	//
	// m_maskProj carries the world-XY -> mask-uv map as scale in xy and bias in zw. The
	// fixed-function path rebuilt an equivalent 4x4 texture matrix per pass; an affine map of
	// the world position is all it ever amounted to.
	static DWORD						m_dwMaskVS;
	static DWORD						m_dwMaskPS;
	static bool							m_bMaskPass;      // current draws are the alpha-mask pass
	static Vector4						m_maskProj;
	static void Set_Mask_Pass(bool active) { m_bMaskPass = active; }
	static void Set_Mask_Projection(float scaleX, float scaleY, float biasX, float biasY)
		{ m_maskProj.X = scaleX; m_maskProj.Y = scaleY; m_maskProj.Z = biasX; m_maskProj.W = biasY; }
	static bool Has_Mask_Shader() { return m_dwMaskVS != 0 && m_dwMaskPS != 0; }

	// Water. Declared by WaterRenderObjClass around its own draws, the same way roads are:
	// the water is not a mesh and carries nothing the routing could classify it by, and
	// guessing from render state would put it in the same bucket as every other soft-blended
	// overlay in the scene.
	//
	// The flag must never be raised around a draw that could reach the depth pass. It
	// cannot today -- water is soft-blended with no alpha test, which useShadowDepth
	// rejects, so the camera depth target keeps the river bed and not the surface, which is
	// precisely what the shader reads to find out how deep the water is. Anything that
	// changes water's blend state has to be checked against that.
	static DWORD						m_dwWaterVS;
	static DWORD						m_dwWaterPS;
	static bool							m_bWaterShaderPass;
	static void Set_Water_Shader_Pass(bool active) { m_bWaterShaderPass = active; }
	static bool Has_Water_Shader() { return m_dwWaterVS != 0 && m_dwWaterPS != 0; }
	// Everything the water shader needs that only the water object knows. Published once
	// per frame rather than per draw: a map with many water areas issues one draw per
	// trapezoid, and none of this varies between them.
	static Vector4						m_waterCtl;         // x = river, y = sparkle, z = depth available, w = phase
	// y is the opacity of fully deep water, not a floor on the ramp -- it is the legacy
	// MinWaterOpacity, which the fixed-function path put in the frame buffer's alpha and
	// blended against. Reading it as a floor makes the ramp flat, since the shipped value
	// is 1.0.
	static Vector4						m_waterDepthCtl;    // x = opacity rate, y = deep opacity, z = colour rate, w = calm depth
	static Vector4						m_waterShallowTint;
	static Vector4						m_waterDeepTint;
	static Vector4						m_waterReflCtl;     // x = reflection, y = F0, z = shadow darkening, w = Fresnel exponent
	static Vector4						m_waterSunDir;      // xyz = toward the sun
	static Vector4						m_waterSunCol;      // rgb = sun colour, w = specular exponent
	static Vector4						m_waterWaveCtl;     // x = specular, y = steepness, z = frequency, w = speed
	static Vector4						m_waterShroudUV;    // xy = world->UV scale, zw = offset
	static Vector4						m_waterNoiseUV;     // x = scale, y = offset
	static Vector4						m_waterBlendCtl;    // x = legacy source-alpha weight
	static Vector4						m_waterFoamCtl;     // x = depth, y = strength, z = noise scale, w = drift
	static Vector4						m_waterFoamCol;
	static Vector4						m_waterRefractCtl;  // x = max offset, y = grab available, z = depth to full
	static Vector4						m_waterAbsorb;      // rgb = per-channel extinction of the bottom
	// The scene as it stood immediately before the water drew. Captured mid-frame by
	// W3DShaderManager::captureRefraction, and bound on stage 1 -- see the note in the
	// water routing branch about why the sparkle texture gave that stage up.
	static GfxTexture*		m_pRefraction;
	static GfxTexture*		m_pWaterShroud;     // fog-of-war projection, or null
	static void Set_Water_Shroud(GfxTexture* tex, float sx, float sy, float ox, float oy)
	{
		m_pWaterShroud = tex;
		m_waterShroudUV.Set(sx, sy, ox, oy);
	}
#ifdef RTS_DEBUG
	// Draws that actually reached the water shaders, counted where the binding happens.
	// A water surface that silently fell back to the fixed-function pipeline renders a
	// frame that looks broadly right, so "it looked fine" is not evidence the path ran --
	// this is. Read and cleared by the water object every 300 frames.
	static unsigned						s_waterRoutedDraws;
#endif
	// Directional shadow mapping. During the depth pass every mesh/terrain draw is
	// re-routed to the shadow-depth shaders (which just pack sun-space depth); during
	// the normal lit passes the shadow map is bound + SunVP is fed so the unit/terrain
	// shaders can reproject and sample it. m_sunVP is the sun view*projection, stored
	// as 16 floats (row-major) to keep D3DX out of this header.
	// Edge length of the square shadow map. The render target, the depth-pass
	// viewport, the frustum-fitting texel snap and the shaders' PCF tap offset all
	// have to agree on this, so it lives here rather than in each of them.
	enum { SHADOW_MAP_SIZE = 4096 };
	static DWORD						m_dwShadowDepthVS;
	static DWORD						m_dwShadowDepthPS;
	// The same depth pass for particle sprites, which differ in two ways no constant can
	// bridge: their opacity lives in the vertex colour rather than the texture (a puff
	// fading out of existence would otherwise cast at full strength to the last frame),
	// and they are translucent, which a one-depth-per-texel map cannot express at all --
	// so this pair dithers the coverage instead. See shadowdepthparticle_ps.hlsl.
	static DWORD						m_dwShadowDepthParticleVS;
	static DWORD						m_dwShadowDepthParticlePS;
	static GfxTexture*		m_pShadowMap;       // depth-packed shadow map (bound for sampling)
	// The cloud shadow field, so meshes can be shaded by the same clouds the ground is.
	// Republished by the terrain each frame rather than cached at creation, because a
	// device reset rebuilds the texture and would leave a stale pointer here.
	static GfxTexture*		m_pCloudMap;
	static float						m_sunVP[16];
	// x = depth-compare bias in sun-clip units, y = shadow strength (0 disables the
	// lookup without unbinding anything), z = one texel in UV, w = the PCF kernel radius
	// in texels. The bias has to track the frustum: it fights the world-space size of a
	// shadow texel, and that now changes with the zoom. The texel size rides along so the
	// PCF taps cannot fall out of step with SHADOW_MAP_SIZE.
	//
	// The radius is here for the same reason the bias is. The penumbra is sized in world
	// units up in W3DView, and a texel is worth a different amount of ground at each zoom,
	// so a radius baked into the shader would make shadows soften and sharpen as the
	// camera moved in and out. Converting per frame is what keeps the look fixed to the
	// world instead of to the shadow map.
	static float						m_shadowParams[4];
	// The mesh receivers' half of the same settings, kept apart because they defend
	// themselves differently: x = how far the lookup is lifted along the surface normal,
	// in world units, and y = the depth-compare bias left over once it is. Terrain and
	// roads carry no vertex normal and stay on m_shadowParams[0]'s blanket bias.
	static float						m_shadowMeshParams[4];
	// Whether the draw about to be submitted can write anything at all: true for a
	// shadow-depth-pass draw with the colour mask at zero, depth writes off and stencil
	// off, which between them are everything that render target records. Draw() drops
	// those rather than spending a draw call, a state validation and a pass over their
	// triangles to produce nothing.
	//
	// Derived at the draw rather than latched when the routing block declines a caster,
	// and that is the whole design. A latched flag stands for the two write masks, and
	// those masks are written directly -- BaseHeightMap, W3DScene, HeightMap and
	// W3DShaderManager all call Set_DX8_Render_State on them --
	// which raises no bit in render_state_changed. So a latch either has to be cleared on
	// every draw, in which case it only ever catches the first declined caster in a run
	// of unchanged state, or it has to survive one, in which case it can outlive the masks
	// it stands for and drop a draw that would have rendered. Measured: the surviving
	// version moved 20485 pixels across frames 540 and 900 of civ_buildings.
	//
	// Reading the masks themselves has neither failure. It is the same test the strict
	// half of the fixed-function draw census makes -- the one that licensed dropping these
	// draws at all -- so the instrument and the behaviour cannot disagree. A poisoned
	// tracked word fails all three comparisons, so the error direction is "submit anyway".
	static bool Is_Inert_Depth_Pass_Draw();
	static void Debug_Note_Depth_Pass_Stencil();
	static void Debug_Report_Depth_Pass_Stencil();
	// Raised by Prepare_Direct_Draw: the vertex stream, index buffer, base vertex index and
	// FVF standing at the device were bound by a caller that went round the wrapper, so the
	// wrapper's flags no longer describe what is bound. Draw() consumes it by asking for the
	// bindings back before the next draw that would otherwise inherit them. See the note in
	// Prepare_Direct_Draw for why it is consumed there and not in Apply_Render_State_Changes.
	static bool							m_bForeignDeviceBindings;
	static bool							m_bShadowDepthPass; // current draws render into the shadow map
	static void Set_Shadow_Depth_Pass(bool active) { m_bShadowDepthPass = active; }
	static bool Is_Shadow_Depth_Pass() { return m_bShadowDepthPass; }
	// How much brighter than display white an additive effect is allowed to emit. 1 means
	// "no brighter", which is what an 8-bit target can hold and therefore the default; the
	// HDR path raises it once the scene is drawn somewhere a value above 1 survives.
	//
	// It is set from W3DShaderManager, a layer above this one, rather than read from there:
	// the wrapper cannot ask the game whether HDR is on without inverting the dependency, so
	// the answer is pushed down when it changes. Applied by the unit vertex shaders, and
	// only to additive effect draws -- see the note where lightingParams is assembled.
	static float						m_hdrEffectGain;
	static void Set_Hdr_Effect_Gain(float gain) { m_hdrEffectGain = gain; }
	static float Get_Hdr_Effect_Gain() { return m_hdrEffectGain; }
	// Backing store for the sun cull box declared below.
	static bool							m_bSunCullBoxValid;
	static Vector3						m_sunCullEye;
	static Vector3						m_sunCullRight;
	static Vector3						m_sunCullUp;
	static Vector3						m_sunCullFwd;
	static float						m_sunCullHalfWidth;
	static float						m_sunCullUpMin;
	static float						m_sunCullUpMax;
	static float						m_sunCullNear;
	static float						m_sunCullFar;
	// The current draw is a mesh the artist marked W3D_MESH_FLAG_CAST_SHADOW. Set by the
	// mesh renderer for the duration of one mesh and cleared straight after, so only draws
	// it owns can carry it. It is the tie-breaker for blended geometry, which the depth
	// pass otherwise cannot tell from a ground decal. See Apply_Render_State_Changes.
	static bool							m_bMeshCastsShadow;
	static void Set_Mesh_Casts_Shadow(bool casts) { m_bMeshCastsShadow = casts; }
	// The current draw is a blended effect its own renderer has declared a physical
	// caster: smoke, dust, steam -- matter that happens to be drawn as sprites. It is the
	// same tie-breaker m_bMeshCastsShadow is for rotor discs, for the callers that have no
	// mesh flag to carry: nothing in a particle's render state distinguishes a dust cloud
	// from a laser beam, and the difference is a property of the effect, not of the draw.
	//
	// Additive is still excluded above it, flag or no flag, so a system that declared
	// itself a caster by mistake and is drawn additively cannot lay solid shadow. Raised
	// by ShadowCastingEffectClass for the duration of one submission and cleared after.
	static bool							m_bEffectCastsShadow;
	static void Set_Effect_Casts_Shadow(bool casts) { m_bEffectCastsShadow = casts; }
	static bool Is_Effect_Casting_Shadow() { return m_bEffectCastsShadow; }
	// Set per draw when the mesh being drawn has at least one depth-writing pass, i.e.
	// it is a surface rather than an effect. Soft-blended passes of a surface are routed
	// (they are part of a mesh that is on the programmable path anyway, and must not be
	// split off it); soft-blended passes of an effect are not. Defaults to false, so
	// anything that is not a mesh-renderer draw -- particles, decals -- keeps the
	// conservative behaviour. See the note at useUnitShader.
	static bool							m_bMeshHasSolidPass;
	static void Set_Mesh_Has_Solid_Pass(bool solid) { m_bMeshHasSolidPass = solid; }
	static bool Get_Mesh_Has_Solid_Pass() { return m_bMeshHasSolidPass; }
	// Whether render_state.shader describes the draw about to happen.
	//
	// It does for anything that arrived through Set_Shader -- the mesh renderer, which is
	// what raises this. It does not for callers that write blend, depth and alpha-test
	// registers straight to the device and never touch the wrapper's shader: the terrain
	// blender, the water and shroud passes, the W3DShaderManager effects. For those,
	// render_state.shader still holds whatever the last mesh left in it, and the device
	// registers are the only description of the draw that exists.
	//
	// So the routing classifies from the shader when this is set and from the device
	// state when it is not. Measured over a replay, that distinction is worth 5031 draws
	// per 600 frames -- every one of them a non-mesh draw, and 3354 of them changing
	// whether the draw entered the shadow map.
	//
	// Stage 2 removes the split by giving those callers a technique of their own to
	// declare, the way terrain and roads already declare theirs.
	static bool							m_bMeshRendererDraw;
	static void Set_Mesh_Renderer_Draw(bool active) { m_bMeshRendererDraw = active; }
	static bool Get_Mesh_Renderer_Draw() { return m_bMeshRendererDraw; }
	// What the asset says this batch is, decided when the mesh type was registered
	// (see meshtechnique.h) rather than inferred here from render state. Set by the
	// mesh renderer per draw and cleared after it, like the flags above;
	// MESH_TECHNIQUE_UNCLASSIFIED for everything that does not come through it.
	static MeshTechnique				m_meshTechnique;
	static void Set_Mesh_Technique(MeshTechnique t) { m_meshTechnique = t; }
	static MeshTechnique Get_Mesh_Technique() { return m_meshTechnique; }
#ifdef RTS_DEBUG
	// Which DeclaredTechniqueClass scope, if any, a draw is inside. Names the source
	// of a declaration so a wrong one can be found from its report.
	static const char*					s_declarationSite;
	static void Set_Declaration_Site(const char* site) { s_declarationSite = site; }
	static const char* Get_Declaration_Site() { return s_declarationSite; }
#endif
	static void Set_Sun_VP(const float* m16);

	// The same orthographic box as m_sunVP, kept in world space so geometry can be culled
	// against it. The depth pass is drawn with the camera -- it has to be, the scene's
	// traversal takes one -- so everything downstream of it would otherwise keep asking
	// the camera what is visible, and a caster whose shadow reaches into the view but
	// which is itself off screen would be dropped. MeshClass::Render is the last such
	// place, and it is below the game layer, which is why the box lives here rather than
	// only in W3DShaderManager.
	//
	// Published once per frame from the same eye/basis/extent that built m_sunVP. Not
	// valid means cull nothing, so a frame that never sets it cannot lose geometry.
	static void Set_Sun_Cull_Box(const Vector3 &eye, const Vector3 &right, const Vector3 &up,
								 const Vector3 &fwd, float halfWidth, float upMin, float upMax,
								 float nearDist, float farDist);
	static void Clear_Sun_Cull_Box() { m_bSunCullBoxValid = false; }
	static bool Has_Sun_Cull_Box() { return m_bSunCullBoxValid; }
	static bool Cull_Sphere_By_Sun(const Vector3 &center, float radius);

	// The sun view's world-space right and up axes. Built from the same forward direction
	// and the same up hint D3DXMatrixLookAtLH used for the matrix behind m_sunVP, so a
	// quad spanned by these two lands square-on in the shadow map.
	//
	// Sprites need them. A billboard is a stand-in for something round, and which way it
	// should face depends on who is looking -- the camera in the visible pass, the sun in
	// the depth pass. Building the depth-pass quad from the camera's basis instead would
	// give a smoke puff a shadow that changes shape as the player orbits.
	static const Vector3 & Get_Sun_Right() { return m_sunCullRight; }
	static const Vector3 & Get_Sun_Up() { return m_sunCullUp; }

	// The direction the sun's light travels, i.e. the sun view's forward axis. A ribbon
	// needs this rather than the two above: it is not a billboard and has no freedom to
	// turn, so what the sun decides is only which way round its own axis it presents its
	// width. See StreakRendererClass::Render_Sun_Depth.
	static const Vector3 & Get_Sun_Forward() { return m_sunCullFwd; }

	// Screen-space reflections. The camera-view depth SSR marches against is produced
	// by re-running the shadow depth pass from the camera instead of the sun: same
	// shaders, same routing, same packed RGBA8 target. Only two things differ, and
	// both are conditioned on this flag -- the matrix handed to the depth shader, and
	// the viewport, which that pass forces square for the shadow map but which is
	// already correct here because this target is the size of the screen.
	static bool							m_bDepthPrepass;
	static float						m_depthVP[16];    // camera view*projection, row-major
	static void Set_Depth_Prepass(bool active) { m_bDepthPrepass = active; }
	static void Set_Depth_VP(const float* m16);
	// Bound for sampling by the PBR shader: the depth just described, and the scene
	// colour the rays actually read. That colour is the *previous* frame's -- the
	// current one is the live render target while units are drawing, and D3D9 leaves
	// a read from the bound render target undefined.
	static GfxTexture*		m_pSceneDepth;
	static GfxTexture*		m_pSceneColor;
	static bool Has_Ssr() { return m_pSceneDepth != nullptr && m_pSceneColor != nullptr; }
	// x = strength (0 disables the march without unbinding anything, as the shadow
	// strength does), y = max ray length in world units, zw = the projection's _33/_43,
	// with which the shader turns a stored z/w back into a view-space distance.
	static float						m_ssrParams[4];
	// Soft particles. Set only around draws that are genuinely airborne sprites, never
	// blanket-enabled for effect geometry: roads, tank tracks and scorch marks reach the
	// same shader, and they sit *on* the ground, so a depth fade would erase them
	// completely rather than soften them.
	static bool							m_softParticles;
	static float						m_softParticleFade;   // world units over which a sprite fades out
	static void Set_Soft_Particles(bool on, float fadeDistance)
		{ m_softParticles = on; m_softParticleFade = fadeDistance; }
	static bool Is_Soft_Particles() { return m_softParticles; }
	static float Get_Soft_Particle_Fade() { return m_softParticleFade; }
	static void Set_Ssr_Params(float strength, float maxDist, float proj33, float proj43)
	{
		m_ssrParams[0] = strength; m_ssrParams[1] = maxDist;
		m_ssrParams[2] = proj33;   m_ssrParams[3] = proj43;
	}
	static void Set_Shadow_Params(float bias, float strength,
								  float normalOffsetWorld, float meshBias,
								  float filterRadiusTexels)
	{
		m_shadowParams[0] = bias; m_shadowParams[1] = strength;
		m_shadowParams[2] = 1.0f / (float)SHADOW_MAP_SIZE;
		m_shadowParams[3] = filterRadiusTexels;
		m_shadowMeshParams[0] = normalOffsetWorld; m_shadowMeshParams[1] = meshBias;
		m_shadowMeshParams[2] = 0.0f; m_shadowMeshParams[3] = 0.0f;
	}
	// The map is bound whenever it exists, even with shadow mapping switched off: the
	// pixel shaders sample stage 5 unconditionally (ps_2_0 has no dynamic branching to
	// skip it) and D3D9 leaves a sample from an unbound stage undefined -- drivers
	// variously give black, white, or whatever was last bound there. The shadow strength
	// is what decides whether the result counts, and it is zero when the feature is off.
	static bool Has_Shadow_Map() { return m_dwShadowDepthVS != 0 && m_dwShadowDepthPS != 0 && m_pShadowMap != nullptr; }
	// Terrain overlay params: cloud scroll offset and which overlays are active.
	static bool							m_terrainCloudEnable;
	static bool							m_terrainNoiseEnable;
	// Cloud shadow: two layers drifting in world units, plus how dark the shade goes.
	// Roads read these too -- a cloud shadow has to cross a road without changing.
	static float						m_cloudScrollAX, m_cloudScrollAY;
	static float						m_cloudScrollBX, m_cloudScrollBY;
	static float						m_cloudStrength;
	static void Set_Terrain_Overlay(bool cloud, bool noise)
	{
		m_terrainCloudEnable = cloud; m_terrainNoiseEnable = noise;
	}
	static void Set_Cloud_Map(GfxTexture* tex) { m_pCloudMap = tex; }
	static void Set_Cloud_Shadow(float ax, float ay, float bx, float by, float strength)
	{
		m_cloudScrollAX = ax; m_cloudScrollAY = ay;
		m_cloudScrollBX = bx; m_cloudScrollBY = by;
		m_cloudStrength = strength;
	}
	// Stochastic tiling of the base terrain texture (terrain_ps c2/c3). The atlas size
	// has to come from the map -- its height is sized to whatever the class list needs --
	// and the lattice size is in world units.
	static Vector4						m_terrainAtlasParams;   // xy = atlas texels, zw = 1/atlas texels
	static Vector4						m_terrainTilingParams;  // x = on, y = lattice size
	static void Set_Terrain_Tiling(float atlasWidth, float atlasHeight, bool on, float latticeSize)
	{
		m_terrainAtlasParams.Set(atlasWidth, atlasHeight,
								 atlasWidth > 0.0f ? 1.0f/atlasWidth : 0.0f,
								 atlasHeight > 0.0f ? 1.0f/atlasHeight : 0.0f);
		m_terrainTilingParams.Set(on ? 1.0f : 0.0f, latticeSize, 0.0f, 0.0f);
	}
	// Procedural detail/relief layer (terrain_ps c4/c5). The sun direction is the same
	// one the CPU baked the terrain's vertex lighting with, so the relief agrees with the
	// shading already in the vertex colour instead of lighting from somewhere else.
	static Vector4						m_terrainDetailParams;  // x = on, y = albedo strength, z = relief strength, w = scale
	static Vector4						m_terrainSunDir;        // xyz = direction toward the sun
	static Vector4						m_terrainColourParams;  // x = macro colour variation strength
	static void Set_Terrain_Detail(bool on, float albedoStrength, float reliefStrength,
								   float scale, float colourStrength, const Vector3 &towardSun)
	{
		m_terrainDetailParams.Set(on ? 1.0f : 0.0f, albedoStrength, reliefStrength, scale);
		m_terrainSunDir.Set(towardSun.X, towardSun.Y, towardSun.Z, 0.0f);
		m_terrainColourParams.Set(colourStrength, 0.0f, 0.0f, 0.0f);
	}

	friend void DX8_Assert();
	friend class WW3D;
	friend class DX8IndexBufferClass;
	friend class DX8VertexBufferClass;
};

/**
** DeclaredTechniqueClass
**
** Declares what the draws inside a scope are, for the renderers that never go through
** the mesh renderer and so have no asset for the routing to ask -- particle systems,
** decals, tracks, projected shadows, water.
**
** Scoped rather than a pair of calls because these render functions return early in
** several places, and a declaration left standing would be inherited by whatever drew
** next. That is the same class of bug as the per-draw flags this replaces: state that
** outlives the thing it describes.
**
** Usage:
**     DeclaredTechniqueClass declare(MESH_TECHNIQUE_EFFECT);
*/
// Marks a scope whose draws are airborne sprites and may fade where they meet the scene.
// See DX8Wrapper::m_softParticles for why this is opt-in per draw site.
//
// Takes the answer either way, so the same class states "these are sprites" and "these are
// explicitly not" -- the sorting flush needs the second to keep a declaration standing at
// the flush site off geometry that was queued somewhere else entirely. It restores what it
// found rather than clearing, so the two nest.
class SoftParticleScopeClass
{
public:
	SoftParticleScopeClass(bool on, float fadeDistance)
		: PrevOn(DX8Wrapper::Is_Soft_Particles()),
		  PrevFade(DX8Wrapper::Get_Soft_Particle_Fade())
		{ DX8Wrapper::Set_Soft_Particles(on, fadeDistance); }
	~SoftParticleScopeClass()
		{ DX8Wrapper::Set_Soft_Particles(PrevOn, PrevFade); }
private:
	bool PrevOn;
	float PrevFade;

	SoftParticleScopeClass(const SoftParticleScopeClass &);
	SoftParticleScopeClass & operator = (const SoftParticleScopeClass &);
};

class DeclaredTechniqueClass
{
public:
	// The name identifies which declaration a draw was made under. Without it a
	// disagreement between a declaration and the routing says only that some scope is
	// wrong, and finding which means bisecting across as many runs as there are scopes.
	explicit DeclaredTechniqueClass(MeshTechnique technique, const char * site = "?")
	{
		DX8Wrapper::Set_Mesh_Technique(technique);
#ifdef RTS_DEBUG
		DX8Wrapper::Set_Declaration_Site(site);
#endif
	}
	~DeclaredTechniqueClass()
	{
		DX8Wrapper::Set_Mesh_Technique(MESH_TECHNIQUE_UNCLASSIFIED);
#ifdef RTS_DEBUG
		DX8Wrapper::Set_Declaration_Site(nullptr);
#endif
	}
private:
	DeclaredTechniqueClass(const DeclaredTechniqueClass &);
	DeclaredTechniqueClass & operator = (const DeclaredTechniqueClass &);
};

/*
** ShadowCastingEffectClass -- declare the draws inside a scope physical casters.
**
** For blended effects that are matter rather than light: smoke, dust, steam. Without
** it the depth pass masks them out, because nothing in a particle's render state tells
** a dust cloud from a laser beam -- both are blended, alpha-tested nowhere and write no
** depth. See m_bEffectCastsShadow.
**
** Scoped for the same reason DeclaredTechniqueClass is: the render functions that raise
** it return early in several places, and a flag left standing would be inherited by
** whatever draws next -- which in the depth pass means laying solid shadow under
** something that should not cast at all.
*/
class ShadowCastingEffectClass
{
public:
	ShadowCastingEffectClass() { DX8Wrapper::Set_Effect_Casts_Shadow(true); }
	~ShadowCastingEffectClass() { DX8Wrapper::Set_Effect_Casts_Shadow(false); }
private:
	ShadowCastingEffectClass(const ShadowCastingEffectClass &);
	ShadowCastingEffectClass & operator = (const ShadowCastingEffectClass &);
};

/*
** FFSiteScope -- name the function whose fixed-function writes these are.
**
** One at the head of every function that still sets a colour/alpha combine, a texgen, a
** light or a material. The wrapper attributes each fixed-function-only state word it
** pushes to the innermost enclosing site, so the census reads as a work order: which
** subsystem, how often, and whether the writes change anything.
**
** Restores the enclosing site rather than clearing it, because these nest -- a drawer
** sets up its own combine and then calls ShaderClass::Apply, which sets more. Clearing
** would credit the rest of the drawer's writes to nobody.
**
** Compiles to nothing outside a debug build.
*/
#ifdef RTS_DEBUG
class FFSiteScope
{
public:
	explicit FFSiteScope(const char * site) : Previous(DX8Wrapper::Debug_Get_FF_Site())
	{
		DX8Wrapper::Debug_Set_FF_Site(site);
	}
	~FFSiteScope() { DX8Wrapper::Debug_Set_FF_Site(Previous); }
private:
	const char * Previous;
	FFSiteScope(const FFSiteScope &);
	FFSiteScope & operator = (const FFSiteScope &);
};
#define FF_SITE(name) FFSiteScope _ff_site_scope(name)
#else
#define FF_SITE(name) ((void)0)
#endif

// shader system updates KJM v
WWINLINE void DX8Wrapper::Set_Vertex_Shader(DWORD vertex_shader)
{
#if 0 //(gth) some code is bypassing this accessor function so we can't count on this variable...
	// may be incorrect if shaders are created and destroyed dynamically
	if (Vertex_Shader==vertex_shader) return;
#endif

	Vertex_Shader=vertex_shader;
#ifdef RTS_DEBUG
	// Only an FVF changes the declaration; a compiled shader leaves the last one standing,
	// which is exactly the fact the vertex-layout census exists to record.
	if (vertex_shader < 0x10000) Debug_Vertex_FVF = vertex_shader;
#endif
	GFXCALL(Set_Vertex_Shader((GfxShaderHandle)Vertex_Shader));
}

WWINLINE void DX8Wrapper::Set_Pixel_Shader(DWORD pixel_shader)
{
	// may be incorrect if shaders are created and destroyed dynamically
	if (Pixel_Shader==pixel_shader) return;

	Pixel_Shader=pixel_shader;
	GFXCALL(Set_Pixel_Shader((GfxShaderHandle)Pixel_Shader));
}

WWINLINE void DX8Wrapper::Set_Vertex_Shader_Constant(int reg, const void* data, int count)
{
	int memsize=sizeof(Vector4)*count;

	// Skipping a redundant upload is only sound while every write goes through here --
	// a subsystem that calls SetVertexShaderConstantF on the device directly leaves this
	// cache claiming registers it no longer owns, and the next matching value is then
	// silently not uploaded. Keep all writes on this path.
	if (memcmp(data, &Vertex_Shader_Constants[reg],memsize)==0) return;

	memcpy(&Vertex_Shader_Constants[reg],data,memsize);
	GFXCALL(Set_Vertex_Shader_Constants(reg,(const float*)data,count));
}

WWINLINE void DX8Wrapper::Set_Pixel_Shader_Constant(int reg, const void* data, int count)
{
	int memsize=sizeof(Vector4)*count;

	// may be incorrect if shaders are destroyed and created dynamically
	if (memcmp(data, &Pixel_Shader_Constants[reg],memsize)==0) return;

	memcpy(&Pixel_Shader_Constants[reg],data,memsize);
	GFXCALL(Set_Pixel_Shader_Constants(reg,(const float*)data,count));
}
// shader system updates KJM ^

WWINLINE int DX8Wrapper::FF_Transform_Slot(unsigned which)
{
	if (which == (unsigned)D3DTS_VIEW)       return 0;
	if (which == (unsigned)D3DTS_PROJECTION) return 1;
	if (which >= (unsigned)D3DTS_TEXTURE0 && which <= (unsigned)D3DTS_TEXTURE7)
		return 2 + (int)(which - (unsigned)D3DTS_TEXTURE0);
	if (which == (unsigned)D3DTS_WORLD)      return 10;
	return -1;
}

WWINLINE unsigned DX8Wrapper::FF_Transform_Which(unsigned slot)
{
	switch (slot) {
	case 0:  return (unsigned)D3DTS_VIEW;
	case 1:  return (unsigned)D3DTS_PROJECTION;
	case 10: return (unsigned)D3DTS_WORLD;
	default: return (unsigned)D3DTS_TEXTURE0 + (slot - 2);
	}
}

WWINLINE void DX8Wrapper::Send_Transform_To_Device(unsigned which, const D3DMATRIX& m)
{
	// Tracked here and nowhere else, which is what makes DX8Transforms readable in place of
	// the device: every path that writes a matrix comes through this function.
	if (which < (unsigned)(D3DTS_WORLD+1)) DX8Transforms[which]=m;

	// Deferred, like the combine and the texgen and for the same reason. A matrix on the
	// device is read by fixed-function vertex processing and by nothing else -- the
	// programmable path concatenates world*view*projection on the CPU out of this array and
	// hands the shader a constant -- so it only has to be there for a draw that is actually
	// going out on fixed function, and Flush_Fixed_Function_State puts it there for one.
	// Measured before deferring: 1195831 matrices a 600-frame window reached D3D on the
	// shadow-map configuration, where the number of fixed-function draws is zero.
	const int slot = FF_Transform_Slot(which);
	if (slot >= 0) {
		FFTransformPending |= (1u << slot);
		return;
	}
	GFXCALL(Set_Transform(which,(const float*)&m));
#ifdef RTS_DEBUG
	Debug_Note_Device_Transform(which,(const float*)&m);
#endif
}

WWINLINE void DX8Wrapper::_Set_DX8_Transform(D3DTRANSFORMSTATETYPE transform, const D3DMATRIX& m)
{
	WWASSERT(transform<=D3DTS_WORLD);
	// The redundancy check that used to live here was disabled with the note "this
	// optimization is breaking generals because they set the transform behind our backs".
	// That is no longer true: the backend seam converted every direct device write, the
	// device-state audit now reads all eleven live transform slots back off D3D on a timer
	// with its own positive control, and over both shadow configurations it finds nothing
	// disagreeing. Re-enabling the check is a separate question -- it is a saved device
	// call, not a correctness fix -- and is deliberately not bundled here.

	// A texture matrix is an input to the routing block, which uploads it to the vertex
	// shader so a generated coordinate set can be transformed there. Writing one has to
	// invalidate a decision already taken, for the same reason the coordinate source
	// does -- see TEXGEN_STATE_CHANGED.
	if (transform >= D3DTS_TEXTURE0 && transform <= D3DTS_TEXTURE7) {
		render_state_changed |= (unsigned)TEXGEN_STATE_CHANGED;
	}
	SNAPSHOT_SAY(("DX8 - SetTransform %d [%f,%f,%f,%f][%f,%f,%f,%f][%f,%f,%f,%f]",
		transform,
		m.m[0][0],m.m[0][1],m.m[0][2],m.m[0][3],
		m.m[1][0],m.m[1][1],m.m[1][2],m.m[1][3],
		m.m[2][0],m.m[2][1],m.m[2][2],m.m[2][3]));
	DX8_RECORD_MATRIX_CHANGE();
	Send_Transform_To_Device((unsigned)transform,m);
}

WWINLINE void DX8Wrapper::_Get_DX8_Transform(D3DTRANSFORMSTATETYPE transform, D3DMATRIX& m)
{
	// Read from what the wrapper sent, not from the device. D3D11 has no transform state to
	// ask, so a read-back here would be a call no second backend could answer; and there is
	// nothing to gain from asking, since Send_Transform_To_Device is the only way a matrix
	// reaches D3D and it records every one.
	WWASSERT(transform<=D3DTS_WORLD);
	m=DX8Transforms[transform];
}

WWINLINE void DX8Wrapper::Note_Texture_Transform_Write(D3DTRANSFORMSTATETYPE transform)
{
	if (transform >= D3DTS_TEXTURE0 && transform <= D3DTS_TEXTURE7) {
		render_state_changed |= (unsigned)TEXGEN_STATE_CHANGED;
	}
}

// ----------------------------------------------------------------------------
//
// Set the index offset for the current index buffer
//
// ----------------------------------------------------------------------------

WWINLINE void DX8Wrapper::Set_Index_Buffer_Index_Offset(unsigned offset)
{
	if (render_state.index_base_offset==offset) return;
	render_state.index_base_offset=offset;
	render_state_changed|=INDEX_BUFFER_CHANGED;
}

// ----------------------------------------------------------------------------
// Set the fog settings. This function should be used, rather than setting the
// appropriate renderstates directly, because the shader sets some of the
// renderstates on a per-mesh / per-pass basis depending on global fog states
// (stored in the wrapper) as well as the shader settings.
// This function should be called rarely - once per scene would be appropriate.
// ----------------------------------------------------------------------------

WWINLINE void DX8Wrapper::Set_Fog(bool enable, const Vector3 &color, float start, float end)
{
	// Set global states
	FogEnable = enable;
	FogColor = Convert_Color(color,0.0f);

	// Invalidate the current shader (since the renderstates set by the shader
	// depend on the global fog settings as well as the actual shader settings)
	ShaderClass::Invalidate();

	// Set renderstates which are not affected by the shader
	Set_DX8_Render_State(D3DRS_FOGSTART, *(DWORD *)(&start));
	Set_DX8_Render_State(D3DRS_FOGEND,   *(DWORD *)(&end));
}


WWINLINE void DX8Wrapper::Set_Ambient(const Vector3& color)
{
	// The scene ambient has two writers -- this one, from WW3D::Render once per scene, and
	// Set_Light_Environment, from the mesh renderer and the heightmap. Both are named, so
	// that a D3DRS_AMBIENT write in the census says which of the two made it. This was the
	// last thing left in the "(unattributed)" row: two render words a frame, which is
	// exactly the number of scenes WW3D::Render draws.
	//
	// Not a candidate for removal in any case: the routing block reads
	// RenderStates[D3DRS_AMBIENT] to build the shared light environment the unit shaders
	// are handed, so this word feeds the programmable path as much as the fixed one.
	FF_SITE("DX8Wrapper::Set_Ambient");
	Ambient_Color=color;
	Set_DX8_Render_State(D3DRS_AMBIENT, DX8Wrapper::Convert_Color(color,0.0f));
}

// ----------------------------------------------------------------------------
//
// Set vertex buffer to be used in the subsequent render calls. If there was
// a vertex buffer being used earlier, release the reference to it. Passing
// nullptr just will release the vertex buffer.
//
// ----------------------------------------------------------------------------

WWINLINE void DX8Wrapper::Set_DX8_Material(const D3DMATERIAL8* mat)
{
	WWASSERT(mat);
	// Tracked, and no longer sent at all. A material is fixed-function vertex lighting and
	// nothing else, and that stage is off: D3DRS_LIGHTING reads FALSE off the device and no
	// draw turns it on, so D3D would ignore anything written here. The copy stays because
	// the routing block reads it to build the shader's material constants.
	//
	// Redundant asks are dropped here, as they are for every other tracked word. This one
	// had no such check and so counted every call as a write, which is why
	// VertexMaterialClass::Apply led the fixed-function census by an order of magnitude:
	// 857432 render words a window against 856292 calls, one apiece. The mesh renderer
	// sets a material per pass and the great majority of consecutive passes share one.
	//
	// CurrentMaterial is what the routing block reads, so equality here means nothing
	// observable changed. There is no longer a second, device-side copy to conflate it
	// with.
	if (memcmp(&CurrentMaterial, mat, sizeof(D3DMATERIAL8)) == 0) return;
	DX8_RECORD_MATERIAL_CHANGE();
	SNAPSHOT_SAY(("DX8 - SetMaterial"));
	CurrentMaterial = *mat;
#ifdef RTS_DEBUG
	Debug_Note_FF_State_Write(0, (unsigned)D3DRS_DIFFUSEMATERIALSOURCE);
#endif
}

WWINLINE void DX8Wrapper::Set_DX8_Render_State(D3DRENDERSTATETYPE state, unsigned value)
{
	// Can't monitor state changes because setShader call to GERD may change the states!
	if (RenderStates[state]==value) return;

#ifdef MESH_RENDER_SNAPSHOT_ENABLED
	if (WW3D::Is_Snapshot_Activated()) {
		StringClass value_name(0,true);
		Get_DX8_Render_State_Value_Name(value_name,state,value);
		SNAPSHOT_SAY(("DX8 - SetRenderState(state: %s, value: %s)",
			Get_DX8_Render_State_Name(state),
			value_name.str()));
	}
#endif

	RenderStates[state]=value;
	if (DX8Wrapper::Is_Deferred_FF_Render_State((unsigned)state)) {
		FFRenderPending[(unsigned)state >> 5] |= (1u << ((unsigned)state & 31u));
		FFStatePending = true;
#ifdef RTS_DEBUG
		Debug_Note_FF_State_Write(0, (unsigned)state);
#endif
		DX8_RECORD_RENDER_STATE_CHANGE();
		return;
	}
	if (state == D3DRS_ALPHATESTENABLE || state == D3DRS_ALPHAFUNC ||
	           state == D3DRS_ALPHAREF) {
		// Tracked, deliberately not sent. The alpha test is done by the shaders now --
		// alphatest.hlsli, from AlphaTestCtl, which DX8Wrapper::Draw derives from these
		// three tracked words at every draw. So they must go on being written and read;
		// they just stop reaching the device, whose own stage D3D11 does not have.
		//
		// Suppressing it here rather than at the call sites is what makes it complete.
		// Every live writer goes through this function -- ShaderClass::Apply twice,
		// dx8renderer's alpha-override scaling of the reference, and W3DWater's legacy
		// clip-plane path -- and editing four call sites would have left
		// the sixth to be found later, with the state sticky in between: nothing would
		// have turned the device's test back off.
		//
		// The one test this loses is W3DWater's NOTEQUAL, which the (ref, sign) encoding
		// cannot express and which therefore falls through as "discard nothing". That path
		// is WATER_TYPE_1_FB_REFLECTION, and WaterType = 0 is the only value present
		// anywhere in shipped content -- GameData.ini, no map.ini, no patch INI -- so no
		// shipped map can reach it. It is gated on a setting rather than structurally dead,
		// which is why the code stays; if it is ever wanted, AlphaTestCtl grows a mode.
		//
		// Everything else goes to the backend as written. The words a particular API has
		// no equivalent for -- ZBIAS, SOFTWAREVERTEXPROCESSING, and the legacy D3D8 states
		// D3D9 dropped -- used to be special-cased here. They are the backend's business
		// now, because which of them survive is a fact about the API and not the engine.
	} else {
		GFXCALL(Set_Render_State( (unsigned)state, value ));
	}
	DX8_RECORD_RENDER_STATE_CHANGE();
}

WWINLINE void DX8Wrapper::Set_DX8_Stream_Source(unsigned stream, GfxVertexBuffer* buffer, unsigned stride)
{
	GFXCALL(Set_Vertex_Stream(stream, (GfxVertexBuffer*)buffer, stride));
}

WWINLINE void DX8Wrapper::Set_DX8_Indices(GfxIndexBuffer* buffer, int base_vertex_index)
{
	GFXCALL(Set_Index_Buffer((GfxIndexBuffer*)buffer, base_vertex_index));
}

WWINLINE void DX8Wrapper::Draw_DX8_Indexed_Primitive(unsigned primitive_type, int base_vertex_index,
	unsigned min_vertex_index, unsigned vertex_count, unsigned start_index, unsigned primitive_count)
{
	GFXCALL(Draw_Indexed(primitive_type, base_vertex_index, min_vertex_index, vertex_count,
		start_index, primitive_count));
}

WWINLINE void DX8Wrapper::Draw_DX8_Primitive(unsigned primitive_type, unsigned start_vertex,
	unsigned primitive_count)
{
	GFXCALL(Draw(primitive_type, start_vertex, primitive_count));
}

WWINLINE void DX8Wrapper::Draw_DX8_Primitive_UP(unsigned primitive_type, unsigned primitive_count,
	const void* vertex_data, unsigned vertex_stride)
{
	GFXCALL(Draw_Up(primitive_type, primitive_count, vertex_data, vertex_stride));
}

WWINLINE bool DX8Wrapper::Get_DX8_Render_State(unsigned state, unsigned& value)
{
	if (Gfx == nullptr) return false;
	return Gfx->Get_Render_State(state, value);
}

WWINLINE GfxSurface* DX8Wrapper::Get_DX8_Render_Target_Surface(unsigned index)
{
	if (Gfx == nullptr) return nullptr;
	return (GfxSurface*)Gfx->Get_Render_Target(index);
}

WWINLINE GfxSurface* DX8Wrapper::Get_DX8_Depth_Target_Surface()
{
	if (Gfx == nullptr) return nullptr;
	return (GfxSurface*)Gfx->Get_Depth_Target();
}

WWINLINE bool DX8Wrapper::Copy_DX8_Surface(GfxSurface* source, const GfxRect* source_rect,
	GfxSurface* dest, const GfxRect* dest_rect)
{
	if (Gfx == nullptr) return false;
	return Gfx->Copy_Surface(source, source_rect, dest, dest_rect);
}

WWINLINE bool DX8Wrapper::Copy_DX8_Surface(GfxSurface* source, GfxSurface* dest)
{
	if (Gfx == nullptr) return false;
	return Gfx->Copy_Surface((GfxSurface*)source, nullptr, (GfxSurface*)dest, nullptr);
}

WWINLINE GfxVertexBuffer* DX8Wrapper::Create_DX8_Vertex_Buffer(unsigned size_in_bytes,
	unsigned fvf, unsigned usage)
{
	if (Gfx == nullptr) return nullptr;
	return (GfxVertexBuffer*)Gfx->Create_Vertex_Buffer(size_in_bytes, fvf, usage);
}

WWINLINE GfxIndexBuffer* DX8Wrapper::Create_DX8_Index_Buffer(unsigned index_count,
	unsigned usage)
{
	if (Gfx == nullptr) return nullptr;
	return (GfxIndexBuffer*)Gfx->Create_Index_Buffer(index_count, usage);
}

WWINLINE void DX8Wrapper::Release_DX8_Vertex_Buffer(GfxVertexBuffer* buffer)
{
	GFXCALL(Release_Vertex_Buffer((GfxVertexBuffer*)buffer));
}

WWINLINE void DX8Wrapper::Release_DX8_Index_Buffer(GfxIndexBuffer* buffer)
{
	GFXCALL(Release_Index_Buffer((GfxIndexBuffer*)buffer));
}

WWINLINE bool DX8Wrapper::Map_DX8_Vertex_Buffer(GfxVertexBuffer* buffer,
	unsigned offset_in_bytes, unsigned size_in_bytes, GfxMapMode mode, void** data)
{
	if (Gfx == nullptr) return false;
	return Gfx->Map_Vertex_Buffer((GfxVertexBuffer*)buffer, offset_in_bytes, size_in_bytes,
		mode, data);
}

WWINLINE void DX8Wrapper::Unmap_DX8_Vertex_Buffer(GfxVertexBuffer* buffer)
{
	GFXCALL(Unmap_Vertex_Buffer((GfxVertexBuffer*)buffer));
}

WWINLINE bool DX8Wrapper::Map_DX8_Index_Buffer(GfxIndexBuffer* buffer,
	unsigned offset_in_bytes, unsigned size_in_bytes, GfxMapMode mode, void** data)
{
	if (Gfx == nullptr) return false;
	return Gfx->Map_Index_Buffer((GfxIndexBuffer*)buffer, offset_in_bytes, size_in_bytes,
		mode, data);
}

WWINLINE void DX8Wrapper::Unmap_DX8_Index_Buffer(GfxIndexBuffer* buffer)
{
	GFXCALL(Unmap_Index_Buffer((GfxIndexBuffer*)buffer));
}

WWINLINE GfxTexture* DX8Wrapper::Create_DX8_Texture_Resource(unsigned width,
	unsigned height, unsigned levels, WW3DFormat format, unsigned usage)
{
	if (Gfx == nullptr) return nullptr;
	return Gfx->Create_Texture(width, height, levels, format, usage);
}

WWINLINE GfxTexture* DX8Wrapper::Create_DX8_Cube_Texture_Resource(unsigned edge_length,
	unsigned levels, WW3DFormat format, unsigned usage)
{
	if (Gfx == nullptr) return nullptr;
	return Gfx->Create_Cube_Texture(edge_length, levels, format, usage);
}

WWINLINE void DX8Wrapper::Reference_DX8_Texture(GfxTexture* texture)
{
	if (Gfx != nullptr) Gfx->Reference_Texture(texture);
}

WWINLINE void DX8Wrapper::Release_DX8_Texture_Resource(GfxTexture* texture)
{
	if (Gfx != nullptr) Gfx->Release_Texture(texture);
}

WWINLINE void DX8Wrapper::Release_DX8_Resource(GfxTexture*& texture)
{
	if (texture != nullptr && Gfx != nullptr) Gfx->Release_Texture(texture);
	texture = nullptr;
}

WWINLINE void DX8Wrapper::Release_DX8_Resource(GfxSurface*& surface)
{
	if (surface != nullptr && Gfx != nullptr) Gfx->Release_Surface(surface);
	surface = nullptr;
}

WWINLINE GfxSurface* DX8Wrapper::Create_DX8_Render_Target_Surface(unsigned width,
	unsigned height, WW3DFormat format, WW3DMultiSampleType multisample)
{
	if (Gfx == nullptr) return nullptr;
	return Gfx->Create_Render_Target_Surface(width, height, format, multisample);
}

WWINLINE GfxSurface* DX8Wrapper::Create_DX8_Depth_Stencil_Surface(unsigned width,
	unsigned height, WW3DZFormat format, WW3DMultiSampleType multisample)
{
	if (Gfx == nullptr) return nullptr;
	return Gfx->Create_Depth_Stencil_Surface(width, height, format, multisample);
}

WWINLINE GfxSurface* DX8Wrapper::Create_DX8_Offscreen_Surface(unsigned width,
	unsigned height, WW3DFormat format)
{
	if (Gfx == nullptr) return nullptr;
	return Gfx->Create_Offscreen_Surface(width, height, format);
}

WWINLINE void DX8Wrapper::Release_DX8_Surface_Resource(GfxSurface* surface)
{
	if (Gfx != nullptr) Gfx->Release_Surface(surface);
}

WWINLINE void DX8Wrapper::Reference_DX8_Surface(GfxSurface* surface)
{
	if (Gfx != nullptr) Gfx->Reference_Surface(surface);
}

WWINLINE bool DX8Wrapper::Copy_DX8_Surface_Rect(GfxSurface* source, const GfxRect* source_rect,
	GfxSurface* dest, const GfxRect* dest_rect, GfxCopyFilter filter)
{
	if (Gfx == nullptr) return false;
	return Gfx->Copy_Surface_Rect(source, source_rect, dest, dest_rect, filter);
}

WWINLINE bool DX8Wrapper::Set_DX8_Hardware_Cursor(GfxSurface* image, unsigned hot_x,
	unsigned hot_y)
{
	if (Gfx == nullptr) return false;
	return Gfx->Set_Hardware_Cursor(image, hot_x, hot_y);
}

WWINLINE void DX8Wrapper::Show_DX8_Hardware_Cursor(bool show)
{
	if (Gfx != nullptr) Gfx->Show_Hardware_Cursor(show);
}

WWINLINE void DX8Wrapper::Set_DX8_Hardware_Cursor_Position(unsigned x, unsigned y)
{
	if (Gfx != nullptr) Gfx->Set_Hardware_Cursor_Position(x, y);
}

WWINLINE bool DX8Wrapper::Save_DX8_Surface_To_File(const char* path, GfxSurface* surface)
{
	if (Gfx == nullptr) return false;
	return Gfx->Save_Surface_To_File(path, surface);
}

WWINLINE bool DX8Wrapper::Generate_DX8_Mips(GfxTexture* texture, unsigned base_level)
{
	if (Gfx == nullptr) return false;
	return Gfx->Generate_Mips(texture, base_level);
}

WWINLINE void DX8Wrapper::Set_DX8_Texture_Detail_Level(GfxTexture* texture,
	unsigned skip_levels)
{
	if (Gfx != nullptr) Gfx->Set_Texture_Detail_Level(texture, skip_levels);
}

WWINLINE unsigned DX8Wrapper::Get_DX8_Texture_Level_Count(GfxTexture* texture)
{
	if (Gfx == nullptr) return 0;
	return Gfx->Get_Texture_Level_Count(texture);
}

WWINLINE GfxSurface* DX8Wrapper::Get_DX8_Texture_Surface_Level(GfxTexture* texture,
	unsigned level)
{
	if (Gfx == nullptr) return nullptr;
	return Gfx->Get_Texture_Surface_Level(texture, level);
}

WWINLINE bool DX8Wrapper::Map_DX8_Texture(GfxTexture* texture, unsigned level,
	const GfxRect* rect, GfxMapMode mode, GfxMappedRect& mapped)
{
	if (Gfx == nullptr) return false;
	return Gfx->Map_Texture(texture, level, rect, mode, mapped);
}

WWINLINE void DX8Wrapper::Unmap_DX8_Texture(GfxTexture* texture, unsigned level)
{
	if (Gfx != nullptr) Gfx->Unmap_Texture(texture, level);
}

WWINLINE bool DX8Wrapper::Map_DX8_Surface(GfxSurface* surface, const GfxRect* rect,
	GfxMapMode mode, GfxMappedRect& mapped)
{
	if (Gfx == nullptr) return false;
	return Gfx->Map_Surface(surface, rect, mode, mapped);
}

WWINLINE void DX8Wrapper::Unmap_DX8_Surface(GfxSurface* surface)
{
	if (Gfx != nullptr) Gfx->Unmap_Surface(surface);
}

WWINLINE bool DX8Wrapper::Map_DX8_Volume_Texture(GfxTexture* texture, unsigned level,
	GfxMapMode mode, GfxMappedBox& mapped)
{
	if (Gfx == nullptr) return false;
	return Gfx->Map_Volume_Texture(texture, level, mode, mapped);
}

WWINLINE void DX8Wrapper::Unmap_DX8_Volume_Texture(GfxTexture* texture, unsigned level)
{
	if (Gfx != nullptr) Gfx->Unmap_Volume_Texture(texture, level);
}

WWINLINE bool DX8Wrapper::Describe_DX8_Surface(GfxSurface* surface,
	WW3DSurfaceDescription& desc)
{
	if (Gfx == nullptr) return false;
	return Gfx->Describe_Surface((GfxSurface*)surface, desc);
}

WWINLINE bool DX8Wrapper::Describe_DX8_Texture_Level(GfxTexture* texture,
	unsigned level, WW3DSurfaceDescription& desc)
{
	if (Gfx == nullptr) return false;
	return Gfx->Describe_Texture_Level((GfxTexture*)texture, level, desc);
}

WWINLINE bool DX8Wrapper::Map_DX8_Cube_Texture(GfxTexture* texture, unsigned face,
	unsigned level, const GfxRect* rect, GfxMapMode mode, GfxMappedRect& mapped)
{
	if (Gfx == nullptr) return false;
	return Gfx->Map_Cube_Texture(texture, face, level, rect, mode, mapped);
}

WWINLINE void DX8Wrapper::Unmap_DX8_Cube_Texture(GfxTexture* texture, unsigned face,
	unsigned level)
{
	if (Gfx != nullptr) Gfx->Unmap_Cube_Texture(texture, face, level);
}

WWINLINE bool DX8Wrapper::Describe_DX8_Depth_Texture_Level(GfxTexture* texture,
	unsigned level, WW3DZFormat& format)
{
	if (Gfx == nullptr) return false;
	return Gfx->Describe_Depth_Texture_Level(texture, level, format);
}

WWINLINE bool DX8Wrapper::Describe_DX8_Volume_Level(GfxTexture* texture, unsigned level,
	WW3DSurfaceDescription& desc, unsigned& depth)
{
	if (Gfx == nullptr) return false;
	return Gfx->Describe_Volume_Level(texture, level, desc, depth);
}

WWINLINE bool DX8Wrapper::Get_DX8_Viewport(D3DVIEWPORT8& viewport)
{
	if (Gfx == nullptr) return false;
	GfxViewport vp;
	if (!Gfx->Get_Viewport(vp)) return false;
	viewport.X = vp.X;
	viewport.Y = vp.Y;
	viewport.Width = vp.Width;
	viewport.Height = vp.Height;
	viewport.MinZ = vp.MinZ;
	viewport.MaxZ = vp.MaxZ;
	return true;
}

WWINLINE void DX8Wrapper::Set_DX8_Clip_Plane(DWORD Index, CONST float* pPlane)
{
	GFXCALL(Set_Clip_Plane( Index, pPlane ));
}

WWINLINE bool DX8Wrapper::Is_Sampler_Stage_State(unsigned state)
{
	switch (state) {
		case D3DTSS_MINFILTER: case D3DTSS_MAGFILTER: case D3DTSS_MIPFILTER:
		case D3DTSS_ADDRESSU:  case D3DTSS_ADDRESSV:  case D3DTSS_ADDRESSW:
		case D3DTSS_MAXANISOTROPY:
			return true;
		default:
			return false;
	}
}

WWINLINE void DX8Wrapper::Set_DX8_Texture_Stage_State(unsigned stage, D3DTEXTURESTAGESTATETYPE state, unsigned value)
{
	// How a texture is filtered and addressed is a sampler description now, not seven
	// indexed words. Set_Sampler is the only writer; see Get_Sampler for the shape a
	// caller uses instead.
	WWASSERT(!Is_Sampler_Stage_State((unsigned)state));
	Set_DX8_Stage_State_Unguarded(stage, state, value);
}

WWINLINE void DX8Wrapper::Set_DX8_Stage_State_Unguarded(unsigned stage, D3DTEXTURESTAGESTATETYPE state, unsigned value)
{
	if (stage >= MAX_TEXTURE_STAGES)
	{
		GFXCALL(Set_Texture_Stage_State(stage, (unsigned)state, value));
		return;
	}

	// Can't monitor state changes because setShader call to GERD may change the states!
	if (TextureStageStates[stage][(unsigned int)state]==value) return;
#ifdef MESH_RENDER_SNAPSHOT_ENABLED
	if (WW3D::Is_Snapshot_Activated()) {
		StringClass value_name(0,true);
		Get_DX8_Texture_Stage_State_Value_Name(value_name,state,value);
		SNAPSHOT_SAY(("DX8 - SetTextureStageState(stage: %d, state: %s, value: %s)",
			stage,
			Get_DX8_Texture_Stage_State_Name(state),
			value_name.str()));
	}
#endif

	TextureStageStates[stage][(unsigned int)state]=value;
	// The routing block reads these two to decide whether this draw generates its texture
	// coordinates, and to hand the vertex shader the mode and matrix that reproduce it.
	// Writing them has to invalidate a decision already taken, or the shader keeps
	// constants chosen before the texgen existed. See TEXGEN_STATE_CHANGED.
	if (state == D3DTSS_TEXCOORDINDEX || state == D3DTSS_TEXTURETRANSFORMFLAGS) {
		render_state_changed |= (unsigned)TEXGEN_STATE_CHANGED;
	}
	if (DX8Wrapper::Is_Deferred_FF_Stage_State((unsigned)state)) {
		FFStagePending[stage] |= (1u << ((unsigned)state & 31u));
		FFStatePending = true;
#ifdef RTS_DEBUG
		Debug_Note_FF_State_Write(1, (unsigned)state);
#endif
		DX8_RECORD_TEXTURE_STAGE_STATE_CHANGE();
		return;
	}
	GFXCALL(Set_Texture_Stage_State(stage, (unsigned)state, value));
	DX8_RECORD_TEXTURE_STAGE_STATE_CHANGE();
}

WWINLINE void DX8Wrapper::Set_DX8_Texture(unsigned int stage, GfxTexture* texture)
{
  	if (stage >= MAX_TEXTURE_STAGES)
  	{	GFXCALL(Set_Texture(stage, (GfxTexture*)texture));
  		return;
  	}

	if (Textures[stage]==texture) return;

	SNAPSHOT_SAY(("DX8 - SetTexture(%x) ",texture));

	if (Textures[stage]) Gfx->Release_Texture(Textures[stage]);
	Textures[stage] = texture;
	if (Textures[stage]) Gfx->Reference_Texture(Textures[stage]);
	GFXCALL(Set_Texture(stage, (GfxTexture*)texture));
	DX8_RECORD_TEXTURE_CHANGE();
}

WWINLINE HRESULT DX8Wrapper::_Copy_DX8_Rects(
  GfxSurface* pSourceSurface,
  CONST RECT* pSourceRectsArray,
  UINT cRects,
  GfxSurface* pDestinationSurface,
  CONST POINT* pDestPointsArray
)
{
	if (DX8Wrapper::Gfx == nullptr) return E_FAIL;

	// The fallback ladder -- plain copy, then stretch, then a CPU conversion -- is a
	// property of the API doing the copying, so it moved behind the seam with the calls.
	// What is left here is the D3D8 shape this entry point still wears for its callers:
	// an array of source rectangles and an array of destination points.
	if (cRects == 0 || !pSourceRectsArray) {
		return DX8Wrapper::Gfx->Copy_Surface((GfxSurface*)pSourceSurface, nullptr,
			(GfxSurface*)pDestinationSurface, nullptr) ? S_OK : E_FAIL;
	}

	HRESULT final_hr = S_OK;
	for (UINT i = 0; i < cRects; ++i) {
		const RECT* srcRect = &pSourceRectsArray[i];
		const POINT* destPt = pDestPointsArray ? &pDestPointsArray[i] : nullptr;
		GfxRect destRect;
		if (destPt) {
			destRect.left = destPt->x;
			destRect.top = destPt->y;
			destRect.right = destPt->x + (srcRect->right - srcRect->left);
			destRect.bottom = destPt->y + (srcRect->bottom - srcRect->top);
		}
		if (!DX8Wrapper::Gfx->Copy_Surface((GfxSurface*)pSourceSurface,
				reinterpret_cast<const GfxRect*>(srcRect),
				(GfxSurface*)pDestinationSurface, destPt ? &destRect : nullptr)) {
			final_hr = E_FAIL;
		}
	}
	return final_hr;
}

WWINLINE HRESULT DX8Wrapper::Set_DX8_Render_Target(
  GfxSurface* pRenderTarget,
  GfxSurface* pNewZStencil
)
{
	if (DX8Wrapper::Gfx == nullptr) return E_FAIL;
	return DX8Wrapper::Gfx->Set_Render_Target((GfxSurface*)pRenderTarget,
		(GfxSurface*)pNewZStencil) ? S_OK : E_FAIL;
}

WWINLINE Vector4 DX8Wrapper::Convert_Color(unsigned color)
{
	Vector4 col;
	col[3]=((color&0xff000000)>>24)/255.0f;
	col[0]=((color&0xff0000)>>16)/255.0f;
	col[1]=((color&0xff00)>>8)/255.0f;
	col[2]=((color&0xff)>>0)/255.0f;
//	col=Vector4(1.0f,1.0f,1.0f,1.0f);
	return col;
}

#if 0
WWINLINE unsigned int DX8Wrapper::Convert_Color(const Vector3& color, const float alpha)
{
	WWASSERT(color.X<=1.0f);
	WWASSERT(color.Y<=1.0f);
	WWASSERT(color.Z<=1.0f);
	WWASSERT(alpha<=1.0f);
	WWASSERT(color.X>=0.0f);
	WWASSERT(color.Y>=0.0f);
	WWASSERT(color.Z>=0.0f);
	WWASSERT(alpha>=0.0f);

	return D3DCOLOR_COLORVALUE(color.X,color.Y,color.Z,alpha);
}
WWINLINE unsigned int DX8Wrapper::Convert_Color(const Vector4& color)
{
	WWASSERT(color.X<=1.0f);
	WWASSERT(color.Y<=1.0f);
	WWASSERT(color.Z<=1.0f);
	WWASSERT(color.W<=1.0f);
	WWASSERT(color.X>=0.0f);
	WWASSERT(color.Y>=0.0f);
	WWASSERT(color.Z>=0.0f);
	WWASSERT(color.W>=0.0f);

	return D3DCOLOR_COLORVALUE(color.X,color.Y,color.Z,color.W);
}
#else

// ----------------------------------------------------------------------------
//
// Convert RGBA color from float vector to 32 bit integer
// Note: Color vector needs to be clamped to [0...1] range!
//
// ----------------------------------------------------------------------------

WWINLINE unsigned int DX8Wrapper::Convert_Color(const Vector3& color,float alpha)
{
#if defined(_MSC_VER) && _MSC_VER < 1300
	const float scale = 255.0;
	unsigned int col;

	// Multiply r, g, b and a components (0.0,...,1.0) by 255 and convert to integer. Or the integer values togerher
	// such that 32 bit integer has AAAAAAAARRRRRRRRGGGGGGGGBBBBBBBB.
	__asm
	{
		sub	esp,20					// space for a, r, g and b float plus fpu rounding mode

		// Store the fpu rounding mode

		fwait
		fstcw		[esp+16]				// store control word to stack
		mov		eax,[esp+16]		// load it to eax
		mov		edi,eax				// take copy
		and		eax,~(1024|2048)	// mask out certain bits
		or			eax,(1024|2048)	// or with precision control value "truncate"
		sub		edi,eax				// did it change?
		jz			skip					// .. if not, skip
		mov		[esp],eax			// .. change control word
		fldcw		[esp]
skip:

		// Convert the color

		mov	esi,dword ptr color
		fld	dword ptr[scale]

		fld	dword ptr[esi]			// r
		fld	dword ptr[esi+4]		// g
		fld	dword ptr[esi+8]		// b
		fld	dword ptr[alpha]		// a
		fld	st(4)
		fmul	st(4),st
		fmul	st(3),st
		fmul	st(2),st
		fmulp	st(1),st
		fistp	dword ptr[esp+0]		// a
		fistp	dword ptr[esp+4]		// b
		fistp	dword ptr[esp+8]		// g
		fistp	dword ptr[esp+12]		// r
		mov	ecx,[esp]				// a
		mov	eax,[esp+4]				// b
		mov	edx,[esp+8]				// g
		mov	ebx,[esp+12]			// r
		shl	ecx,24					// a << 24
		shl	ebx,16					// r << 16
		shl	edx,8						//	g << 8
		or		eax,ecx					// (a << 24) | b
		or		eax,ebx					// (a << 24) | (r << 16) | b
		or		eax,edx					// (a << 24) | (r << 16) | (g << 8) | b

		fstp	st(0)

		// Restore fpu rounding mode

		cmp	edi,0					// did we change the value?
		je		not_changed			// nope... skip now...
		fwait
		fldcw	[esp+16];
not_changed:
		add	esp,20

		mov	col,eax
	}
	return col;
#else
	return color.Convert_To_ARGB(alpha);
#endif // defined(_MSC_VER) && _MSC_VER < 1300
}

// ----------------------------------------------------------------------------
//
// Clamp color vector to [0...1] range
//
// ----------------------------------------------------------------------------

WWINLINE void DX8Wrapper::Clamp_Color(Vector4& color)
{
#if defined(_MSC_VER) && _MSC_VER < 1300
	if (CPUDetectClass::Has_CMOV_Instruction()) {
	__asm
	{
		mov	esi,dword ptr color

		mov edx,0x3f800000

		mov edi,dword ptr[esi]
		mov ebx,edi
		sar edi,31
		not edi			// mask is now zero if negative value
		and edi,ebx
		cmp edi,edx		// if no less than 1.0 set to 1.0
		cmovnb edi,edx
		mov dword ptr[esi],edi

		mov edi,dword ptr[esi+4]
		mov ebx,edi
		sar edi,31
		not edi			// mask is now zero if negative value
		and edi,ebx
		cmp edi,edx		// if no less than 1.0 set to 1.0
		cmovnb edi,edx
		mov dword ptr[esi+4],edi

		mov edi,dword ptr[esi+8]
		mov ebx,edi
		sar edi,31
		not edi			// mask is now zero if negative value
		and edi,ebx
		cmp edi,edx		// if no less than 1.0 set to 1.0
		cmovnb edi,edx
		mov dword ptr[esi+8],edi

		mov edi,dword ptr[esi+12]
		mov ebx,edi
		sar edi,31
		not edi			// mask is now zero if negative value
		and edi,ebx
		cmp edi,edx		// if no less than 1.0 set to 1.0
		cmovnb edi,edx
		mov dword ptr[esi+12],edi
	}
	return;
	}
#endif // defined(_MSC_VER) && _MSC_VER < 1300

	for (int i=0;i<4;++i) {
		float f=(color[i]<0.0f) ? 0.0f : color[i];
		color[i]=(f>1.0f) ? 1.0f : f;
	}
}

// ----------------------------------------------------------------------------
//
// Convert RGBA color from float vector to 32 bit integer
//
// ----------------------------------------------------------------------------

WWINLINE unsigned int DX8Wrapper::Convert_Color(const Vector4& color)
{
	return Convert_Color(reinterpret_cast<const Vector3&>(color),color[3]);
}

WWINLINE unsigned int DX8Wrapper::Convert_Color_Clamp(const Vector4& color)
{
	Vector4 clamped_color=color;
	DX8Wrapper::Clamp_Color(clamped_color);
	return Convert_Color(reinterpret_cast<const Vector3&>(clamped_color),clamped_color[3]);
}

#endif


WWINLINE void DX8Wrapper::Set_Alpha (const float alpha, unsigned int &color)
{
	unsigned char *component = (unsigned char*) &color;

	component [3] = 255.0f * alpha;
}

WWINLINE void DX8Wrapper::Get_Render_State(RenderStateStruct& state)
{
	state=render_state;
}

WWINLINE bool DX8Wrapper::Peek_Light(unsigned index, D3DLIGHT8& light)
{
	if (index>=4 || !render_state.LightEnable[index]) return false;
	light=render_state.Lights[index];
	return true;
}

WWINLINE void DX8Wrapper::Get_Shader(ShaderClass& shader)
{
	shader=render_state.shader;
}

WWINLINE void DX8Wrapper::Set_Texture(unsigned stage,TextureBaseClass* texture)
{
	WWASSERT(stage<(unsigned int)CurrentCaps->Get_Max_Textures_Per_Pass());
	if (texture==render_state.Textures[stage]) return;
	REF_PTR_SET(render_state.Textures[stage],texture);
	render_state_changed|=(TEXTURE0_CHANGED<<stage);
}

WWINLINE void DX8Wrapper::Set_Material(const VertexMaterialClass* material)
{
/*	if (material && render_state.material &&
		// !stricmp(material->Get_Name(),render_state.material->Get_Name())) {
		material->Get_CRC()!=render_state.material->Get_CRC()) {
		return;
	}
*/
//	if (material==render_state.material) {
//		return;
//	}
	REF_PTR_SET(render_state.material,const_cast<VertexMaterialClass*>(material));
	render_state_changed|=MATERIAL_CHANGED;
	SNAPSHOT_SAY(("DX8Wrapper::Set_Material(%s)",material ? material->Get_Name() : "null"));
}

WWINLINE void DX8Wrapper::Set_Shader(const ShaderClass& shader)
{
	if (!ShaderClass::ShaderDirty && ((unsigned&)shader==(unsigned&)render_state.shader)) {
		return;
	}
	render_state.shader=shader;
	render_state_changed|=SHADER_CHANGED;
#ifdef MESH_RENDER_SNAPSHOT_ENABLED
	StringClass str;
#endif
	SNAPSHOT_SAY(("DX8Wrapper::Set_Shader(%s)",shader.Get_Description(str).str()));
}

WWINLINE void DX8Wrapper::Set_Projection_Transform_With_Z_Bias(const Matrix4x4& matrix, float znear, float zfar)
{
	ZFar=zfar;
	ZNear=znear;
	D3DMATRIX projection=To_D3DMATRIX(matrix);

	if (!Get_Current_Caps()->Support_ZBias() && ZNear!=ZFar) {
		float tmp_zbias=ZBias;
		tmp_zbias*=(1.0f/16.0f);
		tmp_zbias*=1.0f / (ZFar - ZNear);
		projection.m[2][2]-=tmp_zbias*projection.m[3][2];
	}
	Send_Transform_To_Device((unsigned)D3DTS_PROJECTION,projection);
}

WWINLINE void DX8Wrapper::Set_Transform(D3DTRANSFORMSTATETYPE transform,const Matrix4x4& m)
{
	switch ((int)transform) {
	case D3DTS_WORLD:
		render_state.world=To_D3DMATRIX(m);
		render_state_changed|=(unsigned)WORLD_CHANGED;
		render_state_changed&=~(unsigned)WORLD_IDENTITY;
		break;
	case D3DTS_VIEW:
		render_state.view=To_D3DMATRIX(m);
		render_state_changed|=(unsigned)VIEW_CHANGED;
		render_state_changed&=~(unsigned)VIEW_IDENTITY;
		break;
	case D3DTS_PROJECTION:
		{
			// This local used to be spelled ProjectionMatrix, shadowing the static member of
			// the same name that the routing block read to build every shader's projection.
			// So a projection set through this overload -- render2d does it twice per 2D
			// pass, dazzle twice more -- reached the device and left the static holding the
			// camera's. It never showed because the routing block preferred a device
			// read-back and only fell through to the static if that failed. Both are gone
			// now: there is one tracked projection, DX8Transforms[D3DTS_PROJECTION], written
			// on the way to the device and read by everything.
			D3DMATRIX projection=To_D3DMATRIX(m);
			ZFar=0.0f;
			ZNear=0.0f;
			Send_Transform_To_Device((unsigned)D3DTS_PROJECTION,projection);
		}
		break;
	default:
		DX8_RECORD_MATRIX_CHANGE();
		D3DMATRIX dxm=To_D3DMATRIX(m);
		Note_Texture_Transform_Write(transform);
		Send_Transform_To_Device((unsigned)transform,dxm);
		break;
	}
}

WWINLINE void DX8Wrapper::Set_Transform(D3DTRANSFORMSTATETYPE transform,const D3DMATRIX& m)
{
	switch ((int)transform) {
	case D3DTS_WORLD:
		render_state.world=m;
		render_state_changed|=(unsigned)WORLD_CHANGED;
		render_state_changed&=~(unsigned)WORLD_IDENTITY;
		break;
	case D3DTS_VIEW:
		render_state.view=m;
		render_state_changed|=(unsigned)VIEW_CHANGED;
		render_state_changed&=~(unsigned)VIEW_IDENTITY;
		break;
	default:
		DX8_RECORD_MATRIX_CHANGE();
		Note_Texture_Transform_Write(transform);
		Send_Transform_To_Device((unsigned)transform,m);
		break;
	}
}

WWINLINE void DX8Wrapper::Set_Transform(D3DTRANSFORMSTATETYPE transform,const Matrix3D& m)
{
	switch ((int)transform) {
	case D3DTS_WORLD:
		render_state.world=To_D3DMATRIX(m);
		render_state_changed|=(unsigned)WORLD_CHANGED;
		render_state_changed&=~(unsigned)WORLD_IDENTITY;
		break;
	case D3DTS_VIEW:
		render_state.view=To_D3DMATRIX(m);
		render_state_changed|=(unsigned)VIEW_CHANGED;
		render_state_changed&=~(unsigned)VIEW_IDENTITY;
		break;
	default:
		DX8_RECORD_MATRIX_CHANGE();
		D3DMATRIX dxm=To_D3DMATRIX(m);
		Note_Texture_Transform_Write(transform);
		Send_Transform_To_Device((unsigned)transform,dxm);
		break;
	}
}

WWINLINE bool DX8Wrapper::Is_World_Identity()
{
	return !!(render_state_changed&(unsigned)WORLD_IDENTITY);
}

WWINLINE bool DX8Wrapper::Is_View_Identity()
{
	return !!(render_state_changed&(unsigned)VIEW_IDENTITY);
}

WWINLINE void DX8Wrapper::Get_Transform(D3DTRANSFORMSTATETYPE transform, Matrix4x4& m)
{
	switch ((int)transform) {
	case D3DTS_WORLD:
		if (render_state_changed&WORLD_IDENTITY) m.Make_Identity();
		else m=To_Matrix4x4(render_state.world);
		break;
	case D3DTS_VIEW:
		if (render_state_changed&VIEW_IDENTITY) m.Make_Identity();
		else m=To_Matrix4x4(render_state.view);
		break;
	default:
		// Projection and the texture matrices: tracked rather than asked for, see
		// _Get_DX8_Transform. World and view above never came off the device at all.
		WWASSERT(transform<=D3DTS_WORLD);
		m=To_Matrix4x4(DX8Transforms[transform]);
		break;
	}
}

WWINLINE void DX8Wrapper::Set_Render_State(const RenderStateStruct& state)
{
	int i;

	if (render_state.index_buffer) {
		render_state.index_buffer->Release_Engine_Ref();
	}

	for (i=0;i<MAX_VERTEX_STREAMS;++i)
	{
		if (render_state.vertex_buffers[i])
		{
			render_state.vertex_buffers[i]->Release_Engine_Ref();
		}
	}

	render_state=state;
	render_state_changed=0xffffffff;

	if (render_state.index_buffer) {
		render_state.index_buffer->Add_Engine_Ref();
	}

	for (i=0;i<MAX_VERTEX_STREAMS;++i)
	{
		if (render_state.vertex_buffers[i])
		{
			render_state.vertex_buffers[i]->Add_Engine_Ref();
		}
	}
}

WWINLINE void DX8Wrapper::Release_Render_State()
{
	int i;

	if (render_state.index_buffer) {
		render_state.index_buffer->Release_Engine_Ref();
	}

	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		if (render_state.vertex_buffers[i]) {
			render_state.vertex_buffers[i]->Release_Engine_Ref();
		}
	}

	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		REF_PTR_RELEASE(render_state.vertex_buffers[i]);
	}
	REF_PTR_RELEASE(render_state.index_buffer);
	REF_PTR_RELEASE(render_state.material);


	for (i=0;i<MAX_TEXTURE_STAGES;++i)
	{
		REF_PTR_RELEASE(render_state.Textures[i]);
	}
}


WWINLINE RenderStateStruct::RenderStateStruct()
	:
	material(0),
	index_buffer(0)
{
	unsigned i;
	for (i=0;i<MAX_VERTEX_STREAMS;++i) vertex_buffers[i]=0;
	for (i=0;i<MAX_TEXTURE_STAGES;++i) Textures[i]=0;
}

WWINLINE RenderStateStruct::~RenderStateStruct()
{
	unsigned i;
	REF_PTR_RELEASE(material);
	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		REF_PTR_RELEASE(vertex_buffers[i]);
	}
	REF_PTR_RELEASE(index_buffer);

	for (i=0;i<MAX_TEXTURE_STAGES;++i)
	{
		REF_PTR_RELEASE(Textures[i]);
	}
}


WWINLINE RenderStateStruct& RenderStateStruct::operator= (const RenderStateStruct& src)
{
	unsigned i;
	REF_PTR_SET(material,src.material);
	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		REF_PTR_SET(vertex_buffers[i],src.vertex_buffers[i]);
	}
	REF_PTR_SET(index_buffer,src.index_buffer);

	for (i=0;i<MAX_TEXTURE_STAGES;++i)
	{
		REF_PTR_SET(Textures[i],src.Textures[i]);
	}

	LightEnable[0]=src.LightEnable[0];
	LightEnable[1]=src.LightEnable[1];
	LightEnable[2]=src.LightEnable[2];
	LightEnable[3]=src.LightEnable[3];
	if (LightEnable[0]) {
		Lights[0]=src.Lights[0];
		if (LightEnable[1]) {
			Lights[1]=src.Lights[1];
			if (LightEnable[2]) {
				Lights[2]=src.Lights[2];
				if (LightEnable[3]) {
					Lights[3]=src.Lights[3];
				}
			}
		}
	}

	shader=src.shader;
	world=src.world;
	view=src.view;
	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		vertex_buffer_types[i]=src.vertex_buffer_types[i];
	}
	index_buffer_type=src.index_buffer_type;
	vba_offset=src.vba_offset;
	vba_count=src.vba_count;
	iba_offset=src.iba_offset;
	index_base_offset=src.index_base_offset;

	return *this;
}

WWINLINE unsigned int DX8Wrapper::Get_Surface_Size(const WW3DSurfaceDescription& desc)
{
	unsigned int width = desc.Width;
	unsigned int height = desc.Height;
	unsigned int aligned_width = (width + 3) & ~3;
	unsigned int aligned_height = (height + 3) & ~3;
	switch (desc.Format) {
		case WW3D_FORMAT_DXT1:
			return (aligned_width * aligned_height) / 2;
		case WW3D_FORMAT_DXT2:
		case WW3D_FORMAT_DXT3:
		case WW3D_FORMAT_DXT4:
		case WW3D_FORMAT_DXT5:
			return aligned_width * aligned_height;
		case WW3D_FORMAT_A8R8G8B8:
		case WW3D_FORMAT_X8R8G8B8:
			return width * height * 4;
		case WW3D_FORMAT_R8G8B8:
			return width * height * 3;
		case WW3D_FORMAT_R5G6B5:
		case WW3D_FORMAT_X1R5G5B5:
		case WW3D_FORMAT_A1R5G5B5:
		case WW3D_FORMAT_A4R4G4B4:
			return width * height * 2;
		case WW3D_FORMAT_A8:
		case WW3D_FORMAT_L8:
			return width * height;
		default:
			return width * height * 4;
	}
}

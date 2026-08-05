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
#include "WWMath/matrix4.h"
#include "statistics.h"
#include "WWLib/wwstring.h"
#include "WW3D2/lightenvironment.h"
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
// view-projection, for the SSR reprojection), c22 (mean cubemap colour) and c23 (shadow
// normal offset + mesh bias). This is the shadow-cache size;
// keep it above the highest register any pixel shader writes, or
// Set_Pixel_Shader_Constant memcpys past the array and corrupts adjacent statics.
const unsigned MAX_PIXEL_SHADER_CONSTANTS=32;
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
		light_changes(0),
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
	unsigned light_changes;
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
#define DX8_RECORD_LIGHT_CHANGE()				FrameStatistics.light_changes++
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

#ifdef WWDEBUG
#define DX8CALL_HRES(x,res) DX8_Assert(); res = DX8Wrapper::_Get_D3D_Device8()->x; DX8_ErrorCode(res); DX8Wrapper::Increment_DX8_CallCount();
#define DX8CALL(x) DX8_Assert(); DX8_ErrorCode(DX8Wrapper::_Get_D3D_Device8()->x); DX8Wrapper::Increment_DX8_CallCount();
#define DX8CALL_D3D(x) DX8_Assert(); DX8_ErrorCode(DX8Wrapper::_Get_D3D8()->x); DX8Wrapper::Increment_DX8_CallCount();
#define DX8_THREAD_ASSERT() if (_DX8SingleThreaded) { WWASSERT_PRINT(DX8Wrapper::_Get_Main_Thread_ID()==ThreadClass::_Get_Current_Thread_ID(),"DX8Wrapper::DX8 calls must be called from the main thread!"); }
#else
#define DX8CALL_HRES(x,res) res = DX8Wrapper::_Get_D3D_Device8()->x; DX8Wrapper::Increment_DX8_CallCount();
#define DX8CALL(x) DX8Wrapper::_Get_D3D_Device8()->x; DX8Wrapper::Increment_DX8_CallCount();
#define DX8CALL_D3D(x) DX8Wrapper::_Get_D3D8()->x; DX8Wrapper::Increment_DX8_CallCount();
#define DX8_THREAD_ASSERT() ;
#endif


#define no_EXTENDED_STATS
// EXTENDED_STATS collects additional timing statistics by turning off parts
// of the 3D drawing system (terrain, objects, etc.)
#ifdef EXTENDED_STATS
class DX8_Stats
{
public:
	bool m_showingStats;
	bool m_disableTerrain;
	bool m_disableWater;
	bool m_disableObjects;
	bool m_disableOverhead;
	bool m_disableConsole;
	int  m_debugLinesToShow;
	int	 m_sleepTime;
public:
	DX8_Stats::DX8_Stats() {
		m_disableConsole = m_showingStats = m_disableTerrain = m_disableWater = m_disableOverhead = m_disableObjects = false;
		m_sleepTime = 0;
		m_debugLinesToShow = -1; // -1 means show all expected lines of output
	}
};
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
#ifdef EXTENDED_STATS
	static DX8_Stats stats;
#endif

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
	static unsigned int Get_Surface_Size(const D3DSURFACE_DESC& desc);

	/*
	** Rendering
	*/
	static void Begin_Scene();
	static void End_Scene(bool flip_frame = true);

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
	static void Set_Render_State(const RenderStateStruct& state);
	static void Release_Render_State();

	static void Set_DX8_Material(const D3DMATERIAL8* mat);

	static void Set_Gamma(float gamma,float bright,float contrast,bool calibrate=true,bool uselimit=true);

	// Set_ and Get_Transform() functions take the matrix in Westwood convention format.

	static void Set_Projection_Transform_With_Z_Bias(const Matrix4x4& matrix,float znear, float zfar);	// pointer to 16 matrices

	static void Set_Transform(D3DTRANSFORMSTATETYPE transform,const Matrix4x4& m);
	static void Set_Transform(D3DTRANSFORMSTATETYPE transform,const Matrix3D& m);
	static void Get_Transform(D3DTRANSFORMSTATETYPE transform, Matrix4x4& m);
	static void Set_World_Identity();
	static void Set_View_Identity();
	static bool Is_World_Identity();
	static bool Is_View_Identity();

	// Note that *_DX8_Transform() functions take the matrix in DX8 format - transposed from Westwood convention.

	static void _Set_DX8_Transform(D3DTRANSFORMSTATETYPE transform, const D3DMATRIX& m);
	static void _Get_DX8_Transform(D3DTRANSFORMSTATETYPE transform, D3DMATRIX& m);

	static void Set_DX8_Light(int index,D3DLIGHT8* light);
	static void Set_DX8_Render_State(D3DRENDERSTATETYPE state, unsigned value);
	static void Set_DX8_Clip_Plane(DWORD Index, CONST float* pPlane);
	static void Set_DX8_Texture_Stage_State(unsigned stage, D3DTEXTURESTAGESTATETYPE state, unsigned value);
	static void Set_DX8_Texture(unsigned int stage, IDirect3DBaseTexture8* texture);
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

	static IDirect3DVolumeTexture8* _Create_DX8_Volume_Texture
	(
		unsigned int width,
		unsigned int height,
		unsigned int depth,
		WW3DFormat format,
		MipCountType mip_level_count,
		D3DPOOL pool=D3DPOOL_MANAGED
	);

	static IDirect3DCubeTexture8* _Create_DX8_Cube_Texture
	(
		unsigned int width,
		unsigned int height,
		WW3DFormat format,
		MipCountType mip_level_count,
		D3DPOOL pool=D3DPOOL_MANAGED,
		bool rendertarget=false
	);


	static IDirect3DTexture8* _Create_DX8_ZTexture
	(
		unsigned int width,
		unsigned int height,
		WW3DZFormat zformat,
		MipCountType mip_level_count,
		D3DPOOL pool=D3DPOOL_MANAGED
	);


	static IDirect3DTexture8 * _Create_DX8_Texture
	(
		unsigned int width,
		unsigned int height,
		WW3DFormat format,
		MipCountType mip_level_count,
		D3DPOOL pool=D3DPOOL_MANAGED,
		bool rendertarget=false
	);
	static IDirect3DTexture8 * _Create_DX8_Texture(const char *filename, MipCountType mip_level_count);
	static IDirect3DTexture8 * _Create_DX8_Texture(IDirect3DSurface8 *surface, MipCountType mip_level_count);

	static IDirect3DSurface8 * _Create_DX8_Surface(unsigned int width, unsigned int height, WW3DFormat format);
	static IDirect3DSurface8 * _Create_DX8_Surface(const char *filename);
	static IDirect3DSurface8 * _Get_DX8_Front_Buffer();
	static SurfaceClass * _Get_DX8_Back_Buffer(unsigned int num=0);

	static HRESULT _Copy_DX8_Rects(
			IDirect3DSurface8* pSourceSurface,
			CONST RECT* pSourceRectsArray,
			UINT cRects,
			IDirect3DSurface8* pDestinationSurface,
			CONST POINT* pDestPointsArray
	);

	static HRESULT D3D9_CreateImageSurface_Helper(
		IDirect3DDevice9* device,
		unsigned int width,
		unsigned int height,
		D3DFORMAT format,
		IDirect3DSurface9** ppSurface
	);


	static HRESULT Set_DX8_Render_Target(
			IDirect3DSurface8* pRenderTarget,
			IDirect3DSurface8* pNewZStencil
	);

	static void _Update_Texture(TextureClass *system, TextureClass *video);
	static void Flush_DX8_Resource_Manager(unsigned int bytes=0);
	static unsigned int Get_Free_Texture_RAM();

	static unsigned _Get_Main_Thread_ID() { return _MainThreadID; }
	static const D3DADAPTER_IDENTIFIER8& Get_Current_Adapter_Identifier() { return CurrentAdapterIdentifier; }

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
	** Additional swap chain interface
	**
	**		Use this interface to render to multiple windows (in windowed mode).
	**	To render to an additional window, the sequence of calls should look
	**	something like this:
	**
	**	DX8Wrapper::Set_Render_Target (swap_chain_ptr);
	**
	**	WW3D::Begin_Render (true, true, Vector3 (0, 0, 0));
	**	WW3D::Render (scene, camera, FALSE, FALSE);
	**	WW3D::End_Render ();
	**
	**	swap_chain_ptr->Present (nullptr, nullptr, nullptr, nullptr);
	**
	**	DX8Wrapper::Set_Render_Target ((IDirect3DSurface8 *)nullptr);
	**
	*/
	static IDirect3DSwapChain8 *	Create_Additional_Swap_Chain (HWND render_window);

	/*
	** Render target interface. If render target format is WW3D_FORMAT_UNKNOWN, current display format is used.
	*/
	static TextureClass *	Create_Render_Target (int width, int height, WW3DFormat format = WW3D_FORMAT_UNKNOWN);

	static void					Set_Render_Target (IDirect3DSurface8 *render_target, bool use_default_depth_buffer = false);
	static void					Set_Render_Target (IDirect3DSurface8* render_target, IDirect3DSurface8* dpeth_buffer);

	static void					Set_Render_Target (IDirect3DSwapChain8 *swap_chain);
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

	static void Set_Vertex_Shader_Constant(int reg, const void* data, int count);
	static void Set_Pixel_Shader_Constant(int reg, const void* data, int count);

	static DWORD Get_Vertex_Processing_Behavior() { return Vertex_Processing_Behavior; }

	// Needed by scene lighting class
	static void						Set_Ambient(const Vector3& color);
	static const Vector3&		Get_Ambient() { return Ambient_Color; }
	// shader system updates KJM ^




	static IDirect3DDevice8* _Get_D3D_Device8() { return D3DDevice; }
	static IDirect3D8* _Get_D3D8() { return D3DInterface; }
	/// Returns the display format - added by TR for video playback - not part of W3D
	static WW3DFormat	getBackBufferFormat();
	static bool Reset_Device(bool reload_assets=true);

	static const DX8Caps*	Get_Current_Caps() { WWASSERT(CurrentCaps); return CurrentCaps; }

	static bool Registry_Save_Render_Device( const char * sub_key );
	static bool Registry_Load_Render_Device( const char * sub_key, bool resize_window );

	static const char* Get_DX8_Render_State_Name(D3DRENDERSTATETYPE state);
	static const char* Get_DX8_Texture_Stage_State_Name(D3DTEXTURESTAGESTATETYPE state);
	static unsigned Get_DX8_Render_State(D3DRENDERSTATETYPE state) { return RenderStates[state]; }

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

	static void Invalidate_Cached_Render_States();
	// Read the texture stage states the draw-routing predicate consults back from the
	// device into the cache. Invalidation fills that cache with a sentinel so no needed
	// write is skipped, which is right for writing but wrong for reading -- and the
	// predicate reads it. Call after invalidating mid-frame.
	static void Resync_Texture_Stage_State_Cache();

	static void Set_Draw_Polygon_Low_Bound_Limit(unsigned n) { DrawPolygonLowBoundLimit=n; }

protected:

	static bool	Create_Device();
	static void Release_Device();

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

	static void Set_MSAA_Mode(D3DMULTISAMPLE_TYPE mode) { MultiSampleAntiAliasing = mode; }
	static D3DMULTISAMPLE_TYPE Get_MSAA_Mode() { return MultiSampleAntiAliasing; }

	static void	Set_Swap_Interval(int swap);
	static int	Get_Swap_Interval();
	static void Set_Polygon_Mode(int mode);

	/*
	** Internal functions
	*/
	static void Resize_And_Position_Window();
	static bool Find_Color_And_Z_Mode(int resx,int resy,int bitdepth,D3DFORMAT * set_colorbuffer,D3DFORMAT * set_backbuffer, D3DFORMAT * set_zmode);
	static bool Find_Color_Mode(D3DFORMAT colorbuffer, int resx, int resy, UINT *mode);
	static bool Find_Z_Mode(D3DFORMAT colorbuffer,D3DFORMAT backbuffer, D3DFORMAT *zmode);
	static bool Test_Z_Mode(D3DFORMAT colorbuffer,D3DFORMAT backbuffer, D3DFORMAT zmode);
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
	static D3DFORMAT					DisplayFormat;
	static D3DMULTISAMPLE_TYPE	MultiSampleAntiAliasing;


	// shader system updates KJM v
	static DWORD							Vertex_Shader;
	static DWORD							Pixel_Shader;

	static Vector4							Vertex_Shader_Constants[MAX_VERTEX_SHADER_CONSTANTS];
	static Vector4							Pixel_Shader_Constants[MAX_PIXEL_SHADER_CONSTANTS];

	static LightEnvironmentClass*		Light_Environment;

	static DWORD							Vertex_Processing_Behavior;

	static ZTextureClass*				Shadow_Map[MAX_SHADOW_MAPS];

	static Vector3							Ambient_Color;
	// shader system updates KJM ^

	static bool								world_identity;
	static unsigned						RenderStates[256];
	static unsigned						TextureStageStates[MAX_TEXTURE_STAGES][32];
	static IDirect3DBaseTexture8 *	Textures[MAX_TEXTURE_STAGES];

	// These fog settings are constant for all objects in a given scene,
	// unlike the matching renderstates which vary based on shader settings.
	static bool								FogEnable;
	static D3DCOLOR						FogColor;

	static DX8FrameStatistics			FrameStatistics;
	static bool								CurrentDX8LightEnables[4];

	static unsigned long FrameCount;

	static DX8Caps*						CurrentCaps;

	static D3DADAPTER_IDENTIFIER8		CurrentAdapterIdentifier;

	static IDirect3D8 *					D3DInterface;			//d3d8;
	static IDirect3DDevice8 *			D3DDevice;				//d3ddevice8;

	static IDirect3DSurface8 *			CurrentRenderTarget;
	static IDirect3DSurface8 *			CurrentDepthBuffer;
	static IDirect3DSurface8 *			DefaultRenderTarget;
	static IDirect3DSurface8 *			DefaultDepthBuffer;

	static unsigned							DrawPolygonLowBoundLimit;

	static bool								IsRenderToTexture;

	static int								ZBias;
	static float							ZNear;
	static float							ZFar;
	static D3DMATRIX					ProjectionMatrix;

public:
	// Programmable (D3D9) unit render path. The handles are populated by
	// W3DShaderManager once the device exists; the render code binds them for
	// object meshes in place of the fixed-function pipeline. The mesh FVF serves
	// as the vertex declaration, so no explicit declaration is needed.
	static DWORD						m_dwUnitVS;
	static DWORD						m_dwUnitPrelitVS;   // meshes with no NORMAL (roads, tracks)
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
		SHADER_ROUTE_PBR           = 1 << 5,   // metallic-roughness shading where an ORM map exists
		// PBR on house-coloured meshes as well. Normally they are held back, because a
		// procedurally generated ORM reads their bright white base texture as
		// near-metallic and PBR then renders dark metal where a team tint belongs. That
		// reasoning is about *generated* maps: an authored ORM whose metallic is
		// deliberate wants to be obeyed, and since every player-owned unit carries a team
		// tint, the exclusion otherwise keeps PBR off all of them.
		SHADER_ROUTE_PBR_TEAMCOLOR = 1 << 7,
	};
	static DWORD						m_shaderRoutingMask;
	// PBR (metallic-roughness, SM3) variant of the unit shader. Bound in place of
	// the plain unit shader for meshes whose base texture ships a <name>_orm map.
	static DWORD						m_dwUnitPbrVS;
	static DWORD						m_dwUnitPbrPS;
	// Shared environment cubemap sampled by the PBR shader for reflections. Bound on
	// texture stage 4 (0=albedo, 1=ORM, 2/3=terrain overlays are already spoken for).
	static IDirect3DBaseTexture8*		m_envCubeMap;
	// Mean colour of the baked cubemap. The PBR shader divides its irradiance tap by
	// this so the directional ambient it derives averages to 1.0, letting it redistribute
	// the engine's ambient by direction without changing the overall exposure.
	static float						m_envAverage[4];
	// Put texture stage 1 back after a PBR draw bound its ORM map straight to it.
	static void Restore_Stage1_After_Pbr();
	static void Restore_Stage5_After_Shadow();
	// Put stages 4/6/7 (env cubemap, SSR scene colour, SSR depth) back after a PBR draw.
	static void Restore_Pbr_Extra_Stages();
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
	// Programmable road path. Roads are decals on the terrain and want the terrain's
	// shading -- cloud, noise and, the reason this exists, cast shadows. Their own
	// fixed-function path could not sample the shadow map, so a road stayed at full
	// brightness through a shadow the ground around it was in. Flagged by W3DRoadBuffer
	// the same way HeightMap flags a terrain pass.
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
#endif
	static DWORD						m_dwRoadVS;
	static DWORD						m_dwRoadPS;
	static bool							m_bRoadShaderPass;    // current draws are road segments
	static void Set_Road_Shader_Pass(bool active) { m_bRoadShaderPass = active; }
	static bool Has_Road_Shader() { return m_dwRoadVS != 0 && m_dwRoadPS != 0; }
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
	static IDirect3DBaseTexture8*		m_pShadowMap;       // depth-packed shadow map (bound for sampling)
	static float						m_sunVP[16];
	// x = depth-compare bias in sun-clip units, y = shadow strength (0 disables the
	// lookup without unbinding anything), z = one texel in UV. The bias has to track the
	// frustum: it fights the world-space size of a shadow texel, and that now changes
	// with the zoom. The texel size rides along so the PCF taps cannot fall out of step
	// with SHADOW_MAP_SIZE.
	static float						m_shadowParams[4];
	// The mesh receivers' half of the same settings, kept apart because they defend
	// themselves differently: x = how far the lookup is lifted along the surface normal,
	// in world units, and y = the depth-compare bias left over once it is. Terrain and
	// roads carry no vertex normal and stay on m_shadowParams[0]'s blanket bias.
	static float						m_shadowMeshParams[4];
	static bool							m_bShadowDepthPass; // current draws render into the shadow map
	static void Set_Shadow_Depth_Pass(bool active) { m_bShadowDepthPass = active; }
	static bool Is_Shadow_Depth_Pass() { return m_bShadowDepthPass; }
	// The current draw is a mesh the artist marked W3D_MESH_FLAG_CAST_SHADOW. Set by the
	// mesh renderer for the duration of one mesh and cleared straight after, so only draws
	// it owns can carry it. It is the tie-breaker for blended geometry, which the depth
	// pass otherwise cannot tell from a ground decal. See Apply_Render_State_Changes.
	static bool							m_bMeshCastsShadow;
	static void Set_Mesh_Casts_Shadow(bool casts) { m_bMeshCastsShadow = casts; }
	// Set per draw when the mesh being drawn has at least one depth-writing pass, i.e.
	// it is a surface rather than an effect. Soft-blended passes of a surface are routed
	// (they are part of a mesh that is on the programmable path anyway, and must not be
	// split off it); soft-blended passes of an effect are not. Defaults to false, so
	// anything that is not a mesh-renderer draw -- particles, decals -- keeps the
	// conservative behaviour. See the note at useUnitShader.
	static bool							m_bMeshHasSolidPass;
	static void Set_Mesh_Has_Solid_Pass(bool solid) { m_bMeshHasSolidPass = solid; }
	static void Set_Sun_VP(const float* m16);

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
	static IDirect3DBaseTexture8*		m_pSceneDepth;
	static IDirect3DBaseTexture8*		m_pSceneColor;
	static bool Has_Ssr() { return m_pSceneDepth != nullptr && m_pSceneColor != nullptr; }
	// x = strength (0 disables the march without unbinding anything, as the shadow
	// strength does), y = max ray length in world units, zw = the projection's _33/_43,
	// with which the shader turns a stored z/w back into a view-space distance.
	static float						m_ssrParams[4];
	static void Set_Ssr_Params(float strength, float maxDist, float proj33, float proj43)
	{
		m_ssrParams[0] = strength; m_ssrParams[1] = maxDist;
		m_ssrParams[2] = proj33;   m_ssrParams[3] = proj43;
	}
	static void Set_Shadow_Params(float bias, float strength,
								  float normalOffsetWorld, float meshBias)
	{
		m_shadowParams[0] = bias; m_shadowParams[1] = strength;
		m_shadowParams[2] = 1.0f / (float)SHADOW_MAP_SIZE; m_shadowParams[3] = 0.0f;
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
	static float						m_terrainCloudOffX;
	static float						m_terrainCloudOffY;
	static bool							m_terrainCloudEnable;
	static bool							m_terrainNoiseEnable;
	static void Set_Terrain_Overlay(float offX, float offY, bool cloud, bool noise)
	{
		m_terrainCloudOffX = offX; m_terrainCloudOffY = offY;
		m_terrainCloudEnable = cloud; m_terrainNoiseEnable = noise;
	}

	friend void DX8_Assert();
	friend class WW3D;
	friend class DX8IndexBufferClass;
	friend class DX8VertexBufferClass;
};

// shader system updates KJM v
WWINLINE void DX8Wrapper::Set_Vertex_Shader(DWORD vertex_shader)
{
#if 0 //(gth) some code is bypassing this accessor function so we can't count on this variable...
	// may be incorrect if shaders are created and destroyed dynamically
	if (Vertex_Shader==vertex_shader) return;
#endif

	Vertex_Shader=vertex_shader;
	if (Vertex_Shader < 0x10000) {
		DX8CALL(SetFVF(Vertex_Shader));
		DX8CALL(SetVertexShader(nullptr));
	} else {
		DX8CALL(SetVertexShader(reinterpret_cast<IDirect3DVertexShader9*>(Vertex_Shader)));
	}
}

WWINLINE void DX8Wrapper::Set_Pixel_Shader(DWORD pixel_shader)
{
	// may be incorrect if shaders are created and destroyed dynamically
	if (Pixel_Shader==pixel_shader) return;

	Pixel_Shader=pixel_shader;
	DX8CALL(SetPixelShader(reinterpret_cast<IDirect3DPixelShader9*>(Pixel_Shader)));
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
	DX8CALL(SetVertexShaderConstantF(reg,(const float*)data,count));
}

WWINLINE void DX8Wrapper::Set_Pixel_Shader_Constant(int reg, const void* data, int count)
{
	int memsize=sizeof(Vector4)*count;

	// may be incorrect if shaders are destroyed and created dynamically
	if (memcmp(data, &Pixel_Shader_Constants[reg],memsize)==0) return;

	memcpy(&Pixel_Shader_Constants[reg],data,memsize);
	DX8CALL(SetPixelShaderConstantF(reg,(const float*)data,count));
}
// shader system updates KJM ^

WWINLINE void DX8Wrapper::_Set_DX8_Transform(D3DTRANSFORMSTATETYPE transform, const D3DMATRIX& m)
{
	WWASSERT(transform<=D3DTS_WORLD);
#if 0 // (gth) this optimization is breaking generals because they set the transform behind our backs.
	if (mtx!=DX8Transforms[transform])
#endif
	{
		DX8Transforms[transform]=m;
		SNAPSHOT_SAY(("DX8 - SetTransform %d [%f,%f,%f,%f][%f,%f,%f,%f][%f,%f,%f,%f]",
			transform,
			m.m[0][0],m.m[0][1],m.m[0][2],m.m[0][3],
			m.m[1][0],m.m[1][1],m.m[1][2],m.m[1][3],
			m.m[2][0],m.m[2][1],m.m[2][2],m.m[2][3]));
		DX8_RECORD_MATRIX_CHANGE();
		DX8CALL(SetTransform(transform,&m));
	}
}

WWINLINE void DX8Wrapper::_Get_DX8_Transform(D3DTRANSFORMSTATETYPE transform, D3DMATRIX& m)
{
	DX8CALL(GetTransform(transform,&m));
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
	DX8_RECORD_MATERIAL_CHANGE();
	WWASSERT(mat);
	SNAPSHOT_SAY(("DX8 - SetMaterial"));
	DX8CALL(SetMaterial(mat));
}

WWINLINE void DX8Wrapper::Set_DX8_Light(int index, D3DLIGHT8* light)
{
	if (light) {
		DX8_RECORD_LIGHT_CHANGE();
		DX8CALL(SetLight(index,light));
		DX8CALL(LightEnable(index,TRUE));
		CurrentDX8LightEnables[index]=true;
		SNAPSHOT_SAY(("DX8 - SetLight %d",index));
	}
	else if (CurrentDX8LightEnables[index]) {
		DX8_RECORD_LIGHT_CHANGE();
		CurrentDX8LightEnables[index]=false;
		DX8CALL(LightEnable(index,FALSE));
		SNAPSHOT_SAY(("DX8 - DisableLight %d",index));
	}
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
	if (state == D3DRS_SOFTWAREVERTEXPROCESSING) {
		DX8CALL(SetSoftwareVertexProcessing(value));
	} else if (state == D3DRS_ZBIAS) {
		float bias = (float)value * -0.000005f;
		DX8CALL(SetRenderState(D3DRS_DEPTHBIAS, *(DWORD*)&bias));
	} else if (state == D3DRS_LINEPATTERN || state == D3DRS_ZVISIBLE || state == D3DRS_PATCHSEGMENTS || state == D3DRS_EDGEANTIALIAS || state == D3DRS_PATCHEDGESTYLE) {
		// Ignore legacy D3D8-only render states that have no direct D3D9 equivalent or are not used/supported in D3D9
	} else {
		DX8CALL(SetRenderState( state, value ));
	}
	DX8_RECORD_RENDER_STATE_CHANGE();
}

WWINLINE void DX8Wrapper::Set_DX8_Clip_Plane(DWORD Index, CONST float* pPlane)
{
	DX8CALL(SetClipPlane( Index, pPlane ));
}

WWINLINE void DX8Wrapper::Set_DX8_Texture_Stage_State(unsigned stage, D3DTEXTURESTAGESTATETYPE state, unsigned value)
{
	if (stage >= MAX_TEXTURE_STAGES)
	{
		bool is_sampler_state = false;
		D3DSAMPLERSTATETYPE sampler_state;
		switch ((unsigned)state) {
			case D3DTSS_ADDRESSU: is_sampler_state = true; sampler_state = D3DSAMP_ADDRESSU; break;
			case D3DTSS_ADDRESSV: is_sampler_state = true; sampler_state = D3DSAMP_ADDRESSV; break;
			case D3DTSS_ADDRESSW: is_sampler_state = true; sampler_state = D3DSAMP_ADDRESSW; break;
			case D3DTSS_BORDERCOLOR: is_sampler_state = true; sampler_state = D3DSAMP_BORDERCOLOR; break;
			case D3DTSS_MAGFILTER: is_sampler_state = true; sampler_state = D3DSAMP_MAGFILTER; break;
			case D3DTSS_MINFILTER: is_sampler_state = true; sampler_state = D3DSAMP_MINFILTER; break;
			case D3DTSS_MIPFILTER: is_sampler_state = true; sampler_state = D3DSAMP_MIPFILTER; break;
			case D3DTSS_MIPMAPLODBIAS: is_sampler_state = true; sampler_state = D3DSAMP_MIPMAPLODBIAS; break;
			case D3DTSS_MAXMIPLEVEL: is_sampler_state = true; sampler_state = D3DSAMP_MAXMIPLEVEL; break;
			case D3DTSS_MAXANISOTROPY: is_sampler_state = true; sampler_state = D3DSAMP_MAXANISOTROPY; break;
		}
		if (is_sampler_state) {
			DX8CALL(SetSamplerState(stage, sampler_state, value));
		} else {
			DX8CALL(SetTextureStageState(stage, state, value));
		}
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
	bool is_sampler_state = false;
	D3DSAMPLERSTATETYPE sampler_state;
	switch ((unsigned)state) {
		case D3DTSS_ADDRESSU: is_sampler_state = true; sampler_state = D3DSAMP_ADDRESSU; break;
		case D3DTSS_ADDRESSV: is_sampler_state = true; sampler_state = D3DSAMP_ADDRESSV; break;
		case D3DTSS_ADDRESSW: is_sampler_state = true; sampler_state = D3DSAMP_ADDRESSW; break;
		case D3DTSS_BORDERCOLOR: is_sampler_state = true; sampler_state = D3DSAMP_BORDERCOLOR; break;
		case D3DTSS_MAGFILTER: is_sampler_state = true; sampler_state = D3DSAMP_MAGFILTER; break;
		case D3DTSS_MINFILTER: is_sampler_state = true; sampler_state = D3DSAMP_MINFILTER; break;
		case D3DTSS_MIPFILTER: is_sampler_state = true; sampler_state = D3DSAMP_MIPFILTER; break;
		case D3DTSS_MIPMAPLODBIAS: is_sampler_state = true; sampler_state = D3DSAMP_MIPMAPLODBIAS; break;
		case D3DTSS_MAXMIPLEVEL: is_sampler_state = true; sampler_state = D3DSAMP_MAXMIPLEVEL; break;
		case D3DTSS_MAXANISOTROPY: is_sampler_state = true; sampler_state = D3DSAMP_MAXANISOTROPY; break;
	}
	if (is_sampler_state) {
		DX8CALL(SetSamplerState(stage, sampler_state, value));
	} else {
		DX8CALL(SetTextureStageState(stage, state, value));
	}
	DX8_RECORD_TEXTURE_STAGE_STATE_CHANGE();
}

WWINLINE void DX8Wrapper::Set_DX8_Texture(unsigned int stage, IDirect3DBaseTexture8* texture)
{
  	if (stage >= MAX_TEXTURE_STAGES)
  	{	DX8CALL(SetTexture(stage, texture));
  		return;
  	}

	if (Textures[stage]==texture) return;

	SNAPSHOT_SAY(("DX8 - SetTexture(%x) ",texture));

	if (Textures[stage]) Textures[stage]->Release();
	Textures[stage] = texture;
	if (Textures[stage]) Textures[stage]->AddRef();
	DX8CALL(SetTexture(stage, texture));
	DX8_RECORD_TEXTURE_CHANGE();
}

WWINLINE HRESULT DX8Wrapper::_Copy_DX8_Rects(
  IDirect3DSurface8* pSourceSurface,
  CONST RECT* pSourceRectsArray,
  UINT cRects,
  IDirect3DSurface8* pDestinationSurface,
  CONST POINT* pDestPointsArray
)
{
	IDirect3DDevice9* device = DX8Wrapper::_Get_D3D_Device8();
	if (!device) return E_FAIL;
	HRESULT final_hr = S_OK;
	if (cRects == 0 || !pSourceRectsArray) {
		HRESULT hr = device->UpdateSurface(pSourceSurface, nullptr, pDestinationSurface, nullptr);
		if (FAILED(hr)) {
			hr = device->StretchRect(pSourceSurface, nullptr, pDestinationSurface, nullptr, D3DTEXF_NONE);
		}
		if (FAILED(hr)) {
			hr = D3DXLoadSurfaceFromSurface(pDestinationSurface, nullptr, nullptr, pSourceSurface, nullptr, nullptr, D3DX_FILTER_NONE, 0);
		}
		final_hr = hr;
	} else {
		for (UINT i = 0; i < cRects; ++i) {
			const RECT* srcRect = &pSourceRectsArray[i];
			const POINT* destPt = pDestPointsArray ? &pDestPointsArray[i] : nullptr;
			HRESULT hr = device->UpdateSurface(pSourceSurface, srcRect, pDestinationSurface, destPt);
			if (FAILED(hr)) {
				RECT destRect;
				if (destPt) {
					destRect.left = destPt->x;
					destRect.top = destPt->y;
					destRect.right = destPt->x + (srcRect->right - srcRect->left);
					destRect.bottom = destPt->y + (srcRect->bottom - srcRect->top);
				}
				hr = device->StretchRect(pSourceSurface, srcRect, pDestinationSurface, destPt ? &destRect : nullptr, D3DTEXF_NONE);
				if (FAILED(hr)) {
					hr = D3DXLoadSurfaceFromSurface(pDestinationSurface, nullptr, destPt ? &destRect : nullptr, pSourceSurface, nullptr, srcRect, D3DX_FILTER_NONE, 0);
				}
			}
			if (FAILED(hr)) {
				final_hr = hr;
			}
		}
	}
	return final_hr;
}

WWINLINE HRESULT DX8Wrapper::Set_DX8_Render_Target(
  IDirect3DSurface8* pRenderTarget,
  IDirect3DSurface8* pNewZStencil
)
{
	IDirect3DDevice9* device = DX8Wrapper::_Get_D3D_Device8();
	if (!device) return E_FAIL;
	HRESULT hr = device->SetRenderTarget(0, pRenderTarget);
	if (FAILED(hr)) return hr;
	return device->SetDepthStencilSurface(pNewZStencil);
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
	ProjectionMatrix=To_D3DMATRIX(matrix);

	if (!Get_Current_Caps()->Support_ZBias() && ZNear!=ZFar) {
		D3DMATRIX tmp=ProjectionMatrix;
		float tmp_zbias=ZBias;
		tmp_zbias*=(1.0f/16.0f);
		tmp_zbias*=1.0f / (ZFar - ZNear);
		tmp.m[2][2]-=tmp_zbias*tmp.m[3][2];
		DX8CALL(SetTransform(D3DTS_PROJECTION,&tmp));
	}
	else {
		DX8CALL(SetTransform(D3DTS_PROJECTION,&ProjectionMatrix));
	}
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
			D3DMATRIX ProjectionMatrix=To_D3DMATRIX(m);
			ZFar=0.0f;
			ZNear=0.0f;
			DX8CALL(SetTransform(D3DTS_PROJECTION,&ProjectionMatrix));
		}
		break;
	default:
		DX8_RECORD_MATRIX_CHANGE();
		D3DMATRIX dxm=To_D3DMATRIX(m);
		DX8CALL(SetTransform(transform,&dxm));
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
		DX8CALL(SetTransform(transform,&dxm));
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
		D3DMATRIX dxm;
		DX8CALL(GetTransform(transform,&dxm));
		m=To_Matrix4x4(dxm);
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

WWINLINE unsigned int DX8Wrapper::Get_Surface_Size(const D3DSURFACE_DESC& desc)
{
	unsigned int width = desc.Width;
	unsigned int height = desc.Height;
	unsigned int aligned_width = (width + 3) & ~3;
	unsigned int aligned_height = (height + 3) & ~3;
	switch (desc.Format) {
		case D3DFMT_DXT1:
			return (aligned_width * aligned_height) / 2;
		case D3DFMT_DXT2:
		case D3DFMT_DXT3:
		case D3DFMT_DXT4:
		case D3DFMT_DXT5:
			return aligned_width * aligned_height;
		case D3DFMT_A8R8G8B8:
		case D3DFMT_X8R8G8B8:
			return width * height * 4;
		case D3DFMT_R8G8B8:
			return width * height * 3;
		case D3DFMT_R5G6B5:
		case D3DFMT_X1R5G5B5:
		case D3DFMT_A1R5G5B5:
		case D3DFMT_A4R4G4B4:
			return width * height * 2;
		case D3DFMT_A8:
		case D3DFMT_L8:
			return width * height;
		default:
			return width * height * 4;
	}
}

#define SetVertexShader(handle) TestCooperativeLevel(), DX8Wrapper::Set_Vertex_Shader(handle)
#define SetPixelShader(handle) TestCooperativeLevel(), DX8Wrapper::Set_Pixel_Shader(handle)

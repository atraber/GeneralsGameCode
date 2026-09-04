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
 *                 Project Name : WW3D                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/dx8wrapper.cpp                         $*
 *                                                                                             *
 *              Original Author:: Jani Penttinen                                               *
 *                                                                                             *
 *                      $Author:: Kenny Mitchell                                               *
 *                                                                                             *
 *                     $Modtime:: 08/05/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 170                                                         $*
 *                                                                                             *
 * 06/26/02 KM Matrix name change to avoid MAX conflicts                                       *
 * 06/27/02 KM Render to shadow buffer texture support														*
 * 06/27/02 KM Shader system updates																				*
 * 08/05/02 KM Texture class redesign
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   DX8Wrapper::_Update_Texture -- Copies a texture from system memory to video memory        *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

//#define CREATE_DX8_MULTI_THREADED
//#define CREATE_DX8_FPU_PRESERVE
#define WW3D_DEVTYPE D3DDEVTYPE_HAL

#if !defined(WINVER) || WINVER < 0x0500
#undef WINVER
#define WINVER 0x0500 // Required to access GetMonitorInfo in VC6.
#endif

#include "dx8wrapper.h"
#include "gfxdevice_d3d9.h"
#include "gputimer.h"
#include "dx8webbrowser.h"
#include "dx8fvf.h"
#include "dx8vertexbuffer.h"
#include "dx8indexbuffer.h"
#include "dx8renderer.h"
#include "WW3D2/ww3d.h"
#include "WW3D2/camera.h"
#include "WWLib/wwstring.h"
#include "WWMath/matrix4.h"
#include "WW3D2/vertmaterial.h"
#include "rddesc.h"
#include "WW3D2/lightenvironment.h"
#include "statistics.h"
#include "WWLib/registry.h"
#include "WW3D2/boxrobj.h"
#include "pointgr.h"
#include "WW3D2/render2d.h"
#include "sortingrenderer.h"
#include "shattersystem.h"
#include "WW3D2/light.h"
#include "WW3D2/assetmgr.h"
#include "textureloader.h"
#include "missingtexture.h"
#include "WWLib/thread.h"
#include <d3dx9.h>
#include <DxErr.h>
#include "WWMath/pot.h"
#include "WWDebug/wwprofile.h"
#include "WWLib/ffactory.h"
#include "dx8caps.h"
#include "formconv.h"
#include "dx8texman.h"
#include "WWLib/bound.h"
#include "WWLib/DbgHelpGuard.h"

#include "shdlib.h"

const int DEFAULT_RESOLUTION_WIDTH = 640;
const int DEFAULT_RESOLUTION_HEIGHT = 480;
const int DEFAULT_BIT_DEPTH = 32;
const int DEFAULT_TEXTURE_BIT_DEPTH = 16;
const WW3DMultiSampleType DEFAULT_MSAA = WW3D_MULTISAMPLE_NONE;

DX8FrameStatistics DX8Wrapper::FrameStatistics;
static DX8FrameStatistics LastFrameStatistics;

bool DX8Wrapper_IsWindowed = true;

// FPU_PRESERVE
int DX8Wrapper_PreserveFPU = 0;

/***********************************************************************************
**
** DX8Wrapper Static Variables
**
***********************************************************************************/

static HWND						_Hwnd															= nullptr;
bool								DX8Wrapper::IsInitted									= false;
bool								DX8Wrapper::_EnableTriangleDraw						= true;

int								DX8Wrapper::CurRenderDevice							= -1;
int								DX8Wrapper::ResolutionWidth							= DEFAULT_RESOLUTION_WIDTH;
int								DX8Wrapper::ResolutionHeight							= DEFAULT_RESOLUTION_HEIGHT;
int								DX8Wrapper::BitDepth										= DEFAULT_BIT_DEPTH;
int								DX8Wrapper::TextureBitDepth							= DEFAULT_TEXTURE_BIT_DEPTH;
bool								DX8Wrapper::IsWindowed									= false;
WW3DFormat					DX8Wrapper::DisplayFormat	= WW3D_FORMAT_UNKNOWN;
WW3DMultiSampleType DX8Wrapper::MultiSampleAntiAliasing	= DEFAULT_MSAA;

// shader system additions KJM v
DWORD								DX8Wrapper::Vertex_Shader								= 0;
DWORD								DX8Wrapper::Pixel_Shader								= 0;

Vector4							DX8Wrapper::Vertex_Shader_Constants[MAX_VERTEX_SHADER_CONSTANTS];
Vector4							DX8Wrapper::Pixel_Shader_Constants[MAX_PIXEL_SHADER_CONSTANTS];

LightEnvironmentClass*		DX8Wrapper::Light_Environment							= nullptr;

ZTextureClass*					DX8Wrapper::Shadow_Map[MAX_SHADOW_MAPS];

Vector3							DX8Wrapper::Ambient_Color;
// shader system additions KJM ^

bool								DX8Wrapper::world_identity;
unsigned							DX8Wrapper::RenderStates[256];
unsigned							DX8Wrapper::TextureStageStates[MAX_TEXTURE_STAGES][32];
SamplerStateClass				DX8Wrapper::Samplers[MAX_TEXTURE_STAGES];
unsigned							DX8Wrapper::FFStagePending[MAX_TEXTURE_STAGES] = { 0 };
unsigned							DX8Wrapper::FFRenderPending[8] = { 0 };
bool								DX8Wrapper::FFStatePending = false;
unsigned							DX8Wrapper::FFDeviceStage[MAX_TEXTURE_STAGES][32];
unsigned							DX8Wrapper::FFDeviceRender[256];
D3DMATRIX						DX8Wrapper::FFDeviceTransform[DX8Wrapper::FF_TRANSFORM_SLOTS];
unsigned							DX8Wrapper::FFTransformPending = 0;
unsigned							DX8Wrapper::FFDeviceTransformValid = 0;
D3DMATERIAL8						DX8Wrapper::CurrentMaterial = { { 1.0f, 1.0f, 1.0f, 1.0f },
																	{ 1.0f, 1.0f, 1.0f, 1.0f },
																	{ 0.0f, 0.0f, 0.0f, 0.0f },
																	{ 0.0f, 0.0f, 0.0f, 0.0f },
																	1.0f };

GfxTexture *		DX8Wrapper::Textures[MAX_TEXTURE_STAGES];
RenderStateStruct				DX8Wrapper::render_state;
unsigned							DX8Wrapper::render_state_changed;

bool								DX8Wrapper::FogEnable									= false;
D3DCOLOR							DX8Wrapper::FogColor										= 0;

GfxAdapterClass *				DX8Wrapper::Adapter										= nullptr;
GfxSwapChainDesc				DX8Wrapper::SwapChain;
GfxDeviceClass *				DX8Wrapper::Gfx											= nullptr;
GfxSurface *			DX8Wrapper::CurrentRenderTarget						= nullptr;
GfxSurface *			DX8Wrapper::CurrentDepthBuffer						= nullptr;
GfxSurface *			DX8Wrapper::DefaultRenderTarget						= nullptr;
GfxSurface *			DX8Wrapper::DefaultDepthBuffer						= nullptr;
bool								DX8Wrapper::IsRenderToTexture							= false;

unsigned							DX8Wrapper::_MainThreadID								= 0;
bool								DX8Wrapper::IsDeviceLost;
int								DX8Wrapper::ZBias;
float								DX8Wrapper::ZNear;
float								DX8Wrapper::ZFar;
D3DMATRIX						DX8Wrapper::DX8Transforms[D3DTS_WORLD+1];

DX8Caps*							DX8Wrapper::CurrentCaps = nullptr;

// Hack test... this disables rendering of batches of too few polygons.
unsigned							DX8Wrapper::DrawPolygonLowBoundLimit=0;


unsigned long DX8Wrapper::FrameCount = 0;

// Programmable (D3D9) unit render path
DWORD							DX8Wrapper::m_dwUnitVS = 0;
DWORD							DX8Wrapper::m_dwUnitPrelitVS = 0;
DWORD							DX8Wrapper::m_dwUnitUv2VS = 0;
DWORD							DX8Wrapper::m_dwUnitPS = 0;
DWORD							DX8Wrapper::m_dwUnitDetailPS = 0;
DWORD							DX8Wrapper::m_shaderRoutingMask = DX8Wrapper::SHADER_ROUTE_BASELINE;
DWORD							DX8Wrapper::m_dwUnitPbrVS = 0;
DWORD							DX8Wrapper::m_dwUnitPbrPS = 0;
GfxTexture*			DX8Wrapper::m_envCubeMap = nullptr;
float							DX8Wrapper::m_envAverage[4] = { 0.2f, 0.2f, 0.2f, 1.0f };
DX8Wrapper::OrmResolverFunc		DX8Wrapper::s_ormResolver = nullptr;
DWORD							DX8Wrapper::m_dwTerrainVS = 0;
DWORD							DX8Wrapper::m_dwTerrainPS = 0;
#ifdef RTS_DEBUG
// The control for the whole exercise. The site census below counts what callers *asked*
// for; this counts what the device was actually told, which after the deferral is only
// the words some genuinely fixed-function draw needed. The difference between the two is
// the fixed-function traffic that no longer happens, and a zero here is the claim being
// made -- a zero in the census would instead mean the instrument stopped being reached.
static unsigned s_ffFlushedWrites = 0;

// ...and who they were flushed for. The total on its own says how much fixed-function
// state still reaches the device but not which draw wanted it, which is the only form
// of the number a conversion can be aimed at: "176400 words somewhere" is not a piece
// of work, "the shroud pass asks for 4 stage words a draw" is. Keyed on the declaration
// scope in force at the flush, since that is what names the drawing subsystem.
enum { MAX_FF_FLUSH_SITES = 24 };
struct FFFlushSite { const char* who; unsigned writes; unsigned vertexFF; unsigned pixelFF; };
static FFFlushSite s_ffFlushSites[MAX_FF_FLUSH_SITES];
static int s_ffFlushSiteCount = 0;
static unsigned s_ffFlushDropped = 0;
// Set for the duration of a direct drawer's flush, so its own name is used rather than
// whatever declaration scope happens to enclose it.
static const char* s_ffFlushSiteOverride = nullptr;

// Which state words the flush is actually sending. The site says which pass wants
// fixed-function state; the word says which subsystem wrote it, and between them there
// is something to go and convert. Same idea as NoteUnattributedWord below.
// Wide enough for all 17 deferred stage words plus the ten deferred render states and
// the material, so a full re-send after an invalidation is reported whole rather than
// truncated at whichever words happened to be seen first.
enum { MAX_FF_FLUSH_WORDS = 32 };
// The stage is part of the identity, not decoration. Once the residue is down to a
// couple of words a frame, "a COLOROP" is not something anybody can go and look at and
// "stage 1's COLOROP" is -- it says which pass of which draw is still describing itself
// in fixed function.
struct FFFlushWord { unsigned isStage; unsigned stage; unsigned state; unsigned count; };
static FFFlushWord s_ffFlushWords[MAX_FF_FLUSH_WORDS];
static int s_ffFlushWordCount = 0;

static void NoteFlushedWord(unsigned isStage, unsigned stage, unsigned state)
{
	for (int i = 0; i < s_ffFlushWordCount; ++i) {
		if (s_ffFlushWords[i].isStage == isStage && s_ffFlushWords[i].stage == stage &&
			s_ffFlushWords[i].state == state) {
			++s_ffFlushWords[i].count; return;
		}
	}
	if (s_ffFlushWordCount >= MAX_FF_FLUSH_WORDS) return;
	s_ffFlushWords[s_ffFlushWordCount].isStage = isStage;
	s_ffFlushWords[s_ffFlushWordCount].stage = stage;
	s_ffFlushWords[s_ffFlushWordCount].state = state;
	s_ffFlushWords[s_ffFlushWordCount].count = 1;
	++s_ffFlushWordCount;
}

static void NoteFlushedWrite(const char* who)
{
	if (who == nullptr) who = DX8Wrapper::Debug_Current_Pass_Name();
	// Which half of the pipeline made this a fixed-function draw. They are separate
	// failures with separate fixes -- no pixel shader means the combine still decides
	// the colour, an FVF where a vertex shader belongs means the transform, the lighting
	// and the texgen are all still the device's -- and a site can be either or both.
	const unsigned v = DX8Wrapper::Is_Fixed_Function_Vertex_Draw() ? 1u : 0u;
	const unsigned p = DX8Wrapper::Is_Fixed_Function_Pixel_Draw() ? 1u : 0u;
	FFFlushSite* e = nullptr;
	for (int i = 0; i < s_ffFlushSiteCount; ++i) {
		if (s_ffFlushSites[i].who == who) { e = &s_ffFlushSites[i]; break; }
	}
	if (e == nullptr) {
		if (s_ffFlushSiteCount >= MAX_FF_FLUSH_SITES) { ++s_ffFlushDropped; return; }
		e = &s_ffFlushSites[s_ffFlushSiteCount++];
		e->who = who; e->writes = 0; e->vertexFF = 0; e->pixelFF = 0;
	}
	++e->writes;
	e->vertexFF += v;
	e->pixelFF += p;
}
#endif

//-----------------------------------------------------------------------------
// Deferred fixed-function state.
//
// Which state words stop at the tracked arrays, and which go straight on to D3D.
//
// The test is not "did fixed function use this" but "can anything other than fixed
// function be affected by it", and the two are not the same. Three that look like they
// belong here and do not:
//
//   D3DRS_SPECULARENABLE -- still live with a vertex shader bound. It is what decides
//     whether the rasteriser adds oD1 to oD0, and a vertex shader writes oD1.
//   D3DRS_FOG* -- the fog blend is a stage of its own, downstream of the pixel shader,
//     and D3D9 applies it to programmable output as readily as to fixed-function output.
//     Deferring it would unfog the scene wherever the scene asked to be fogged.
//   The sampler states that arrive here wearing D3DTSS_ names -- filtering and address
//     modes govern every fetch, programmable or not. Those are remapped above this and
//     never reach the predicate.
//
// Everything below is genuinely inert while a vertex and pixel shader are bound: the
// texture combine, the texgen, fixed-function vertex lighting, the material sources and
// the texture factor, which nothing but a fixed-function combine can read.
//-----------------------------------------------------------------------------
//**********************************************************************************************
// The sampler description, and the one place in the engine where it meets D3D's constants.
//
// Set_Sampler emits a state word only for a field that differs from the description the slot
// is already holding, so a caller that takes the current description and changes four fields
// produces exactly the four writes it used to make by hand. Nothing here decides whether a
// write reaches the device: Set_DX8_Texture_Stage_State's own redundancy check still does
// that, and the tracked state stays the wrapper's single model of the device.
//
// Samplers[] is reset to Unknown() wherever that model is invalidated, so that every field of
// the next description bound is treated as a change and re-sent. It is intent, not a claim
// about the device, which is why it is a separate array rather than a reading of the tracked
// one -- a poisoned tracked word says the wrapper knows nothing, and a filter mode has no
// spelling for that.
//
// A backend that binds sampler objects rather than state words replaces the body of this
// function with a single call taking `sampler` whole, and no caller changes. That is the
// reason the description exists.
//**********************************************************************************************

static unsigned Sampler_Filter_To_D3D(SamplerStateClass::FilterType filter)
{
	switch (filter) {
		case SamplerStateClass::FILTER_POINT:		return D3DTEXF_POINT;
		case SamplerStateClass::FILTER_LINEAR:		return D3DTEXF_LINEAR;
		case SamplerStateClass::FILTER_ANISOTROPIC:	return D3DTEXF_ANISOTROPIC;
		case SamplerStateClass::FILTER_NONE:
		default:									return D3DTEXF_NONE;
	}
}

static unsigned Sampler_Address_To_D3D(SamplerStateClass::AddressType address)
{
	switch (address) {
		case SamplerStateClass::ADDRESS_MIRROR:			return D3DTADDRESS_MIRROR;
		case SamplerStateClass::ADDRESS_CLAMP:			return D3DTADDRESS_CLAMP;
		case SamplerStateClass::ADDRESS_BORDER:			return D3DTADDRESS_BORDER;
		case SamplerStateClass::ADDRESS_MIRROR_ONCE:	return D3DTADDRESS_MIRRORONCE;
		case SamplerStateClass::ADDRESS_WRAP:
		default:										return D3DTADDRESS_WRAP;
	}
}

const SamplerStateClass & DX8Wrapper::Get_Sampler(unsigned stage)
{
	WWASSERT(stage < MAX_TEXTURE_STAGES);
	return Samplers[stage];
}

void DX8Wrapper::Set_Sampler(unsigned stage, const SamplerStateClass & sampler)
{
	WWASSERT(stage < MAX_TEXTURE_STAGES);
	SamplerStateClass & current = Samplers[stage];
	if (current == sampler) return;

	if (current.Get_Min_Filter() != sampler.Get_Min_Filter())
		Set_DX8_Stage_State_Unguarded(stage, D3DTSS_MINFILTER, Sampler_Filter_To_D3D(sampler.Get_Min_Filter()));
	if (current.Get_Mag_Filter() != sampler.Get_Mag_Filter())
		Set_DX8_Stage_State_Unguarded(stage, D3DTSS_MAGFILTER, Sampler_Filter_To_D3D(sampler.Get_Mag_Filter()));
	if (current.Get_Mip_Filter() != sampler.Get_Mip_Filter())
		Set_DX8_Stage_State_Unguarded(stage, D3DTSS_MIPFILTER, Sampler_Filter_To_D3D(sampler.Get_Mip_Filter()));
	if (current.Get_U_Address() != sampler.Get_U_Address())
		Set_DX8_Stage_State_Unguarded(stage, D3DTSS_ADDRESSU, Sampler_Address_To_D3D(sampler.Get_U_Address()));
	if (current.Get_V_Address() != sampler.Get_V_Address())
		Set_DX8_Stage_State_Unguarded(stage, D3DTSS_ADDRESSV, Sampler_Address_To_D3D(sampler.Get_V_Address()));
	if (current.Get_W_Address() != sampler.Get_W_Address())
		Set_DX8_Stage_State_Unguarded(stage, D3DTSS_ADDRESSW, Sampler_Address_To_D3D(sampler.Get_W_Address()));
	if (current.Get_Anisotropy() != sampler.Get_Anisotropy())
		Set_DX8_Stage_State_Unguarded(stage, D3DTSS_MAXANISOTROPY, sampler.Get_Anisotropy());

	current = sampler;
}

DWORD DX8Wrapper::Create_Vertex_Shader(const void * bytecode, unsigned size)
{
	if (Gfx == nullptr) return 0;
	return (DWORD)Gfx->Create_Vertex_Shader(bytecode, size);
}

DWORD DX8Wrapper::Create_Pixel_Shader(const void * bytecode, unsigned size)
{
	if (Gfx == nullptr) return 0;
	return (DWORD)Gfx->Create_Pixel_Shader(bytecode, size);
}

void DX8Wrapper::Release_Vertex_Shader(DWORD vertex_shader)
{
	if (Gfx == nullptr || vertex_shader == 0) return;
	Gfx->Release_Vertex_Shader((GfxShaderHandle)vertex_shader);
}

void DX8Wrapper::Release_Pixel_Shader(DWORD pixel_shader)
{
	if (Gfx == nullptr || pixel_shader == 0) return;
	Gfx->Release_Pixel_Shader((GfxShaderHandle)pixel_shader);
}

bool DX8Wrapper::Is_Deferred_FF_Stage_State(unsigned state)
{
	switch (state) {
		case D3DTSS_COLOROP:   case D3DTSS_COLORARG0: case D3DTSS_COLORARG1:
		case D3DTSS_COLORARG2: case D3DTSS_ALPHAOP:   case D3DTSS_ALPHAARG0:
		case D3DTSS_ALPHAARG1: case D3DTSS_ALPHAARG2: case D3DTSS_RESULTARG:
		case D3DTSS_TEXCOORDINDEX: case D3DTSS_TEXTURETRANSFORMFLAGS:
		case D3DTSS_BUMPENVMAT00: case D3DTSS_BUMPENVMAT01:
		case D3DTSS_BUMPENVMAT10: case D3DTSS_BUMPENVMAT11:
		case D3DTSS_BUMPENVLSCALE: case D3DTSS_BUMPENVLOFFSET:
			return true;
		default:
			return false;
	}
}

bool DX8Wrapper::Is_Deferred_FF_Render_State(unsigned state)
{
	switch (state) {
		case D3DRS_LIGHTING: case D3DRS_AMBIENT:
		case D3DRS_COLORVERTEX: case D3DRS_NORMALIZENORMALS: case D3DRS_LOCALVIEWER:
		case D3DRS_DIFFUSEMATERIALSOURCE: case D3DRS_SPECULARMATERIALSOURCE:
		case D3DRS_AMBIENTMATERIALSOURCE: case D3DRS_EMISSIVEMATERIALSOURCE:
		case D3DRS_TEXTUREFACTOR:
			return true;
		default:
			return false;
	}
}

void DX8Wrapper::Flush_Fixed_Function_State()
{
	// The material used to be flushed here. It described a lit surface for a stage that is
	// switched off: D3DRS_LIGHTING reads FALSE off the device and no draw in either shadow
	// configuration turns it on, and with lighting off D3D ignores the material entirely.
	// CurrentMaterial itself stays -- the routing block reads it to build the shader's
	// material constants, so it is tracked-state IR like the stage words, not residue.

	while (FFTransformPending) {
		unsigned long bit;
		_BitScanForward(&bit, FFTransformPending);
		FFTransformPending &= FFTransformPending - 1;
		const unsigned which = FF_Transform_Which(bit);
		const D3DMATRIX & wanted = DX8Transforms[which];
		if ((FFDeviceTransformValid & (1u << bit)) &&
			memcmp(&FFDeviceTransform[bit], &wanted, sizeof(D3DMATRIX)) == 0) continue;
		FFDeviceTransform[bit] = wanted;
		FFDeviceTransformValid |= (1u << bit);
		GFXCALL(Set_Transform(which,(const float*)&wanted));
#ifdef RTS_DEBUG
		Debug_Note_Device_Transform(which,(const float*)&wanted);
		++s_ffFlushedWrites;
		NoteFlushedWrite(s_ffFlushSiteOverride != nullptr
			? s_ffFlushSiteOverride : s_declarationSite);
#endif
	}

	if (!FFStatePending) return;
	FFStatePending = false;

	for (unsigned stage = 0; stage < MAX_TEXTURE_STAGES; ++stage) {
		unsigned pending = FFStagePending[stage];
		FFStagePending[stage] = 0;
		while (pending) {
			// Lowest set bit first; the order state words are applied in does not matter,
			// only that every one of them lands before the draw.
			unsigned long bit;
			_BitScanForward(&bit, pending);
			pending &= pending - 1;
			const unsigned value = TextureStageStates[stage][bit];
			if (FFDeviceStage[stage][bit] == value) continue;   // device already has it
			FFDeviceStage[stage][bit] = value;
			GFXCALL(Set_Texture_Stage_State(stage, bit, value));
#ifdef RTS_DEBUG
			NoteFlushedWord(1, stage, bit);
			++s_ffFlushedWrites;
			NoteFlushedWrite(s_ffFlushSiteOverride != nullptr
				? s_ffFlushSiteOverride : s_declarationSite);
#endif
		}
	}
	for (unsigned word = 0; word < 8; ++word) {
		unsigned pending = FFRenderPending[word];
		FFRenderPending[word] = 0;
		while (pending) {
			unsigned long bit;
			_BitScanForward(&bit, pending);
			pending &= pending - 1;
			const unsigned state = (word << 5) | bit;
			const unsigned value = RenderStates[state];
			if (FFDeviceRender[state] == value) continue;
			FFDeviceRender[state] = value;
			GFXCALL(Set_Render_State(state, value));
#ifdef RTS_DEBUG
			NoteFlushedWord(0, 0, state);
			++s_ffFlushedWrites;
			NoteFlushedWrite(s_ffFlushSiteOverride != nullptr
				? s_ffFlushSiteOverride : s_declarationSite);
#endif
		}
	}
}

void DX8Wrapper::Prepare_Direct_Draw(const char * site)
{
	// A drawer that goes to the device itself binds nothing, so it inherits whatever
	// shaders the previous draw left -- including none at all, which is fixed function.
	// Which it is can still be read off the device's bindings, and that is the same test
	// Draw() makes. A direct drawer that bound both a vertex and a pixel shader needs none
	// of this state.
	if (Is_Fixed_Function_Draw()) {
#ifdef RTS_DEBUG
		// Attribute what this costs to the drawer, not to whatever declaration scope
		// happens to enclose it.
		s_ffFlushSiteOverride = site;
#endif
		Flush_Fixed_Function_State();
#ifdef RTS_DEBUG
		s_ffFlushSiteOverride = nullptr;
#endif
	}

	// Everything this drawer bound at the device, the wrapper does not know about -- and
	// what it does not know about, it will not put back.
	//
	// Apply_Render_State_Changes re-issues SetStreamSource and SetIndices only when
	// VERTEX_BUFFER_CHANGED / INDEX_BUFFER_CHANGED say the buffers moved, which is a
	// statement about the wrapper's own render_state and says nothing about the device. A
	// direct drawer binds its own stream, its own indices and its own FVF and never touches
	// render_state, so those flags stay clear -- and the next draw through Draw() that
	// happens not to change buffers itself keeps drawing with the direct drawer's
	// bindings. The renderers this reaches are the ones that bind once and then draw many
	// times: every bridge in W3DBridgeBuffer::drawBridges after the first, every polygon
	// renderer in an FVF category after the first.
	//
	// Three separate ways for that to be wrong, and they are worth naming because only the
	// first is obvious:
	//
	//   * The stream is somebody else's buffer. The decal flush leaves
	//     shadowDecalVertexBufferD3D on stream 0; a draw that reads it as bridge geometry
	//     gets positions that are not positions.
	//   * The base vertex index is somebody else's. It was device state in D3D8 and is a
	//     draw argument in D3D9, so the compatibility layer keeps it in a global written at
	//     SetIndices time -- see the note in Draw(). The decal flush sets it to
	//     nShadowDecalStartBatchVertex, which climbs towards 32768 across a frame.
	//   * There is no stream at all. DrawPrimitiveUP and DrawIndexedPrimitiveUP set the
	//     stream 0 binding to NULL as a documented side effect, which D3D8 did not do, and
	//     that is how every screen-space quad in the frame ends.
	//
	// All three produce the same thing on screen: triangles built from unrelated vertices,
	// which rasterise as long thin slivers reaching across the view. Which draws are hit
	// depends on submission order, so it changes as the camera turns and does not reproduce
	// from one run to the next.
	//
	// So record that the device's bindings are no longer ours. Draw() acts on it, not
	// Apply_Render_State_Changes: the flags that would repair this are read by Apply, and
	// Apply is called from about thirty places that are only tidying up. Raising them here
	// would fire at those too -- and the VERTEX_BUFFER_CHANGED branch sets the FVF from
	// render_state's own vertex buffer, which would take down the D3DFVF_XYZRHW declaration
	// a screen-space caller had just set for the DrawPrimitiveUP it has not issued yet.
	// (ScreenMotionBlurFilter::postRender does exactly that: FVF, draw, Apply, draw.) It
	// would also un-gate Apply's early return and re-run the per-draw routing block for a
	// call that is not describing a draw.
	//
	// Costs one SetStreamSource and one SetIndices at each boundary between a direct drawer
	// and the wrapper -- not per draw -- and it is stated once, at the point every direct
	// drawer already passes through, rather than left as a rule each of them must remember.
	m_bForeignDeviceBindings = true;

#ifdef RTS_DEBUG
	Debug_Note_Direct_Draw(site);
	// A direct drawer always reaches a device -- nothing downstream can decline it, because
	// there is no downstream: it issues the draw call itself.
	Debug_Note_Vertex_Layout(site, true, true);
#else
	(void)site;
#endif
}


#ifdef RTS_DEBUG
// Split-pipeline watchdog -- see the note in dx8wrapper.h. Records which pipeline drew
// each pass of each mesh over a frame, and names any mesh that was drawn by more than
// one, once per mesh per session.
const char*						DX8Wrapper::s_debugMeshName = nullptr;
namespace {
	struct MeshRouteEntry { const char* name; unsigned mask; unsigned ffReasons; };
	MeshRouteEntry s_meshRoutes[512];
	int s_meshRouteCount = 0;
	const char* s_reportedSplits[64];
	int s_reportedSplitCount = 0;

	bool AlreadyReportedSplit(const char* name)
	{
		for (int i = 0; i < s_reportedSplitCount; ++i) {
			if (s_reportedSplits[i] == name) return true;
		}
		if (s_reportedSplitCount < 64) s_reportedSplits[s_reportedSplitCount++] = name;
		return false;
	}
}

static bool s_applyIsDraw = false;

//-----------------------------------------------------------------------------
// Direct-device draws: the callers that bypass DX8Wrapper::Draw entirely.
//
// Each records which halves of the pipeline were programmable at the moment it drew.
// Nothing on this path binds anything, so a draw takes whatever the *previous* draw
// left bound, which is how the same geometry can land on different pipelines from
// frame to frame.
//
// The two halves are counted apart because they are lost and regained separately, and
// counting only one of them reads as further along than the code is. A pixel shader
// with a D3DFVF_XYZRHW vertex format is the common case: the fragment side is
// programmable while the vertex side is not merely fixed-function but skipped
// altogether, the position arriving already in screen space. Every screen-space quad
// in the frame -- the bloom chain, the tone map, the filters -- is drawn that way. The
// old single "fixed function" column tested the pixel shader alone and so reported all
// of them as done.
//
// The pair of tests is the same one Prepare_Direct_Draw makes to decide whether the
// deferred fixed-function state has to be flushed; they are written once, here, so the
// census and the flush cannot drift apart again.
//-----------------------------------------------------------------------------
struct DirectDrawGroup
{
	const char * site;
	unsigned     total;
	unsigned     ffPixel;    // no pixel shader bound
	unsigned     ffVertex;   // an FVF rather than a vertex shader
};
static DirectDrawGroup s_directDraws[32];
static int      s_directDrawCount = 0;
static unsigned s_directDrawFrames = 0;

//-----------------------------------------------------------------------------
// Bindings inherited from a direct-device drawer -- see the note in Prepare_Direct_Draw
// for what goes wrong, and the one in Draw() for where it is repaired.
//
// Sampled at the boundary rather than per draw: what matters is the *first* wrapper draw
// after a direct one, because that is the draw that would have used its stream and its
// base vertex index. Grouped by the drawer that left them, since the mismatch on its own
// says nothing about where it came from.
//
// This keeps reporting after the repair lands, and should. It is a gauge of how much work
// the repair is doing, not a count of anything still broken -- a run of zeroes here would
// mean the boundary had stopped mattering, which is a different claim and one worth being
// able to see.
//-----------------------------------------------------------------------------
struct ForeignBindingGroup
{
	const char * site;        // the direct-device drawer whose bindings were still standing
	unsigned     draws;       // wrapper draws that reached the boundary
	unsigned     wrongBase;   // ...of those, how many had the wrong base vertex index
	unsigned     wrongStream; // ...and how many had somebody else's vertex buffer, or none
	int          worstDelta;  // largest inherited-minus-expected base seen
};
static ForeignBindingGroup s_foreign[32];
static int            s_foreignCount = 0;
static const char *   s_lastDirectDrawSite = nullptr;
static int            s_foreignReported = 0;

void DX8Wrapper::Debug_Note_Foreign_Bindings(int expectedBase, int inheritedBase, bool streamWrong)
{
	const char * site = s_lastDirectDrawSite != nullptr ? s_lastDirectDrawSite : "(none yet)";
	const int delta = inheritedBase - expectedBase;
	const bool baseWrong = (delta != 0);

	// The first few go out immediately rather than waiting for the window report. This
	// exists to answer a question somebody is holding a controller to ask -- "is what I can
	// see on screen this?" -- and a report 600 frames later cannot be matched up with what
	// the camera was doing when it happened.
	if ((baseWrong || streamWrong) && s_foreignReported < 12) {
		++s_foreignReported;
		const char * victimSite = Debug_Get_FF_Site();
		WWDEBUG_SAY(("FOREIGN BINDINGS: draw in %s / mesh %s, after %s, would have used base %d "
					 "where its own buffers imply %d (delta %d)%s -- repaired",
			victimSite != nullptr ? victimSite : "(unattributed)",
			s_debugMeshName != nullptr ? s_debugMeshName : "(none)",
			site, inheritedBase, expectedBase, delta,
			streamWrong ? ", and somebody else's vertex stream" : ""));
	}

	for (int i = 0; i < s_foreignCount; ++i) {
		if (s_foreign[i].site == site) {
			++s_foreign[i].draws;
			if (baseWrong)   ++s_foreign[i].wrongBase;
			if (streamWrong) ++s_foreign[i].wrongStream;
			const int mag   = delta < 0 ? -delta : delta;
			const int worst = s_foreign[i].worstDelta < 0 ? -s_foreign[i].worstDelta : s_foreign[i].worstDelta;
			if (mag > worst) s_foreign[i].worstDelta = delta;
			return;
		}
	}
	if (s_foreignCount >= 32) return;
	ForeignBindingGroup & g = s_foreign[s_foreignCount++];
	g.site = site;
	g.draws = 1;
	g.wrongBase   = baseWrong   ? 1 : 0;
	g.wrongStream = streamWrong ? 1 : 0;
	g.worstDelta  = delta;
}

void DX8Wrapper::Debug_Note_Direct_Draw(const char * site)
{
	if (site == nullptr) site = "?";
	s_lastDirectDrawSite = site;
	const bool ffPixel  = Is_Fixed_Function_Pixel_Draw();
	const bool ffVertex = Is_Fixed_Function_Vertex_Draw();
	for (int i = 0; i < s_directDrawCount; ++i) {
		if (s_directDraws[i].site == site) {
			++s_directDraws[i].total;
			if (ffPixel)  ++s_directDraws[i].ffPixel;
			if (ffVertex) ++s_directDraws[i].ffVertex;
			return;
		}
	}
	if (s_directDrawCount >= 32) return;
	DirectDrawGroup & g = s_directDraws[s_directDrawCount++];
	g.site = site;
	g.total = 1;
	g.ffPixel  = ffPixel  ? 1 : 0;
	g.ffVertex = ffVertex ? 1 : 0;
}

void DX8Wrapper::Debug_Report_Direct_Draws()
{
	if (++s_directDrawFrames < 600) return;
	s_directDrawFrames = 0;
	if (s_directDrawCount == 0) {
		WWDEBUG_SAY(("DIRECT-DEVICE DRAWS: none this window"));
		return;
	}
	WWDEBUG_SAY(("DIRECT-DEVICE DRAWS over 600 frames (these bypass DX8Wrapper::Draw and "
				 "are not in the census above). FF-vertex means an FVF rather than a vertex "
				 "shader; FF-pixel means no pixel shader. A site is only done when both are 0:"));
	for (int i = 0; i < s_directDrawCount; ++i) {
		const DirectDrawGroup & g = s_directDraws[i];
		const unsigned vpc = g.total ? (unsigned)((unsigned __int64)g.ffVertex * 100 / g.total) : 0;
		const unsigned ppc = g.total ? (unsigned)((unsigned __int64)g.ffPixel  * 100 / g.total) : 0;
		WWDEBUG_SAY(("  %-24s x%-7u  FF-vertex %u (%u%%)  FF-pixel %u (%u%%)",
			g.site, g.total, g.ffVertex, vpc, g.ffPixel, ppc));
	}
	s_directDrawCount = 0;

	if (s_foreignCount != 0) {
		WWDEBUG_SAY(("  ...and the first wrapper draw after each of them, which would have "
					 "inherited its bindings had Draw() not asked for ours back:"));
		for (int i = 0; i < s_foreignCount; ++i) {
			const ForeignBindingGroup & g = s_foreign[i];
			WWDEBUG_SAY(("    after %-22s x%-7u  wrong base %u (worst delta %+d)  wrong stream %u",
				g.site, g.draws, g.wrongBase, g.worstDelta, g.wrongStream));
		}
		s_foreignCount = 0;
	}
}


//-----------------------------------------------------------------------------
// Vertex layouts: (vertex format x vertex shader) per drawer.
//
// D3D11 builds an input layout from a vertex layout and a compiled vertex shader's input
// signature *together* -- CreateInputLayout takes both -- so the set of layouts a backend
// has to create is the set of distinct pairs. Neither half on its own is the answer, and
// until this census existed nothing in the tree reported the pair at all.
//
// The format is read from Debug_Vertex_FVF rather than from Vertex_Shader, because those
// stop being the same word as soon as a shader is bound: Set_Vertex_Shader overloads one
// DWORD for FVF and shader handle, and only the FVF branch changes the declaration. A draw
// with unit_vs bound over DX8_FVF_XYZNDUV2 reports its shader in Vertex_Shader and its
// format nowhere else.
//
// A row with shader 0 is a fixed-function vertex draw: no vertex shader at all, the
// transform done by a pipeline stage D3D11 does not have. Those rows are the conversion
// work. Every other row is a layout to create.
//-----------------------------------------------------------------------------
namespace {
	struct VertexLayoutRow
	{
		const char * site;
		unsigned     fvf;        // the declaration standing at the device
		unsigned     shader;     // the bound vertex shader, or 0 for fixed function
		unsigned     draws;
		unsigned     submitted;  // ...of those, how many reached a device
		unsigned     noPixel;    // ...and how many had no pixel shader either
		bool         direct;     // straight at the device rather than through Draw()
	};
	// 195 is the static ceiling -- 15 FVF codes named in the tree by 13 compiled vertex
	// shaders -- so a table that cannot overflow costs nothing worth economising on.
	VertexLayoutRow s_vertexLayouts[256];
	int      s_vertexLayoutCount = 0;
	unsigned s_vertexLayoutFrames = 0;
	unsigned s_vertexLayoutDropped = 0;

	// FVF codes are a bitfield plus a two-bit-per-set texture coordinate size, and reading
	// one off a hex number by eye is how the wrong layout gets written. Spell it out.
	void Describe_FVF(unsigned fvf, char * out, unsigned outSize)
	{
		StringClass s;
		const unsigned pos = fvf & D3DFVF_POSITION_MASK;
		if (pos == D3DFVF_XYZRHW)      s += "XYZRHW";
		else if (pos == D3DFVF_XYZ)    s += "XYZ";
		else if (pos == 0)             s += "(no position)";
		else                           s += "XYZB?";
		if (fvf & D3DFVF_NORMAL)   s += "|N";
		if (fvf & D3DFVF_DIFFUSE)  s += "|D";
		if (fvf & D3DFVF_SPECULAR) s += "|S";
		if (fvf & D3DFVF_PSIZE)    s += "|PSIZE";
		const unsigned texCount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
		for (unsigned t = 0; t < texCount && t < 8; ++t) {
			// Two bits per set, and the encoding is rotated: 0 means 2 floats, not 0.
			static const unsigned sizes[4] = { 2, 3, 4, 1 };
			StringClass one;
			one.Format("|UV%u", sizes[(fvf >> (16 + t * 2)) & 3]);
			s += one;
		}
		if (texCount == 0 && pos != 0) s += "|no UV";
		strncpy(out, s.Peek_Buffer(), outSize - 1);
		out[outSize - 1] = 0;
	}
}

void DX8Wrapper::Debug_Note_Vertex_Layout(const char * site, bool direct, bool submitted)
{
	if (site == nullptr) site = "(unattributed)";
	const unsigned fvf = Debug_Vertex_FVF;
	// Below 0x10000 the handle is an FVF and not a shader, so there is no shader: that is
	// the whole distinction this table is being built to measure.
	const unsigned shader = Is_Fixed_Function_Vertex_Draw() ? 0u : (unsigned)Vertex_Shader;

	for (int i = 0; i < s_vertexLayoutCount; ++i) {
		VertexLayoutRow & r = s_vertexLayouts[i];
		if (r.site == site && r.fvf == fvf && r.shader == shader && r.direct == direct) {
			++r.draws;
			if (submitted) ++r.submitted;
			if (submitted && Is_Fixed_Function_Pixel_Draw()) ++r.noPixel;
			return;
		}
	}
	if (s_vertexLayoutCount >= 256) { ++s_vertexLayoutDropped; return; }
	VertexLayoutRow & r = s_vertexLayouts[s_vertexLayoutCount++];
	r.site = site;
	r.fvf = fvf;
	r.shader = shader;
	r.draws = 1;
	r.submitted = submitted ? 1 : 0;
	r.noPixel = (submitted && Is_Fixed_Function_Pixel_Draw()) ? 1 : 0;
	r.direct = direct;
}

void DX8Wrapper::Debug_Report_Vertex_Layouts()
{
	if (++s_vertexLayoutFrames < 600) return;
	s_vertexLayoutFrames = 0;
	if (s_vertexLayoutCount == 0) {
		WWDEBUG_SAY(("VERTEX LAYOUT CENSUS: no draws this window -- CONTROL FAILED, the "
					 "figures below are not a measurement of anything."));
		return;
	}

	// Distinct pairs first, because that is the number Phase 5 sizes an input-layout cache
	// against; the per-drawer rows below it are what each pair is for.
	unsigned pairs = 0;
	unsigned ffVertexSubmitted = 0;
	unsigned ffPixelSubmitted = 0;
	for (int i = 0; i < s_vertexLayoutCount; ++i) {
		bool seen = false;
		for (int j = 0; j < i; ++j) {
			if (s_vertexLayouts[j].fvf == s_vertexLayouts[i].fvf &&
				s_vertexLayouts[j].shader == s_vertexLayouts[i].shader) { seen = true; break; }
		}
		if (!seen) ++pairs;
		if (s_vertexLayouts[i].shader == 0) ffVertexSubmitted += s_vertexLayouts[i].submitted;
		ffPixelSubmitted += s_vertexLayouts[i].noPixel;
	}

	WWDEBUG_SAY(("VERTEX LAYOUT CENSUS over 600 frames: %u distinct (vertex format x vertex "
				 "shader) pairs across %d drawer rows%s. This is the set of input layouts a "
				 "D3D11 backend has to create, since CreateInputLayout takes a layout and a "
				 "shader signature together and neither half alone identifies one. A row "
				 "with shader (none) is a fixed-function vertex draw and has no shader to "
				 "build a layout from at all -- those are the conversions, not the layouts. "
				 "`direct` means the drawer went straight at the device; `submitted` is "
				 "draws that reached one, which is fewer where the depth pass declined.",
		pairs, s_vertexLayoutCount, s_vertexLayoutDropped ? "  -- TABLE FULL" : ""));
	// The two figures this census exists to drive to zero, said plainly rather than left
	// to be summed out of the rows. A fixed-function draw that is never submitted is not
	// work for a second backend: it reaches no device, so there is nothing to write a
	// shader for. Only the submitted ones are.
	WWDEBUG_SAY(("  of those, draws that reached a device with NO VERTEX SHADER: %u, and "
				 "with no pixel shader: %u. Both must be 0 before a backend with no "
				 "fixed-function pipeline can draw this frame.",
		ffVertexSubmitted, ffPixelSubmitted));

	for (int rank = 0; rank < s_vertexLayoutCount; ++rank) {
		int best = -1;
		unsigned bestDraws = 0;
		for (int i = 0; i < s_vertexLayoutCount; ++i) {
			if (s_vertexLayouts[i].draws > bestDraws) {
				bestDraws = s_vertexLayouts[i].draws; best = i;
			}
		}
		if (best < 0) break;
		const VertexLayoutRow & r = s_vertexLayouts[best];
		char desc[128];
		Describe_FVF(r.fvf, desc, sizeof(desc));
		const char * shaderName = r.shader != 0 ? Debug_Shader_Name(r.shader) : nullptr;
		WWDEBUG_SAY(("  %-26s %-7s fvf 0x%-6x %-26s shader %-18s x%-7u submitted %u%s",
			r.site, r.direct ? "direct" : "wrapper", r.fvf, desc,
			r.shader == 0 ? "(none: FIXED FUNCTION)"
				: (shaderName != nullptr ? shaderName : "(unregistered)"),
			r.draws, r.submitted,
			r.noPixel ? "  *** and no pixel shader either" : ""));
		s_vertexLayouts[best].draws = 0;
	}
	s_vertexLayoutCount = 0;
	s_vertexLayoutDropped = 0;
}


void DX8Wrapper::Debug_Note_Mesh_Routing(unsigned pipelineBit, unsigned ffReason)
{
	if (s_debugMeshName == nullptr) return;
	for (int i = 0; i < s_meshRouteCount; ++i) {
		if (s_meshRoutes[i].name == s_debugMeshName) {
			s_meshRoutes[i].mask |= pipelineBit;
			if (ffReason) s_meshRoutes[i].ffReasons |= (1u << ffReason);
			return;
		}
	}
	if (s_meshRouteCount < 512) {
		MeshRouteEntry& e = s_meshRoutes[s_meshRouteCount++];
		e.name = s_debugMeshName;
		e.mask = pipelineBit;
		e.ffReasons = ffReason ? (1u << ffReason) : 0u;
	}
}

//-----------------------------------------------------------------------------
// Routing census.
//
// The split watchdog above answers "is any one mesh drawn by two pipelines". This
// answers the other question: across the whole frame, how many mesh passes each
// pipeline actually claimed, and which meshes are on each.
//
// It exists because the PBR gate was widened from "meshes shipping an <name>_orm"
// to "every eligible mesh, on the default map where it has none", and the only
// honest way to check that landed is to have the renderer say what it did. Reading
// it off the screen does not distinguish PBR-on-default from the M3 shader at a
// glance -- that is the point of the default values -- so a screenshot cannot
// confirm this either way.
//
// Every category is counted, not just the new one: a census where the numbers only
// moved into pbr-default would be indistinguishable from one where the exclusions
// stopped working, and the fixed-function and effect-geometry counts are what say
// they still hold.
//-----------------------------------------------------------------------------
namespace {
	enum { CENSUS_CATS = 6 };
	const char* const s_censusNames[CENSUS_CATS] = {
		"fixed-function", "unit_ps", "unit_detail_ps",
		"pbr(authored orm)", "pbr(default orm)", "pbr(default orm, team-tinted)"
	};
	unsigned s_censusDraws[CENSUS_CATS] = { 0 };
	// A few distinct model names per category, so the totals can be sanity-checked
	// against what is actually on screen rather than taken on trust.
	enum { CENSUS_NAMES = 8 };
	const char* s_censusSamples[CENSUS_CATS][CENSUS_NAMES] = { { nullptr } };
	int s_censusSampleCount[CENSUS_CATS] = { 0 };
	int s_censusFrames = 0;
	// Draws that were handed an emissive gain above 1, i.e. told they may exceed display
	// white. Counted over the same window as the census and reported beside it.
	unsigned s_censusHdrEmissiveDraws = 0;

	void CensusNote(unsigned cat, const char* name)
	{
		if (cat >= CENSUS_CATS) return;
		++s_censusDraws[cat];
		if (name == nullptr) return;
		for (int i = 0; i < s_censusSampleCount[cat]; ++i) {
			if (s_censusSamples[cat][i] == name) return;
		}
		if (s_censusSampleCount[cat] < CENSUS_NAMES)
			s_censusSamples[cat][s_censusSampleCount[cat]++] = name;
	}
}

void DX8Wrapper::Debug_Note_Routing_Census(unsigned category)
{
	CensusNote(category, s_debugMeshName);
}

//-----------------------------------------------------------------------------
// Unclassified draws: everything that reaches the routing block without a
// declared technique, grouped by its stage-0 texture.
//
// These are the callers that never come through the mesh renderer -- water, the
// shroud, decals, particles, projected textures -- and they are the last users of
// the inference. Giving them techniques means knowing which they are and what
// each would declare, and the texture is the only identity a non-mesh draw has:
// there is no model name, and the FVF is shared by half the renderer.
//
// Counted per reporting window so a caller that only draws under some condition
// (water on a map that has some, the shroud once it is torn) still appears.
//-----------------------------------------------------------------------------
namespace {
	struct UnclassifiedGroup {
		const char* texture;
		unsigned fvf;
		unsigned count;
		unsigned toShader;      // how many the inference sent to the programmable path
		unsigned blended;       // how many were blended at all
		unsigned softOverlay;   // blended with no alpha test
	};
	enum { MAX_UNCLASSIFIED = 24 };
	UnclassifiedGroup s_unclassified[MAX_UNCLASSIFIED];
	int s_unclassifiedCount = 0;
	unsigned s_unclassifiedTotal = 0;
	int s_unclassifiedFrames = 0;
}

void DX8Wrapper::Debug_Note_Unclassified_Draw(
	TextureBaseClass* tex0, unsigned fvf, bool wentToShader, bool blended, bool softOverlay)
{
	++s_unclassifiedTotal;
	const char* name = tex0 ? tex0->Get_Texture_Name().str() : "(no texture)";
	for (int i = 0; i < s_unclassifiedCount; ++i) {
		UnclassifiedGroup& g = s_unclassified[i];
		if (g.texture == name && g.fvf == fvf) {
			++g.count;
			if (wentToShader) ++g.toShader;
			if (blended)      ++g.blended;
			if (softOverlay)  ++g.softOverlay;
			return;
		}
	}
	if (s_unclassifiedCount >= MAX_UNCLASSIFIED) return;
	UnclassifiedGroup& g = s_unclassified[s_unclassifiedCount++];
	g.texture = name;
	g.fvf = fvf;
	g.count = 1;
	g.toShader = wentToShader ? 1 : 0;
	g.blended = blended ? 1 : 0;
	g.softOverlay = softOverlay ? 1 : 0;
}

void DX8Wrapper::Debug_Report_Unclassified_Draws()
{
	if (++s_unclassifiedFrames < 600) return;
	s_unclassifiedFrames = 0;

	if (s_unclassifiedTotal == 0) {
		WWDEBUG_SAY(("UNCLASSIFIED DRAWS: none this window -- every draw reaching the "
					 "routing block declared a technique"));
		return;
	}
	WWDEBUG_SAY(("UNCLASSIFIED DRAWS: %u over 600 frames, %d distinct "
				 "(texture x vertex format):", s_unclassifiedTotal, s_unclassifiedCount));
	for (int i = 0; i < s_unclassifiedCount; ++i) {
		const UnclassifiedGroup& g = s_unclassified[i];
		WWDEBUG_SAY(("  %-28s fvf=%08x  x%-7u  ->shader %u  blended %u  soft %u",
			g.texture, g.fvf, g.count, g.toShader, g.blended, g.softOverlay));
	}
	s_unclassifiedCount = 0;
	s_unclassifiedTotal = 0;
}

//-----------------------------------------------------------------------------
// Fixed-function attribution.
//
// The routing census says how much of the frame is still drawn by the fixed-function
// pipeline -- around 40% of all draws. It does not say what that 40% *is*: the
// fixed-function row's example names are empty, because a name is only attached to
// mesh-renderer draws and almost nothing left on fixed function is one.
//
// So attribute every one of them. A draw's identity is whichever of these it has:
// the technique declaration scope it was made inside, its mesh name, or -- for the
// callers that have neither -- the frame pass it belongs to, which at least separates
// terrain from water from the 2D interface. Texture and vertex format go alongside,
// because for an undeclared caller those two together are usually enough to recognise
// it (excloud01.tga at FVF 0x252 is the cloud layer, and nothing else is).
//
// Temporary, in service of removing fixed function entirely: it exists to put the
// remaining work in order of what it actually costs, and comes out when the last
// caller is converted.
//-----------------------------------------------------------------------------
// Grouped by *who*, with textures kept only as examples. The first attempt keyed on the
// texture as well, and the interface filled all 40 rows before the map had finished
// loading: the 2D path draws through a font cache whose textures are procedural, so each
// one is a distinct short-lived name and every row was a different glyph page. 497891 of
// 524578 draws went uncounted behind them. Who drew it is the question anyway -- the
// texture is a hint for recognising an undeclared caller, not an identity.
namespace {
	enum { FF_SAMPLES = 3 };
	struct FFGroup {
		const char* who;        // declaration site, mesh name, or frame pass
		unsigned fvf;
		unsigned count;
		unsigned identityView;  // drawn with an identity view, i.e. 2D
		unsigned reasons;       // bitmask of the ffReason codes seen
		const char* samples[FF_SAMPLES];
		int sampleCount;
	};
	enum { MAX_FF_GROUPS = 48 };
	FFGroup s_ffGroups[MAX_FF_GROUPS];
	int s_ffGroupCount = 0;
	unsigned s_ffTotal = 0;
	unsigned s_ffDropped = 0;   // draws that did not fit the table, so the total stays honest
	int s_ffFrames = 0;
	// The control. A fixed-function count means nothing on its own: zero reads the same
	// whether the last caller was converted or the instrument stopped being reached.
	// Reported as a share of the two, so the number that matters is visible directly.
	unsigned s_ffRouted = 0;
	// Depth-pass draws dropped before submission because they could write neither colour
	// nor depth. Counted apart from both: they are not fixed-function draws, and they are
	// not programmable ones either -- they are not draws. Folding them into either side
	// would misstate it, and folding them into the fixed-function side is exactly what
	// made the suppression look like it had done nothing.
	unsigned s_ffSuppressed = 0;
	// The same draws counted where they are actually dropped, at the submission rather
	// than at the routing block's verdict. The two differ, and the gap is the point of the
	// change that introduced this: a run of draws whose render state has not moved never
	// re-enters the routing block, so it never reaches the verdict above, but every one of
	// them still arrives at the submission with the declined caster's write masks standing
	// at the device. s_ffSuppressed counts verdicts; this counts draw calls not made.
	unsigned s_ffUnsubmitted = 0;

	// Texture names are only ever used as examples, so a name that is not printable ASCII
	// is dropped rather than written to the log. Some of them are not names at all: the
	// 2D path hands out procedural textures whose name field holds uninitialised bytes,
	// which arrive in the log as mojibake and, worse, as a plausible-looking row.
	const char* FFSafeName(const char* n)
	{
		if (n == nullptr || *n == '\0') return nullptr;
		for (const char* p = n; *p; ++p) {
			if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x7e) return nullptr;
		}
		return n;
	}
}

//-----------------------------------------------------------------------------
// Fixed-function call sites.
//
// The census above says no draw still uses fixed function. This one says who still
// writes it. Those are different questions with different answers: a caller can build a
// whole two-stage combine and then have its draw claimed by a pixel shader that reads
// none of it, and the combine is still a dozen D3D calls per draw.
//
// See the note on Debug_Set_FF_Site in dx8wrapper.h for why writes are counted at the
// device rather than at the tracked-state array.
//-----------------------------------------------------------------------------
namespace {
	struct FFSiteGroup {
		const char* site;
		unsigned    calls;        // times the emitting function ran
		unsigned    stageWrites;  // SetTextureStageState calls it caused
		unsigned    renderWrites; // SetRenderState/SetMaterial calls it caused
	};
	enum { MAX_FF_SITES = 64 };
	FFSiteGroup s_ffSites[MAX_FF_SITES];
	int      s_ffSiteCount = 0;
	int      s_ffSiteFrames = 0;
	unsigned s_ffSiteDropped = 0;
	const char* s_ffSite = nullptr;

	// Which state words arrive with no site around them. The "(unattributed)" row says
	// only that some emitting function has no FF_SITE on it, and the count alone does not
	// say which -- there are ten deferred render states and seventeen stage states, and
	// the writers of each are in different subsystems. The word names the writer.
	enum { MAX_UNATTRIBUTED_WORDS = 12 };
	struct UnattributedWord { unsigned isStage; unsigned state; unsigned count; };
	UnattributedWord s_ffUnattributed[MAX_UNATTRIBUTED_WORDS];
	int s_ffUnattributedCount = 0;

	void NoteUnattributedWord(unsigned isStage, unsigned state)
	{
		for (int i = 0; i < s_ffUnattributedCount; ++i) {
			if (s_ffUnattributed[i].isStage == isStage && s_ffUnattributed[i].state == state) {
				++s_ffUnattributed[i].count;
				return;
			}
		}
		if (s_ffUnattributedCount >= MAX_UNATTRIBUTED_WORDS) return;
		UnattributedWord& w = s_ffUnattributed[s_ffUnattributedCount++];
		w.isStage = isStage; w.state = state; w.count = 1;
	}

	FFSiteGroup* FindOrAddFFSite(const char* site)
	{
		for (int i = 0; i < s_ffSiteCount; ++i) {
			if (s_ffSites[i].site == site) return &s_ffSites[i];
		}
		if (s_ffSiteCount >= MAX_FF_SITES) return nullptr;
		FFSiteGroup& g = s_ffSites[s_ffSiteCount++];
		g.site = site;
		g.calls = 0;
		g.stageWrites = 0;
		g.renderWrites = 0;
		return &g;
	}
}

const char* DX8Wrapper::Debug_Get_FF_Site()
{
	return s_ffSite;
}

void DX8Wrapper::Debug_Set_FF_Site(const char* site)
{
	s_ffSite = site;
	// Counted on entry, not on the first write, so a site that runs constantly and writes
	// nothing is distinguishable from one that never runs at all. Those want opposite
	// treatment -- the first is redundant state to delete outright, the second is dead
	// code -- and a writes-only census cannot tell them apart.
	if (site == nullptr) return;
	FFSiteGroup* g = FindOrAddFFSite(site);
	if (g == nullptr) { ++s_ffSiteDropped; return; }
	++g->calls;
}

void DX8Wrapper::Debug_Note_FF_State_Write(unsigned isTextureStage, unsigned state)
{
	// An unattributed write is the interesting failure here, so it gets a row of its own
	// rather than being dropped: it means some emitting function has no FF_SITE on it.
	// The word it wrote is recorded too, because that is what names the function.
	if (s_ffSite == nullptr) NoteUnattributedWord(isTextureStage, state);
	FFSiteGroup* g = FindOrAddFFSite(s_ffSite != nullptr ? s_ffSite : "(unattributed)");
	if (g == nullptr) { ++s_ffSiteDropped; return; }
	if (isTextureStage) ++g->stageWrites; else ++g->renderWrites;
}

//-----------------------------------------------------------------------------
// Device-state audit.
//
// The wrapper's whole claim is that it knows what the device holds. This checks it:
// every word the wrapper claims to know is read back off D3D and compared with what
// the wrapper believes the device holds. Any disagreement is somebody writing the
// device behind its back, and the word that disagrees names the writer -- the same
// trick the fixed-function census uses.
//
// It began as an audit of Invalidate_Cached_Render_States, which asserts exactly the
// opposite: "forget what you knew, it is no longer true". Over gla_midgame in both
// shadow configurations, 28650 invalidations from the render path found the device
// disagreeing zero times, which is what retired them. What is left of the call is the
// device-lifetime path, where the disagreement is real and total -- a reset caught mid
// run reported 109 wrong words -- and the check now runs on a timer instead, where a
// new escape shows up as it appears rather than wherever the next invalidation is.
//
// What "the wrapper believes the device holds" is two different arrays. For the
// deferred fixed-function words it is FFDeviceRender / FFDeviceStage, which are only
// written at a flush. For everything else the tracked array *is* the belief, because
// those writes go straight through -- and for the transforms that array is
// DX8Transforms, which Send_Transform_To_Device fills on the way to D3D.
//
// Words whose tracked value is still the sentinel are skipped: the wrapper is not
// claiming anything about them, so there is nothing to be wrong about. So are the
// seven D3D8-only render states the compatibility layer parks in slots 220..226,
// which the device does not store at all -- the ZBIAS trap, in instrument form.
//-----------------------------------------------------------------------------
namespace {
	struct InvalidateSite {
		const char* who;
		unsigned    calls;        // times this site asked to invalidate
		unsigned    wrongCalls;   // ...of which found the device holding something else
		unsigned    wrongWords;   // and how many words over all of them
	};
	enum { MAX_INVALIDATE_SITES = 32 };
	InvalidateSite s_invSites[MAX_INVALIDATE_SITES];
	int      s_invSiteCount = 0;
	unsigned s_invDropped = 0;
	int      s_invFrames = 0;
	// How many words the positive control's deliberate desynchronisation was seen as.
	// -1 means it did not get to run this window.
	int      s_invControlSaw = -1;

	// kind: 0 render state, 1 texture stage state, 2 material
	struct InvWord { unsigned kind; unsigned state; unsigned count; };
	enum { MAX_INV_WORDS = 32 };
	InvWord  s_invWords[MAX_INV_WORDS];
	int      s_invWordCount = 0;
	unsigned s_invWordDropped = 0;

	void NoteInvWord(unsigned kind, unsigned state)
	{
		for (int i = 0; i < s_invWordCount; ++i) {
			if (s_invWords[i].kind == kind && s_invWords[i].state == state) {
				++s_invWords[i].count; return;
			}
		}
		if (s_invWordCount >= MAX_INV_WORDS) { ++s_invWordDropped; return; }
		s_invWords[s_invWordCount].kind = kind;
		s_invWords[s_invWordCount].state = state;
		s_invWords[s_invWordCount].count = 1;
		++s_invWordCount;
	}

	InvalidateSite* FindOrAddInvSite(const char* who)
	{
		for (int i = 0; i < s_invSiteCount; ++i) {
			if (s_invSites[i].who == who) return &s_invSites[i];
		}
		if (s_invSiteCount >= MAX_INVALIDATE_SITES) return nullptr;
		InvalidateSite& g = s_invSites[s_invSiteCount++];
		g.who = who; g.calls = 0; g.wrongCalls = 0; g.wrongWords = 0;
		return &g;
	}

	// The ten D3D8 stage states the compatibility layer turns into D3D9 sampler states.
	// Reading one back has to go the same way it was written or it is a different word.

	// -1 means the transform half of the positive control did not get to run this window.
	// Which slots the wrapper claims is DX8Wrapper::FFDeviceTransformValid; the audit reads
	// that directly rather than keeping a second copy here.
	int       s_invControlSawXform = -1;

	const char * AuditTransformName(unsigned which)
	{
		switch (which) {
		case D3DTS_WORLD:      return "D3DTS_WORLD";
		case D3DTS_VIEW:       return "D3DTS_VIEW";
		case D3DTS_PROJECTION: return "D3DTS_PROJECTION";
		case D3DTS_TEXTURE0:   return "D3DTS_TEXTURE0";
		case D3DTS_TEXTURE1:   return "D3DTS_TEXTURE1";
		case D3DTS_TEXTURE2:   return "D3DTS_TEXTURE2";
		case D3DTS_TEXTURE3:   return "D3DTS_TEXTURE3";
		case D3DTS_TEXTURE4:   return "D3DTS_TEXTURE4";
		case D3DTS_TEXTURE5:   return "D3DTS_TEXTURE5";
		case D3DTS_TEXTURE6:   return "D3DTS_TEXTURE6";
		case D3DTS_TEXTURE7:   return "D3DTS_TEXTURE7";
		default:               return "D3DTS_(other)";
		}
	}

	// The fixed-function lighting census. Counted per draw, after the flush, so what it
	// reads is what the device will use and not what somebody asked for earlier.
	unsigned s_transformDeviceWrites = 0; // matrices handed to D3D
	unsigned s_lightDraws = 0;          // every draw through DX8Wrapper::Draw
	unsigned s_lightFFDraws = 0;        // ...of which fixed function
	unsigned s_lightFFLit = 0;          // ...with D3DRS_LIGHTING enabled at the device
	unsigned s_lightFFLitByDefault = 0; // ...of which by D3D's default, nothing having flushed
	int      s_lightFrames = 0;

	// ...and which drawer each fixed-function draw belonged to.
	//
	// The count on its own is the thing that cannot be acted on. "4.9% of draws are
	// fixed function" is a percentage; "the shadow volumes and the six screen quads are
	// fixed function" is a list of files, and a D3D11 backend has to answer for every
	// one of them -- there is no FVF, no transform-and-lighting stage and no combine, so
	// each of these draws needs an input layout and a vertex shader written for it.
	//
	// Keyed the way the call-site census keys its flushes: the declaration scope if one
	// is in force, otherwise the pass. The same key means the two tables can be read side
	// by side -- one says who still *writes* fixed-function state, this one says who
	// still *draws* with it, and they are not the same set.
	//
	// The two halves are split because they are separate pieces of work. A draw that is
	// fixed function only in its pixel stage has a vertex shader already and needs a
	// pixel shader; one that is fixed function only in its vertex stage arrives with an
	// FVF and needs an input layout and a vertex shader. A draw that is both needs both.
	enum { MAX_FF_DRAW_SITES = 24 };
	struct FFDrawSite {
		const char* who;
		unsigned    draws;      // fixed-function draws attributed here
		unsigned    vertexOnly; // ...fixed function in the vertex stage alone
		unsigned    pixelOnly;  // ...in the pixel stage alone
		unsigned    both;       // ...in both
		unsigned    lit;        // ...with D3DRS_LIGHTING enabled at the device
		unsigned    fvf;        // an example vertex format, for the vertex-side ones
		unsigned    routed;     // ...for which the routing block actually ran
		unsigned    masked;     // ...that may write neither colour nor depth
		unsigned    inert;      // ...and does not touch stencil either
	};
	FFDrawSite s_ffDrawSites[MAX_FF_DRAW_SITES];
	int      s_ffDrawSiteCount = 0;
	unsigned s_ffDrawSiteDropped = 0;

	// Whether the routing block ran for the draw being counted.
	//
	// Apply_Render_State_Changes returns early on !render_state_changed, so a draw whose
	// state is unchanged since the last one is submitted with whatever shaders the last
	// routed draw left bound -- and if that draw was one the routing declined, what it
	// left is an FVF. Read at the top of Draw(), because Apply clears the word.
	bool s_ffDrawRoutingRan = false;
}

unsigned DX8Wrapper::Debug_Audit_Invalidation(const char * site)
{
	if (site == nullptr) site = "(unnamed)";
	InvalidateSite* g = FindOrAddInvSite(site);
	if (g == nullptr) { ++s_invDropped; return 0; }
	++g->calls;

	if (Gfx == nullptr) return 0;

	unsigned wrong = 0;

	for (unsigned a = 0; a < sizeof(RenderStates)/sizeof(unsigned); ++a) {
		if (RenderStates[a] == 0x12345678) continue;      // wrapper claims nothing
		if (a >= 220 && a <= 226) continue;               // compatibility-layer dummies
		// The three alpha-test words are tracked but never sent (see Set_DX8_Render_State),
		// so the wrapper is not claiming anything about the device here either -- the same
		// category as the dummies above, reached a different way. Without this the audit
		// would report them wrong on every check and drown any real finding.
		if (a == D3DRS_ALPHATESTENABLE || a == D3DRS_ALPHAFUNC || a == D3DRS_ALPHAREF)
			continue;
		const bool deferred = Is_Deferred_FF_Render_State(a);
		const unsigned believed = deferred ? FFDeviceRender[a] : RenderStates[a];
		if (believed == 0x12345678) continue;
		unsigned actual = 0;
		if (!Gfx->Get_Render_State(a, actual)) continue;
		if (actual == believed) continue;
		++wrong;
		NoteInvWord(0, a);
	}

	for (unsigned stage = 0; stage < MAX_TEXTURE_STAGES; ++stage) {
		for (unsigned b = 1; b < 32; ++b) {
			if (TextureStageStates[stage][b] == 0x12345678) continue;
			const bool deferred = Is_Deferred_FF_Stage_State(b);
			const unsigned believed = deferred ? FFDeviceStage[stage][b] : TextureStageStates[stage][b];
			if (believed == 0x12345678) continue;
			unsigned actual = 0;
			if (!Gfx->Get_Texture_Stage_State(stage, b, actual)) continue;
			if (actual == believed) continue;
			++wrong;
			NoteInvWord(1, b);
		}
	}

	// Transforms. Only the slots something has actually sent are read: an untouched slot
	// holds whatever D3D defaulted it to and the wrapper is not claiming anything about
	// it, which is the same rule the 0x12345678 sentinel enforces above.
	// Transforms. Compared against what was last *flushed*, not against DX8Transforms:
	// since the deferral, the tracked array is what the wrapper wants the device to have
	// and FFDeviceTransform is what it believes the device does have, exactly as
	// FFDeviceRender is for the deferred render states. A slot nothing has flushed is a
	// slot the wrapper claims nothing about, and is skipped for the same reason a
	// 0x12345678 word is -- so on a configuration with no fixed-function draws this sweep
	// legitimately has nothing to check, and says so through its control.
	for (unsigned slot = 0; slot < FF_TRANSFORM_SLOTS; ++slot) {
		if (!(FFDeviceTransformValid & (1u << slot))) continue;
		const unsigned which = FF_Transform_Which(slot);
		D3DMATRIX actual;
		if (!Gfx->Get_Transform(which, (float*)&actual)) continue;
		if (memcmp(&actual, &FFDeviceTransform[slot], sizeof(D3DMATRIX)) == 0) continue;
		++wrong;
		NoteInvWord(3, which);
	}

	if (wrong) { ++g->wrongCalls; g->wrongWords += wrong; }
	return wrong;
}

void DX8Wrapper::Debug_Audit_Frame_End()
{
	// The positive control, and the reason the zero above is worth reading. A zero from a
	// device read-back means the same thing whether the wrapper's model is right or the
	// instrument has quietly stopped being reached, so once per reporting window one word
	// is written straight at the device behind the wrapper's back and the audit is asked
	// whether it noticed.
	//
	// D3DRS_FILLMODE, and here: this runs after EndScene, so no draw can see the wrong
	// value, and it is put back from the tracked value on the next line. If the control
	// ever reports 0 the instrument is broken and every other number on this report is
	// worthless.
	if (s_invFrames + 1 >= 600 && Gfx != nullptr &&
		RenderStates[D3DRS_FILLMODE] != 0x12345678)
	{
		const unsigned real = RenderStates[D3DRS_FILLMODE];
		const unsigned wrong = (real == D3DFILL_POINT) ? D3DFILL_WIREFRAME : D3DFILL_POINT;
		Gfx->Set_Render_State(D3DRS_FILLMODE, wrong);
		s_invControlSaw = (int)Debug_Audit_Invalidation("(control -- one word poked at the device)");
		Gfx->Set_Render_State(D3DRS_FILLMODE, real);
	}

	// The same control for the transform sweep, run separately rather than folded into the
	// one above so that a failure names which half of the instrument went quiet. D3DTS_VIEW
	// is poked because something writes it every frame, so the slot is always valid; the
	// matrix put there is not a transform anything could mistake for real, and it is put
	// back from the recorded value on the next line -- again after EndScene, so no draw can
	// see it.
	if (s_invFrames + 1 >= 600 && Gfx != nullptr && FFDeviceTransformValid != 0) {
		// Whichever slot the wrapper actually claims. On a configuration with no
		// fixed-function draws none of them is claimed and this does not run, which the
		// report distinguishes from the control running and seeing nothing.
		unsigned long slot;
		_BitScanForward(&slot, FFDeviceTransformValid);
		const unsigned which = FF_Transform_Which(slot);
		const D3DMATRIX real = FFDeviceTransform[slot];
		D3DMATRIX bogus = real;
		bogus.m[3][0] += 12345.0f;
		Gfx->Set_Transform(which, (const float*)&bogus);
		s_invControlSawXform = (int)Debug_Audit_Invalidation("(control -- one transform poked at the device)");
		Gfx->Set_Transform(which, (const float*)&real);
	}

	// Every thirtieth frame, not every frame. The check is ~300 device reads, each a
	// round trip, and at 30 Hz that was enough to cost the debug build a third of its
	// frame rate -- which matters beyond the frame rate itself, because the cloud map
	// scrolls on wall time, so two runs at different speeds photograph the terrain under
	// different cloud shadows and no frame dump can be compared with another. Nothing it
	// looks for happens once: a write that escapes the wrapper escapes it every frame.
	if ((s_invFrames % 30) == 0)
		Debug_Audit_Invalidation("(frame end)");
}

void DX8Wrapper::Debug_Report_Invalidations()
{
	if (++s_invFrames < 600) return;
	s_invFrames = 0;

	unsigned totalCalls = 0, totalWrongCalls = 0, totalWrongWords = 0;
	for (int i = 0; i < s_invSiteCount; ++i) {
		totalCalls += s_invSites[i].calls;
		totalWrongCalls += s_invSites[i].wrongCalls;
		totalWrongWords += s_invSites[i].wrongWords;
	}

	WWDEBUG_SAY(("DEVICE STATE AUDIT over 600 frames: %u checks from %d sites%s -- %u of "
				 "them found the device holding something the wrapper did not "
				 "expect, over %u words",
		totalCalls, s_invSiteCount, s_invDropped ? "  -- TABLE FULL" : "",
		totalWrongCalls, totalWrongWords));
	if (s_invControlSaw > 0) {
		WWDEBUG_SAY(("  control: one word written at the device behind the wrapper's back "
					 "was seen, so a zero above is the wrapper being right and not the "
					 "instrument being unreachable."));
	} else if (s_invControlSaw == 0) {
		WWDEBUG_SAY(("  CONTROL FAILED: a word written at the device behind the wrapper's "
					 "back was NOT seen. Every number on this report is worthless."));
	} else {
		WWDEBUG_SAY(("  control did not run this window; the zeroes above are unqualified."));
	}
	s_invControlSaw = -1;
	if (s_invControlSawXform > 0) {
		WWDEBUG_SAY(("  transform control: one matrix written at the device behind the "
					 "wrapper's back was seen, so the transform sweep is reaching D3D."));
	} else if (s_invControlSawXform == 0) {
		WWDEBUG_SAY(("  TRANSFORM CONTROL FAILED: a matrix written at the device behind the "
					 "wrapper's back was NOT seen. The transform numbers are worthless."));
	} else {
		WWDEBUG_SAY(("  transform control did not run: the wrapper has not flushed a matrix "
					 "to the device, so it is claiming nothing about one and the sweep above "
					 "had nothing to check. Expected wherever there are no fixed-function draws."));
	}
	s_invControlSawXform = -1;

	for (int rank = 0; rank < s_invSiteCount; ++rank) {
		int best = -1;
		unsigned bestCalls = 0;
		for (int i = 0; i < s_invSiteCount; ++i) {
			if (s_invSites[i].calls > bestCalls) { bestCalls = s_invSites[i].calls; best = i; }
		}
		if (best < 0) break;
		WWDEBUG_SAY(("    %-40s x%-8u  %u checks wrong, %u words",
			s_invSites[best].who, s_invSites[best].calls,
			s_invSites[best].wrongCalls, s_invSites[best].wrongWords));
		s_invSites[best].calls = 0;
	}
	if (s_invWordCount > 0) {
		WWDEBUG_SAY(("  the words the device disagreed on%s, which name their writer:",
			s_invWordDropped ? " -- TABLE FULL" : ""));
		for (int rank = 0; rank < s_invWordCount; ++rank) {
			int best = -1;
			unsigned bestCount = 0;
			for (int i = 0; i < s_invWordCount; ++i) {
				if (s_invWords[i].count > bestCount) { bestCount = s_invWords[i].count; best = i; }
			}
			if (best < 0) break;
			const InvWord& w = s_invWords[best];
			WWDEBUG_SAY(("      %-36s x%u",
				w.kind == 3 ? AuditTransformName(w.state)
					: (w.kind == 1
						? Get_DX8_Texture_Stage_State_Name((D3DTEXTURESTAGESTATETYPE)w.state)
						: Get_DX8_Render_State_Name((D3DRENDERSTATETYPE)w.state)),
				w.count));
			s_invWords[best].count = 0;
		}
	}
	s_invSiteCount = 0;
	s_invWordCount = 0;
	s_invDropped = 0;
	s_invWordDropped = 0;
}

void DX8Wrapper::Debug_Note_Device_Transform(unsigned which, const float * matrix4x4)
{
	(void)which; (void)matrix4x4;
	++s_transformDeviceWrites;
}

void DX8Wrapper::Debug_Note_Lighting_Draw()
{
	++s_lightDraws;
	if (!Is_Fixed_Function_Draw()) return;
	++s_lightFFDraws;

	// D3DRS_LIGHTING is a deferred fixed-function word, so the tracked array holds what a
	// caller asked for and FFDeviceRender holds what the device was actually given. This
	// runs after the flush in Draw(), so the second is the one that decides the pixel.
	//
	// The sentinel is the trap here, and reading it as "off" would invert this whole
	// census. FFDeviceRender is poison until something flushes that word, and nothing ever
	// has: DX8Wrapper::Init writes D3DRS_LIGHTING FALSE through Set_DX8_Render_State, which
	// defers it like any other fixed-function word, so the value the device is actually
	// holding is D3D9's own default -- and that default is TRUE. Poison here therefore
	// means lighting is ON at the device, not unknown and not off. Debug_Report_Lighting
	// reads the word back off D3D once a window rather than leaving that as an argument.
	unsigned lighting = FFDeviceRender[D3DRS_LIGHTING];
	if (lighting == 0x12345678) { lighting = TRUE; ++s_lightFFLitByDefault; }

	// Attribute the draw before returning on the lighting test, because the lighting word
	// is a property of these draws and not the reason for counting them. A D3D11 backend
	// owes an input layout and a shader to every row of this table, lit or not.
	{
		const char* who = s_declarationSite;
		if (who == nullptr) who = Debug_Current_Pass_Name();
		FFDrawSite* e = nullptr;
		for (int i = 0; i < s_ffDrawSiteCount; ++i) {
			if (s_ffDrawSites[i].who == who) { e = &s_ffDrawSites[i]; break; }
		}
		if (e == nullptr && s_ffDrawSiteCount >= MAX_FF_DRAW_SITES) {
			++s_ffDrawSiteDropped;
		} else {
			if (e == nullptr) {
				e = &s_ffDrawSites[s_ffDrawSiteCount++];
				e->who = who; e->draws = 0; e->vertexOnly = 0; e->pixelOnly = 0;
				e->both = 0; e->lit = 0; e->fvf = 0; e->routed = 0; e->masked = 0;
				e->inert = 0;
			}
			++e->draws;
			const bool v = Is_Fixed_Function_Vertex_Draw();
			const bool p = Is_Fixed_Function_Pixel_Draw();
			if (v && p)      ++e->both;
			else if (v)      ++e->vertexOnly;
			else             ++e->pixelOnly;
			// An example rather than a group key: the same drawer can submit several
			// formats, and the point of the number is to say what an input layout for
			// this row would have to describe.
			if (v && e->fvf == 0) e->fvf = Vertex_Shader;
			if (lighting != FALSE) ++e->lit;
			if (s_ffDrawRoutingRan) ++e->routed;
			// A draw that may write neither colour nor depth cannot change a render
			// target, so a second backend owes it nothing at all -- it is a draw call
			// spent on producing nothing, and the work is to stop submitting it rather
			// than to write a shader for it. Read from the tracked states, which is
			// what the device is holding: the masks are cached and stay set until
			// something changes them.
			//
			// Both write masks off is not the same as writing nothing: stencil is live in
			// this engine (the player-colour pass, the shadow volumes and the shadow decals
			// all enable it) and a draw with colour and depth masked can still increment a
			// stencil buffer. `inert` is the test that actually licenses deleting a draw;
			// `masked` is kept beside it as the control, so the difference between the two
			// is the set that does something after all.
			//
			// A poisoned tracked word fails every one of these comparisons, so both figures
			// can only under-report.
			if (RenderStates[D3DRS_COLORWRITEENABLE] == 0 &&
				RenderStates[D3DRS_ZWRITEENABLE] == FALSE) {
				++e->masked;
				if (RenderStates[D3DRS_STENCILENABLE] == FALSE) ++e->inert;
			}
		}
	}

	if (lighting == FALSE) return;
	++s_lightFFLit;
}

void DX8Wrapper::Debug_Report_Lighting()
{
	if (++s_lightFrames < 600) return;
	s_lightFrames = 0;

	// Three nested counts, each the control for the one below it: a zero on the last line
	// only means something if the lines above it are not zero for a different reason.
	// Read the word back off the device rather than asserting what D3D's default is.
	unsigned deviceLighting = 0xffffffff;
	if (Gfx != nullptr && !Gfx->Get_Render_State(D3DRS_LIGHTING, deviceLighting))
		deviceLighting = 0xffffffff;
	// Nothing sends a light any more, so this is now a watch rather than a census: if the
	// last figure is ever nonzero, some draw has turned the fixed-function lighting stage
	// on and there are no lights in it, and it will render black rather than warn anybody.
	WWDEBUG_SAY(("FIXED-FUNCTION LIGHTING CENSUS over 600 frames: %u draws, %u fixed "
				 "function, %u of those with lighting enabled at the device (%u of them "
				 "because nothing ever flushed the word and D3D's default is on). "
				 "D3DRS_LIGHTING read back off the device now: %u. Matrices handed to "
				 "D3D: %u",
		s_lightDraws, s_lightFFDraws, s_lightFFLit, s_lightFFLitByDefault,
		deviceLighting, s_transformDeviceWrites));
	if (s_lightDraws == 0) {
		WWDEBUG_SAY(("  CONTROL FAILED: no draws reached this census at all, so the counts "
					 "above are not a measurement of anything."));
	}

	// Who those fixed-function draws belonged to.
	//
	// This is the number a D3D11 estimate is made of, and it is not the one the
	// burn-down reports. FIXED-FUNCTION DRAWS is taken inside the routing block and asks
	// whether every mesh draw the block saw was handed a shader; it reads zero and its
	// own header says direct-device drawers are not counted. This asks what is *bound*
	// at the draw, which is what the hardware uses -- and D3D11 has nothing to run for a
	// draw with an FVF and no shader.
	if (s_ffDrawSiteCount > 0) {
		WWDEBUG_SAY(("  the %u by the drawer that submitted them%s -- these are the draws a "
					 "second backend would have to be given an input layout and a shader "
					 "for, since it has no fixed-function pipeline to fall back on. This "
					 "counts draws through DX8Wrapper::Draw only: a drawer that goes to "
					 "the device itself never reaches here, so the volumetric shadows are "
					 "NOT in this table even though they are the largest fixed-function "
					 "draw left. The call-site census above is where those show up.",
			s_lightFFDraws, s_ffDrawSiteDropped ? "  -- TABLE FULL" : ""));
		for (int rank = 0; rank < s_ffDrawSiteCount; ++rank) {
			int best = -1;
			unsigned bestDraws = 0;
			for (int i = 0; i < s_ffDrawSiteCount; ++i) {
				if (s_ffDrawSites[i].draws > bestDraws) {
					bestDraws = s_ffDrawSites[i].draws; best = i;
				}
			}
			if (best < 0) break;
			const FFDrawSite& d = s_ffDrawSites[best];
			WWDEBUG_SAY(("    %-32s %8u draws  (vertex only %u, pixel only %u, both %u; "
						 "%u lit; example FVF 0x%x; %u routed, %u inherited; "
						 "%u write neither colour nor depth, %u of those with stencil off too)",
				d.who, d.draws, d.vertexOnly, d.pixelOnly, d.both, d.lit, d.fvf,
				d.routed, d.draws - d.routed, d.masked, d.inert));
			s_ffDrawSites[best].draws = 0;
		}
	}
	else if (s_lightFFDraws > 0) {
		WWDEBUG_SAY(("  ATTRIBUTION FAILED: %u fixed-function draws were counted and none "
					 "were attributed, so the table is not reading the same draws.",
			s_lightFFDraws));
	}
	s_ffDrawSiteCount = 0;
	s_ffDrawSiteDropped = 0;

	s_lightDraws = 0;
	s_lightFFDraws = 0;
	s_lightFFLit = 0;
	s_lightFFLitByDefault = 0;
	s_transformDeviceWrites = 0;
}

void DX8Wrapper::Debug_Report_FF_Sites()
{
	if (++s_ffSiteFrames < 600) return;
	s_ffSiteFrames = 0;

	unsigned totalWrites = 0;
	for (int i = 0; i < s_ffSiteCount; ++i)
		totalWrites += s_ffSites[i].stageWrites + s_ffSites[i].renderWrites;

	if (s_ffSiteCount == 0) {
		WWDEBUG_SAY(("FIXED-FUNCTION CALL SITES: none reached this window"));
		return;
	}
	WWDEBUG_SAY(("FIXED-FUNCTION CALL SITES over 600 frames: %u state words asked for from "
				 "%d sites%s -- of which %u actually reached the device",
		totalWrites, s_ffSiteCount,
		s_ffSiteDropped ? "  -- TABLE FULL, some writes uncounted" : "",
		s_ffFlushedWrites));
	// Which draws the device writes were actually made for. This is the number the
	// burn-down is finished on -- the census above counts what callers asked for, and
	// most of that is the routing block's input language rather than device traffic.
	if (s_ffFlushSiteCount > 0) {
		WWDEBUG_SAY(("  of those %u, by the draw that needed them%s:", s_ffFlushedWrites,
			s_ffFlushDropped ? " -- TABLE FULL" : ""));
		for (int rank = 0; rank < s_ffFlushSiteCount; ++rank) {
			int best = -1;
			unsigned bestWrites = 0;
			for (int i = 0; i < s_ffFlushSiteCount; ++i) {
				if (s_ffFlushSites[i].writes > bestWrites) {
					bestWrites = s_ffFlushSites[i].writes; best = i;
				}
			}
			if (best < 0) break;
			WWDEBUG_SAY(("    %-32s %8u words  (FF-vertex %u, FF-pixel %u)",
				s_ffFlushSites[best].who, s_ffFlushSites[best].writes,
				s_ffFlushSites[best].vertexFF, s_ffFlushSites[best].pixelFF));
			s_ffFlushSites[best].writes = 0;
		}
	}
	else if (s_ffFlushedWrites == 0) {
		WWDEBUG_SAY(("  no fixed-function state reached the device at all this window."));
	}
	if (s_ffFlushWordCount > 0) {
		WWDEBUG_SAY(("    ...and the words themselves, which name their writer:"));
		for (int rank = 0; rank < s_ffFlushWordCount; ++rank) {
			int best = -1;
			unsigned bestCount = 0;
			for (int i = 0; i < s_ffFlushWordCount; ++i) {
				if (s_ffFlushWords[i].count > bestCount) {
					bestCount = s_ffFlushWords[i].count; best = i;
				}
			}
			if (best < 0) break;
			const FFFlushWord& w = s_ffFlushWords[best];
			if (w.isStage == 1) {
				WWDEBUG_SAY(("      stage %u  %-30s x%u", w.stage,
					Get_DX8_Texture_Stage_State_Name((D3DTEXTURESTAGESTATETYPE)w.state),
					w.count));
			} else {
				WWDEBUG_SAY(("      %-37s x%u",
					w.isStage == 2 ? "SetMaterial"
						: Get_DX8_Render_State_Name((D3DRENDERSTATETYPE)w.state),
					w.count));
			}
			s_ffFlushWords[best].count = 0;
		}
	}
	s_ffFlushWordCount = 0;
	s_ffFlushSiteCount = 0;
	s_ffFlushDropped = 0;
	s_ffFlushedWrites = 0;

	WWDEBUG_SAY(("  the rest stopped at the tracked state the routing block reads, which is "
				 "where the work that is left is: a site still here still describes its "
				 "draws in fixed function, and needs a declared technique to stop."));
	WWDEBUG_SAY(("  a site with calls and no writes is asking for state that already held "
				 "*on this replay* -- that is not on its own a licence to delete it, since "
				 "another draw order could leave a different value in the same word."));
	WWDEBUG_SAY(("  the top two rows are not residue and will not reach zero. The routing "
				 "block reads TextureStageStates[0..1] and the material to build TexCtl, "
				 "the stage-1 combine and the texgen constants, so what ShaderClass::Apply "
				 "and VertexMaterialClass::Apply write *is* the description the shader path "
				 "translates -- it is this census's input language, not its backlog. They "
				 "shrink only if the combine comes to be expressed some other way."));
	// Descending by writes, then by calls, so the top of the list is where the work goes.
	for (int rank = 0; rank < s_ffSiteCount; ++rank) {
		int best = -1;
		unsigned bestWrites = 0, bestCalls = 0;
		for (int i = 0; i < s_ffSiteCount; ++i) {
			const unsigned w = s_ffSites[i].stageWrites + s_ffSites[i].renderWrites;
			if (s_ffSites[i].calls == 0 && w == 0) continue;   // already printed
			if (w > bestWrites || (w == bestWrites && s_ffSites[i].calls > bestCalls)) {
				bestWrites = w; bestCalls = s_ffSites[i].calls; best = i;
			}
		}
		if (best < 0) break;
		const FFSiteGroup& g = s_ffSites[best];
		WWDEBUG_SAY(("  %-44s calls x%-7u writes: %u stage + %u render",
			g.site, g.calls, g.stageWrites, g.renderWrites));
		s_ffSites[best].calls = 0;
		s_ffSites[best].stageWrites = 0;
		s_ffSites[best].renderWrites = 0;
	}

	for (int i = 0; i < s_ffUnattributedCount; ++i) {
		const UnattributedWord& w = s_ffUnattributed[i];
		// One word is ambiguous: Set_DX8_Material has no state number of its own and
		// borrows D3DRS_DIFFUSEMATERIALSOURCE to report itself, so an entry naming that
		// state is either a real write of it or a material set, and the reader has to
		// look at both. Every other word here identifies its writer outright.
		WWDEBUG_SAY(("    unattributed word: %-34s x%u",
			w.isStage ? Get_DX8_Texture_Stage_State_Name((D3DTEXTURESTAGESTATETYPE)w.state)
					  : Get_DX8_Render_State_Name((D3DRENDERSTATETYPE)w.state),
			w.count));
	}
	s_ffUnattributedCount = 0;

	s_ffSiteCount = 0;
	s_ffSiteDropped = 0;
}

// Depth-pass draws that the routing block declined and Is_Inert_Depth_Pass_Draw would not
// call inert. Both write masks are off, so the only way one of these can leave a mark is
// through the stencil buffer -- and on the shadow-map configuration nothing enables stencil
// during the depth pass, which is why the count is zero there and not here. This says what
// the stencil is actually set to do, so "enabled" and "writes" can be told apart.
namespace {
	struct DepthStencilGroup {
		unsigned func, pass, fail, zfail, writeMask, ref, count;
	};
	enum { MAX_DEPTH_STENCIL_GROUPS = 8 };
	DepthStencilGroup s_depthStencil[MAX_DEPTH_STENCIL_GROUPS];
	int      s_depthStencilCount = 0;
	unsigned s_depthStencilDropped = 0;
}

void DX8Wrapper::Debug_Note_Depth_Pass_Stencil()
{
	const unsigned func      = RenderStates[D3DRS_STENCILFUNC];
	const unsigned pass      = RenderStates[D3DRS_STENCILPASS];
	const unsigned fail      = RenderStates[D3DRS_STENCILFAIL];
	const unsigned zfail     = RenderStates[D3DRS_STENCILZFAIL];
	const unsigned writeMask = RenderStates[D3DRS_STENCILWRITEMASK];
	const unsigned ref       = RenderStates[D3DRS_STENCILREF];
	for (int i = 0; i < s_depthStencilCount; ++i) {
		DepthStencilGroup& g = s_depthStencil[i];
		if (g.func == func && g.pass == pass && g.fail == fail && g.zfail == zfail &&
			g.writeMask == writeMask && g.ref == ref) { ++g.count; return; }
	}
	if (s_depthStencilCount >= MAX_DEPTH_STENCIL_GROUPS) { ++s_depthStencilDropped; return; }
	DepthStencilGroup& g = s_depthStencil[s_depthStencilCount++];
	g.func = func; g.pass = pass; g.fail = fail; g.zfail = zfail;
	g.writeMask = writeMask; g.ref = ref; g.count = 1;
}

static const char* Stencil_Op_Name(unsigned op)
{
	switch (op) {
	case D3DSTENCILOP_KEEP:    return "KEEP";
	case D3DSTENCILOP_ZERO:    return "ZERO";
	case D3DSTENCILOP_REPLACE: return "REPLACE";
	case D3DSTENCILOP_INCRSAT: return "INCRSAT";
	case D3DSTENCILOP_DECRSAT: return "DECRSAT";
	case D3DSTENCILOP_INVERT:  return "INVERT";
	case D3DSTENCILOP_INCR:    return "INCR";
	case D3DSTENCILOP_DECR:    return "DECR";
	case 0x12345678:           return "(unknown)";
	default:                   return "?";
	}
}

void DX8Wrapper::Debug_Report_Depth_Pass_Stencil()
{
	if (s_depthStencilCount == 0 && s_depthStencilDropped == 0) return;
	WWDEBUG_SAY(("  depth-pass draws the routing block declined that were still submitted, "
				 "because stencil was enabled and so they are not provably inert:"));
	for (int i = 0; i < s_depthStencilCount; ++i) {
		const DepthStencilGroup& g = s_depthStencil[i];
		WWDEBUG_SAY(("    x%-7u func=%u ref=%u writeMask=0x%02x  pass=%s fail=%s zfail=%s%s",
			g.count, g.func, g.ref, g.writeMask,
			Stencil_Op_Name(g.pass), Stencil_Op_Name(g.fail), Stencil_Op_Name(g.zfail),
			(g.pass == D3DSTENCILOP_KEEP && g.fail == D3DSTENCILOP_KEEP &&
			 g.zfail == D3DSTENCILOP_KEEP) ? "   -- keeps every way out, so it writes nothing" : ""));
	}
	if (s_depthStencilDropped)
		WWDEBUG_SAY(("    (%u further draws did not fit the table)", s_depthStencilDropped));
	s_depthStencilCount = 0;
	s_depthStencilDropped = 0;
}

bool DX8Wrapper::Is_Inert_Depth_Pass_Draw()
{
	// Only the depth pass, because only there is the answer this cheap. That render
	// target is packed depth written as colour, with a depth buffer behind it and
	// stencil unused, so these three words are the complete list of ways a draw could
	// leave a mark on it. Elsewhere a draw with both write masks off can still be doing
	// something -- filling stencil for the player-colour pass or the shadow volumes --
	// and the same three reads would not settle it.
	if (!m_bShadowDepthPass) return false;
	return RenderStates[D3DRS_COLORWRITEENABLE] == 0 &&
		   RenderStates[D3DRS_ZWRITEENABLE] == FALSE &&
		   RenderStates[D3DRS_STENCILENABLE] == FALSE;
}

const char* DX8Wrapper::Debug_Current_Pass_Name()
{
	if (m_bShadowDepthPass)     return "(shadow depth pass)";
	if (m_bTerrainShaderPass)   return "(terrain pass)";
	if (m_bRoadShaderPass)      return "(road pass)";
	if (m_bWaterShaderPass)     return "(water pass)";
	if (m_bMaskPass)            return "(alpha mask pass)";
	if (render_state_changed & (unsigned)VIEW_IDENTITY) return "(2D / identity view)";
	if (s_debugMeshName != nullptr) return "(mesh renderer, no technique)";
	return "(undeclared 3D)";
}

void DX8Wrapper::Debug_Note_Routed_Draw()
{
	++s_ffRouted;
}

void DX8Wrapper::Debug_Note_Suppressed_Draw()
{
	++s_ffSuppressed;
}

void DX8Wrapper::Debug_Note_Unsubmitted_Draw()
{
	++s_ffUnsubmitted;
}

void DX8Wrapper::Debug_Note_FF_Draw(TextureBaseClass* tex0, unsigned fvf,
									bool viewIdentity, unsigned ffReason)
{
	++s_ffTotal;

	// Identity, most specific first. Every value is a literal or a declaration-site
	// literal, so a group key stays a stable pointer and the comparison below is valid.
	//
	// Deliberately *not* the mesh name, though one is usually available. Keying on it
	// put one row per model in the table and filled all 48 with supply-depot crates
	// while leaving 586115 draws uncounted -- and the answer it was hiding is that a
	// mesh name says nothing about which subsystem to go and convert. The pass does.
	// Mesh names still come through as examples below.
	const char* who = s_declarationSite;
	if (who == nullptr) {
		if (m_bShadowDepthPass)          who = "(shadow depth pass)";
		else if (m_bTerrainShaderPass)   who = "(terrain pass)";
		else if (m_bRoadShaderPass)      who = "(road pass)";
		else if (m_bWaterShaderPass)     who = "(water pass)";
		else if (viewIdentity)           who = "(2D / identity view)";
		else if (s_debugMeshName != nullptr) who = "(mesh renderer)";
		else                             who = "(undeclared 3D)";
	}
	// The mesh name is the more useful example where there is one -- "which model is
	// this" is answerable from it, and a shared texture name does not narrow much.
	const char* name = FFSafeName(s_debugMeshName);
	if (name == nullptr && tex0 != nullptr) name = FFSafeName(tex0->Get_Texture_Name().str());

	FFGroup* found = nullptr;
	for (int i = 0; i < s_ffGroupCount; ++i) {
		if (s_ffGroups[i].who == who && s_ffGroups[i].fvf == fvf) {
			found = &s_ffGroups[i];
			break;
		}
	}
	if (found == nullptr) {
		if (s_ffGroupCount >= MAX_FF_GROUPS) { ++s_ffDropped; return; }
		found = &s_ffGroups[s_ffGroupCount++];
		found->who = who;
		found->fvf = fvf;
		found->count = 0;
		found->identityView = 0;
		found->reasons = 0;
		found->sampleCount = 0;
	}
	++found->count;
	if (viewIdentity) ++found->identityView;
	found->reasons |= (1u << (ffReason & 31u));
	if (name != nullptr && found->sampleCount < FF_SAMPLES) {
		for (int i = 0; i < found->sampleCount; ++i) {
			if (found->samples[i] == name) return;
		}
		found->samples[found->sampleCount++] = name;
	}
}

void DX8Wrapper::Debug_Report_FF_Draws()
{
	// Same 600-frame window as the routing census, so the share and its attribution can
	// be read as one thing.
	if (++s_ffFrames < 600) return;
	s_ffFrames = 0;

	const unsigned all = s_ffTotal + s_ffRouted;
	WWDEBUG_SAY(("FIXED-FUNCTION DRAWS (via DX8Wrapper::Draw; direct-device drawers not counted): "
				 "%u of %u draws (%u%%) over 600 frames, "
				 "%d groups (caller x vertex format)%s "
				 "[+%u depth-pass draws declined by the routing block, %u draw calls not "
				 "made -- the second is the larger because a run of draws whose state has "
				 "not moved inherits the declined caster's write masks without re-entering "
				 "the block. Their state still reaches the device; only the submission is "
				 "skipped, and that distinction is worth 20485 pixels]",
		s_ffTotal, all, all ? (unsigned)((unsigned __int64)s_ffTotal * 100 / all) : 0,
		s_ffGroupCount,
		s_ffDropped ? " -- TABLE FULL, some draws uncounted" : "",
		s_ffSuppressed, s_ffUnsubmitted));
	// reasons is a bitmask of which gate sent the draw here, one bit per code. Bit 0 is
	// left over for a draw no gate claims, and should not appear.
	WWDEBUG_SAY(("  reason bits: 2=effects-held-back 3=no-position/normal 4=foreign-vs "
				 "5=untextured 6=blend 7=multitexture 8=texgen | 9=depth-pass "
				 "10=terrain 11=road 12=water 13=2D 14=declared-fixed-fn 15=routing-off "
				 "16=alpha-mask"));
	// Descending by count: the top of this list is the order the conversion work goes in.
	for (int rank = 0; rank < s_ffGroupCount; ++rank) {
		int best = -1;
		unsigned bestCount = 0;
		for (int i = 0; i < s_ffGroupCount; ++i) {
			if (s_ffGroups[i].count > bestCount) { bestCount = s_ffGroups[i].count; best = i; }
		}
		if (best < 0) break;
		const FFGroup& g = s_ffGroups[best];
		WWDEBUG_SAY(("  %-24s fvf=%08x x%-7u %s reasons=%04x  e.g. %s %s %s",
			g.who, g.fvf, g.count,
			g.identityView == g.count ? "2D " : (g.identityView ? "2D?" : "3D "),
			g.reasons,
			g.sampleCount > 0 ? g.samples[0] : "-",
			g.sampleCount > 1 ? g.samples[1] : "",
			g.sampleCount > 2 ? g.samples[2] : ""));
		s_ffGroups[best].count = 0;   // consumed; the table is cleared below anyway
	}
	if (s_ffDropped)
		WWDEBUG_SAY(("  (%u further draws did not fit the table)", s_ffDropped));

	Debug_Report_Depth_Pass_Stencil();

	s_ffGroupCount = 0;
	s_ffTotal = 0;
	s_ffDropped = 0;
	s_ffRouted = 0;
	s_ffSuppressed = 0;
	s_ffUnsubmitted = 0;
}

//-----------------------------------------------------------------------------
// Frame timing.
//
// Reported over the same window as the census so the two line up: what the
// renderer spent, next to what it drew.
//
// This measures whether the renderer kept up, and nothing finer. Replay playback
// drives rendering from the 30 Hz simulation, so a frame that finishes early waits
// and reads 33.3 ms whatever it did -- useful for "did anything miss the budget",
// useless for "which of these two is cheaper".
//
// D3D9 timestamp queries were tried for the finer question and removed. They are
// not trustworthy here: a positive control rendering 2.25x the pixels (1280x720
// against 1920x1080, identical shaders) measured 18.6% *faster*, reproducibly and
// well outside its noise floor. The likely cause is GPU power management -- at the
// 30 fps cap there is roughly half the frame spare, so the GPU sits downclocked
// and heavier work makes it boost, shortening every timestamp delta including the
// ones being compared. Whatever the cause, an instrument that reports more work as
// less time cannot rank configurations, and one that is silently inverted is worse
// than none at all. Answering that question properly needs locked clocks or a
// vendor profiler.
//
// The mean alone is not enough to accept or reject a cost. A change that adds a
// millisecond evenly is a different thing from one that leaves most frames alone
// and doubles the worst ones, and only the second is felt as stutter -- so the
// 95th percentile and the worst frame are reported beside it.
//
// The first frame of a window is dropped: after a load, or after the routing mask
// changes, it carries shader compilation and texture residency that belong to
// neither configuration being compared.
//-----------------------------------------------------------------------------
namespace {
	enum { FRAME_WINDOW = 600 };
	double s_frameMs[FRAME_WINDOW];
	int s_frameCount = 0;
	LARGE_INTEGER s_lastFrameTick = { 0 };
	double s_tickToMs = 0.0;

	int CompareDouble(const void* a, const void* b)
	{
		const double x = *(const double*)a, y = *(const double*)b;
		return (x < y) ? -1 : (x > y) ? 1 : 0;
	}
}

// ----------------------------------------------------------------------------
// Alpha test and fog census. See the note in dx8wrapper.h for what it is for.
// ----------------------------------------------------------------------------

#define MAX_SHADER_NAMES 64

struct ShaderNameEntry { unsigned handle; const char* name; };
static ShaderNameEntry s_shaderNames[MAX_SHADER_NAMES];
static int s_shaderNameCount = 0;
static char s_shaderNamePool[MAX_SHADER_NAMES][32];

void DX8Wrapper::Debug_Register_Shader_Name(unsigned handle, const char* path)
{
	if (handle == 0 || path == nullptr) return;
	for (int i = 0; i < s_shaderNameCount; ++i) {
		if (s_shaderNames[i].handle == handle) return;
	}
	if (s_shaderNameCount >= MAX_SHADER_NAMES) return;
	// "shaders\tree_ps.pso" -> "tree_ps". Copied into a pool rather than kept by
	// pointer: some callers pass a literal and some a temporary, and the table outlives
	// the call either way.
	const char* base = path;
	for (const char* p = path; *p != '\0'; ++p) {
		if (*p == '\\' || *p == '/') base = p + 1;
	}
	char* out = s_shaderNamePool[s_shaderNameCount];
	int n = 0;
	while (base[n] != '\0' && base[n] != '.' && n < 31) { out[n] = base[n]; ++n; }
	out[n] = '\0';
	s_shaderNames[s_shaderNameCount].handle = handle;
	s_shaderNames[s_shaderNameCount].name = out;
	++s_shaderNameCount;
}

const char* DX8Wrapper::Debug_Shader_Name(unsigned handle)
{
	if (handle == 0) return "(fixed function)";
	for (int i = 0; i < s_shaderNameCount; ++i) {
		if (s_shaderNames[i].handle == handle) return s_shaderNames[i].name;
	}
	// Named by value rather than by a bare "(unregistered)": a shader created by a direct
	// CreatePixelShader call still has an identity, and two of them in one table are
	// distinguishable only by the handle.
	static char unknown[24];
	sprintf(unknown, "(ps %08x)", handle);
	return unknown;
}

// The registry itself, said once. A census that names shaders is only as good as this
// table, and "the shader did not appear" and "the shader is not in the table" look
// identical from the report.
void DX8Wrapper::Debug_Report_Shader_Names()
{
	static bool reported = false;
	if (reported || s_shaderNameCount == 0) return;
	reported = true;
	WWDEBUG_SAY(("SHADER NAME REGISTRY: %d shaders loaded through W3DShaderManager",
		s_shaderNameCount));
	for (int i = 0; i < s_shaderNameCount; ++i) {
		WWDEBUG_SAY(("    %08x  %s", s_shaderNames[i].handle, s_shaderNames[i].name));
	}
}

// ---------------------------------------------------------------------------
// TEXTURE REQUIREMENTS census.
//
// Every 2-D texture creation is recorded as (asked for) -> (got), grouped, and the
// groups where the two differ are called out. The point of it is a before-and-after:
// the same instrument runs over D3DXCreateTexture's own requirements check and over the
// one written above the seam to replace it, and the two tables have to be the same table.
//
// Cube and volume creations are counted but not described, because Describe_Texture_Level
// takes a 2-D texture and asking it about a cube would report a number that is not the
// cube's.
// ---------------------------------------------------------------------------
#define MAX_TEXREQ_GROUPS 64

struct TexReqGroup {
	unsigned	req_w, req_h, req_levels;
	unsigned	got_w, got_h, got_levels;
	WW3DFormat	req_fmt, got_fmt;
	unsigned	count;
};
static TexReqGroup s_texReq[MAX_TEXREQ_GROUPS];
static int s_texReqCount = 0;
static unsigned s_texReqDropped = 0;
static unsigned s_texReqTotal = 0;
static unsigned s_texReqFailed = 0;
static unsigned s_texReqCube = 0;
static unsigned s_texReqVolume = 0;
static unsigned s_texReqZ = 0;

void DX8Wrapper::Debug_Note_Texture_Made_Other(const char * kind)
{
	if (kind == nullptr) return;
	if (kind[0] == 'c') ++s_texReqCube;
	else if (kind[0] == 'v') ++s_texReqVolume;
	else ++s_texReqZ;
}

void DX8Wrapper::Debug_Note_Texture_Made(unsigned req_w, unsigned req_h, WW3DFormat req_fmt,
	unsigned req_levels, GfxTexture * made)
{
	++s_texReqTotal;
	if (made == nullptr) { ++s_texReqFailed; return; }

	// Read the answer back through the seam rather than from the creation call's own
	// arguments: what this wants to know is what the texture IS, and under D3DX that is
	// not what it was asked for.
	WW3DSurfaceDescription desc;
	if (Gfx == nullptr || !Gfx->Describe_Texture_Level(made, 0, desc)) return;
	const unsigned got_levels = Gfx->Get_Texture_Level_Count(made);

	for (int i = 0; i < s_texReqCount; ++i) {
		TexReqGroup& g = s_texReq[i];
		if (g.req_w == req_w && g.req_h == req_h && g.req_fmt == req_fmt &&
			g.req_levels == req_levels && g.got_w == desc.Width && g.got_h == desc.Height &&
			g.got_fmt == desc.Format && g.got_levels == got_levels) {
			++g.count;
			return;
		}
	}
	if (s_texReqCount >= MAX_TEXREQ_GROUPS) { ++s_texReqDropped; return; }
	TexReqGroup& g = s_texReq[s_texReqCount++];
	g.req_w = req_w; g.req_h = req_h; g.req_fmt = req_fmt; g.req_levels = req_levels;
	g.got_w = desc.Width; g.got_h = desc.Height; g.got_fmt = desc.Format;
	g.got_levels = got_levels;
	g.count = 1;
}

void DX8Wrapper::Debug_Report_Texture_Requirements()
{
	// Reported on the same 600-frame beat as everything else so it lands beside them in
	// the log, but the table itself is never cleared -- see the note in the header.
	static unsigned frames = 0;
	if (++frames < 600) return;
	frames = 0;

	unsigned changed = 0;
	for (int i = 0; i < s_texReqCount; ++i) {
		const TexReqGroup& g = s_texReq[i];
		if (g.req_w != g.got_w || g.req_h != g.got_h || g.req_fmt != g.got_fmt ||
			(g.req_levels != 0 && g.req_levels != g.got_levels)) changed += g.count;
	}

	WWDEBUG_SAY(("TEXTURE REQUIREMENTS: %u 2-D textures created (%u failed), %d distinct "
		"(asked for -> got) groups%s; %u textures came back different from what was asked "
		"for. Also %u cube, %u volume, %u depth textures, not described here.",
		s_texReqTotal, s_texReqFailed, s_texReqCount,
		s_texReqDropped ? "  -- TABLE FULL" : "", changed,
		s_texReqCube, s_texReqVolume, s_texReqZ));
	if (s_texReqTotal == 0) {
		WWDEBUG_SAY(("  CONTROL FAILED: no texture reached this census at all, so the "
					 "zero above is not a measurement of anything."));
		return;
	}
	for (int i = 0; i < s_texReqCount; ++i) {
		const TexReqGroup& g = s_texReq[i];
		const bool diff = (g.req_w != g.got_w || g.req_h != g.got_h || g.req_fmt != g.got_fmt ||
			(g.req_levels != 0 && g.req_levels != g.got_levels));
		StringClass req_name(0,true), got_name(0,true);
		Get_WW3D_Format_Name(g.req_fmt, req_name);
		Get_WW3D_Format_Name(g.got_fmt, got_name);
		WWDEBUG_SAY(("    %s %4ux%-4u %-14s mips %-2u  ->  %4ux%-4u %-14s mips %-2u   x%u",
			diff ? "DIFF" : "same", g.req_w, g.req_h, req_name.str(), g.req_levels,
			g.got_w, g.got_h, got_name.str(), g.got_levels, g.count));
	}
}

#define MAX_ALPHA_GROUPS 96

struct AlphaGroup { unsigned func; unsigned ref; unsigned ps; unsigned count; bool on; };
static AlphaGroup s_alphaGroups[MAX_ALPHA_GROUPS];
static int s_alphaGroupCount = 0;
static unsigned s_alphaDropped = 0;
static unsigned s_alphaOn = 0;
static unsigned s_alphaOff = 0;
static unsigned s_fogOn = 0;
static unsigned s_fogOff = 0;
static unsigned s_fogGlobalOn = 0;
static unsigned s_fogUnknown = 0;
static int s_alphaFogFrames = 0;

void DX8Wrapper::Debug_Note_Alpha_Fog_Draw()
{
	// Read out of the tracked state rather than off the device, deliberately: this is a
	// census of what the renderer asked the hardware to do, and the tracked array is
	// where ShaderClass::Apply put it. Whether the device agrees is a different question
	// and already has its own instrument (Debug_Audit_Invalidation).
	// 0x12345678 is Invalidate_Cached_Render_States' "the wrapper claims nothing about
	// this word" sentinel, not a value. Reading it as a boolean makes it true, and this
	// census duly reported fog enabled on 100% of draws the moment the write that kept
	// the word fresh was deleted. A tracked read is not a device read; see
	// the device escape classification.
	const unsigned fogWord = RenderStates[D3DRS_FOGENABLE];
	if (fogWord == 0x12345678) ++s_fogUnknown;
	else if (fogWord) ++s_fogOn;
	else ++s_fogOff;
	if (FogEnable) ++s_fogGlobalOn;

	const bool on = RenderStates[D3DRS_ALPHATESTENABLE] != 0;
	if (on) ++s_alphaOn; else ++s_alphaOff;

	// The draws with the test *off* are grouped by shader too, and not as a courtesy:
	// a shader that appears only in the off table is one whose geometry is cut out by
	// something other than this stage, and a shader that appears in both is one where
	// the cutout is a per-draw property. Either answer changes what has to be ported.
	const unsigned func = on ? RenderStates[D3DRS_ALPHAFUNC] : 0;
	const unsigned ref = on ? RenderStates[D3DRS_ALPHAREF] : 0;
	const unsigned ps = Pixel_Shader;
	for (int i = 0; i < s_alphaGroupCount; ++i) {
		if (s_alphaGroups[i].func == func && s_alphaGroups[i].ref == ref &&
			s_alphaGroups[i].ps == ps && s_alphaGroups[i].on == on) {
			++s_alphaGroups[i].count;
			return;
		}
	}
	if (s_alphaGroupCount >= MAX_ALPHA_GROUPS) { ++s_alphaDropped; return; }
	AlphaGroup& g = s_alphaGroups[s_alphaGroupCount++];
	g.func = func;
	g.ref = ref;
	g.ps = ps;
	g.count = 1;
	g.on = on;
}

void DX8Wrapper::Debug_Report_Alpha_Fog()
{
	// Same 600-frame window as every other census here, so the numbers can be read
	// against each other.
	if (++s_alphaFogFrames < 600) return;
	s_alphaFogFrames = 0;

	const unsigned allDraws = s_alphaOn + s_alphaOff;
	// "asked for", not "set on the device": the three alpha words are tracked but no
	// longer forwarded, so what this counts is the test the shader was told to do.
	WWDEBUG_SAY(("ALPHA TEST CENSUS over 600 frames: %u of %u draws asked for an alpha "
				 "test, in %d (compare, ref, pixel shader) groups%s",
		s_alphaOn, allDraws, s_alphaGroupCount,
		s_alphaDropped ? "  -- TABLE FULL" : ""));
	if (allDraws == 0) {
		WWDEBUG_SAY(("  CONTROL FAILED: no draws reached this census at all, so the "
					 "count above is not a measurement of anything."));
	}
	for (int rank = 0; rank < s_alphaGroupCount; ++rank) {
		int best = -1;
		unsigned bestCount = 0;
		for (int i = 0; i < s_alphaGroupCount; ++i) {
			if (s_alphaGroups[i].count > bestCount) {
				bestCount = s_alphaGroups[i].count;
				best = i;
			}
		}
		if (best < 0) break;
		const AlphaGroup& g = s_alphaGroups[best];
		if (g.on) {
			WWDEBUG_SAY(("    ON   %-20s ref 0x%02x  %-26s x%u",
				Get_DX8_Cmp_Func_Name(g.func), g.ref, Debug_Shader_Name(g.ps), g.count));
		} else {
			WWDEBUG_SAY(("    off  %-31s %-26s x%u",
				"", Debug_Shader_Name(g.ps), g.count));
		}
		s_alphaGroups[best].count = 0;
	}

	// The fog question is only "does it do anything", so this is a count and its
	// control rather than a table. The global flag is reported beside the render state
	// because they can disagree: ShaderClass::Apply gates the state on the flag, so a
	// nonzero global with a zero state would mean the caps check turned it off.
	const unsigned allFog = s_fogOn + s_fogOff + s_fogUnknown;
	WWDEBUG_SAY(("FOG CENSUS over 600 frames: %u of %u draws had D3DRS_FOGENABLE set "
				 "(%u more the wrapper claimed nothing about); DX8Wrapper's global fog "
				 "flag was on for %u of them",
		s_fogOn, allFog, s_fogUnknown, s_fogGlobalOn));

	s_alphaGroupCount = 0;
	s_alphaDropped = 0;
	s_alphaOn = 0;
	s_alphaOff = 0;
	s_fogOn = 0;
	s_fogOff = 0;
	s_fogGlobalOn = 0;
	s_fogUnknown = 0;
}

void DX8Wrapper::Debug_Report_Frame_Timing()
{
	if (s_tickToMs == 0.0) {
		LARGE_INTEGER freq;
		if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0) return;
		s_tickToMs = 1000.0 / (double)freq.QuadPart;
	}

	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	if (s_lastFrameTick.QuadPart != 0 && s_frameCount < FRAME_WINDOW) {
		s_frameMs[s_frameCount++] =
			(double)(now.QuadPart - s_lastFrameTick.QuadPart) * s_tickToMs;
	}
	s_lastFrameTick = now;

	if (s_frameCount < FRAME_WINDOW) return;

	// Copy before sorting: the percentile needs order, the mean needs the samples.
	static double sorted[FRAME_WINDOW];
	double total = 0.0;
	for (int i = 0; i < FRAME_WINDOW; ++i) { sorted[i] = s_frameMs[i]; total += s_frameMs[i]; }
	qsort(sorted, FRAME_WINDOW, sizeof(double), CompareDouble);

	const double mean = total / FRAME_WINDOW;
	const double median = sorted[FRAME_WINDOW / 2];
	const double p95 = sorted[(int)(FRAME_WINDOW * 0.95)];
	const double worst = sorted[FRAME_WINDOW - 1];

	WWDEBUG_SAY(("FRAME TIMING over %d frames (routing mask %u): "
		"mean %.2f ms (%.1f fps) | median %.2f | p95 %.2f | worst %.2f",
		FRAME_WINDOW, (unsigned)m_shaderRoutingMask,
		mean, mean > 0.0 ? 1000.0 / mean : 0.0, median, p95, worst));

	s_frameCount = 0;
}

//-----------------------------------------------------------------------------
// Technique check.
//
// The technique a mesh declares (meshtechnique.h, decided once at registration)
// against what the per-draw routing block works out for itself. Nothing reads the
// declared technique yet; this is what has to be quiet before anything does.
//
// Agreements are counted as well as mismatches, because a check that only counts
// failures cannot tell "the classifier agrees everywhere" from "the classifier
// never ran". The prelit column is counted separately: those are draws the
// classifier calls pre-lit and the old block called a surface, which is the
// defect this is meant to fix rather than a classifier error, so they are
// expected to be non-zero and each one is a mesh that was being shaded wrong.
//-----------------------------------------------------------------------------
namespace {
	struct TechniqueMismatch {
		const char* mesh;
		const char* texture;
		MeshTechnique declared;
		MeshTechnique live;
		const char* site;
		unsigned fvf;
		unsigned count;
	};
	enum { MAX_TECH_MISMATCHES = 48 };
	TechniqueMismatch s_techMismatches[MAX_TECH_MISMATCHES];
	int s_techMismatchCount = 0;
	unsigned s_techMismatchTotal = 0;
	bool s_techMismatchOverflow = false;
	unsigned s_techAgreed = 0;
	unsigned s_techPrelitGain = 0;
	const char* s_techPrelitSamples[8] = { nullptr };
	int s_techPrelitSampleCount = 0;
	int s_techFrames = 0;
}

void DX8Wrapper::Debug_Note_Technique_Agreement(MeshTechnique declared, bool prelitGain)
{
	if (prelitGain) {
		++s_techPrelitGain;
		const char* mesh = s_debugMeshName ? s_debugMeshName : "(non-mesh)";
		for (int i = 0; i < s_techPrelitSampleCount; ++i)
			if (s_techPrelitSamples[i] == mesh) return;
		if (s_techPrelitSampleCount < 8)
			s_techPrelitSamples[s_techPrelitSampleCount++] = mesh;
		return;
	}
	(void)declared;
	++s_techAgreed;
}

void DX8Wrapper::Debug_Note_Technique_Mismatch(
	MeshTechnique declared, MeshTechnique live, TextureBaseClass* tex0, unsigned fvf)
{
	++s_techMismatchTotal;
	const char* mesh = s_debugMeshName ? s_debugMeshName : "(non-mesh)";
	for (int i = 0; i < s_techMismatchCount; ++i) {
		TechniqueMismatch& e = s_techMismatches[i];
		if (e.mesh == mesh && e.declared == declared && e.live == live) {
			++e.count;
			return;
		}
	}
	if (s_techMismatchCount >= MAX_TECH_MISMATCHES) { s_techMismatchOverflow = true; return; }
	TechniqueMismatch& e = s_techMismatches[s_techMismatchCount++];
	e.mesh = mesh;
	e.texture = tex0 ? tex0->Get_Texture_Name().str() : "(no texture)";
	e.declared = declared;
	e.live = live;
	e.fvf = fvf;
	e.site = s_declarationSite;
	e.count = 1;
}

void DX8Wrapper::Debug_Report_Technique_Check()
{
	// Same 600-frame window as the census, for the same reason: the early frames are
	// the loading screen, where silence would mean "nothing drawn" rather than "agreed".
	if (++s_techFrames < 600) return;
	s_techFrames = 0;

	if (s_techAgreed == 0 && s_techMismatchTotal == 0 && s_techPrelitGain == 0) {
		WWDEBUG_SAY(("TECHNIQUE CHECK: no classified draws this window "
					 "(no mesh declared a technique -- the classifier did not run)"));
		return;
	}

	WWDEBUG_SAY(("TECHNIQUE CHECK: %u agreed, %u mismatched, %u newly pre-lit",
		s_techAgreed, s_techMismatchTotal, s_techPrelitGain));
	if (s_techPrelitGain > 0) {
		WWDEBUG_SAY(("  meshes the asset calls pre-lit that the old block shaded as "
					 "surfaces: %s %s %s %s",
			s_techPrelitSampleCount > 0 ? s_techPrelitSamples[0] : "-",
			s_techPrelitSampleCount > 1 ? s_techPrelitSamples[1] : "",
			s_techPrelitSampleCount > 2 ? s_techPrelitSamples[2] : "",
			s_techPrelitSampleCount > 3 ? s_techPrelitSamples[3] : ""));
	}
	for (int i = 0; i < s_techMismatchCount; ++i) {
		const TechniqueMismatch& e = s_techMismatches[i];
		WWDEBUG_SAY(("  declared %-14s live %-14s x%-7u fvf=%08x site=%s mesh=%s tex=%s",
			Mesh_Technique_Name(e.declared), Mesh_Technique_Name(e.live),
			e.count, e.fvf, e.site ? e.site : "(none)", e.mesh, e.texture));
	}
	if (s_techMismatchOverflow)
		WWDEBUG_SAY(("  (mismatch list truncated)"));

	s_techMismatchCount = 0;
	s_techMismatchTotal = 0;
	s_techMismatchOverflow = false;
	s_techAgreed = 0;
	s_techPrelitGain = 0;
	s_techPrelitSampleCount = 0;
}

//-----------------------------------------------------------------------------
// Particle shadow casting, counted at both ends. See the declarations for why both.
//-----------------------------------------------------------------------------
static unsigned s_partShadowFrames = 0;
static unsigned s_partShadowSystemsSeen = 0;
static unsigned s_partShadowSystemsCast = 0;
static unsigned s_partShadowParticles = 0;
static unsigned s_partShadowDrawsParticle = 0;
static unsigned s_partShadowDrawsMesh = 0;

static float s_partShadowSizeSum = 0.0f;
static float s_partShadowAlphaSum = 0.0f;
static unsigned s_partShadowAlphaHist[5] = { 0, 0, 0, 0, 0 };
static unsigned s_partShadowFrameMax = 0;
static unsigned s_partShadowFramesWithAny = 0;

void DX8Wrapper::Debug_Note_Particle_Shadow_Submit(unsigned systemsSeen, unsigned systemsCast,
												   unsigned particles)
{
	s_partShadowSystemsSeen += systemsSeen;
	s_partShadowSystemsCast += systemsCast;
	s_partShadowParticles += particles;
	if (particles > s_partShadowFrameMax) s_partShadowFrameMax = particles;
	if (particles > 0) ++s_partShadowFramesWithAny;
}

// The two numbers that decide whether a submitted sprite is worth anything: how wide it
// is (against a shadow texel) and how opaque (against the dither). A sprite can be
// counted, routed and rasterised and still contribute nothing if either is near zero,
// which is indistinguishable from the outside from the feature not working.
void DX8Wrapper::Debug_Note_Particle_Shadow_Sprite(float size, float alpha)
{
	s_partShadowSizeSum += size;
	s_partShadowAlphaSum += alpha;
	int bucket = (int)(alpha * 5.0f);
	if (bucket < 0) bucket = 0;
	if (bucket > 4) bucket = 4;
	++s_partShadowAlphaHist[bucket];
}

void DX8Wrapper::Debug_Note_Shadow_Caster_Draw(bool particleVariant)
{
	if (particleVariant) ++s_partShadowDrawsParticle;
	else                 ++s_partShadowDrawsMesh;
}

void DX8Wrapper::Debug_Report_Particle_Shadows()
{
	// Same 600-frame window as the census, so the numbers can be read side by side.
	if (++s_partShadowFrames < 600) return;
	s_partShadowFrames = 0;

	// The mesh count is the control. Zero for both means the depth pass never ran and
	// nothing below it can be concluded; a healthy mesh count next to a zero particle
	// count localises the fault to this feature.
	WWDEBUG_SAY(("PARTICLE SHADOWS: %u systems seen, %u cast, %u particles submitted; "
				 "draws reaching the depth shaders: %u sprite, %u mesh (control)",
		s_partShadowSystemsSeen, s_partShadowSystemsCast, s_partShadowParticles,
		s_partShadowDrawsParticle, s_partShadowDrawsMesh));
	if (s_partShadowParticles > 0 && s_partShadowDrawsParticle == 0)
		WWDEBUG_SAY(("  submitted but never routed -- the sprite shaders are missing, or "
					 "the declaration is not reaching Apply_Render_State_Changes"));
	if (s_partShadowParticles > 0) {
		WWDEBUG_SAY(("  sprite size mean %.1f world units, alpha mean %.3f; "
					 "alpha buckets [0-.2 .2-.4 .4-.6 .6-.8 .8-1] = %u %u %u %u %u",
			s_partShadowSizeSum / (float)s_partShadowParticles,
			s_partShadowAlphaSum / (float)s_partShadowParticles,
			s_partShadowAlphaHist[0], s_partShadowAlphaHist[1], s_partShadowAlphaHist[2],
			s_partShadowAlphaHist[3], s_partShadowAlphaHist[4]));
		WWDEBUG_SAY(("  per frame: %u frames had any, busiest frame %u particles",
			s_partShadowFramesWithAny, s_partShadowFrameMax));
	}

	s_partShadowSystemsSeen = 0;
	s_partShadowSystemsCast = 0;
	s_partShadowParticles = 0;
	s_partShadowDrawsParticle = 0;
	s_partShadowDrawsMesh = 0;
	s_partShadowSizeSum = 0.0f;
	s_partShadowAlphaSum = 0.0f;
	for (int i = 0; i < 5; ++i) s_partShadowAlphaHist[i] = 0;
	s_partShadowFrameMax = 0;
	s_partShadowFramesWithAny = 0;
}


void DX8Wrapper::Debug_Report_Routing_Census()
{
	++s_censusFrames;

	// Accumulate over a window rather than reporting per frame: one frame is a
	// snapshot of whatever happened to be on screen, and the early frames are the
	// loading screen, where every count is zero and would read as a finding.
	const int CENSUS_WINDOW = 600;
	if (s_censusFrames < CENSUS_WINDOW)
		return;

	unsigned total = 0;
	for (int c = 0; c < CENSUS_CATS; ++c) total += s_censusDraws[c];
	if (total == 0) {   // nothing drawn yet; keep the window open
		s_censusFrames = 0;
		return;
	}

	// The CPU side of the shadow constants, logged next to the census so the values the
	// shader was handed can be checked against the values it actually received (see the
	// PBR shader's debug mode 15). Same numbers reach both the M3 and the PBR path.
	WWDEBUG_SAY(("SHADOW CONSTANTS: bias=%.6f strength=%.3f texel=%.6f | "
		"normalOffset=%.4f meshBias=%.6f | map=%s",
		m_shadowParams[0], m_shadowParams[1], m_shadowParams[2],
		m_shadowMeshParams[0], m_shadowMeshParams[1],
		m_pShadowMap != nullptr ? "bound" : "NULL"));
	// How much of the frame is actually being allowed above display white. Without this the
	// HDR path cannot be told apart from a tone curve applied to a scene that never exceeds
	// 1.0 -- which looks like a restyle and is one, and is the failure mode to catch. Zero
	// here with HDR on means the gain is reaching no draws and stage 4 did nothing.
	WWDEBUG_SAY(("HDR EMISSIVE: gain=%.2f applied to %u draws over %d frames",
		m_hdrEffectGain, s_censusHdrEmissiveDraws, s_censusFrames));
	s_censusHdrEmissiveDraws = 0;
	WWDEBUG_SAY(("ROUTING CENSUS over %d frames, %u mesh passes:", s_censusFrames, total));
	for (int c = 0; c < CENSUS_CATS; ++c) {
		WWDEBUG_SAY(("  %-30s %8u (%2u%%)  e.g. %s %s %s %s",
			s_censusNames[c], s_censusDraws[c],
			(unsigned)((unsigned __int64)s_censusDraws[c] * 100 / total),
			s_censusSampleCount[c] > 0 ? s_censusSamples[c][0] : "-",
			s_censusSampleCount[c] > 1 ? s_censusSamples[c][1] : "",
			s_censusSampleCount[c] > 2 ? s_censusSamples[c][2] : "",
			s_censusSampleCount[c] > 3 ? s_censusSamples[c][3] : ""));
	}

	for (int c = 0; c < CENSUS_CATS; ++c) {
		s_censusDraws[c] = 0;
		s_censusSampleCount[c] = 0;
	}
	s_censusFrames = 0;
}

void DX8Wrapper::Debug_Check_Mesh_Routing_Split()
{
	// Say so once, so that a silent log is known to mean "no splits" rather than
	// "the watchdog was compiled out".
	static bool announced = false;
	if (!announced && s_meshRouteCount > 0) {
		announced = true;
		WWDEBUG_SAY(("Mesh routing watchdog active (%d meshes in the first traced frame).",
			s_meshRouteCount));
	}
	for (int i = 0; i < s_meshRouteCount; ++i) {
		const unsigned m = s_meshRoutes[i].mask;
		// What the invariant is actually about is depth agreement, not shader identity.
		// unit_vs, unit_prelit_vs and unit_pbr_vs all transform the position by the same
		// CPU-concatenated WorldViewProj, so a mesh spread across unit_ps, the detail
		// variant and PBR computes one depth and cannot z-fight with itself. Only the
		// fixed-function pipeline, which concatenates on the device, disagrees in the low
		// bits. So the split that matters is bit 0 against any of the rest.
		//
		// Reporting every multi-bit mask instead would have made this stage look like a
		// regression: moving a building's additive glow off fixed function and onto
		// unit_ps leaves the mesh on two shaders and one pipeline, which is the fix, and
		// the old test would have called it the same fault under a new name.
		const bool onFixedFunction  = (m & 1u) != 0;
		const bool onProgrammable   = (m & ~1u) != 0;
		if (!(onFixedFunction && onProgrammable)) continue;   // one pipeline: invariant holds
		if (AlreadyReportedSplit(s_meshRoutes[i].name)) continue;
		const unsigned r = s_meshRoutes[i].ffReasons;
		WWDEBUG_SAY(("MESH ROUTING SPLIT: %s drawn by%s%s%s%s in one frame -- its passes "
			"will z-fight. The passes that fell to fixed function did so because:%s%s%s%s%s%s%s%s",
			s_meshRoutes[i].name,
			(m & 1) ? " fixed-function" : "", (m & 2) ? " unit_ps" : "",
			(m & 4) ? " unit_detail_ps" : "", (m & 8) ? " unit_pbr_ps" : "",
			(r & (1u<<1)) ? " sorted-mesh" : "", (r & (1u<<2)) ? " additive-blend" : "",
			(r & (1u<<3)) ? " no-position-or-normal" : "", (r & (1u<<4)) ? " foreign-vertex-shader" : "",
			(r & (1u<<5)) ? " no-texture" : "", (r & (1u<<6)) ? " unreproducible-blend" : "",
			(r & (1u<<7)) ? " unreproducible-detail-combine" : "", (r & (1u<<8)) ? " unreproducible-texgen" : ""));
	}
	s_meshRouteCount = 0;
}
#endif // RTS_DEBUG
DWORD							DX8Wrapper::m_dwRoadVS = 0;
DWORD							DX8Wrapper::m_dwRoadPS = 0;
bool							DX8Wrapper::m_bRoadShaderPass = false;
DWORD							DX8Wrapper::m_dwUiVS = 0;
DWORD							DX8Wrapper::m_dwUiPS = 0;
bool							DX8Wrapper::m_bUiPass = false;
bool							DX8Wrapper::m_uiGreyscale = false;
DWORD							DX8Wrapper::m_dwScreenQuadVS = 0;
DWORD							DX8Wrapper::m_dwMaskVS = 0;
DWORD							DX8Wrapper::m_dwMaskPS = 0;
bool							DX8Wrapper::m_bMaskPass = false;
Vector4							DX8Wrapper::m_maskProj(0.0f, 0.0f, 0.0f, 0.0f);
DWORD							DX8Wrapper::m_dwWaterVS = 0;
DWORD							DX8Wrapper::m_dwWaterPS = 0;
bool							DX8Wrapper::m_bWaterShaderPass = false;
// Defaults chosen so that a water object which never publishes its parameters renders the
// legacy combine and nothing else: no reflection, no glint, no depth ramp, flat surface.
Vector4							DX8Wrapper::m_waterCtl(0.0f, 1.0f, 0.0f, 0.0f);
Vector4							DX8Wrapper::m_waterDepthCtl(0.0f, 1.0f, 0.0f, 0.0f);
Vector4							DX8Wrapper::m_waterShallowTint(1.0f, 1.0f, 1.0f, 1.0f);
Vector4							DX8Wrapper::m_waterDeepTint(1.0f, 1.0f, 1.0f, 1.0f);
Vector4							DX8Wrapper::m_waterReflCtl(0.0f, 0.02f, 0.0f, 0.0f);
Vector4							DX8Wrapper::m_waterSunDir(0.0f, 0.0f, 1.0f, 0.0f);
Vector4							DX8Wrapper::m_waterSunCol(1.0f, 1.0f, 1.0f, 64.0f);
Vector4							DX8Wrapper::m_waterWaveCtl(0.0f, 0.0f, 0.05f, 1.0f);
Vector4							DX8Wrapper::m_waterShroudUV(0.0f, 0.0f, 0.0f, 0.0f);
Vector4							DX8Wrapper::m_waterNoiseUV(1.0f / 16.0f, 0.0f, 0.0f, 0.0f);
Vector4							DX8Wrapper::m_waterBlendCtl(1.0f, 0.0f, 0.0f, 0.0f);
Vector4							DX8Wrapper::m_waterFoamCtl(1.0f, 0.0f, 0.02f, 0.0f);
Vector4							DX8Wrapper::m_waterFoamCol(1.0f, 1.0f, 1.0f, 0.0f);
Vector4							DX8Wrapper::m_waterRefractCtl(0.0f, 0.0f, 1.0f, 0.0f);
Vector4							DX8Wrapper::m_waterAbsorb(0.0f, 0.0f, 0.0f, 0.0f);
GfxTexture*			DX8Wrapper::m_pRefraction = nullptr;
GfxTexture*			DX8Wrapper::m_pWaterShroud = nullptr;
#ifdef RTS_DEBUG
unsigned						DX8Wrapper::s_waterRoutedDraws = 0;
#endif
DWORD							DX8Wrapper::m_dwShadowDepthVS = 0;
DWORD							DX8Wrapper::m_dwShadowDepthPS = 0;
DWORD							DX8Wrapper::m_dwShadowDepthParticleVS = 0;
DWORD							DX8Wrapper::m_dwShadowDepthParticlePS = 0;
GfxTexture*			DX8Wrapper::m_pShadowMap = nullptr;
GfxTexture*			DX8Wrapper::m_pCloudMap = nullptr;
float							DX8Wrapper::m_sunVP[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
float							DX8Wrapper::m_shadowParams[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
float							DX8Wrapper::m_shadowMeshParams[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
bool							DX8Wrapper::m_bForeignDeviceBindings = false;
bool							DX8Wrapper::m_bShadowDepthPass = false;
float							DX8Wrapper::m_hdrEffectGain = 1.0f;
bool							DX8Wrapper::m_bMeshCastsShadow = false;
bool							DX8Wrapper::m_bEffectCastsShadow = false;
bool							DX8Wrapper::m_bMeshHasSolidPass = false;
bool							DX8Wrapper::m_bMeshRendererDraw = false;
MeshTechnique					DX8Wrapper::m_meshTechnique = MESH_TECHNIQUE_UNCLASSIFIED;
#ifdef RTS_DEBUG
const char*						DX8Wrapper::s_declarationSite = nullptr;
DWORD							DX8Wrapper::Debug_Vertex_FVF = 0;
#endif
void DX8Wrapper::Set_Sun_VP(const float* m16)
{
	for (int i = 0; i < 16; ++i) m_sunVP[i] = m16[i];
}
bool							DX8Wrapper::m_bSunCullBoxValid = false;
Vector3							DX8Wrapper::m_sunCullEye(0.0f, 0.0f, 0.0f);
Vector3							DX8Wrapper::m_sunCullRight(1.0f, 0.0f, 0.0f);
Vector3							DX8Wrapper::m_sunCullUp(0.0f, 1.0f, 0.0f);
Vector3							DX8Wrapper::m_sunCullFwd(0.0f, 0.0f, 1.0f);
float							DX8Wrapper::m_sunCullHalfWidth = 0.0f;
float							DX8Wrapper::m_sunCullUpMin = 0.0f;
float							DX8Wrapper::m_sunCullUpMax = 0.0f;
float							DX8Wrapper::m_sunCullNear = 0.0f;
float							DX8Wrapper::m_sunCullFar = 0.0f;

void DX8Wrapper::Set_Sun_Cull_Box(const Vector3 &eye, const Vector3 &right, const Vector3 &up,
								  const Vector3 &fwd, float halfWidth, float upMin, float upMax,
								  float nearDist, float farDist)
{
	if (halfWidth <= 0.0f || upMax <= upMin || farDist <= nearDist) {
		m_bSunCullBoxValid = false;
		return;
	}
	m_sunCullEye = eye;
	m_sunCullRight = right;
	m_sunCullUp = up;
	m_sunCullFwd = fwd;
	m_sunCullHalfWidth = halfWidth;
	m_sunCullUpMin = upMin;
	m_sunCullUpMax = upMax;
	m_sunCullNear = nearDist;
	m_sunCullFar = farDist;
	m_bSunCullBoxValid = true;
}

bool DX8Wrapper::Cull_Sphere_By_Sun(const Vector3 &center, float radius)
{
	if (!m_bSunCullBoxValid) return false;   // no box published: cull nothing

	const Vector3 d = center - m_sunCullEye;
	if (WWMath::Fabs(Vector3::Dot_Product(d, m_sunCullRight)) > m_sunCullHalfWidth + radius)
		return true;
	const float up = Vector3::Dot_Product(d, m_sunCullUp);
	if (up < m_sunCullUpMin - radius || up > m_sunCullUpMax + radius)
		return true;
	const float along = Vector3::Dot_Product(d, m_sunCullFwd);
	return along < m_sunCullNear - radius || along > m_sunCullFar + radius;
}
bool							DX8Wrapper::m_bDepthPrepass = false;
float							DX8Wrapper::m_depthVP[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
GfxTexture*			DX8Wrapper::m_pSceneDepth = nullptr;
GfxTexture*			DX8Wrapper::m_pSceneColor = nullptr;
float							DX8Wrapper::m_ssrParams[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
bool							DX8Wrapper::m_softParticles = false;
float							DX8Wrapper::m_softParticleFade = 0.0f;
void DX8Wrapper::Set_Depth_VP(const float* m16)
{
	for (int i = 0; i < 16; ++i) m_depthVP[i] = m16[i];
}
bool							DX8Wrapper::m_bUnitShaderBound = false;
bool							DX8Wrapper::m_bTerrainShaderPass = false;
bool							DX8Wrapper::m_terrainCloudEnable = false;
bool							DX8Wrapper::m_terrainNoiseEnable = false;
float							DX8Wrapper::m_cloudScrollAX = 0.0f;
float							DX8Wrapper::m_cloudScrollAY = 0.0f;
float							DX8Wrapper::m_cloudScrollBX = 0.0f;
float							DX8Wrapper::m_cloudScrollBY = 0.0f;
float							DX8Wrapper::m_cloudStrength = 0.75f;
// Sized from the atlas the moment a map's terrain texture is built; the defaults stand
// only for the window before that, when no terrain is being drawn anyway.
Vector4							DX8Wrapper::m_terrainAtlasParams(2048.0f, 1024.0f, 1.0f/2048.0f, 1.0f/1024.0f);
Vector4							DX8Wrapper::m_terrainTilingParams(0.0f, 160.0f, 0.0f, 0.0f);
Vector4							DX8Wrapper::m_terrainDetailParams(0.0f, 0.0f, 0.0f, 0.0f);
Vector4							DX8Wrapper::m_terrainSunDir(0.0f, 0.0f, 1.0f, 0.0f);
Vector4							DX8Wrapper::m_terrainColourParams(0.0f, 0.0f, 0.0f, 0.0f);
static DWORD s_dwOriginalPS = 0;  // fixed-function pixel shader to restore after unit draws
// True while a PBR draw's ORM map is still bound on texture stage 1. That bind goes
// straight to the device, so nothing else knows to undo it -- but only a PBR draw can
// set this, and undoing it unconditionally would strip stage 1 from draws that put
// their own texture there.
static bool s_pbrOrmBound = false;
GfxTexture*			DX8Wrapper::m_defaultOrmMap = nullptr;

// Put texture stage 1 back the way the engine expects it after a PBR draw bound its
// ORM map there. That bind went straight to the device, behind the applied-texture
// cache, so re-applying whatever render_state holds (usually nothing) is what returns
// the stage to a known state. Clearing it to NULL unconditionally instead would strip
// stage 1 from draws that legitimately use it -- the fixed-function detail passes do.
void DX8Wrapper::Restore_Stage1_After_Pbr()
{
	if (!s_pbrOrmBound)
		return;
	s_pbrOrmBound = false;
	Set_DX8_Texture(1, render_state.Textures[1] != nullptr
					   ? render_state.Textures[1]->Peek_D3D_Base_Texture()
					   : NULL);
}

// The same story one stage further out: the cloud shadow field is bound to stage 2, the
// shared environment cubemap to stage 4 for a PBR draw, and the two screen-space
// reflection targets to 6 and 7 -- all four straight to the device like the ORM.
// Nothing in the applied-texture cache knows to take them off, so the next
// fixed-function draw inherits textures on stages it never asked for and renders
// through them.
//
// Sorted translucent geometry -- rotor discs, glow cones -- is exactly the kind of
// draw that shows this, because it never routes to the programmable path and so is
// always the one inheriting. Stage 4 had this restore first; 6, 7 and now 2 each
// arrived without it and would have put those draws back in the same hole.
static bool s_pbrExtraStagesBound = false;

void DX8Wrapper::Restore_Pbr_Extra_Stages()
{
	if (!s_pbrExtraStagesBound)
		return;
	s_pbrExtraStagesBound = false;
	const unsigned pbrExtraStages[4] = { 2, 4, 6, 7 };
	for (int i = 0; i < 4; ++i) {
		const unsigned stage = pbrExtraStages[i];
		Set_DX8_Texture(stage, render_state.Textures[stage] != nullptr
							   ? render_state.Textures[stage]->Peek_D3D_Base_Texture()
							   : NULL);
		Set_DX8_Texture_Stage_State(stage, D3DTSS_COLOROP, D3DTOP_DISABLE);
		Set_DX8_Texture_Stage_State(stage, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	}
}

// Same story one stage along: the shadow map is bound to stage 5 for the lit passes that
// sample it, straight to the device. Nothing else knows to undo it, so a later
// fixed-function draw inherits a texture on a stage it never asked for, and renders
// through it. Helicopter rotor discs are drawn that way and disappeared entirely.
static bool s_shadowStage5Bound = false;

void DX8Wrapper::Restore_Stage5_After_Shadow()
{
	if (!s_shadowStage5Bound)
		return;
	s_shadowStage5Bound = false;
	Set_DX8_Texture(5, render_state.Textures[5] != nullptr
					   ? render_state.Textures[5]->Peek_D3D_Base_Texture()
					   : NULL);
	Set_DX8_Texture_Stage_State(5, D3DTSS_COLOROP, D3DTOP_DISABLE);
	Set_DX8_Texture_Stage_State(5, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}

/*
** Guarantee the fixed-function pipeline for a caller that is about to draw on the device
** itself, rather than through Draw().
**
** Apply_Render_State_Changes binds a vertex and pixel shader for the draw *it* is
** describing -- the one that will come back through Draw() with the render state it just
** applied. A caller that calls it to flush transforms and then issues its own
** DrawPrimitive is not that draw, and inherits whatever was bound for the previous one.
**
** Setting an FVF is not enough, and that is the trap. SetVertexShader(fvf) replaces the
** vertex shader, so the geometry transforms correctly and everything looks handled -- but
** nothing in D3D9 unbinds the *pixel* shader, so fixed-function vertex output is fed to a
** shader that declares inputs the FVF never supplied. The projected-shadow decals are
** what found this: mines, radius cursors and special-power targeting reticles all draw
** through W3DProjectedShadowManager::flushDecals at FVF 0x142 (position, diffuse, one
** texture coordinate set) into unit_ps, which reads COLOR0 and TEXCOORD0-3. Measured, six
** of eight sampled decal draws had a live pixel shader bound. They rendered as nothing.
**
** Callers in this position should declare MESH_TECHNIQUE_FIXED_FUNCTION *and* call this.
** The declaration stops the routing choosing a shader; this undoes one already chosen,
** including in the case the declaration cannot reach -- Apply_Render_State_Changes
** returns early when no render state changed, so a shader bound for an earlier draw is
** still bound and no branch of the routing runs to take it down.
*/
void DX8Wrapper::Force_Fixed_Function_Pipeline()
{
	Restore_Stage1_After_Pbr();
	Restore_Stage5_After_Shadow();
	Restore_Pbr_Extra_Stages();
	Set_Pixel_Shader(s_dwOriginalPS);
	m_bUnitShaderBound = false;
}

/*
** Put the interface shader pair on a draw the routing block will never see, with a
** transform the caller supplies.
**
** ui_vs is a passthrough with a matrix in front of it and ui_ps is a stage-0 combine of
** texture against vertex diffuse -- between them the whole of what the remaining
** direct-device drawers were asking the fixed-function pipeline for. What separates one
** caller from another is only the matrix (screen space for the quads, world-view-proj for
** geometry) and whether the combine samples the texture, so those are the arguments and
** everything else is shared.
**
** Returns false if the shaders are unavailable, in which case the caller must keep its
** fixed-function path -- so drawers can be moved across one at a time.
*/
bool DX8Wrapper::Bind_Ui_Shader_Direct(const float * wvp, bool sampleColour, bool sampleAlpha)
{
	if (m_dwUiVS == 0 || m_dwUiPS == 0) return false;
	if (wvp == nullptr) return false;

	Set_Vertex_Shader(m_dwUiVS);
	Set_Pixel_Shader(m_dwUiPS);
	Set_Vertex_Shader_Constant(0, wvp, 4);

	// (samples colour, desaturate, samples alpha, unused). Never desaturating: that path
	// exists for disabled interface buttons, and none of these callers is one.
	const D3DXVECTOR4 uiCtl(sampleColour ? 1.0f : 0.0f, 0.0f, sampleAlpha ? 1.0f : 0.0f, 0.0f);
	Set_Pixel_Shader_Constant(0, &uiCtl, 1);

	// Say what the fixed-function baseline is, or the next caller will get this shader
	// back when it asks for fixed function. Force_Fixed_Function_Pipeline restores
	// s_dwOriginalPS rather than binding nothing, and s_dwOriginalPS is captured from
	// whatever was bound the first time the routing block claimed a draw -- so a shader
	// bound *here*, outside that block, would be captured as the thing to go back to.
	//
	// Measured: leaving it alone put the interface pixel shader on 45% of the shadow
	// decals, which are declared fixed function precisely because the routing cannot see
	// them. Zero is the truthful answer -- fixed function is what preceded this draw and
	// what should follow it.
	s_dwOriginalPS = 0;
	m_bUnitShaderBound = true;
	return true;
}

/*
** The world-space form: the caller supplies its world matrix and the view and projection
** are taken from the device, which is where its own draw would have read them.
**
** Reading them back rather than tracking them is deliberate. These callers reach the device
** through Apply_Render_State_Changes, which is what puts the current view and projection
** there; asking the device gives the matrices that draw would actually have used, including
** in the cases where a pass set up a projection of its own. The alternative -- a cached copy
** in the wrapper -- is a second account of the same fact, and the first thing it would do is
** disagree with the device for the one pass nobody remembered to update.
*/
bool DX8Wrapper::Bind_Ui_Shader_World(const float * world, bool sampleColour, bool sampleAlpha)
{
	if (m_dwUiVS == 0 || m_dwUiPS == 0) return false;
	if (world == nullptr || Gfx == nullptr) return false;

	D3DXMATRIX view, proj;
	if (!Gfx->Get_Transform(D3DTS_VIEW, reinterpret_cast<float*>(&view)) ||
		!Gfx->Get_Transform(D3DTS_PROJECTION, reinterpret_cast<float*>(&proj)))
		return false;

	// Row-vector order, matching both D3D9's fixed-function transform (position * W * V * P)
	// and the shader's mul(float4(position, 1), WorldViewProj). Getting this backwards does
	// not draw the geometry wrong, it draws nothing, which is worth knowing before going
	// looking for a blend state.
	D3DXMATRIX wvp;
	D3DXMatrixMultiply(&wvp, reinterpret_cast<const D3DXMATRIX*>(world), &view);
	D3DXMatrixMultiply(&wvp, &wvp, &proj);

	return Bind_Ui_Shader_Direct(&wvp._11, sampleColour, sampleAlpha);
}

/*
** The pixels-to-clip mapping D3DFVF_XYZRHW used to imply, built from the viewport.
**
** The viewport and not the back buffer: these quads are sized against a view that need not
** fill the window, and the mapping has to agree with whatever the rasteriser is clipping to.
** XYZRHW was defined against the viewport too, so this reproduces it rather than
** approximating it.
**
** Returns false if there is no usable viewport, in which case the caller must not draw.
*/
bool DX8Wrapper::Build_Pixels_To_Clip(float * out)
{
	if (out == nullptr) return false;

	GfxViewport vp;
	if (!Gfx->Get_Viewport(vp) || vp.Width == 0 || vp.Height == 0)
		return false;

	// x: [0,W] -> [-1,1]. y: [0,H] -> [1,-1], because screen y grows downward and clip y
	// grows up. Row-major, to match the shaders' row_major float4x4 and the mul() order in
	// them -- the translation is in the last row, which is the row-vector convention D3D9's
	// own fixed-function transform used.
	const float sx =  2.0f / (float)vp.Width;
	const float sy = -2.0f / (float)vp.Height;
	const float m[16] = {   sx, 0.0f, 0.0f, 0.0f,
						  0.0f,   sy, 0.0f, 0.0f,
						  0.0f, 0.0f, 1.0f, 0.0f,
						 -1.0f, 1.0f, 0.0f, 1.0f };
	::memcpy(out, m, sizeof(m));

#ifdef RTS_DEBUG
	// The matrix is the whole of what replaced D3DFVF_XYZRHW, and a sign error in it puts
	// the quad upside down or off screen -- which on a post-process pass reads as "the
	// effect stopped working", not as "the matrix is wrong". So state the mapping rather
	// than going looking for it in a screenshot: the viewport's own corners, run through the
	// matrix the shader will use, must come out as the clip-space corners.
	//
	// Reported per distinct viewport rather than once, because "once" answered the wrong
	// question: the first call came while the 4096x4096 shadow map was still the target, and
	// one sample cannot tell a wrong basis from an unusual first frame.
	{
		static unsigned seen[4] = { 0, 0, 0, 0 };
		static int seenCount = 0;
		const unsigned key = (vp.Width << 16) | vp.Height;
		bool isNew = true;
		for (int i = 0; i < seenCount; ++i) if (seen[i] == key) { isNew = false; break; }
		if (isNew && seenCount < 4) {
			seen[seenCount++] = key;
			D3DXVECTOR4 tl, br;
			D3DXVECTOR4 tlIn(0.0f, 0.0f, 0.0f, 1.0f);
			D3DXVECTOR4 brIn((float)vp.Width, (float)vp.Height, 0.0f, 1.0f);
			const D3DXMATRIX * const mm = reinterpret_cast<const D3DXMATRIX*>(out);
			D3DXVec4Transform(&tl, &tlIn, mm);
			D3DXVec4Transform(&br, &brIn, mm);
			WWDEBUG_SAY(("SCREEN-SPACE SHADER: viewport %ux%u -> top-left (%.3f, %.3f) "
						 "bottom-right (%.3f, %.3f)  [expect (-1, 1) and (1, -1)]",
				vp.Width, vp.Height, tl.x, tl.y, br.x, br.y));
		}
	}
#endif
	return true;
}

/*
** The vertex half only: the post-process callers bring their own pixel shader.
**
** Does not touch s_dwOriginalPS or m_bUnitShaderBound, unlike the ui binds below. Those
** exist to tell the routing block what "back to fixed function" means for a draw it can
** see; a post-process quad is drawn straight at the device between passes, and claiming
** the fixed-function baseline had changed would be a lie about a pipeline this never
** entered.
*/
bool DX8Wrapper::Bind_Screen_Quad_Shader()
{
	if (m_dwScreenQuadVS == 0) return false;

	float m[16];
	if (!Build_Pixels_To_Clip(m)) return false;

	Set_Vertex_Shader(m_dwScreenQuadVS);
	Set_Vertex_Shader_Constant(0, m, 4);
	return true;
}

bool DX8Wrapper::Bind_Screen_Space_Shader(bool sampleColour, bool sampleAlpha)
{
	if (m_dwUiVS == 0 || m_dwUiPS == 0) return false;

	float m[16];
	if (!Build_Pixels_To_Clip(m)) return false;

	return Bind_Ui_Shader_Direct(m, sampleColour, sampleAlpha);
}

bool								_DX8SingleThreaded										= false;

INT g_D3D9_BaseVertexIndex = 0;

static DynamicVectorClass<StringClass>					_RenderDeviceNameTable;
static DynamicVectorClass<StringClass>					_RenderDeviceShortNameTable;
static DynamicVectorClass<RenderDeviceDescClass>	_RenderDeviceDescriptionTable;



DX8_CleanupHook	 *DX8Wrapper::m_pCleanupHook=nullptr;
/***********************************************************************************
**
** DX8Wrapper Implementation
**
***********************************************************************************/

void Log_DX8_ErrorCode(unsigned res)
{
	WWDEBUG_SAY(("DX8 Error: %s: %s", DXGetErrorStringA(res), DXGetErrorDescriptionA(res)));

	WWASSERT(0);
}

void Non_Fatal_Log_DX8_ErrorCode(unsigned res,const char * file,int line)
{
	WWDEBUG_SAY(("DX8 Error: %s: %s, File: %s, Line: %d", DXGetErrorStringA(res), DXGetErrorDescriptionA(res), file, line));
}

// TheSuperHackers @info helmutbuhler 14/04/2025
// Helper function that moves x and y such that the inner rect fits into the outer rect.
// If the inner rect already is in the outer rect, then this does nothing.
// If the inner rect is larger than the outer rect, then the inner rect will be aligned to the top left of the outer rect.
void MoveRectIntoOtherRect(const RECT& inner, const RECT& outer, int* x, int* y)
{
	int dx = 0;
	if (inner.right > outer.right)
		dx = outer.right-inner.right;
	if (inner.left < outer.left)
		dx = outer.left-inner.left;

	int dy = 0;
	if (inner.bottom > outer.bottom)
		dy = outer.bottom-inner.bottom;
	if (inner.top < outer.top)
		dy = outer.top-inner.top;

	*x += dx;
	*y += dy;
}


bool DX8Wrapper::Init(void * hwnd, bool lite)
{
	WWASSERT(!IsInitted);

	// zero memory
	memset(Textures,0,sizeof(GfxTexture*)*MAX_TEXTURE_STAGES);
	memset(RenderStates,0,sizeof(unsigned)*256);
	memset(TextureStageStates,0,sizeof(unsigned)*32*MAX_TEXTURE_STAGES);
	memset(Vertex_Shader_Constants,0,sizeof(Vector4)*MAX_VERTEX_SHADER_CONSTANTS);
	memset(Pixel_Shader_Constants,0,sizeof(Vector4)*MAX_PIXEL_SHADER_CONSTANTS);
	memset(&render_state,0,sizeof(RenderStateStruct));
	memset(Shadow_Map,0,sizeof(ZTextureClass*)*MAX_SHADOW_MAPS);

	/*
	** Initialize all variables!
	*/
	_Hwnd = (HWND)hwnd;
	_MainThreadID=ThreadClass::_Get_Current_Thread_ID();
	WWDEBUG_SAY(("DX8Wrapper main thread: 0x%x",_MainThreadID));
	CurRenderDevice = -1;
	ResolutionWidth = DEFAULT_RESOLUTION_WIDTH;
	ResolutionHeight = DEFAULT_RESOLUTION_HEIGHT;
	// Initialize Render2DClass Screen Resolution
	Render2DClass::Set_Screen_Resolution( RectClass( 0, 0, ResolutionWidth, ResolutionHeight ) );
	BitDepth = DEFAULT_BIT_DEPTH;
	IsWindowed = false;
	DX8Wrapper_IsWindowed = false;


	//old_vertex_shader; TODO
	//old_sr_shader;
	//current_shader;

	//world_identity;
	//CurrentFogColor;

	Adapter = nullptr;

	WWDEBUG_SAY(("Reset DX8Wrapper statistics"));
	Reset_Statistics();

	// There is no device yet, so nothing the wrapper might remember about one can be
	// true. This is the base case the sentinel exists for.
	Invalidate_Cached_Render_States("DX8Wrapper::Init");

	if (!lite) {
		/*
		** Open the adapter. This is the one place a concrete backend is chosen on the way
		** in, as Create_Device is on the way out; loading the API's library and asking it
		** for an interface both happen behind it.
		*/
		WWDEBUG_SAY(("Create graphics adapter"));
		Adapter = Gfx_Create_Adapter();
		if (Adapter == nullptr) {
			return(false);
		}
		IsInitted = true;

		/*
		** Enumerate the available devices
		*/
		WWDEBUG_SAY(("Enumerate devices"));
		Enumerate_Devices();
		WWDEBUG_SAY(("DX8Wrapper Init completed"));
	}

	return(true);
}

void DX8Wrapper::Shutdown()
{
	if (Gfx) {

		Set_Render_Target ((GfxSurface *)nullptr);
		Release_Device();
	}

	if (Adapter) {
		delete Adapter;
		Adapter=nullptr;
	}

	if (CurrentCaps)
	{
		int max=CurrentCaps->Get_Max_Textures_Per_Pass();
		for (int i = 0; i < max; i++)
		{
			if (Textures[i])
			{
				Gfx->Release_Texture(Textures[i]);
				Textures[i] = nullptr;
			}
		}
	}

	_RenderDeviceNameTable.Clear();		 // note - Delete_All() resizes the vector, causing a reallocation.  Clear is better. jba.
	_RenderDeviceShortNameTable.Clear();
	_RenderDeviceDescriptionTable.Clear();

	DX8Caps::Shutdown();
	IsInitted = false;		// 010803 srj
}

void DX8Wrapper::Do_Onetime_Device_Dependent_Inits()
{
	/*
	** Set Global render states (some of which depend on caps)
	*/
	Compute_Caps(DisplayFormat);

   /*
	** Initialize any other subsystems inside of WW3D
	*/
	MissingTexture::_Init();
	TextureFilterClass::_Init_Filters(
		(TextureFilterClass::TextureFilterMode)WW3D::Get_Texture_Filter(),
		(TextureFilterClass::AnisotropicFilterMode)WW3D::Get_Anisotropy_Level()
	);
	TheDX8MeshRenderer.Init();
	SHD_INIT;
	BoxRenderObjClass::Init();
	VertexMaterialClass::Init();
	PointGroupClass::_Init(); // This needs the VertexMaterialClass to be initted
	ShatterSystem::Init();
	TextureLoader::Init();

	Set_Default_Global_Render_States();
}

inline DWORD F2DW(float f) { return *((unsigned*)&f); }
void DX8Wrapper::Set_Default_Global_Render_States()
{
	DX8_THREAD_ASSERT();
	// Fog off for the life of the device, and nothing else about fog said at all.
	//
	// D3DRS_RANGEFOGENABLE, FOGTABLEMODE and FOGVERTEXMODE were written here too, the
	// first of them out of D3DCAPS8::RasterCaps. All three only mean anything while fog
	// is enabled, and it never is: Phase 2 deleted fog, this line is the only thing that
	// writes D3DRS_FOGENABLE on the render path, and the fog census reads 0 of 1070690
	// draws with it set. So they configured a feature nothing can reach -- and the
	// RasterCaps read was the last thing outside dx8caps.cpp that wanted the raw
	// D3DCAPS8 struct for anything but a texture limit.
	//
	// ShaderClass::Apply used to write this on every shader change, and that is what
	// re-established it after Invalidate_Cached_Render_States poisoned the tracked word:
	// the poison does not compare equal to FALSE, so the write reached the device. That
	// block is gone -- fog is dead, see the note there -- and without this line the
	// device's fog state would rest on the D3D9 default rather than on anything the
	// engine said. The default is FALSE, so nothing moved; the point is that it rests on
	// something the engine said, and keeps resting on it across a reset.
	//
	// Here rather than in the invalidation, which was the first attempt: this function
	// runs once per device with the device up, whereas the invalidation is reached from
	// DX8Wrapper::Init before the device exists (where the same write null-dereferenced)
	// and otherwise only from Reset_Device. Note DX8Wrapper::Apply_Default_State, which
	// looks like it does this job, has no callers at all.
	Set_DX8_Render_State(D3DRS_FOGENABLE, FALSE);
	// ...and the hardware alpha test off for the life of the device, never to be turned
	// back on. Every shader that can receive an alpha-tested draw now does the test
	// itself from AlphaTestCtl, and Set_DX8_Render_State no longer forwards the three
	// alpha words to the device at all -- which is exactly why this one is sent straight
	// at it. D3D9's own default for this state is already FALSE, so nothing moves; the
	// point is that the device's test rests on something the engine said, and keeps
	// resting on it across a reset, which also comes through here.
	GFXCALL(Set_Render_State(D3DRS_ALPHATESTENABLE, FALSE));
	Set_DX8_Render_State(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);
	Set_DX8_Render_State(D3DRS_COLORVERTEX, TRUE);
	Set_DX8_Render_State(D3DRS_ZBIAS,0);
	Set_DX8_Texture_Stage_State(1, D3DTSS_BUMPENVLSCALE, F2DW(1.0f));
	Set_DX8_Texture_Stage_State(1, D3DTSS_BUMPENVLOFFSET, F2DW(0.0f));
	Set_DX8_Texture_Stage_State(0, D3DTSS_BUMPENVMAT00,F2DW(1.0f));
	Set_DX8_Texture_Stage_State(0, D3DTSS_BUMPENVMAT01,F2DW(0.0f));
	Set_DX8_Texture_Stage_State(0, D3DTSS_BUMPENVMAT10,F2DW(0.0f));
	Set_DX8_Texture_Stage_State(0, D3DTSS_BUMPENVMAT11,F2DW(1.0f));

//	Set_DX8_Render_State(D3DRS_CULLMODE, D3DCULL_CW);
	// Set dither mode here?
}

void DX8Wrapper::Invalidate_Cached_Shader()
{
	// Two caches, one statement. ShaderClass::Apply compares the shader it is asked for
	// against CurrentShader and writes nothing when they match; DX8Wrapper::Set_Shader
	// does not even call Apply when the value matches render_state.shader. A caller that
	// wrote a shader-owned render state by hand has to defeat both, or the next draw
	// asking for the shader it already had gets the hand-written states instead.
	ShaderClass::Invalidate();
	render_state_changed |= (unsigned)SHADER_CHANGED;
}

void DX8Wrapper::Invalidate_Cached_Render_States(const char * site)
{
#ifdef RTS_DEBUG
	// Before anything is poisoned: was there anything here to invalidate?
	Debug_Audit_Invalidation(site);
#else
	(void)site;
#endif
	// Everything, not nothing. This line used to read render_state_changed=0, which says
	// the next draw needs no shader bound, no texture applied, no material and no
	// transform -- on the strength of a record that was about to be thrown away. It is the
	// exact opposite of what has just happened: the device is about to be declared unknown,
	// so every sub-apply has to run again.
	//
	// The two identity bits are not change flags. They say the world/view *is* the identity
	// matrix, which is how the routing block tells a 2D draw from a 3D one, and
	// Apply_Render_State_Changes deliberately preserves them across a draw for that reason.
	// Clearing them told the routing block that the menu it was about to draw was a mesh.
	render_state_changed =
		(render_state_changed & ((unsigned)WORLD_IDENTITY | (unsigned)VIEW_IDENTITY)) |
		(unsigned)WORLD_CHANGED | (unsigned)VIEW_CHANGED |
		(unsigned)LIGHTS_CHANGED | (unsigned)TEXTURES_CHANGED |
		(unsigned)MATERIAL_CHANGED | (unsigned)SHADER_CHANGED |
		(unsigned)VERTEX_BUFFER_CHANGED | (unsigned)INDEX_BUFFER_CHANGED |
		(unsigned)TEXGEN_STATE_CHANGED;

	int a;
	for (a=0;a<sizeof(RenderStates)/sizeof(unsigned);++a) {
		// Same as the stage states below: a deferred word keeps its value, and it is the
		// shadow of what the *device* holds that is poisoned instead. That is the one this
		// invalidation is actually about -- somebody wrote the device behind the wrapper's
		// back -- while the tracked value is still a true statement of what the caller
		// asked for, and is read as such by the routing block.
		if (Is_Deferred_FF_Render_State((unsigned)a)) {
			FFRenderPending[a >> 5] |= (1u << (a & 31));
			FFDeviceRender[a] = 0x12345678;
			FFStatePending = true;
			continue;
		}
		RenderStates[a]=0x12345678;
	}
	// The device has been reset or taken over, so it no longer holds the material either.
	// Power is not a colour and is never negative, so this cannot match a real material
	// and the next flush is guaranteed to resend.
	for (a=0;a<MAX_TEXTURE_STAGES;++a)
	{
		for (int b=0; b<32;b++)
		{
			// Deferred fixed-function words keep their value; the device shadow is
			// poisoned in their place. The sentinel's job is to force the next write
			// through, and doing that to the shadow achieves it without destroying the
			// value -- which matters because the routing predicate reads these same
			// entries to decide what a draw wants, and 0x12345678 is not a texture op.
			// That used to be repaired afterwards by asking the device what it held; it
			// no longer holds them, so the repair has to be not breaking them at all.
			if (Is_Deferred_FF_Stage_State((unsigned)b)) {
				FFStagePending[a] |= (1u << b);
				FFDeviceStage[a][b] = 0x12345678;
				FFStatePending = true;
				continue;
			}
			TextureStageStates[a][b]=0x12345678;
		}
		// The sampler description is intent rather than a claim about the device, so it
		// cannot hold the poison above. Unknown() is its equivalent: nothing a caller builds
		// compares equal to it, so the next description bound is re-sent field by field.
		Samplers[a] = SamplerStateClass::Unknown();
		//Need to explicitly set texture to null, otherwise app will not be able to
		//set it to null because of redundant state checker. MW
		if (Gfx)
			Gfx->Set_Texture(a,nullptr);
		if (Textures[a] != nullptr) {
			Gfx->Release_Texture(Textures[a]);
		}
		Textures[a]=nullptr;
	}

	ShaderClass::Invalidate();

	//Need to explicitly set render_state texture pointers to null. MW
	Release_Render_State();

	// (gth) clear the matrix shadows too -- to identity, not to zero, which matters now
	// that they are read in place of the device. This runs on a device reset, and a reset
	// puts D3D's own transform state back to identity; zeroing the shadows would have made
	// a later read return a zero matrix where asking the device would have returned
	// identity. The world and view are re-applied from render_state regardless (see below),
	// but nothing re-applies the projection or a texture matrix.
	//
	// Unverifiable by the replay harness, which creates one device and never resets it.
	for (unsigned t = 0; t < D3DTS_WORLD+1; ++t) {
		memset(&DX8Transforms[t], 0, sizeof(D3DMATRIX));
		DX8Transforms[t].m[0][0] = 1.0f;
		DX8Transforms[t].m[1][1] = 1.0f;
		DX8Transforms[t].m[2][2] = 1.0f;
		DX8Transforms[t].m[3][3] = 1.0f;
	}

	// And the wrapper stops believing it has sent the device any of them, so the next
	// fixed-function draw resends whatever it needs rather than matching against a value a
	// reset has already thrown away. Same role as poisoning FFDeviceRender.
	FFDeviceTransformValid = 0;

	// Poison the shader-constant shadow caches so the next Set_*_Shader_Constant
	// always writes through. Set_Vertex/Pixel_Shader_Constant skip the device
	// write when the value matches its cache; a device reset zeroes the device
	// constants but leaves these caches intact, so a constant-valued register
	// (e.g. an overlay-enable flag) would stay stuck at the reset value. Filling
	// the caches with a sentinel that no real value matches forces a resend,
	// mirroring the 0x12345678 render-state sentinel above.
	memset(Vertex_Shader_Constants, 0xFF, sizeof(Vertex_Shader_Constants));
	memset(Pixel_Shader_Constants, 0xFF, sizeof(Pixel_Shader_Constants));

	// The transform shadows were just zeroed, but render_state still holds the correct
	// world/view, and WORLD_CHANGED|VIEW_CHANGED are already up from the head of the
	// function. The fixed-function terrain used to re-touch these every pass; the
	// programmable terrain shader instead carries the view in its WVP constant and never
	// re-applies D3DTS_VIEW, so without them a later fixed-function pass that reads the
	// cached view -- the shroud builds its projection from inverse(D3DTS_VIEW) -- would
	// read a zero matrix and swim with the camera.

	// SHADER_CHANGED is up from the head of the function for the same reason, and
	// ShaderClass::Invalidate above defeats the second of the two shader caches.
	// RenderStates was just filled with the sentinel, and ShaderClass::Apply only runs
	// when the shader actually changes -- so without both, a run of draws sharing one
	// shader would read blend, z and alpha-test values that are sentinels rather than
	// what the device holds. That is not merely a redundant-write question: the routing
	// predicate reads those same entries to decide whether a draw can be reproduced, so
	// it would decline draws over a blend mode they do not have.
}

void DX8Wrapper::Do_Onetime_Device_Dependent_Shutdowns()
{
	/*
	** Shutdown ww3d systems
	*/
	int i;
	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		if (render_state.vertex_buffers[i]) render_state.vertex_buffers[i]->Release_Engine_Ref();
		REF_PTR_RELEASE(render_state.vertex_buffers[i]);
	}
	if (render_state.index_buffer) render_state.index_buffer->Release_Engine_Ref();
	REF_PTR_RELEASE(render_state.index_buffer);
	REF_PTR_RELEASE(render_state.material);
	for (i=0;i<CurrentCaps->Get_Max_Textures_Per_Pass();++i) REF_PTR_RELEASE(render_state.Textures[i]);


	TextureLoader::Deinit();
	SortingRendererClass::Deinit();
	DynamicVBAccessClass::_Deinit();
	DynamicIBAccessClass::_Deinit();
	ShatterSystem::Shutdown();
	PointGroupClass::_Shutdown();
	VertexMaterialClass::Shutdown();
	BoxRenderObjClass::Shutdown();
	SHD_SHUTDOWN;
	TheDX8MeshRenderer.Shutdown();
	MissingTexture::_Deinit();

	delete CurrentCaps;
	CurrentCaps=nullptr;

}


bool DX8Wrapper::Create_Device()
{
	WWASSERT(Gfx==nullptr);	// for now, once you've created a device, you're stuck with it!
	if (Adapter == nullptr) return false;

	/*
	** Make the device and its swap chain. Everything the API forces a choice about at this
	** point -- how vertices are processed, how the FPU is left, what to do when the depth
	** format the adapter claimed turns out not to work with the back buffer -- is the
	** backend's business, and none of it appears here any more. SwapChain says what the
	** engine wants; the backend may hand back a description with the depth format dropped,
	** which is why it goes in by reference.
	*/
	delete Gfx;
	Gfx = Adapter->Create_Device(Get_Adapter_Index(), SwapChain);
	if (Gfx == nullptr) {
		return false;
	}

	/*
	** Initialize all subsystems
	*/
	Do_Onetime_Device_Dependent_Inits();
	return true;
}

bool DX8Wrapper::Reset_Device(bool reload_assets)
{
	WWDEBUG_SAY(("Resetting device."));
	DX8_THREAD_ASSERT();
	if ((IsInitted) && (Gfx != nullptr)) {
		// A fullscreen-exclusive Reset needs the window to be the active foreground
		// window; calling it while the app is backgrounded or mid focus-transition
		// (common on startup -- "it starts if I don't touch the window") can block the
		// driver indefinitely. Pump pending messages so a focus change settles, and if
		// the window still isn't foreground, defer so the caller retries next frame.
		if (!SwapChain.Windowed && _Hwnd != nullptr) {
			MSG msg;
			while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
				::TranslateMessage(&msg);
				::DispatchMessage(&msg);
			}
			if (::IsIconic(_Hwnd) || ::GetForegroundWindow() != _Hwnd) {
				WWDEBUG_SAY(("Reset_Device: window not foreground/minimised; deferring reset."));
				return false;
			}
		}
		// Take everything off the device first. A resource that is still bound stays
		// alive however many times the app releases it -- the driver holds its own
		// reference on whatever is bound -- and one live D3DPOOL_DEFAULT resource is all
		// it takes for Reset() to return D3DERR_INVALIDCALL. It then does so on every
		// retry, because nothing here ever unbinds, so the device never comes back and
		// the frame loop runs on with the terrain and shaders already released.
		//
		// Release_Device has always unbound before releasing; Reset_Device never did.
		// The releases below (Set_Vertex_Buffer, the cleanup hook, _Deinit) only drop
		// *our* pointers, which is not the same thing.
		Set_Render_Target((GfxSurface *)nullptr);	// back to the back buffer
		for (unsigned stage=0;stage<MAX_TEXTURE_STAGES;++stage)
		{
			GFXCALL(Set_Texture(stage,nullptr));
			if (Textures[stage]) {
				Gfx->Release_Texture(Textures[stage]);
				Textures[stage] = nullptr;
			}
		}
		for (unsigned stream=0;stream<MAX_VERTEX_STREAMS;++stream)
		{
			GFXCALL(Set_Vertex_Stream(stream, nullptr, 0));
		}
		GFXCALL(Set_Index_Buffer(nullptr,0));

		// The wrapper's own render state holds ref-counted TextureClass / vertex buffer /
		// material pointers of its own, separate from the raw bindings above, and they
		// outlive whatever the owning subsystem does in its ReleaseResources. The smudge
		// manager's scene copy is the one that bites: it is a D3DPOOL_DEFAULT render
		// target bound with Set_Texture, so the render state kept it alive on its own and
		// no amount of releasing elsewhere could get the device down to zero.
		Release_Render_State();

		// Release all non-MANAGED stuff
		WW3D::_Invalidate_Textures();

		for (unsigned i=0;i<MAX_VERTEX_STREAMS;++i)
		{
			Set_Vertex_Buffer (nullptr,i);
		}
		Set_Index_Buffer (nullptr, 0);
		if (m_pCleanupHook) {
			m_pCleanupHook->ReleaseResources();
		}
		DynamicVBAccessClass::_Deinit();
		DynamicIBAccessClass::_Deinit();
		DX8TextureManagerClass::Release_Textures();
		SHD_SHUTDOWN_SHADERS;

#if defined(RTS_DEBUG)
		// Query objects are one more thing that can hold Reset() at D3DERR_INVALIDCALL, and
		// this codebase has already lost a session to that failure mode. The pool rebuilds
		// itself on the next Begin_Frame, so nothing reacquires it below.
		GpuTimer::Release();
#endif

		// Reset frame count to reflect the flipping chain being reset by Reset()
		FrameCount = 0;

		memset(Vertex_Shader_Constants,0,sizeof(Vector4)*MAX_VERTEX_SHADER_CONSTANTS);
		memset(Pixel_Shader_Constants,0,sizeof(Vector4)*MAX_PIXEL_SHADER_CONSTANTS);

		// The device is put back through the backend, which owns the API's own creation
		// parameters and knows what "not ready yet" looks like. A false here is not a
		// failure: the device is commonly not resettable for a few frames after a mode or
		// focus change, and the caller retries next frame.
		if (!Gfx->Reset_Swap_Chain(SwapChain)) {
			return false;
		}

		if (reload_assets)
		{
			DX8TextureManagerClass::Recreate_Textures();
			if (m_pCleanupHook) {
				m_pCleanupHook->ReAcquireResources();
			}
		}
		// The device has just been reset. It holds none of its former render state, none of
		// its former texture bindings and none of its former shader constants, and no
		// amount of asking it will get them back -- this is the one place the whole
		// forget-everything path is describing what actually happened.
		Invalidate_Cached_Render_States("Reset_Device");
		Set_Default_Global_Render_States();
		SHD_INIT_SHADERS;
		WWDEBUG_SAY(("Device reset completed"));
		return true;
	}
	WWDEBUG_SAY(("Device reset failed"));
	return false;
}

void DX8Wrapper::Release_Device()
{
	if (Gfx) {

		for (int a=0;a<MAX_TEXTURE_STAGES;++a)
		{	//release references to any textures that were used in last rendering call
			GFXCALL(Set_Texture(a,nullptr));
		}

		GFXCALL(Set_Vertex_Stream(0, nullptr, 0));	//release reference count on last rendered vertex buffer
		GFXCALL(Set_Index_Buffer(nullptr,0));	//release reference count on last rendered index buffer


		/*
		** Release the current vertex and index buffers
		*/
		for (unsigned i=0;i<MAX_VERTEX_STREAMS;++i)
		{
			if (render_state.vertex_buffers[i]) render_state.vertex_buffers[i]->Release_Engine_Ref();
			REF_PTR_RELEASE(render_state.vertex_buffers[i]);
		}
		if (render_state.index_buffer) render_state.index_buffer->Release_Engine_Ref();
		REF_PTR_RELEASE(render_state.index_buffer);

		/*
		** Shutdown all subsystems
		*/
		Do_Onetime_Device_Dependent_Shutdowns();

		/*
		** Release the device. The backend owns the reference, so deleting it is the
		** release; the wrapper has no device of its own to drop first any more.
		*/
		delete Gfx;
		Gfx=nullptr;
	}
}

void DX8Wrapper::Enumerate_Devices()
{
	if (Adapter == nullptr) return;

	const unsigned adapter_count = Adapter->Get_Adapter_Count();
	for (unsigned adapter_index=0; adapter_index<adapter_count; adapter_index++) {

		GfxAdapterInfo info;
		if (!Adapter->Get_Adapter_Info(adapter_index, info))
			continue;

		/*
		** Set up the render device description
		*/
		RenderDeviceDescClass desc;
		desc.set_device_name(info.Description);
		desc.set_driver_name(info.Driver);
		desc.set_driver_version(info.DriverVersion);

		DX8Caps dx8caps(WW3D_FORMAT_UNKNOWN,adapter_index);

		/*
		** Enumerate the resolutions
		*/
		desc.reset_resolution_list();
		static const WW3DFormat formats[] = {
			WW3D_FORMAT_X8R8G8B8, WW3D_FORMAT_R5G6B5, WW3D_FORMAT_A8R8G8B8,
			WW3D_FORMAT_X1R5G5B5, WW3D_FORMAT_R8G8B8 };
		for (int f = 0; f < sizeof(formats)/sizeof(formats[0]); f++) {
			const WW3DFormat fmt = formats[f];
			const unsigned mode_count = Adapter->Get_Display_Mode_Count(adapter_index, fmt);
			for (unsigned mode_index=0; mode_index<mode_count; mode_index++) {
				GfxDisplayMode mode;
				if (!Adapter->Get_Display_Mode(adapter_index, fmt, mode_index, mode))
					continue;

				int bits = 0;
				switch (mode.Format)
				{
					case WW3D_FORMAT_R8G8B8:
					case WW3D_FORMAT_A8R8G8B8:
					case WW3D_FORMAT_X8R8G8B8:		bits = 32; break;

					case WW3D_FORMAT_R5G6B5:
					case WW3D_FORMAT_X1R5G5B5:		bits = 16; break;

					default: break;
				}

				// Some cards fail in certain modes, DX8Caps keeps list of those.
				if (!dx8caps.Is_Valid_Display_Format(mode.Width,mode.Height,mode.Format)) {
					bits=0;
				}

				if (bits != 0) {
					desc.add_resolution(mode.Width,mode.Height,bits);
				}
			}
		}

		// IML: If the device has one or more valid resolutions add it to the device list.
		// NOTE: Testing has shown that there are drivers with zero resolutions.
		if (desc.Enumerate_Resolutions().Count() > 0) {

			/*
			** Set up the device name
			*/
			StringClass device_name(info.Description,true);
			_RenderDeviceNameTable.Add(device_name);
			_RenderDeviceShortNameTable.Add(device_name);	// for now, just add the same name to the "pretty name table"

			/*
			** Add the render device to our table
			*/
			_RenderDeviceDescriptionTable.Add(desc);
		}
	}
}

bool DX8Wrapper::Set_Any_Render_Device()
{
	// Try fullscreen first
	int dev_number = 0;
	for (; dev_number < _RenderDeviceNameTable.Count(); dev_number++) {
		if (Set_Render_Device(dev_number,-1,-1,-1,0,false)) {
			return true;
		}
	}

	// Then windowed
	for (dev_number = 0; dev_number < _RenderDeviceNameTable.Count(); dev_number++) {
		if (Set_Render_Device(dev_number,-1,-1,-1,1,false)) {
			return true;
		}
	}

	return false;
}

bool DX8Wrapper::Set_Render_Device
(
	const char * dev_name,
	int width,
	int height,
	int bits,
	int windowed,
	bool resize_window
)
{
	for ( int dev_number = 0; dev_number < _RenderDeviceNameTable.Count(); dev_number++) {
		if ( strcmp( dev_name, _RenderDeviceNameTable[dev_number]) == 0) {
			return Set_Render_Device( dev_number, width, height, bits, windowed, resize_window );
		}

		if ( strcmp( dev_name, _RenderDeviceShortNameTable[dev_number]) == 0) {
			return Set_Render_Device( dev_number, width, height, bits, windowed, resize_window );
		}
	}
	return false;
}

void DX8Wrapper::Get_Format_Name(unsigned int format, StringClass *tex_format)
{
		*tex_format="Unknown";
		switch (format) {
		case D3DFMT_A8R8G8B8: *tex_format="D3DFMT_A8R8G8B8"; break;
		case D3DFMT_R8G8B8: *tex_format="D3DFMT_R8G8B8"; break;
		case D3DFMT_A4R4G4B4: *tex_format="D3DFMT_A4R4G4B4"; break;
		case D3DFMT_A1R5G5B5: *tex_format="D3DFMT_A1R5G5B5"; break;
		case D3DFMT_R5G6B5: *tex_format="D3DFMT_R5G6B5"; break;
		case D3DFMT_L8: *tex_format="D3DFMT_L8"; break;
		case D3DFMT_A8: *tex_format="D3DFMT_A8"; break;
		case D3DFMT_P8: *tex_format="D3DFMT_P8"; break;
		case D3DFMT_X8R8G8B8: *tex_format="D3DFMT_X8R8G8B8"; break;
		case D3DFMT_X1R5G5B5: *tex_format="D3DFMT_X1R5G5B5"; break;
		case D3DFMT_R3G3B2: *tex_format="D3DFMT_R3G3B2"; break;
		case D3DFMT_A8R3G3B2: *tex_format="D3DFMT_A8R3G3B2"; break;
		case D3DFMT_X4R4G4B4: *tex_format="D3DFMT_X4R4G4B4"; break;
		case D3DFMT_A8P8: *tex_format="D3DFMT_A8P8"; break;
		case D3DFMT_A8L8: *tex_format="D3DFMT_A8L8"; break;
		case D3DFMT_A4L4: *tex_format="D3DFMT_A4L4"; break;
		case D3DFMT_V8U8: *tex_format="D3DFMT_V8U8"; break;
		case D3DFMT_L6V5U5: *tex_format="D3DFMT_L6V5U5"; break;
		case D3DFMT_X8L8V8U8: *tex_format="D3DFMT_X8L8V8U8"; break;
		case D3DFMT_Q8W8V8U8: *tex_format="D3DFMT_Q8W8V8U8"; break;
		case D3DFMT_V16U16: *tex_format="D3DFMT_V16U16"; break;
		case D3DFMT_UYVY: *tex_format="D3DFMT_UYVY"; break;
		case D3DFMT_YUY2: *tex_format="D3DFMT_YUY2"; break;
		case D3DFMT_DXT1: *tex_format="D3DFMT_DXT1"; break;
		case D3DFMT_DXT2: *tex_format="D3DFMT_DXT2"; break;
		case D3DFMT_DXT3: *tex_format="D3DFMT_DXT3"; break;
		case D3DFMT_DXT4: *tex_format="D3DFMT_DXT4"; break;
		case D3DFMT_DXT5: *tex_format="D3DFMT_DXT5"; break;
		case D3DFMT_D16_LOCKABLE: *tex_format="D3DFMT_D16_LOCKABLE"; break;
		case D3DFMT_D32: *tex_format="D3DFMT_D32"; break;
		case D3DFMT_D15S1: *tex_format="D3DFMT_D15S1"; break;
		case D3DFMT_D24S8: *tex_format="D3DFMT_D24S8"; break;
		case D3DFMT_D16: *tex_format="D3DFMT_D16"; break;
		case D3DFMT_D24X8: *tex_format="D3DFMT_D24X8"; break;
		case D3DFMT_D24X4S4: *tex_format="D3DFMT_D24X4S4"; break;
		default:	break;
		}
}

void DX8Wrapper::Resize_And_Position_Window()
{
	// Get the current dimensions of the 'render area' of the window
	RECT rect = { 0 };
	::GetClientRect (_Hwnd, &rect);

	// Is the window the correct size for this resolution?
	if ((rect.right-rect.left) != ResolutionWidth ||
			(rect.bottom-rect.top) != ResolutionHeight) {

		// Calculate what the main window's bounding rectangle should be to
		// accommodate this resolution
		rect.left = 0;
		rect.top = 0;
		rect.right = ResolutionWidth;
		rect.bottom = ResolutionHeight;
		DWORD dwstyle = ::GetWindowLong (_Hwnd, GWL_STYLE);
		AdjustWindowRect (&rect, dwstyle, FALSE);
		int width = rect.right-rect.left;
		int height = rect.bottom-rect.top;

		// Resize the window to fit this resolution
		if (!IsWindowed)
		{
			::SetWindowPos(_Hwnd, HWND_TOPMOST, 0, 0, width, height, 0);

			DEBUG_LOG(("Window resized to w:%d h:%d", width, height));
		}
		else
		{
			// TheSuperHackers @feature helmutbuhler 14/04/2025
			// Center the window in the workarea of the monitor it is on.
			MONITORINFO mi = {sizeof(MONITORINFO)};
			GetMonitorInfo(MonitorFromWindow(_Hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
			int left = (mi.rcWork.left + mi.rcWork.right - width) / 2;
			int top  = (mi.rcWork.top + mi.rcWork.bottom - height) / 2;

			// TheSuperHackers @feature helmutbuhler 14/04/2025
			// Move the window to try fit it into the monitor area, if one of its dimensions is larger than the work area.
			// Otherwise align the window to the top left edges, if it is even larger than the monitor area.
			RECT rectClient;
			rectClient.left = left - rect.left;
			rectClient.top = top - rect.top;
			rectClient.right = rectClient.left + ResolutionWidth;
			rectClient.bottom = rectClient.top + ResolutionHeight;
			MoveRectIntoOtherRect(rectClient, mi.rcMonitor, &left, &top);

			::SetWindowPos (_Hwnd, nullptr, left, top, width, height, SWP_NOZORDER);

			DEBUG_LOG(("Window positioned to x:%d y:%d, resized to w:%d h:%d", left, top, width, height));
		}
	}
}

bool DX8Wrapper::Set_Render_Device(int dev, int width, int height, int bits, int windowed,
								   bool resize_window,bool reset_device, bool restore_assets)
{
	WWASSERT(IsInitted);
	WWASSERT(dev >= -1);
	WWASSERT(dev < _RenderDeviceNameTable.Count());

	/*
	** If user has never selected a render device, start out with device 0
	*/
	if ((CurRenderDevice == -1) && (dev == -1)) {
		CurRenderDevice = 0;
	} else if (dev != -1) {
		CurRenderDevice = dev;
	}

	/*
	** If user doesn't want to change res, set the res variables to match the
	** current resolution
	*/
	if (width != -1)		ResolutionWidth = width;
	if (height != -1)		ResolutionHeight = height;

	if (bits != -1)		BitDepth = bits;
	if (windowed != -1)	IsWindowed = (windowed != 0);
	DX8Wrapper_IsWindowed = IsWindowed;

	WWDEBUG_SAY(("Attempting Set_Render_Device: name: %s (%s:%s), width: %d, height: %d, windowed: %d",
		_RenderDeviceNameTable[CurRenderDevice].str(),_RenderDeviceDescriptionTable[CurRenderDevice].Get_Driver_Name(),
		_RenderDeviceDescriptionTable[CurRenderDevice].Get_Driver_Version(),ResolutionWidth,ResolutionHeight,(IsWindowed ? 1 : 0)));

#ifdef _WIN32
	// PWG 4/13/2000 - changed so that if you say to resize the window it resizes
	// regardless of whether its windowed or not as OpenGL resizes its self around
	// the caption and edges of the window type you provide, so its important to
	// push the client area to be the size you really want.
	// if ( resize_window && windowed ) {
	if (resize_window) {
		Resize_And_Position_Window();
	}
#endif
	//must be either resetting existing device or creating a new one.
	WWASSERT(reset_device || Gfx == nullptr);

	/*
	** Initialize values for D3DPRESENT_PARAMETERS members.
	*/
	/*
	** Say what the engine wants of the swap chain. Nothing here is in the API's vocabulary
	** any more: the backend turns this into whatever its own creation call needs.
	*/
	SwapChain = GfxSwapChainDesc();
	SwapChain.Window = _Hwnd;
	SwapChain.Width = ResolutionWidth;
	SwapChain.Height = ResolutionHeight;
	SwapChain.BackBufferCount = IsWindowed ? 1 : 2;
	SwapChain.Windowed = IsWindowed != 0;
	SwapChain.RefreshRate = 0;					// whatever the adapter defaults to
	SwapChain.SwapInterval = -1;				// and whatever it defaults to for waiting
	SwapChain.BackBufferFormat = WW3D_FORMAT_UNKNOWN;
	SwapChain.DepthStencilFormat = WW3D_ZFORMAT_UNKNOWN;
	SwapChain.MultiSample = WW3D_MULTISAMPLE_NONE;

	/*
	** Set up the buffer formats.  Several issues here:
	** - if in windowed mode, the backbuffer must use the current display format.
	** - the depth buffer must use
	*/
	if (IsWindowed) {

		GfxDisplayMode desktop_mode;
		if (Adapter == nullptr || !Adapter->Get_Current_Display_Mode(Get_Adapter_Index(), desktop_mode))
			return false;

		DisplayFormat = SwapChain.BackBufferFormat = desktop_mode.Format;

		// In windowed mode, define the bitdepth from desktop mode (as it can't be changed)
		switch (SwapChain.BackBufferFormat) {
		case WW3D_FORMAT_X8R8G8B8:
		case WW3D_FORMAT_A8R8G8B8:
		case WW3D_FORMAT_R8G8B8: BitDepth=32; break;
		case WW3D_FORMAT_A4R4G4B4:
		case WW3D_FORMAT_A1R5G5B5:
		case WW3D_FORMAT_R5G6B5: BitDepth=16; break;
		case WW3D_FORMAT_L8:
		case WW3D_FORMAT_A8:
		case WW3D_FORMAT_P8: BitDepth=8; break;
		default:
			// Unknown backbuffer format probably means the device can't do windowed
			return false;
		}

		if (BitDepth==32 && Adapter->Supports_Display_Format(Get_Adapter_Index(),
				desktop_mode.Format, WW3D_FORMAT_A8R8G8B8, true))
		{	//promote 32-bit modes to include destination alpha
			SwapChain.BackBufferFormat = WW3D_FORMAT_A8R8G8B8;
		}

		/*
		** Find a appropriate Z buffer
		*/
		if (!Find_Z_Mode(DisplayFormat,SwapChain.BackBufferFormat,&SwapChain.DepthStencilFormat))
		{
			// If opening 32 bit mode failed, try 16 bit, even if the desktop happens to be 32 bit
			if (BitDepth==32) {
				BitDepth=16;
				SwapChain.BackBufferFormat=WW3D_FORMAT_R5G6B5;
				if (!Find_Z_Mode(SwapChain.BackBufferFormat,SwapChain.BackBufferFormat,&SwapChain.DepthStencilFormat)) {
					SwapChain.DepthStencilFormat=WW3D_ZFORMAT_UNKNOWN;
				}
			}
			else {
				SwapChain.DepthStencilFormat=WW3D_ZFORMAT_UNKNOWN;
			}
		}

	} else {

		/*
		** Try to find a mode that matches the user's desired bit-depth.
		*/
		Find_Color_And_Z_Mode(ResolutionWidth,ResolutionHeight,BitDepth,&DisplayFormat,
			&SwapChain.BackBufferFormat,&SwapChain.DepthStencilFormat);
	}

	/*
	** Set default for depth stencil format if auto Z buffer failed.
	*/
	if (SwapChain.DepthStencilFormat==WW3D_ZFORMAT_UNKNOWN) {
		if (BitDepth==32) {
			SwapChain.DepthStencilFormat=WW3D_ZFORMAT_D32;
		}
		else {
			SwapChain.DepthStencilFormat=WW3D_ZFORMAT_D16;
		}
	}

	/*
	** Check the devices support for the requested MSAA mode then setup the multi sample type
	*/
	if (MultiSampleAntiAliasing > WW3D_MULTISAMPLE_NONE && Adapter != nullptr) {

		const bool back_ok = Adapter->Supports_Multisample(Get_Adapter_Index(),
			SwapChain.BackBufferFormat, IsWindowed != 0, MultiSampleAntiAliasing);
		const bool depth_ok = Adapter->Supports_Depth_Multisample(Get_Adapter_Index(),
			SwapChain.DepthStencilFormat, IsWindowed != 0, MultiSampleAntiAliasing);

		if (!back_ok || !depth_ok) {
			// IF we fail then disable MSAA entirely.
			// External code needs to retrieve the configured MSAA mode after device creation
			WWDEBUG_SAY(("Requested MSAA Mode Not Supported"));
			MultiSampleAntiAliasing = WW3D_MULTISAMPLE_NONE;
		}
	}

	SwapChain.MultiSample = MultiSampleAntiAliasing;

	/*
	** Time to actually create the device.
	*/
	StringClass displayFormat;
	StringClass backbufferFormat;

	Get_WW3D_Format_Name(DisplayFormat,displayFormat);
	Get_WW3D_Format_Name(SwapChain.BackBufferFormat,backbufferFormat);

	WWDEBUG_SAY(("Using Display/BackBuffer Formats: %s/%s",displayFormat.str(),backbufferFormat.str()));

	bool ret;

	if (reset_device)
	{
		WWDEBUG_SAY(("DX8Wrapper::Set_Render_Device is resetting the device."));
		ret = Reset_Device(restore_assets);	//reset device without restoring data - we're likely switching out of the app.
	}
	else
		ret = Create_Device();

	WWDEBUG_SAY(("Reset/Create_Device done, reset_device=%d, restore_assets=%d", reset_device, restore_assets));

	if (ret)
	{
		Render2DClass::Set_Screen_Resolution( RectClass( 0, 0, ResolutionWidth, ResolutionHeight ) );
	}

	return ret;
}

bool DX8Wrapper::Set_Next_Render_Device()
{
	int new_dev = (CurRenderDevice + 1) % _RenderDeviceNameTable.Count();
	return Set_Render_Device(new_dev);
}

bool DX8Wrapper::Toggle_Windowed()
{
#ifdef WW3D_DX8
	// State OK?
	assert (IsInitted);
	if (IsInitted) {

		// Get information about the current render device's resolutions
		const RenderDeviceDescClass &render_device = Get_Render_Device_Desc ();
		const DynamicVectorClass<ResolutionDescClass> &resolutions = render_device.Enumerate_Resolutions ();

		// Loop through all the resolutions supported by the current device.
		// If we aren't currently running under one of these resolutions,
		// then we should probably		 to the closest resolution before
		// toggling the windowed state.
		int curr_res = -1;
		for (int res = 0;
		     (res < resolutions.Count ()) && (curr_res == -1);
			  res ++) {

			// Is this the resolution we are looking for?
			if ((resolutions[res].Width == ResolutionWidth) &&
				 (resolutions[res].Height == ResolutionHeight) &&
				 (resolutions[res].BitDepth == BitDepth)) {
				curr_res = res;
			}
		}

		if (curr_res == -1) {

			// We don't match any of the standard resolutions,
			// so set the first resolution and toggle the windowed state.
			return Set_Device_Resolution (resolutions[0].Width,
								 resolutions[0].Height,
								 resolutions[0].BitDepth,
								 !IsWindowed, true);
		} else {

			// Toggle the windowed state
			return Set_Device_Resolution (-1, -1, -1, !IsWindowed, true);
		}
	}
#endif //WW3D_DX8

	return false;
}

void DX8Wrapper::Set_Swap_Interval(int swap)
{
	// How many retraces to wait, straight. Anything outside 0..3 means one, which is what
	// the D3D9 constants this used to hold mapped to.
	SwapChain.SwapInterval = (swap >= 0 && swap <= 3) ? swap : 1;

	WWDEBUG_SAY(("DX8Wrapper::Set_Swap_Interval is resetting the device."));
	Reset_Device();
}

int DX8Wrapper::Get_Swap_Interval()
{
	return SwapChain.SwapInterval;
}

bool DX8Wrapper::Has_Stencil()
{
	return SwapChain.DepthStencilFormat == WW3D_ZFORMAT_D24S8 ||
		   SwapChain.DepthStencilFormat == WW3D_ZFORMAT_D24X4S4;
}

int DX8Wrapper::Get_Render_Device_Count()
{
	return _RenderDeviceNameTable.Count();

}
int DX8Wrapper::Get_Render_Device()
{
	assert(IsInitted);
	return CurRenderDevice;
}

const RenderDeviceDescClass & DX8Wrapper::Get_Render_Device_Desc(int deviceidx)
{
	WWASSERT(IsInitted);

	if ((deviceidx == -1) && (CurRenderDevice == -1)) {
		CurRenderDevice = 0;
	}

	// if the device index is -1 then we want the current device
	if (deviceidx == -1) {
		WWASSERT(CurRenderDevice >= 0);
		WWASSERT(CurRenderDevice < _RenderDeviceNameTable.Count());
		return _RenderDeviceDescriptionTable[CurRenderDevice];
	}

	// We can only ask for multiple device information if the devices
	// have been detected.
	WWASSERT(deviceidx >= 0);
	WWASSERT(deviceidx < _RenderDeviceNameTable.Count());
	return _RenderDeviceDescriptionTable[deviceidx];
}

const char * DX8Wrapper::Get_Render_Device_Name(int device_index)
{
	device_index = device_index % _RenderDeviceShortNameTable.Count();
	return _RenderDeviceShortNameTable[device_index];
}

bool DX8Wrapper::Set_Device_Resolution(int width,int height,int bits,int windowed, bool resize_window)
{
	if (Gfx != nullptr) {

		if (width != -1) {
			SwapChain.Width = ResolutionWidth = width;
		}
		if (height != -1) {
			SwapChain.Height = ResolutionHeight = height;
		}
		if (resize_window)
		{
			Resize_And_Position_Window();
		}
#pragma message("TODO: support changing windowed status and changing the bit depth")
		WWDEBUG_SAY(("DX8Wrapper::Set_Device_Resolution is resetting the device."));
		return Reset_Device();
	} else {
		return false;
	}
}

void DX8Wrapper::Get_Device_Resolution(int & set_w,int & set_h,int & set_bits,bool & set_windowed)
{
	WWASSERT(IsInitted);

	set_w = ResolutionWidth;
	set_h = ResolutionHeight;
	set_bits = BitDepth;
	set_windowed = IsWindowed;
}

void DX8Wrapper::Get_Render_Target_Resolution(int & set_w,int & set_h,int & set_bits,bool & set_windowed)
{
	WWASSERT(IsInitted);

	if (CurrentRenderTarget != nullptr) {
		WW3DSurfaceDescription info;
		Gfx->Describe_Surface(CurrentRenderTarget, info);

		set_w				= info.Width;
		set_h				= info.Height;
		set_bits			= BitDepth;		// should we get the actual bit depth of the target?
		set_windowed	= IsWindowed;	// this doesn't really make sense for render targets (shouldn't matter)...

	} else {
		Get_Device_Resolution (set_w, set_h, set_bits, set_windowed);
	}
}

bool DX8Wrapper::Registry_Save_Render_Device( const char * sub_key )
{
	int	width, height, depth;
	bool	windowed;
	Get_Device_Resolution(width, height, depth, windowed);
	return Registry_Save_Render_Device(sub_key, CurRenderDevice, ResolutionWidth, ResolutionHeight, BitDepth, IsWindowed, TextureBitDepth);
}

bool DX8Wrapper::Registry_Save_Render_Device( const char *sub_key, int device, int width, int height, int depth, bool windowed, int texture_depth)
{
	RegistryClass * registry = W3DNEW RegistryClass( sub_key );
	WWASSERT( registry );

	if ( !registry->Is_Valid() ) {
		delete registry;
		WWDEBUG_SAY(( "Error getting Registry" ));
		return false;
	}

	registry->Set_String( VALUE_NAME_RENDER_DEVICE_NAME,
		_RenderDeviceShortNameTable[device] );
	registry->Set_Int( VALUE_NAME_RENDER_DEVICE_WIDTH,	width );
	registry->Set_Int( VALUE_NAME_RENDER_DEVICE_HEIGHT, height );
	registry->Set_Int( VALUE_NAME_RENDER_DEVICE_DEPTH, depth );
	registry->Set_Int( VALUE_NAME_RENDER_DEVICE_WINDOWED, windowed );
	registry->Set_Int( VALUE_NAME_RENDER_DEVICE_TEXTURE_DEPTH, texture_depth );

	delete registry;
	return true;
}

bool DX8Wrapper::Registry_Load_Render_Device( const char * sub_key, bool resize_window )
{
	char	name[ 200 ];
	int	width,height,depth,windowed;

	if (	Registry_Load_Render_Device(	sub_key,
													name,
													sizeof(name),
													width,
													height,
													depth,
													windowed,
													TextureBitDepth) &&
			(*name != 0))
	{
		WWDEBUG_SAY(( "Device %s (%d X %d) %d bit windowed:%d", name,width,height,depth,windowed));

		if (TextureBitDepth==16 || TextureBitDepth==32) {
//			WWDEBUG_SAY(( "Texture depth %d", TextureBitDepth));
		} else {
			WWDEBUG_SAY(( "Invalid texture depth %d, switching to 16 bits", TextureBitDepth));
			TextureBitDepth=16;
		}

		if ( Set_Render_Device( name, width,height,depth,windowed, resize_window ) != true) {
			if (depth==16) depth=32;
			else depth=16;
			if ( Set_Render_Device( name, width,height,depth,windowed, resize_window ) == true) {
				return true;
			}
			if (depth==16) depth=32;
			else depth=16;
			// we'll test resolutions down, so if start is 640, increase to begin with...
			if (width==640) {
				width=1024;
				height=768;
			}
			for(;;) {
				if (width>2048) {
					width=2048;
					height=1536;
				}
				else if (width>1920) {
					width=1920;
					height=1440;
				}
				else if (width>1600) {
					width=1600;
					height=1200;
				}
				else if (width>1280) {
					width=1280;
					height=1024;
				}
				else if (width>1024) {
					width=1024;
					height=768;
				}
				else if (width>800) {
					width=800;
					height=600;
				}
				else if (width!=640) {
					width=640;
					height=480;
				}
				else {
					return Set_Any_Render_Device();
				}
				for (int i=0;i<2;++i) {
					if ( Set_Render_Device( name, width,height,depth,windowed, resize_window ) == true) {
						return true;
					}
					if (depth==16) depth=32;
					else depth=16;
				}
			}
		}

		return true;
	}

	WWDEBUG_SAY(( "Error getting Registry" ));

	return Set_Any_Render_Device();
}

bool DX8Wrapper::Registry_Load_Render_Device( const char * sub_key, char *device, int device_len, int &width, int &height, int &depth, int &windowed, int &texture_depth)
{
	RegistryClass registry( sub_key );

	if ( registry.Is_Valid() ) {
		registry.Get_String( VALUE_NAME_RENDER_DEVICE_NAME,
			device, device_len);

		width =		registry.Get_Int( VALUE_NAME_RENDER_DEVICE_WIDTH, -1 );
		height =		registry.Get_Int( VALUE_NAME_RENDER_DEVICE_HEIGHT, -1 );
		depth =		registry.Get_Int( VALUE_NAME_RENDER_DEVICE_DEPTH, -1 );
		windowed =	registry.Get_Int( VALUE_NAME_RENDER_DEVICE_WINDOWED, -1 );
		texture_depth = registry.Get_Int( VALUE_NAME_RENDER_DEVICE_TEXTURE_DEPTH, -1 );
		return true;
	}
	*device=0;
	width=-1;
	height=-1;
	depth=-1;
	windowed=-1;
	texture_depth=-1;
	return false;
}


bool DX8Wrapper::Find_Color_And_Z_Mode(int resx,int resy,int bitdepth,WW3DFormat * set_colorbuffer,WW3DFormat * set_backbuffer,WW3DZFormat * set_zmode)
{
	static const WW3DFormat _formats16[] =
	{
		WW3D_FORMAT_R5G6B5,
		WW3D_FORMAT_X1R5G5B5,
		WW3D_FORMAT_A1R5G5B5
	};

	static const WW3DFormat _formats32[] =
	{
		WW3D_FORMAT_A8R8G8B8,
		WW3D_FORMAT_X8R8G8B8,
		WW3D_FORMAT_R8G8B8,
	};

	/*
	** Select which list to use
	*/
	const WW3DFormat * format_table = nullptr;
	int format_count = 0;

	if (bitdepth == 16) {
		format_table = _formats16;
		format_count = sizeof(_formats16) / sizeof(_formats16[0]);
	} else {
		format_table = _formats32;
		format_count = sizeof(_formats32) / sizeof(_formats32[0]);
	}

	/*
	** now search for a valid format
	*/
	bool found = false;
	unsigned mode = 0;

	int format_index=0;
	for (; format_index < format_count; format_index++) {
		found |= Find_Color_Mode(format_table[format_index],resx,resy,&mode);
		if (found) break;
	}

	if (!found) {
		return false;
	} else {
		*set_backbuffer=*set_colorbuffer = format_table[format_index];
	}

	if (bitdepth==32 && *set_colorbuffer == WW3D_FORMAT_X8R8G8B8 && Adapter != nullptr &&
		Adapter->Supports_Display_Format(Get_Adapter_Index(), *set_colorbuffer, WW3D_FORMAT_A8R8G8B8, true))
	{	//promote 32-bit modes to include destination alpha when supported
		*set_backbuffer = WW3D_FORMAT_A8R8G8B8;
	}

	/*
	** We found a backbuffer format, now find a zbuffer format
	*/
	return Find_Z_Mode(*set_colorbuffer,*set_backbuffer, set_zmode);
};


// find the resolution mode with at least resx,resy with the highest supported
// refresh rate
bool DX8Wrapper::Find_Color_Mode(WW3DFormat colorbuffer, int resx, int resy, unsigned *mode)
{
	if (Adapter == nullptr) return false;

	const unsigned rx=(unsigned)resx;
	const unsigned ry=(unsigned)resy;

	bool found=false;

	// Asked of the adapter the device will actually be created on, and in the order the
	// adapter reports -- which is by size and then by refresh rate, and the walk below
	// relies on that.
	const unsigned adapter = Get_Adapter_Index();
	const unsigned modemax = Adapter->Get_Display_Mode_Count(adapter, colorbuffer);

	GfxDisplayMode dmode;
	unsigned i=0;
	while (i<modemax && !found)
	{
		if (Adapter->Get_Display_Mode(adapter, colorbuffer, i, dmode) &&
			dmode.Width==rx && dmode.Height==ry && dmode.Format==colorbuffer) {
			WWDEBUG_SAY(("Found valid color mode.  Width = %d Height = %d Format = %d",dmode.Width,dmode.Height,(int)dmode.Format));
			found=true;
		}
		i++;
	}

	i--; // this is the first valid mode

	// no match
	if (!found) {
		WWDEBUG_SAY(("Failed to find a valid color mode"));
		return false;
	}

	// go to the highest refresh rate in this mode
	bool stillok=true;

	unsigned j=i;
	while (j<modemax && stillok)
	{
		if (Adapter->Get_Display_Mode(adapter, colorbuffer, j, dmode) &&
			dmode.Width==rx && dmode.Height==ry && dmode.Format==colorbuffer)
			stillok=true; else stillok=false;
		j++;
	}

	if (stillok==false) *mode=j-2;
	else *mode=i;

	return true;
}

// Helper function to find a Z buffer mode for the colorbuffer
// Will look for greatest Z precision
bool DX8Wrapper::Find_Z_Mode(WW3DFormat colorbuffer,WW3DFormat backbuffer, WW3DZFormat *zmode)
{
	// Stencil modes first, deliberately: the shadow volumes need one and there is no way
	// to ask for it again later.
	static const WW3DZFormat _order[] = {
		WW3D_ZFORMAT_D24S8,
		WW3D_ZFORMAT_D32,
		WW3D_ZFORMAT_D24X8,
		WW3D_ZFORMAT_D24X4S4,
		WW3D_ZFORMAT_D16,
		WW3D_ZFORMAT_D15S1
	};

	for (int i = 0; i < sizeof(_order)/sizeof(_order[0]); ++i) {
		if (Test_Z_Mode(colorbuffer,backbuffer,_order[i])) {
			*zmode=_order[i];
			StringClass name(0,true);
			Get_WW3D_ZFormat_Name(_order[i],name);
			WWDEBUG_SAY(("Found zbuffer mode %s",name.str()));
			return true;
		}
	}

	// can't find a match
	WWDEBUG_SAY(("Failed to find a valid zbuffer mode"));
	return false;
}

bool DX8Wrapper::Test_Z_Mode(WW3DFormat colorbuffer,WW3DFormat backbuffer, WW3DZFormat zmode)
{
	// Query the adapter the device will actually be created on, not adapter 0. Create_Device
	// passes the same index to the creation call, so validating formats against the default
	// adapter asked the wrong GPU on any multi-adapter machine.
	//
	// This reports what the chosen adapter supports and nothing else. It must not go looking
	// for an adapter that does support the format: by the time we get here Set_Render_Device
	// has already taken the display format and resolution from this adapter, CurRenderDevice
	// is the user's saved choice, and Registry_Save_Render_Device will persist whatever it
	// holds. A device created on one adapter with another's display mode is worse than
	// falling through to the next z format.
	if (Adapter == nullptr) return false;

	if (!Adapter->Supports_Depth_Stencil_Format(Get_Adapter_Index(), colorbuffer, backbuffer, zmode)) {
		WWDEBUG_SAY(("Depth format rejected.  Colorbuffer format = %d  Backbuffer format = %d  Zbufferformat = %d",
			(int)colorbuffer,(int)backbuffer,(int)zmode));
		return false;
	}
	return true;
}


void DX8Wrapper::Reset_Statistics()
{
	FrameStatistics = DX8FrameStatistics();
	LastFrameStatistics = DX8FrameStatistics();
}

void DX8Wrapper::Begin_Statistics()
{
	FrameStatistics = DX8FrameStatistics();
}

void DX8Wrapper::End_Statistics()
{
	LastFrameStatistics = FrameStatistics;
}

const DX8FrameStatistics& DX8Wrapper::Get_Last_Frame_Statistics()
{
	return LastFrameStatistics;
}

unsigned long DX8Wrapper::Get_FrameCount() {return FrameCount;}

void DX8_Assert()
{
	WWASSERT(DX8Wrapper::Get_Adapter());
	DX8_THREAD_ASSERT();
}

void DX8Wrapper::Begin_Scene()
{
	DX8_THREAD_ASSERT();

#if ENABLE_EMBEDDED_BROWSER
	DX8WebBrowser::Update();
#endif

	GFXCALL(Begin_Scene());

	DX8WebBrowser::Update();
}

//-----------------------------------------------------------------------------
// Post-scene callback. See the header for why this instant in the frame is the only
// one at which the finished back buffer can be read.
//-----------------------------------------------------------------------------
static DX8Wrapper::PostSceneCallbackFunc s_postSceneCallback = nullptr;
static void* s_postSceneCallbackData = nullptr;

void DX8Wrapper::Request_Post_Scene_Callback(PostSceneCallbackFunc func, void* userData)
{
	s_postSceneCallback = func;
	s_postSceneCallbackData = userData;
}

// Outside the debug block for the same reason the callback above is: the header
// declares it unconditionally, so a definition inside RTS_DEBUG would leave the
// release build with a use and no definition.
bool DX8Wrapper::Dump_Shadow_Map(const char* pathname)
{
	if (m_pShadowMap == nullptr)
		return false;

	if (Gfx == nullptr) return false;
	GfxSurface * surface = Gfx->Get_Texture_Surface_Level((GfxTexture*)m_pShadowMap, 0);
	if (surface == nullptr) return false;

	const bool ok = Gfx->Save_Surface_To_File(pathname, surface);
	Gfx->Release_Surface(surface);
	return ok;
}

DebugVisMode					DX8Wrapper::m_debugVisMode = DEBUG_VIS_OFF;
DWORD							DX8Wrapper::m_dwDebugTintPS = 0;
DWORD							DX8Wrapper::m_dwDebugNormalVS = 0;
DWORD							DX8Wrapper::m_dwDebugNormalPS = 0;

void DX8Wrapper::Set_Debug_Vis_Mode(DebugVisMode mode)
{
	if (mode == m_debugVisMode) return;
	const DebugVisMode previous = m_debugVisMode;
	m_debugVisMode = mode;

	// DEBUG_VIS_OVERDRAW writes texture-stage state directly, behind ShaderClass's back.
	// ShaderClass only re-issues those when it believes the shader has changed, so on the
	// way out of such a mode it would skip the stages they overwrote and the whole scene
	// would stay flat-shaded until something else happened to change shader. Marking it
	// dirty makes the next Apply rewrite all of them, which is exactly the repair.
	//
	// Done on entry as well as exit, and unconditionally rather than only for the modes
	// that need it: the cost is one redundant state re-apply on a key press.
	ShaderClass::Invalidate();

	// The fill mode has to be put back by hand. ShaderClass does not set D3DRS_FILLMODE,
	// so unlike everything else the wireframe mode touches, invalidating the shader does
	// not undo it -- leaving the entire game wireframe for the rest of the session.
	// Written straight to the device rather than through the tracked setter because the
	// tracked value may already read SOLID from before the mode was entered, in which
	// case the setter would consider the write redundant and skip it.
	if (previous == DEBUG_VIS_WIREFRAME && Gfx != nullptr) {
		Gfx->Set_Render_State(D3DRS_FILLMODE, D3DFILL_SOLID);
		// ...and the line above is the only write in the game that goes straight at the
		// device past the wrapper, so it is the only one that leaves the wrapper's record
		// of the device wrong. Once per debug-view cycle, on a key press.
		Invalidate_Cached_Render_States("debugVisWireframeExit");
	}
}

// Replace this draw's shading to say something about the draw. See debugvis.h.
//
// Everything here runs at the very end of Apply_Render_State_Changes, which is what makes
// it able to override at all: ShaderClass::Apply has already run by this point, so the
// blend, depth and stage state written here is the last word for this draw.
//
// Each mode has to work on both pipelines, and they do not take instruction the same way:
// a programmable draw has its pixel shader swapped, a fixed-function draw is steered
// through TFACTOR with the texture stages collapsed onto it. Where a mode cannot express
// itself on a given draw it leaves that draw alone rather than approximating, so what is
// on screen is never a guess.
void DX8Wrapper::Apply_Debug_Draw_Override(bool fixedFunction, bool hasNormal,
										   unsigned routeBit)
{
	switch (m_debugVisMode) {

		case DEBUG_VIS_MESH_TECHNIQUE:
			// The declared technique, and whether the draw actually reached a shader.
			// Both halves matter: the technique alone says what the asset asked for, and
			// the pipeline alone says what happened, but only the pair says whether the
			// two agree -- which is the whole question this milestone is about.
			Debug_Flat_Shade(Debug_Vis_Technique_Color(m_meshTechnique, fixedFunction),
							 fixedFunction);
			break;

		case DEBUG_VIS_WIREFRAME:
			// Lines only, in one colour. Depth is left exactly as the draw set it, so
			// this is a solid wireframe rather than an x-ray one: what is behind a hill
			// stays behind it, and the density on screen is the density actually being
			// rasterised for this view.
			//
			// The fill mode itself is set by the caller, which sees the excluded passes
			// this function never runs for -- see the note there on why they need it set
			// too rather than merely left alone.
			Debug_Flat_Shade(0xFF90FF90, fixedFunction);
			break;

		case DEBUG_VIS_OVERDRAW: {
			// Count layers by adding a small constant per draw and letting the frame
			// accumulate.
			//
			// Depth is left exactly as each draw set it, and that choice is the whole
			// character of the mode. Forcing the test off would count every fragment the
			// rasteriser ever produced, which sounds like the truer measure of fill and
			// is not a picture: with nothing occluding anything, every wall, floor and
			// terrain chunk in the level composites on top of every other, and the result
			// is a bright mess with no spatial structure left to point at. Tried first,
			// and unreadable.
			//
			// What this counts instead is the layers that survive depth -- which is the
			// number worth acting on anyway. Opaque geometry is depth-tested and mostly
			// costs one layer; the fill that actually hurts is the translucent pile
			// (smoke, dust, glows, decals, the sorted pass) and none of that writes depth,
			// so all of it still accumulates in full.
			//
			// The colour is not grey. Adding a constant with unequal channels makes the
			// channels clip at different counts, so the sum walks red -> orange -> yellow
			// -> white on its own as the layers pile up, and a single additive draw does
			// the work a separate ramp pass would otherwise be needed for.
			Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, TRUE);
			Set_DX8_Render_State(D3DRS_SRCBLEND,  D3DBLEND_ONE);
			Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_ONE);
			Debug_Flat_Shade(0xFF140A03, fixedFunction);
			break;
		}

		case DEBUG_VIS_NORMALS:
			// The one mode that cannot cover the whole frame, and it declines rather than
			// invents. It needs a vertex normal in the stream, and it needs this draw to
			// have routed to the mesh shaders -- because it substitutes for unit_vs and
			// reads the matrices unit_vs was handed, which mean something else entirely
			// in the terrain pass. Everything else is flat grey, and the legend says so.
			if (m_dwDebugNormalVS != 0 && m_dwDebugNormalPS != 0 &&
				hasNormal && (routeBit & (2u | 4u | 8u)) != 0) {
				// Upload the object -> camera matrix rather than inheriting whatever the draw
				// left in c4. debugnormal_vs substitutes for unit_vs and reads unit_vs's
				// registers, but c4 is the one register whose meaning depends on the route: a
				// PBR draw puts plain world there, because unit_pbr_ps shades in world space.
				// Inheriting it would draw PBR meshes' normals in a different space from
				// everything else's, which is exactly the comparison this view exists to make.
				D3DXMATRIX nWorld = *reinterpret_cast<const D3DXMATRIX*>(&render_state.world);
				D3DXMATRIX nView  = *reinterpret_cast<const D3DXMATRIX*>(&render_state.view);
				D3DXMATRIX nWorldView;
				D3DXMatrixMultiply(&nWorldView, &nWorld, &nView);
				Set_Vertex_Shader_Constant(4, &nWorldView, 4);
				Set_Vertex_Shader(m_dwDebugNormalVS);
				Set_Pixel_Shader(m_dwDebugNormalPS);
			} else {
				Debug_Flat_Shade(0xFF606060, fixedFunction);
			}
			break;

		default:
			break;
	}
}

// Make this draw come out one flat colour, whichever pipeline is drawing it.
void DX8Wrapper::Debug_Flat_Shade(unsigned color, bool fixedFunction)
{
	if (!fixedFunction) {
		// Missing shader: leave the draw shaded normally rather than guessing. Half a
		// visualization that silently omits the programmable draws would read as
		// "everything is fixed function", which is the exact wrong conclusion.
		if (m_dwDebugTintPS == 0) return;
		const float inv = 1.0f / 255.0f;
		const float rgba[4] = {
			((color >> 16) & 0xFF) * inv,
			((color >>  8) & 0xFF) * inv,
			( color        & 0xFF) * inv,
			((color >> 24) & 0xFF) * inv
		};
		Set_Pixel_Shader_Constant(DEBUG_TINT_PS_REGISTER, rgba, 1);
		Set_Pixel_Shader(m_dwDebugTintPS);
		return;
	}

	// Fixed function. Stage 0 selects the constant outright and stage 1 is switched
	// off, so no texture, vertex colour or material reaches the result -- if a
	// fixed-function draw were tinted by modulating what it already produced, the ones
	// drawing something nearly black would come out nearly black and the mode would
	// hide precisely the draws it exists to find.
	Set_DX8_Render_State(D3DRS_TEXTUREFACTOR, color);
	Set_DX8_Texture_Stage_State(0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1);
	Set_DX8_Texture_Stage_State(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
	Set_DX8_Texture_Stage_State(0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
	Set_DX8_Texture_Stage_State(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
	Set_DX8_Texture_Stage_State(1, D3DTSS_COLOROP,   D3DTOP_DISABLE);
	Set_DX8_Texture_Stage_State(1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE);
}

void DX8Wrapper::End_Scene(bool flip_frames)
{
	DX8_THREAD_ASSERT();
	GFXCALL(End_Scene());

	// The back buffer is finished and still readable here; one Present later it is not.
	// Cleared before the call so a callback that asks for another one gets the next frame
	// rather than being re-entered on this one.
	if (s_postSceneCallback != nullptr) {
		const PostSceneCallbackFunc func = s_postSceneCallback;
		void* const userData = s_postSceneCallbackData;
		s_postSceneCallback = nullptr;
		s_postSceneCallbackData = nullptr;
		func(userData);
	}

	DX8WebBrowser::Render(0);

#ifdef RTS_DEBUG
	// Measures the debug build, which pays for every per-draw diagnostic below it, so the
	// numbers are not the shipped build's. Kept here anyway because the report is a
	// WWDEBUG_SAY and logging follows RTS_DEBUG: outside this block it would have cost a
	// timestamp and a sort per frame in release and printed nothing at all.
	Debug_Report_Frame_Timing();
	Debug_Check_Mesh_Routing_Split();
	Debug_Report_Routing_Census();
	Debug_Report_FF_Draws();
	Debug_Report_FF_Sites();
	Debug_Audit_Frame_End();
	Debug_Report_Invalidations();
	Debug_Report_Unclassified_Draws();
	Debug_Report_Direct_Draws();
	Debug_Report_Vertex_Layouts();
	Debug_Report_Technique_Check();
	Debug_Report_Particle_Shadows();
	Debug_Report_Alpha_Fog();
	Debug_Report_Lighting();
	SortingRendererClass::Debug_Report_Sorted_Lights();
	GfxDeviceD3D9::Report_Nondynamic_Discards();
	Debug_Report_Shader_Names();
	Debug_Report_Texture_Requirements();
	Mesh_Technique_Report_Registrations();
#endif

	if (flip_frames) {
		DX8_Assert();
		GfxDeviceStatus present;
		{
			WWPROFILE("DX8Device::Present()");
			present=Gfx->Present();
		}

		DX8_RECORD_DX8_CALLS();

		if (present==GFX_DEVICE_OK) {
			IsDeviceLost=false;
			FrameCount++;
		}
		else {
			IsDeviceLost=true;
		}

		// If the device was lost we need to check for cooperative level and possibly reset the device
		if (present==GFX_DEVICE_LOST) {
			if (Gfx->Get_Device_Status()==GFX_DEVICE_NEEDS_RESET) {
				WWDEBUG_SAY(("DX8Wrapper::End_Scene is resetting the device."));
				Reset_Device();
			}
			else {
				// Sleep it not active
				ThreadClass::Sleep_Ms(200);
			}
		}
	}

	// Each frame, release all of the buffers and textures.
	Set_Vertex_Buffer(nullptr);
	Set_Index_Buffer(nullptr,0);
	for (int i=0;i<CurrentCaps->Get_Max_Textures_Per_Pass();++i) Set_Texture(i,nullptr);
	Set_Material(nullptr);
}


void DX8Wrapper::Flip_To_Primary()
{
	// If we are fullscreen and the current frame is odd then we need
	// to force a page flip to ensure that the first buffer in the flipping
	// chain is the one visible.
	if (!IsWindowed) {
		DX8_Assert();

		int numBuffers = (int)(SwapChain.BackBufferCount + 1);
		int visibleBuffer = (FrameCount % numBuffers);
		int flipCount = ((numBuffers - visibleBuffer) % numBuffers);
		int resetAttempts = 0;

		while ((flipCount > 0) && (resetAttempts < 3)) {
			const GfxDeviceStatus status = Gfx->Get_Device_Status();

			if (status != GFX_DEVICE_OK) {
				WWDEBUG_SAY(("TestCooperativeLevel Failed!"));

				if (GFX_DEVICE_LOST == status) {
					IsDeviceLost=true;
					WWDEBUG_SAY(("DEVICELOST: Cannot flip to primary."));
					return;
				}
				IsDeviceLost=false;

				if (GFX_DEVICE_NEEDS_RESET == status) {
					WWDEBUG_SAY(("DEVICENOTRESET"));
					Reset_Device();
					resetAttempts++;
				}
			} else {
				WWDEBUG_SAY(("Flipping: %ld", FrameCount));

				if (Gfx->Present()==GFX_DEVICE_OK) {
					IsDeviceLost=false;
					FrameCount++;
					WWDEBUG_SAY(("Flip to primary succeeded %ld", FrameCount));
				}
				else {
					IsDeviceLost=true;
				}
			}

			--flipCount;
		}
	}
}


//**********************************************************************************************
//! Clear current render device
/*! KM
/* 5/17/02 KM Fixed support for render to texture with depth/stencil buffers
*/
void DX8Wrapper::Clear(bool clear_color, bool clear_z_stencil, const Vector3 &color, float dest_alpha, float z, unsigned int stencil)
{
	DX8_THREAD_ASSERT();

	// If we try to clear a stencil buffer which is not there, the entire call will fail
	// KJM fixed this to get format from back buffer (incase render to texture is used)
	const bool has_stencil = Gfx->Has_Stencil_Target();

	GFXCALL(Clear(clear_color, clear_z_stencil, clear_z_stencil && has_stencil,
		Convert_Color(color,dest_alpha), z, stencil));
}

void DX8Wrapper::Set_Viewport(CONST D3DVIEWPORT8* pViewport)
{
	DX8_THREAD_ASSERT();
	GfxViewport vp;
	vp.X = pViewport->X;
	vp.Y = pViewport->Y;
	vp.Width = pViewport->Width;
	vp.Height = pViewport->Height;
	vp.MinZ = pViewport->MinZ;
	vp.MaxZ = pViewport->MaxZ;
	GFXCALL(Set_Viewport(vp));
}

// ----------------------------------------------------------------------------
//
// Set vertex buffer. A reference to previous vertex buffer is released and
// this one is assigned the current vertex buffer. The DX8 vertex buffer will
// actually be set in Apply() which is called by Draw_Indexed_Triangles().
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Set_Vertex_Buffer(const VertexBufferClass* vb, unsigned stream)
{
	render_state.vba_offset=0;
	render_state.vba_count=0;
	if (render_state.vertex_buffers[stream]) {
		render_state.vertex_buffers[stream]->Release_Engine_Ref();
	}
	REF_PTR_SET(render_state.vertex_buffers[stream],const_cast<VertexBufferClass*>(vb));
	if (vb) {
		vb->Add_Engine_Ref();
		render_state.vertex_buffer_types[stream]=vb->Type();
	}
	else {
		render_state.vertex_buffer_types[stream]=BUFFER_TYPE_INVALID;
	}
	render_state_changed|=VERTEX_BUFFER_CHANGED;
}

// ----------------------------------------------------------------------------
//
// Set index buffer. A reference to previous index buffer is released and
// this one is assigned the current index buffer. The DX8 index buffer will
// actually be set in Apply() which is called by Draw_Indexed_Triangles().
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Set_Index_Buffer(const IndexBufferClass* ib,unsigned short index_base_offset)
{
	render_state.iba_offset=0;
	if (render_state.index_buffer) {
		render_state.index_buffer->Release_Engine_Ref();
	}
	REF_PTR_SET(render_state.index_buffer,const_cast<IndexBufferClass*>(ib));
	render_state.index_base_offset=index_base_offset;
	if (ib) {
		ib->Add_Engine_Ref();
		render_state.index_buffer_type=ib->Type();
	}
	else {
		render_state.index_buffer_type=BUFFER_TYPE_INVALID;
	}
	render_state_changed|=INDEX_BUFFER_CHANGED;
}

// ----------------------------------------------------------------------------
//
// Set vertex buffer using dynamic access object.
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Set_Vertex_Buffer(const DynamicVBAccessClass& vba_)
{
	// Release all streams (only one stream allowed in the legacy pipeline)
	for (int i=1;i<MAX_VERTEX_STREAMS;++i) {
		DX8Wrapper::Set_Vertex_Buffer(nullptr, i);
	}

	if (render_state.vertex_buffers[0]) render_state.vertex_buffers[0]->Release_Engine_Ref();
	DynamicVBAccessClass& vba=const_cast<DynamicVBAccessClass&>(vba_);
	render_state.vertex_buffer_types[0]=vba.Get_Type();
	render_state.vba_offset=vba.VertexBufferOffset;
	render_state.vba_count=vba.Get_Vertex_Count();
	REF_PTR_SET(render_state.vertex_buffers[0],vba.VertexBuffer);
	render_state.vertex_buffers[0]->Add_Engine_Ref();
	render_state_changed|=VERTEX_BUFFER_CHANGED;
	render_state_changed|=INDEX_BUFFER_CHANGED;		// vba_offset changes so index buffer needs to be reset as well.
}

// ----------------------------------------------------------------------------
//
// Set index buffer using dynamic access object.
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Set_Index_Buffer(const DynamicIBAccessClass& iba_,unsigned short index_base_offset)
{
	if (render_state.index_buffer) render_state.index_buffer->Release_Engine_Ref();

	DynamicIBAccessClass& iba=const_cast<DynamicIBAccessClass&>(iba_);
	render_state.index_base_offset=index_base_offset;
	render_state.index_buffer_type=iba.Get_Type();
	render_state.iba_offset=iba.IndexBufferOffset;
	REF_PTR_SET(render_state.index_buffer,iba.IndexBuffer);
	render_state.index_buffer->Add_Engine_Ref();
	render_state_changed|=INDEX_BUFFER_CHANGED;
}

// ----------------------------------------------------------------------------
//
// Private function for the special case of rendering polygons from sorting
// index and vertex buffers.
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Draw_Sorting_IB_VB(
	unsigned primitive_type,
	unsigned short start_index,
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	WWASSERT(render_state.vertex_buffer_types[0]==BUFFER_TYPE_SORTING || render_state.vertex_buffer_types[0]==BUFFER_TYPE_DYNAMIC_SORTING);
	WWASSERT(render_state.index_buffer_type==BUFFER_TYPE_SORTING || render_state.index_buffer_type==BUFFER_TYPE_DYNAMIC_SORTING);

	// Fill dynamic vertex buffer with sorting vertex buffer vertices
	DynamicVBAccessClass dyn_vb_access(BUFFER_TYPE_DYNAMIC_DX8,dynamic_fvf_type,vertex_count);
	{
		DynamicVBAccessClass::WriteLockClass lock(&dyn_vb_access);
		VertexFormatXYZNDUV2* src = static_cast<SortingVertexBufferClass*>(render_state.vertex_buffers[0])->VertexBuffer;
		VertexFormatXYZNDUV2* dest= lock.Get_Formatted_Vertex_Array();
		src += render_state.vba_offset + render_state.index_base_offset + min_vertex_index;
		unsigned  size = dyn_vb_access.FVF_Info().Get_FVF_Size()*vertex_count/sizeof(unsigned);
		unsigned *dest_u =(unsigned*) dest;
		unsigned *src_u = (unsigned*) src;

		for (unsigned i=0;i<size;++i) {
			*dest_u++=*src_u++;
		}
	}

	GFXCALL(Set_Vertex_Stream(
		0,
		(GfxVertexBuffer*)static_cast<DX8VertexBufferClass*>(dyn_vb_access.VertexBuffer)->Get_DX8_Vertex_Buffer(),
		dyn_vb_access.FVF_Info().Get_FVF_Size()));
	// If using FVF format VB, set the FVF as vertex shader (may not be needed here KM)
	unsigned fvf=dyn_vb_access.FVF_Info().Get_FVF();
	if (fvf!=0) {
		// The declaration has to describe the buffer just filled, but setting an FVF also
		// unbinds the vertex shader -- and Apply_Render_State_Changes chose one a moment
		// ago, since the depth pass binds its packing shader for every caster. Losing it
		// here would put raw fixed-function colour in the middle of a depth map. Set the
		// declaration, then put the shader straight back; the two are independent in D3D9.
		const DWORD boundVS = Vertex_Shader;
		Set_Vertex_Shader(fvf);
		if (boundVS >= 0x10000) {
			Set_Vertex_Shader(boundVS);
		}
	}
	DX8_RECORD_VERTEX_BUFFER_CHANGE();

	unsigned index_count=0;
	switch (primitive_type) {
	case D3DPT_TRIANGLELIST: index_count=polygon_count*3; break;
	case D3DPT_TRIANGLESTRIP: index_count=polygon_count+2; break;
	case D3DPT_TRIANGLEFAN: index_count=polygon_count+2; break;
	default: WWASSERT(0); break; // Unsupported primitive type
	}

	// Fill dynamic index buffer with sorting index buffer vertices
	DynamicIBAccessClass dyn_ib_access(BUFFER_TYPE_DYNAMIC_DX8,index_count);
	{
		DynamicIBAccessClass::WriteLockClass lock(&dyn_ib_access);
		unsigned short* dest=lock.Get_Index_Array();
		unsigned short* src=nullptr;
		src=static_cast<SortingIndexBufferClass*>(render_state.index_buffer)->index_buffer;
		src+=render_state.iba_offset+start_index;

		for (unsigned short i=0;i<index_count;++i) {
			unsigned short index=*src++;
			index-=min_vertex_index;
			WWASSERT(index<vertex_count);
			*dest++=index;
		}
	}

	GFXCALL(Set_Index_Buffer(
		(GfxIndexBuffer*)static_cast<DX8IndexBufferClass*>(dyn_ib_access.IndexBuffer)->Get_DX8_Index_Buffer(),
		dyn_vb_access.VertexBufferOffset));
	DX8_RECORD_INDEX_BUFFER_CHANGE();

	DX8_RECORD_DRAW_CALLS();
	GFXCALL(Draw_Indexed(
		D3DPT_TRIANGLELIST,
		dyn_vb_access.VertexBufferOffset,
		0,		// min vertex index
		vertex_count,
		dyn_ib_access.IndexBufferOffset,
		polygon_count));

	DX8_RECORD_RENDER(polygon_count,vertex_count,render_state.shader);
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Draw(
	unsigned primitive_type,
	unsigned short start_index,
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	if (DrawPolygonLowBoundLimit && DrawPolygonLowBoundLimit>=polygon_count) return;

	DX8_THREAD_ASSERT();
	SNAPSHOT_SAY(("DX8 - draw"));

#ifdef RTS_DEBUG
	// Tell the attribution below that this application of state is actually about to draw
	// something. Apply_Render_State_Changes is public and called from about thirty places
	// that are only tidying up -- unbinding textures, restoring a shader -- and counting
	// those as fixed-function draws inflated the remaining work with entries that render
	// nothing. Every "group" of exactly 559 per 600-frame window was one renderer's
	// end-of-frame cleanup, which is why naming their draw functions never moved them.
	//
	// The bias this leaves, stated because the number is meaningless without it: callers
	// that apply state and then issue DrawIndexedPrimitive/DrawPrimitiveUP against the
	// device themselves are real draws and are no longer counted, because their Apply does
	// not come through here. That is the volumetric shadows, the scene overlay quad, the
	// water grid mesh, the screen filters and the smudge pass. This census now reads
	// "fixed-function draws submitted through DX8Wrapper::Draw", not "all of them".
	s_applyIsDraw = true;
#endif

	// A drawer that went straight to the device has bound its own stream, indices and FVF
	// since the wrapper last bound anything -- see Prepare_Direct_Draw, which is where the
	// three ways that goes wrong are written down. This draw is about to use those
	// bindings, so ask for ours back. Here rather than in Prepare_Direct_Draw because this
	// is the point at which a draw actually depends on them.
	if (m_bForeignDeviceBindings) {
		m_bForeignDeviceBindings = false;
#ifdef RTS_DEBUG
		// Measured here and not further down, because the repair on the next line is what
		// there would otherwise be to measure. This says what this draw *would* have used.
		{
			const int expectedBase = (int)(render_state.index_base_offset + render_state.vba_offset);
			bool streamWrong = false;
			if (render_state.vertex_buffers[0] != nullptr &&
				(render_state.vertex_buffer_types[0] == BUFFER_TYPE_DX8 ||
				 render_state.vertex_buffer_types[0] == BUFFER_TYPE_DYNAMIC_DX8)) {
				GfxVertexBuffer * bound = nullptr;
				unsigned offset = 0, stride = 0;
				if (Gfx->Get_Vertex_Stream(0, &bound, &offset, &stride)) {
					streamWrong = (bound != static_cast<DX8VertexBufferClass*>(
						render_state.vertex_buffers[0])->Get_DX8_Vertex_Buffer());
					if (bound) Gfx->Release_Vertex_Buffer(bound);
				}
			}
			Debug_Note_Foreign_Bindings(expectedBase, (int)g_D3D9_BaseVertexIndex, streamWrong);
		}
#endif
		render_state_changed |= (unsigned)VERTEX_BUFFER_CHANGED | (unsigned)INDEX_BUFFER_CHANGED;
	}

#ifdef RTS_DEBUG
	// Read here rather than at the top of the function: the foreign-binding repair above
	// raises two of these bits itself, and when it does the routing block really does run
	// and really does decide. What this has to distinguish is a draw the routing block saw
	// from one it returned early on -- Apply clears the word, so it cannot be read after.
	s_ffDrawRoutingRan = (render_state_changed != 0);
#endif
	Apply_Render_State_Changes();
#ifdef RTS_DEBUG
	s_applyIsDraw = false;
#endif

	// Debug feature to disable triangle drawing...
	if (!_Is_Triangle_Draw_Enabled()) return;

	// A depth-pass draw that the depth shaders did not claim cannot affect the shadow
	// map: Apply_Render_State_Changes has masked off both colour and depth writes for
	// it, and those are the only two things this render target records. Submitting it
	// spends a draw call, a state validation and a pass over its triangles to produce
	// nothing. So do not.
	//

#ifdef RTS_DEBUG
	// Alpha test and fog, counted for every draw regardless of which pipeline claimed
	// it -- both are hardware stages downstream of the shader, so a routed draw is
	// exactly as exposed to them as a fixed-function one.
	//
	// Counted here rather than inside Apply_Render_State_Changes, which is where the
	// other per-draw censuses live, because that function returns early on
	// !render_state_changed. A caller that applies its own state and then draws --
	// W3DTreeBuffer does, three times -- hits that early return on the way through
	// Draw and never reaches the body. The first version of this census sat there and
	// reported no tree draw at all, on a frame that drew 6400 tree triangles.
	Debug_Note_Alpha_Fog_Draw();
#endif

	// The alpha test, for the shaders that perform it themselves. See alphatest.hlsli
	// for the encoding and for why a disabled test is a cutoff that discards nothing
	// rather than a separate shader permutation.
	//
	// Uploaded here rather than in ShaderClass::Apply, which is where the render states
	// it mirrors are written, for three reasons that each on their own would be enough.
	// Apply runs only when the shader bits actually changed. The reference is written
	// from outside it as well -- dx8renderer.cpp scales it by the mesh's alpha override,
	// after Apply has already run. And a caller that applies its own state and then
	// draws skips the body of Apply_Render_State_Changes altogether, which is exactly
	// how the first version of the census came to report no tree draws at all.
	//
	// Reading the tracked states at the draw makes every writer of them correct by
	// construction, with no call-site enumeration to keep up to date. The cost is a
	// 16-byte memcmp per draw on the unchanged path -- Set_Pixel_Shader_Constant's
	// shadow cache suppresses the device call.
	{
		const bool alphaOn = RenderStates[D3DRS_ALPHATESTENABLE] != 0;
		const unsigned func = RenderStates[D3DRS_ALPHAFUNC];
		// Anything this encoding cannot express -- NOTEQUAL, and the compares nothing
		// sets -- falls through as "discard nothing". There is no longer a hardware
		// stage behind it to catch that: the three alpha words stop at the tracked
		// array now. The only writer of an inexpressible compare is W3DWater's
		// WATER_TYPE_1_FB_REFLECTION path, which no shipped map can select (WaterType
		// = 0 everywhere), and the census over gla_midgame and civ_buildings has never
		// once shown a group that is not GREATEREQUAL at 0x60. If one ever appears,
		// this is the constant that has to grow a mode.
		const bool expressible = alphaOn &&
			(func == D3DCMP_GREATEREQUAL || func == D3DCMP_LESSEQUAL);
		Vector4 alphaTestCtl(0.0f, 1.0f, 0.0f, 0.0f);
		if (expressible) {
			alphaTestCtl.X = (float)RenderStates[D3DRS_ALPHAREF] * (1.0f / 255.0f);
			alphaTestCtl.Y = (func == D3DCMP_GREATEREQUAL) ? 1.0f : -1.0f;
		}
		Set_Pixel_Shader_Constant((int)ALPHA_TEST_PS_CONSTANT, &alphaTestCtl, 1);
	}

	// The one place fixed-function state still has to reach the device: a draw with no
	// pixel shader on it is a fixed-function draw, and it renders from the combine, the
	// texgen, the lighting and the material its caller asked for -- all of which have
	// been accumulating in the tracked arrays without being sent. Send them now.
	//
	// Tested on the bound shaders rather than on the routing block's own verdict, because
	// they are what the device will actually use and cannot disagree with themselves.
	// The same reasoning corrected the suppression test above, in the other direction:
	// there the bound shader was the wrong question because nothing unbinds it. Here it is
	// the right one, because binding is exactly what did or did not happen.
	//
	// Both halves of the pipeline are asked, and they fail differently. No pixel shader
	// means the combine decides the colour. A vertex shader below 0x10000 is not a shader
	// at all but an FVF -- Set_Vertex_Shader overloads the word -- so vertex processing is
	// fixed function, and the lighting, the material and the texgen all still matter even
	// though a pixel shader may be bound over the top of it.
	//
	// Costs a predictable branch per draw on the path where nothing is pending, which is
	// every draw in a frame that has no fixed-function geometry left in it.
	if (Is_Fixed_Function_Draw() && Has_Pending_Fixed_Function_State()) {
		Flush_Fixed_Function_State();
	}

#ifdef RTS_DEBUG
	// After the flush, so what the census reads is what the device will draw with.
	Debug_Note_Lighting_Draw();
	{
		// Same identity the fixed-function draw census uses, so the two tables name the
		// same drawers. Is_Inert_Depth_Pass_Draw is asked here rather than inferred later:
		// a draw that is dropped below needs no input layout, and 53428 dropped depth-pass
		// draws would otherwise sit at the top of a table meant to size a backend's work.
		const char* who = s_declarationSite;
		if (who == nullptr) who = Debug_Current_Pass_Name();
		Debug_Note_Vertex_Layout(who, false, !Is_Inert_Depth_Pass_Draw());
	}
#endif

#ifdef MESH_RENDER_SNAPSHOT_ENABLED
	if (WW3D::Is_Snapshot_Activated()) {
		unsigned passes=0;
		if (Gfx->Validate_Draw_State(passes)) {
			SNAPSHOT_SAY(("ValidateDevice: OK, %u passes",passes));
		} else {
			SNAPSHOT_SAY(("ValidateDevice: this state cannot be drawn"));
		}
	}
#endif	// MESH_RENDER_SNAPSHOT_ENABLED


	// Here, and not one line higher. A depth-pass draw the depth shaders declined writes
	// nothing -- the census says all of them hold a zero colour mask, depth writes off and
	// stencil off, and those are the only three things that render target records -- so the
	// submission below is a draw call, a state validation and a pass over its triangles
	// spent on producing nothing. Skipping it is free: measured on civ_buildings frames 540
	// and 900, 0 differing pixels.
	//
	// But everything above this line still has to run, and that was not free. Moving this
	// return up past Flush_Fixed_Function_State -- which looks obviously right, since a draw
	// that is not submitted cannot need state -- changed 20485 pixels across the same two
	// frames. The declined draws are the only fixed-function draws left in the frame, so
	// they are the only thing that ever flushes the accumulated D3DTSS combine words, and
	// `scorches` and `terrainTracks` draw with what they left behind. Bisected: with the
	// return above the flush, 0 words reach the device and the 20485 pixels move; with it
	// here, 10680 words reach it and the frame is identical.
	//
	// So this is not a draw whose state is inert, only one whose *rasterisation* is. The
	// fixed-function burn-down reads those 1391 words as the residue of no-op draws; they
	// are load-bearing, and the drawer that depends on them does not appear in the census
	// that counts them, because it is device state and outlives the draw that carried it.
	if (Is_Inert_Depth_Pass_Draw()) {
#ifdef RTS_DEBUG
		Debug_Note_Unsubmitted_Draw();
#endif
		return;
	}

	SNAPSHOT_SAY(("DX8 - draw %d polygons (%d vertices)",polygon_count,vertex_count));

	if (vertex_count<3) {
		min_vertex_index=0;
		switch (render_state.vertex_buffer_types[0]) {
		case BUFFER_TYPE_DX8:
		case BUFFER_TYPE_SORTING:
			vertex_count=render_state.vertex_buffers[0]->Get_Vertex_Count()-render_state.index_base_offset-render_state.vba_offset-min_vertex_index;
			break;
		case BUFFER_TYPE_DYNAMIC_DX8:
		case BUFFER_TYPE_DYNAMIC_SORTING:
			vertex_count=render_state.vba_count;
			break;
		}
	}

	switch (render_state.vertex_buffer_types[0]) {
	case BUFFER_TYPE_DX8:
	case BUFFER_TYPE_DYNAMIC_DX8:
		switch (render_state.index_buffer_type) {
		case BUFFER_TYPE_DX8:
		case BUFFER_TYPE_DYNAMIC_DX8:
			{
/*				if ((start_index+render_state.iba_offset+polygon_count*3) > render_state.index_buffer->Get_Index_Count())
				{	WWASSERT_PRINT(0,"OVERFLOWING INDEX BUFFER");
					///@todo: MUST FIND OUT WHY THIS HAPPENS WITH LOTS OF PARTICLES ON BIG FIGHT!  -MW
					break;
				}*/
				// State the base vertex index for this draw rather than inheriting one.
				//
				// In D3D8 the base was device state, set by SetIndices; in D3D9 it is an
				// argument to DrawIndexedPrimitive. The compatibility layer bridges the two by
				// stashing it in a global at SetIndices time and passing that global to every
				// draw afterwards (see d3d9_compat.h), which is faithful for a caller that sets
				// its indices and then immediately draws -- and every drawer that goes straight
				// to the device is one of those.
				//
				// This path is not. Apply_Render_State_Changes only re-issues SetIndices when
				// INDEX_BUFFER_CHANGED, so a run of draws sharing an index buffer -- each bridge
				// in W3DBridgeBuffer::drawBridges, each polygon renderer in an FVF category --
				// re-establishes the base on the first draw and inherits it thereafter. What it
				// inherits is whichever value was written last, and the direct drawers write
				// their own: the projected-shadow decals set it to nShadowDecalStartBatchVertex,
				// which climbs towards 32768 across a frame as shadows are queued; the
				// volumetric shadows set a buffer-manager slot start; the water grid sets its
				// vertex buffer offset. A base that belongs to somebody else is added to every
				// index in this draw, so it reads vertices from elsewhere in the buffer, or
				// past its end -- and triangles built from unrelated positions rasterise as
				// long thin slivers reaching across the screen.
				//
				// Which draws are exposed therefore depends on submission order, which depends
				// on what is in view: it changes as the camera turns and it does not reproduce.
				//
				// Assigning the global is the whole of the repair and costs nothing, because in
				// D3D9 no device state holds this -- it is read straight into the draw call
				// below. This is exactly the value Apply_Render_State_Changes passes to
				// SetIndices when it does re-issue it.
				DX8_RECORD_RENDER(polygon_count,vertex_count,render_state.shader);
				DX8_RECORD_DRAW_CALLS();
				GFXCALL(Draw_Indexed(
					primitive_type,
					(int)(render_state.index_base_offset + render_state.vba_offset),
					min_vertex_index,
					vertex_count,
					start_index+render_state.iba_offset,
					polygon_count));
			}
			break;
		case BUFFER_TYPE_SORTING:
		case BUFFER_TYPE_DYNAMIC_SORTING:
			WWASSERT_PRINT(0,"VB and IB must of same type (sorting or dx8)");
			break;
		case BUFFER_TYPE_INVALID:
			WWASSERT(0);
			break;
		}
		break;
	case BUFFER_TYPE_SORTING:
	case BUFFER_TYPE_DYNAMIC_SORTING:
		switch (render_state.index_buffer_type) {
		case BUFFER_TYPE_DX8:
		case BUFFER_TYPE_DYNAMIC_DX8:
			WWASSERT_PRINT(0,"VB and IB must of same type (sorting or dx8)");
			break;
		case BUFFER_TYPE_SORTING:
		case BUFFER_TYPE_DYNAMIC_SORTING:
			Draw_Sorting_IB_VB(primitive_type,start_index,polygon_count,min_vertex_index,vertex_count);
			break;
		case BUFFER_TYPE_INVALID:
			WWASSERT(0);
			break;
		}
		break;
	case BUFFER_TYPE_INVALID:
		WWASSERT(0);
		break;
	}
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Draw_Triangles(
	unsigned buffer_type,
	unsigned short start_index,
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	if (buffer_type==BUFFER_TYPE_SORTING || buffer_type==BUFFER_TYPE_DYNAMIC_SORTING) {
		// Sorted geometry is drawn where it stands in the depth pass rather than deferred.
		// This call does not draw anything on its own -- it hands the triangles to the
		// sorting renderer, which replays them at its next flush -- and a depth pass has
		// nothing to gain by waiting: it writes nearest-wins depth, so blend order is
		// meaningless, while the wait costs a second traversal's worth of nodes out of the
		// buffer the visible frame's own sorted draws need. Draw_Sorting_IB_VB copies
		// straight out of the sorting buffers, which is all this needs.
		if (m_bShadowDepthPass) {
			Draw(D3DPT_TRIANGLELIST,start_index,polygon_count,min_vertex_index,vertex_count);
			return;
		}
		SortingRendererClass::Insert_Triangles(start_index,polygon_count,min_vertex_index,vertex_count);
	}
	else {
		Draw(D3DPT_TRIANGLELIST,start_index,polygon_count,min_vertex_index,vertex_count);
	}
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Draw_Triangles(
	unsigned short start_index,
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	Draw(D3DPT_TRIANGLELIST,start_index,polygon_count,min_vertex_index,vertex_count);
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Draw_Strip(
	unsigned short start_index,
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	Draw(D3DPT_TRIANGLESTRIP,start_index,polygon_count,min_vertex_index,vertex_count);
}

// ----------------------------------------------------------------------------
// Map a fixed-function texture-stage argument (D3DTSS_COLORARGn / ALPHAARGn) to the
// source selector the unit detail pixel shader uses: (texture, current, diffuse).
// Returns false for sources the shader does not carry, and for the COMPLEMENT /
// ALPHAREPLICATE modifiers -- those draws stay on the fixed-function path rather than
// being reproduced approximately.
// ----------------------------------------------------------------------------
static bool Map_Texture_Stage_Arg(DWORD arg, D3DXVECTOR4& selector)
{
	if ((arg & ~(DWORD)D3DTA_SELECTMASK) != 0) {
		return false;   // COMPLEMENT / ALPHAREPLICATE not reproduced
	}
	switch (arg & D3DTA_SELECTMASK) {
		case D3DTA_TEXTURE: selector = D3DXVECTOR4(1.0f, 0.0f, 0.0f, 0.0f); return true;
		case D3DTA_CURRENT: selector = D3DXVECTOR4(0.0f, 1.0f, 0.0f, 0.0f); return true;
		case D3DTA_DIFFUSE: selector = D3DXVECTOR4(0.0f, 0.0f, 1.0f, 0.0f); return true;
		default:            return false;
	}
}

// ----------------------------------------------------------------------------
// Classify a texture stage's coordinate source for the unit vertex shader. Returns
// false for generation modes the shader does not implement, so those draws stay on the
// fixed-function path. The mode values match Select_TexGen_Source in unit_vs_body.hlsli.
// ----------------------------------------------------------------------------
// uvSetCount is how many coordinate sets the vertex format in the stream actually
// supplies. It is not a formality: the mode this returns decides which input register
// the vertex shader reads, and a shader may only read what the declaration provides.
// Asking for set 1 of a one-set format is undefined, and undefined here would look
// like a detail texture wrapped by plausible-but-wrong coordinates rather than like
// an error -- so the format, not the stage state, has the last word.
static bool Map_Texture_Coord_Source(DWORD coordIndex, DWORD transformFlags,
									 unsigned uvSetCount, float& mode, bool& usesMatrix)
{
	switch (coordIndex & 0xFFFF0000u) {
		case D3DTSS_TCI_PASSTHRU:
			switch (coordIndex & 0x0000FFFFu) {
				case 0: mode = 0.0f; break;
				// The mesh's second set, carried by unit_uv2_vs. Every sorted draw is
				// built on dynamic_fvf_type, which is D3DFVF_TEX2, so the passes that
				// want this have the data -- what they lacked was a shader declaring it.
				case 1: if (uvSetCount < 2) return false; mode = 4.0f; break;
				default: return false;   // no shader carries a third set
			}
			break;
		case D3DTSS_TCI_CAMERASPACEPOSITION:         mode = 1.0f; break;
		case D3DTSS_TCI_CAMERASPACENORMAL:           mode = 2.0f; break;
		case D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR: mode = 3.0f; break;
		default: return false;
	}

	// Only the plain two-component transform is reproduced; projected and three
	// component transforms would need the divide and a third output.
	const DWORD count = transformFlags & ~(DWORD)D3DTTFF_PROJECTED;
	if (transformFlags & D3DTTFF_PROJECTED) return false;
	if (count == D3DTTFF_DISABLE)      { usesMatrix = false; return true; }
	if (count == D3DTTFF_COUNT2)       { usesMatrix = true;  return true; }
	return false;
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

void DX8Wrapper::Apply_Render_State_Changes()
{
	SNAPSHOT_SAY(("DX8Wrapper::Apply_Render_State_Changes()"));

	if (!render_state_changed) return;
	if (render_state_changed&SHADER_CHANGED) {
		SNAPSHOT_SAY(("DX8 - apply shader"));
		render_state.shader.Apply();
	}

	unsigned mask=TEXTURE0_CHANGED;
	int i=0;
	for (;i<CurrentCaps->Get_Max_Textures_Per_Pass();++i,mask<<=1)
	{
		if (render_state_changed&mask)
		{
			SNAPSHOT_SAY(("DX8 - apply texture %d (%s)",i,render_state.Textures[i] ? render_state.Textures[i]->Get_Full_Path().str() : "null"));

			if (render_state.Textures[i])
			{
				render_state.Textures[i]->Apply(i);
			}
			else
			{
				TextureBaseClass::Apply_Null(i);
			}
		}
	}

	if (render_state_changed&MATERIAL_CHANGED)
	{
		SNAPSHOT_SAY(("DX8 - apply material"));
		VertexMaterialClass* material=const_cast<VertexMaterialClass*>(render_state.material);
		if (material)
		{
			material->Apply();
		}
		else VertexMaterialClass::Apply_Null();
	}

	// The LIGHTS_CHANGED flag is still raised and still read, but nothing is sent: what
	// used to be here pushed a D3DLIGHT8 at the device's transform-and-lighting stage, and
	// that stage draws nothing. Over civ_buildings in both shadow configurations, no draw
	// -- of 914163 on the shadow map, 493228 on the volumes -- reaches the device with
	// D3DRS_LIGHTING enabled, and the word reads FALSE off D3D at the end of every window.
	// The 876773 SetLight/LightEnable calls a 600-frame window was making could not reach a
	// pixel.
	//
	// The flag has to stay up. Apply_Render_State_Changes returns early when nothing
	// changed, and the routing block inside it is what reads render_state.Lights into the
	// vertex shader's LightDir/LightDiffuse constants -- so a frame where only the lighting
	// moved must still get here, or the shaders keep the previous frame's sun.

	if (render_state_changed&WORLD_CHANGED) {
		SNAPSHOT_SAY(("DX8 - apply world matrix"));
		_Set_DX8_Transform(D3DTS_WORLD,render_state.world);
	}
	if (render_state_changed&VIEW_CHANGED) {
		SNAPSHOT_SAY(("DX8 - apply view matrix"));
		_Set_DX8_Transform(D3DTS_VIEW,render_state.view);
	}
	if (render_state_changed&VERTEX_BUFFER_CHANGED) {
		SNAPSHOT_SAY(("DX8 - apply vb change"));
		for (i=0;i<MAX_VERTEX_STREAMS;++i) {
			if (render_state.vertex_buffers[i]) {
				switch (render_state.vertex_buffer_types[i]) {//->Type()) {
				case BUFFER_TYPE_DX8:
				case BUFFER_TYPE_DYNAMIC_DX8:
					GFXCALL(Set_Vertex_Stream(
						i,
						(GfxVertexBuffer*)static_cast<DX8VertexBufferClass*>(render_state.vertex_buffers[i])->Get_DX8_Vertex_Buffer(),
						render_state.vertex_buffers[i]->FVF_Info().Get_FVF_Size()));
					DX8_RECORD_VERTEX_BUFFER_CHANGE();
					{
						// If the VB format is FVF, set the FVF as a vertex shader
						unsigned fvf=render_state.vertex_buffers[i]->FVF_Info().Get_FVF();
						if (fvf!=0) {
							Set_Vertex_Shader(fvf);
						}
					}
					break;
				case BUFFER_TYPE_SORTING:
				case BUFFER_TYPE_DYNAMIC_SORTING:
					break;
				default:
					WWASSERT(0);
				}
			} else {
				GFXCALL(Set_Vertex_Stream(i,nullptr,0));
				DX8_RECORD_VERTEX_BUFFER_CHANGE();
			}
		}
	}
	if (render_state_changed&INDEX_BUFFER_CHANGED) {
		SNAPSHOT_SAY(("DX8 - apply ib change"));
		if (render_state.index_buffer) {
			switch (render_state.index_buffer_type) {//->Type()) {
			case BUFFER_TYPE_DX8:
			case BUFFER_TYPE_DYNAMIC_DX8:
				GFXCALL(Set_Index_Buffer(
					(GfxIndexBuffer*)static_cast<DX8IndexBufferClass*>(render_state.index_buffer)->Get_DX8_Index_Buffer(),
					render_state.index_base_offset+render_state.vba_offset));
				DX8_RECORD_INDEX_BUFFER_CHANGE();
				break;
			case BUFFER_TYPE_SORTING:
			case BUFFER_TYPE_DYNAMIC_SORTING:
				break;
			default:
				WWASSERT(0);
			}
		}
		else {
			GFXCALL(Set_Index_Buffer(
				nullptr,
				0));
			DX8_RECORD_INDEX_BUFFER_CHANGE();
		}
	}

	// --- Programmable object render path -----------------------------------
	// Route any lit, textured 3D mesh (FVF with XYZ position + NORMAL and a base
	// texture) through the HLSL unit shader in place of the fixed-function
	// transform & lighting. The mesh's FVF doubles as the vertex declaration, so a
	// single shader handles every vertex format.
	//
	// The decision reads the *current* draw's vertex buffer directly.
	//
	// Sorting buffers used to be excluded here, reported as having no FVF so they could
	// never be mistaken for a 3D mesh -- a conservative choice from when 2D menu textures
	// were being corrupted, and one that stopped being true of the code it described. A
	// SortingVertexBufferClass is built with dynamic_fvf_type and Draw_Sorting_IB_VB
	// copies it into a dynamic DX8 buffer of that same format before drawing it, so the
	// vertex format at the moment the triangles are rasterised is known exactly. Reporting
	// zero here did not make those draws safe, it made them invisible to the routing: 35k
	// draws a window -- sorted glows, beam segments, building light fixtures -- declined
	// on a position test for a position they demonstrably have.
	//
	// 2D is still excluded, by the identity-view test below, which is what was actually
	// keeping the menus intact.
	{
		DWORD curFVF = 0;
		if (render_state.vertex_buffers[0] != nullptr) {
			switch (render_state.vertex_buffer_types[0]) {
			case BUFFER_TYPE_DX8:
			case BUFFER_TYPE_DYNAMIC_DX8:
			case BUFFER_TYPE_SORTING:
			case BUFFER_TYPE_DYNAMIC_SORTING:
				curFVF = render_state.vertex_buffers[0]->FVF_Info().Get_FVF();
				break;
			default:
				break;
			}
		}

		// The dynamic vertex format used for 2D UI (Render2DClass) also carries a
		// NORMAL, so FVF alone can't tell menus from 3D meshes. 2D/UI draws render
		// with an identity view (Set_View_Identity); real 3D meshes use the camera
		// view. Require a non-identity view so the menu path is excluded.
		// Terrain tiles are drawn with a HeightMap-set flag (they have no NORMAL --
		// lighting is baked into the vertex colour -- so they take a dedicated shader).
		// Fixed-function texture-coordinate generation (D3DTSS_TCI_CAMERASPACE*, used by
		// the shroud's camera-space projection and similar effects) has no equivalent in
		// these vertex shaders, which only pass the mesh UVs through. When a texgen is
		// active on stage 0 the generated coordinates would be lost and the projection
		// swims with the camera, so such draws must stay on the fixed-function pipeline.
		// The TCI_* selector lives in the high 16 bits of D3DTSS_TEXCOORDINDEX; match the
		// camera-space values explicitly so the 0x12345678 "invalidated" sentinel that
		// Invalidate_Cached_Render_States writes is not mistaken for a texgen.
		const DWORD texCoordGen = TextureStageStates[0][D3DTSS_TEXCOORDINDEX] & 0xFFFF0000u;
		// A projected texture transform (projected shadows project their shadow map
		// onto the receiver this way) is another coordinate generation this shader
		// can't reproduce. The TTF value encodes a count in the low bits plus a
		// PROJECTED flag; the 0x12345678 invalidation sentinel matches neither, so
		// normal meshes (DISABLE / sentinel) are not affected.
		const DWORD ttf = TextureStageStates[0][D3DTSS_TEXTURETRANSFORMFLAGS];
		const bool texTransformActive =
			((ttf & 0xFFu) >= (DWORD)D3DTTFF_COUNT1 && (ttf & 0xFFu) <= (DWORD)D3DTTFF_COUNT4) ||
			(ttf & (DWORD)D3DTTFF_PROJECTED) != 0;
		// Stage 1 generates coordinates too -- the cloud layer a bridge draws over itself
		// projects from camera space exactly as the shroud does -- and a generation this
		// gate does not see is a generation the shader is never told to reproduce, leaving
		// that stage sampled through the mesh's own UVs. Only meaningful when stage 1
		// actually carries a texture: otherwise the state is whatever a previous draw left
		// behind, and treating that as a texgen would push ordinary single-texture meshes
		// off the programmable path for no reason. Measured over a full replay, this
		// catches four draws, all of them the cloud projected onto bridge and road decks.
		const DWORD texCoordGen1 = TextureStageStates[1][D3DTSS_TEXCOORDINDEX] & 0xFFFF0000u;
		const DWORD ttf1 = TextureStageStates[1][D3DTSS_TEXTURETRANSFORMFLAGS];
		const bool stage1Texgen =
			render_state.Textures[1] != nullptr &&
			(texCoordGen1 == D3DTSS_TCI_CAMERASPACENORMAL ||
			 texCoordGen1 == D3DTSS_TCI_CAMERASPACEPOSITION ||
			 texCoordGen1 == D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR ||
			 ((ttf1 & 0xFFu) >= (DWORD)D3DTTFF_COUNT1 && (ttf1 & 0xFFu) <= (DWORD)D3DTTFF_COUNT4) ||
			 (ttf1 & (DWORD)D3DTTFF_PROJECTED) != 0);
		const bool texgenActive =
			texCoordGen == D3DTSS_TCI_CAMERASPACENORMAL ||
			texCoordGen == D3DTSS_TCI_CAMERASPACEPOSITION ||
			texCoordGen == D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR ||
			texTransformActive ||
			stage1Texgen;

		// Texture coordinate generation for both stages. The vertex shader can produce
		// camera-space coordinates and apply the stage's texture matrix, so a draw that
		// uses them no longer has to stay on the fixed-function pipeline -- which matters
		// because these are exactly the passes (the shroud projection, the projection on
		// foliage's second stage) that were splitting a mesh across both pipelines.
		const bool texgenRoutingOn =
			(m_shaderRoutingMask & (SHADER_ROUTE_TEXGEN | SHADER_ROUTE_EVERYTHING)) != 0;
		float texGenMode0 = 0.0f, texGenMode1 = 0.0f;
		bool texGenMatrix0 = false, texGenMatrix1 = false;
		// Stage 1's coordinate state is only meaningful when stage 1 has a texture; for a
		// single-texture draw it holds whatever a previous draw left behind, so requiring
		// it to map would wrongly reject the draw.
		//
		// How many coordinate sets the format in the stream carries. curFVF is read off
		// the bound vertex buffer, so it describes the data the shader will actually be
		// handed rather than what some earlier draw asked the device for.
		const unsigned uvSetCount =
			(curFVF & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
		const bool texGenSupported =
			Map_Texture_Coord_Source(TextureStageStates[0][D3DTSS_TEXCOORDINDEX],
									 TextureStageStates[0][D3DTSS_TEXTURETRANSFORMFLAGS],
									 uvSetCount, texGenMode0, texGenMatrix0) &&
			(render_state.Textures[1] == nullptr ||
			 Map_Texture_Coord_Source(TextureStageStates[1][D3DTSS_TEXCOORDINDEX],
									  TextureStageStates[1][D3DTSS_TEXTURETRANSFORMFLAGS],
									  uvSetCount, texGenMode1, texGenMatrix1));

		// A draw that reads the mesh's second coordinate set has to be given the shader
		// that declares one. This is a property of the *format*, so it is decided here
		// and every later gate reads the same answer.
		const bool needsUvSet1 = (texGenMode0 > 3.5f) || (texGenMode1 > 3.5f);

		// Only solid geometry belongs in a shadow map. A soft blended overlay has no
		// silhouette to cast and, being drawn a hair above the surface it decorates,
		// would write depth just in front of it and shadow the very ground it sits on --
		// which is what turned road and tank-track decals into dark smears. Cutout
		// foliage is the exception and has to keep casting: it is alpha blended too, but
		// the alpha *test* is what gives it a real silhouette, so that is the dividing
		// line rather than blending alone.
		// Blend classification. Declared here rather than further down because the
		// shadow-cast decision below needs it; the routing decisions later use the same
		// values.
		//
		// Asked of the ShaderClass the draw is carrying rather than of the device
		// registers that ShaderClass wrote -- but only for draws where it is the thing
		// that wrote them.
		//
		// Where it is, the shader is the better source because the registers are not a
		// faithful record of it. Apply() writes D3DRS_SRCBLEND and D3DRS_DESTBLEND only
		// when blending is on, so when it is off they still hold whatever the last
		// blended draw left behind, and reading them regardless declared ordinary opaque
		// meshes additive. Which meshes it caught depended on nothing but draw order:
		// measured on one frame, the entire USA barracks, power plant and supply centre
		// were classified additive and dropped to the fixed-function pipeline, where
		// there is no shadow map to sample, while the command centre beside them (drawn
		// after something that left an ordinary blend) kept the shader and its shadows.
		// That is the whole of "the barracks has no shadows on it but the HQ does". The
		// old code contained the hazard by guarding every read with alphaBlendOn; asking
		// the shader removes it, and the guard goes away with it.
		//
		// Where it is not -- the terrain blender, water, the shroud, the W3DShaderManager
		// effects, all of which write blend and depth registers straight to the device
		// and never call Set_Shader -- render_state.shader still holds the last mesh's
		// bits, and reading it would be the same stale read pointed the other way. Those
		// keep the device state, which for them is the only account of the draw that
		// exists. m_bMeshRendererDraw is what separates the two cases; measured over a
		// replay it is worth 5031 draws per 600 frames, every one of them non-mesh, and
		// 3354 of them changing whether the draw entered the shadow map.
		//
		// Stage 2 closes the gap by having those callers declare a technique of their
		// own, the way terrain and roads already declare theirs, at which point the
		// device-state half of each of these can go.
		const ShaderClass & meshShader = render_state.shader;
		const bool shaderDescribesDraw = m_bMeshRendererDraw;

		const bool alphaBlendOn = shaderDescribesDraw
			? meshShader.Is_Blend_Enabled()
			: (RenderStates[D3DRS_ALPHABLENDENABLE] != FALSE);
		const bool standardAlphaBlend = shaderDescribesDraw
			? meshShader.Is_Standard_Alpha_Blend()
			: (RenderStates[D3DRS_SRCBLEND] == D3DBLEND_SRCALPHA &&
			   RenderStates[D3DRS_DESTBLEND] == D3DBLEND_INVSRCALPHA);
		const bool additiveBlend = shaderDescribesDraw
			? meshShader.Is_Additive_Blend()
			: (alphaBlendOn &&
			   (RenderStates[D3DRS_SRCBLEND] == D3DBLEND_ONE ||
			    RenderStates[D3DRS_SRCBLEND] == D3DBLEND_SRCALPHA) &&
			   RenderStates[D3DRS_DESTBLEND] == D3DBLEND_ONE);
		const bool multiplyBlend = shaderDescribesDraw
			? meshShader.Is_Multiply_Blend()
			: ((RenderStates[D3DRS_SRCBLEND] == D3DBLEND_ZERO &&
			    RenderStates[D3DRS_DESTBLEND] == D3DBLEND_SRCCOLOR) ||
			   (RenderStates[D3DRS_SRCBLEND] == D3DBLEND_DESTCOLOR &&
			    RenderStates[D3DRS_DESTBLEND] == D3DBLEND_ZERO));

		const bool softBlendedOverlay = alphaBlendOn &&
			(shaderDescribesDraw
				? meshShader.Get_Alpha_Test() == ShaderClass::ALPHATEST_DISABLE
				: RenderStates[D3DRS_ALPHATESTENABLE] == FALSE);

		const bool meshDepthWrite = shaderDescribesDraw
			? meshShader.Get_Depth_Mask() == ShaderClass::DEPTH_WRITE_ENABLE
			: (RenderStates[D3DRS_ZWRITEENABLE] != FALSE);

		// Whether this mesh expects to be lit at all, from the vertex material that
		// decides it: VertexMaterialClass::Apply() sets D3DRS_LIGHTING from UseLighting,
		// and forces it off for a null material and under Is_Coloring_Enabled -- both
		// reproduced here.
		//
		// This is the fact that separates a pre-lit mesh, whose colour is already in its
		// vertices or its material, from one that wants a lighting equation run over it.
		// It is what the W3D interface models rely on, and reading it back off the device
		// left it a draw-order-dependent guess like the blend states above. Used both for
		// the diffuse-alpha resolution further down and for the technique check.
		const bool litMesh = shaderDescribesDraw
			? (render_state.material != nullptr &&
			   const_cast<VertexMaterialClass*>(render_state.material)->Get_Lighting() &&
			   !WW3D::Is_Coloring_Enabled())
			: (RenderStates[D3DRS_LIGHTING] != FALSE);


		// Shadow depth pass: every solid 3D draw (terrain, units, props) is re-routed to
		// the depth-packing shaders so it casts into the shadow map. 2D/UI (identity
		// view) and non-mesh draws are excluded. When active it pre-empts the normal
		// shaders.
		// Which blended geometry is allowed to cast.
		//
		// Excluding all of it meant a helicopter cast a body shadow with a hole where its
		// rotor should be, since rotor discs are blended with no alpha test. Admitting all
		// of it is worse: ground decals, tyre tracks, scorch marks, water, light beams and
		// lasers are blended too, and they would lay solid shadow over the terrain.
		//
		// Depth-write covers most of the split. Measured over a frame: those overlays draw
		// with ZWRITEENABLE off, because they are marks painted onto a surface rather than
		// surfaces themselves, while every solid caster -- rocks, crates, roofs, foliage,
		// riverbanks -- writes depth. Something that does not occlude the scene's own
		// depth has no business occluding the sun's.
		//
		// It does not cover the rotor. A rotor disc is a sorted, blended mesh with
		// depth-write *off*, which is a decal's signature exactly: measured, it arrives
		// byte-identical to a tyre track, so no render state can tell them apart. Where
		// the state cannot, the asset does -- m_bMeshCastsShadow carries
		// W3D_MESH_FLAG_CAST_SHADOW from the mesh the renderer is drawing right now, and
		// over a full scene that flag is set on the two rotor discs and on no other sorted
		// mesh. Only the mesh renderer raises it, so no decal or terrain draw can reach it.
		//
		// Particles are the third case, and they have neither state nor mesh flag to be
		// told apart by: a dust cloud and a laser beam are both blended, both alpha-tested
		// nowhere and both write no depth. So their renderer declares them instead --
		// m_bEffectCastsShadow, raised only around a submission the particle system has
		// been classified as matter rather than light. See ShadowCastingEffectClass.
		//
		// Additive stays out either way, flag or no flag: its alpha is brightness, not
		// coverage, so no cutoff applied to it would mean anything.
		const bool softBlendedCaster =
			softBlendedOverlay && !additiveBlend &&
			(meshDepthWrite || m_bMeshCastsShadow || m_bEffectCastsShadow);
		// The depth pass has to see sorted geometry too, which curFVF deliberately does not
		// -- see the note above: sorting buffers are excluded there so the unit/PBR routing
		// can never mistake one for a lit mesh. But a sorting buffer still carries a real
		// FVF, and a SORT-flagged mesh (rotor discs, and anything else the artist marked
		// for per-triangle sorting) is as solid a caster as any other. Reading the format
		// separately here admits them to the depth pass without letting the colour routing
		// near them. Without this every sorted mesh arrived with curFVF == 0, failed the
		// position test, and was masked out as if it were a screen-space overlay.
		DWORD depthFVF = curFVF;
		if (depthFVF == 0 && render_state.vertex_buffers[0] != nullptr &&
			(render_state.vertex_buffer_types[0] == BUFFER_TYPE_SORTING ||
			 render_state.vertex_buffer_types[0] == BUFFER_TYPE_DYNAMIC_SORTING)) {
			depthFVF = render_state.vertex_buffers[0]->FVF_Info().Get_FVF();
		}
		const bool useShadowDepth =
			m_bShadowDepthPass && m_dwShadowDepthVS != 0 && m_dwShadowDepthPS != 0 &&
			(depthFVF & D3DFVF_XYZ) &&
			(!softBlendedOverlay || softBlendedCaster) &&
			!(render_state_changed & (unsigned)VIEW_IDENTITY);


		const bool useTerrainShader =
			!m_bShadowDepthPass &&
			m_bTerrainShaderPass && m_dwTerrainVS != 0 && m_dwTerrainPS != 0 &&
			!(render_state_changed & (unsigned)VIEW_IDENTITY) &&
			!texgenActive;

		// Roads, flagged by W3DRoadBuffer. Unlike the terrain there is no !texgenActive
		// condition: the road shader reproduces the cloud and noise projections itself,
		// from the world position, which is the whole reason a road can now be shaded in
		// one pass instead of the fixed-function pipeline's stack of projected stages.
		const bool useRoadShader =
			!m_bShadowDepthPass &&
			m_bRoadShaderPass && m_dwRoadVS != 0 && m_dwRoadPS != 0 &&
			!(render_state_changed & (unsigned)VIEW_IDENTITY);

		// The 2D interface, flagged by Render2DClass around its own draws. This is the one
		// path that *wants* the identity view every other branch declines on -- but it is
		// claimed by the declaration, not by the view, so a camera-relative 3D pass or a
		// dazzle cannot fall in here by looking similar.
		const bool useUiShader =
			!m_bShadowDepthPass &&
			m_bUiPass && m_dwUiVS != 0 && m_dwUiPS != 0;

		// The projected alpha mask, flagged by W3DMaskMaterialPassClass around the whole
		// scene. It claims meshes *and* the terrain, so it has to be tested before the
		// terrain branch below or the terrain would draw itself normally into a pass whose
		// entire purpose is to replace what everything draws.
		const bool useMaskShader =
			!m_bShadowDepthPass &&
			m_bMaskPass && m_dwMaskVS != 0 && m_dwMaskPS != 0 &&
			!(render_state_changed & (unsigned)VIEW_IDENTITY);

		// The water surface, flagged by WaterRenderObjClass around its own draws. The
		// depth-pass exclusion is belt and braces: water is soft-blended with no alpha
		// test, so useShadowDepth already declines it and the branch below is unreachable
		// during that pass -- but the whole feature depends on water staying out of the
		// camera depth target, and that is worth stating where it can be read rather than
		// leaving it to be inferred from a blend mode three hundred lines away.
		const bool useWaterShader =
			!m_bShadowDepthPass &&
			m_bWaterShaderPass && m_dwWaterVS != 0 && m_dwWaterPS != 0 &&
			!(render_state_changed & (unsigned)VIEW_IDENTITY);

		// The frame-buffer blend is applied by hardware after the shader, so the shader's
		// "texture * light" output composites exactly as the equivalent fixed-function
		// single-texture pass did -- for opaque, standard SRCALPHA/INVSRCALPHA, additive
		// and multiply blends alike. Routing these (rather than leaving them on the fixed
		// pipeline) matters because such overlay passes are frequently drawn co-planar
		// over a shader-drawn base pass; splitting the two across the shader and fixed
		// pipelines gave them slightly different depth and they z-fought (surface shimmer)
		// as the camera rotates. Exotic blends we do not recognise stay on the fixed path.
		// (multiplyBlend is classified with the other blend modes above.)
		const bool reproducibleBlend =
			!alphaBlendOn || standardAlphaBlend || additiveBlend || multiplyBlend;

		// The unit shader samples a single base texture and takes its alpha straight
		// through. Multi-textured draws (a detail / house-colour second stage) combine
		// stages in the fixed-function pipeline -- including the alpha that drives the
		// frame-buffer blend -- which this shader cannot reproduce. House-colour
		// building emblems are drawn this way: reproducing only stage 0 renders their
		// background as opaque black (or drops them entirely). Leave multi-texture
		// draws to the fixed-function path.
		const bool singleTexture = render_state.Textures[1] == nullptr;

		// Untextured meshes: a multi-pass mesh often draws an untextured, lit sub-pass
		// (stage 0 selects the diffuse alone -- SELECTARG2/DISABLE, no texture bound)
		// alongside textured passes over the same geometry. Leaving it on the fixed
		// pipeline while its siblings run through the shader gives the two slightly
		// different depth, and the coincident passes then z-fight (the mottled pattern on
		// civilian buildings, whose untextured pass shows through as flat lit white).
		// The shader reproduces this exactly -- lit colour with no texture modulation --
		// so route it too and keep the whole mesh on one pipeline.
		//
		// The blend is not asked about here. It used to be -- this required blending to be
		// off outright -- and that was a second blend test standing in front of the one
		// that already exists: reproducibleBlend, a few lines below, is what decides
		// whether a blend mode can be reproduced, and it is applied to this draw either
		// way. Blending has nothing to do with whether an untextured stage 0 can be
		// expressed, and TexCtl.x already expresses it: unit_ps folds the base sample to
		// white, leaving the lit colour, and the frame-buffer blend is hardware downstream
		// of the pixel shader for a blended untextured pass exactly as for a textured one.
		// This being a capability gate and not a kind gate, widening it can only merge
		// draws onto one pipeline, never split a mesh across two.
		const DWORD s0ColorOp = TextureStageStates[0][D3DTSS_COLOROP];
		const DWORD s0ColorArg2 = TextureStageStates[0][D3DTSS_COLORARG2] & D3DTA_SELECTMASK;
		const bool untexturedDiffuseOnly =
			render_state.Textures[0] == nullptr &&
			((s0ColorOp == D3DTOP_SELECTARG2 && s0ColorArg2 == D3DTA_DIFFUSE) ||
			 s0ColorOp == D3DTOP_DISABLE);

		// Detail (stage 1) combine. A multi-texture pass can be drawn by the shader when
		// its stage 1 samples with texture coordinate set 0 (the only set the shader
		// carries today) and uses a combine the pixel shader reproduces. Claiming these
		// keeps a mesh whose passes are partly multi-textured on a single pipeline --
		// splitting it across the shader and the fixed-function path gives the passes
		// slightly different depth and they z-fight.
		const DWORD s1ColorOp    = TextureStageStates[1][D3DTSS_COLOROP];
		const DWORD s1AlphaOp    = TextureStageStates[1][D3DTSS_ALPHAOP];
		// D3DTSS_TEXCOORDINDEX packs two things: the low 16 bits select which coordinate
		// set of the vertex to use, the high 16 bits select camera-space generation
		// (D3DTSS_TCI_*). Both have to be checked. Reading only the low half makes a
		// generated-coordinate stage look like plain coordinate set 0, and the shader then
		// samples the detail texture with the mesh UVs instead of the generated camera
		// space coordinates -- which is what rendered foliage black, whose stage 1 is a
		// CAMERASPACEPOSITION projection.
		const DWORD s1CoordSet   = TextureStageStates[1][D3DTSS_TEXCOORDINDEX] & 0x0000FFFFu;
		const DWORD s1CoordGen   = TextureStageStates[1][D3DTSS_TEXCOORDINDEX] & 0xFFFF0000u;
		const DWORD s1XformFlags = TextureStageStates[1][D3DTSS_TEXTURETRANSFORMFLAGS];

		// Resolve the stage 1 combine into shader constants: a source selector per
		// argument plus a one-hot operation weight. The arguments matter -- a MODULATE of
		// TEXTURE by DIFFUSE is not the same as CURRENT times the detail texture, and
		// assuming the latter double-darkens the pass (this rendered foliage with black
		// splotches). Any op or argument the shader cannot express keeps the draw on the
		// fixed-function path.
		D3DXVECTOR4 s1CArg1(1.0f, 0.0f, 0.0f, 0.0f), s1CArg2(0.0f, 1.0f, 0.0f, 0.0f);
		D3DXVECTOR4 s1AArg1(1.0f, 0.0f, 0.0f, 0.0f), s1AArg2(0.0f, 1.0f, 0.0f, 0.0f);
		D3DXVECTOR4 s1COp(0.0f, 0.0f, 0.0f, 1.0f);   // (modulate, add, select1, select2)
		D3DXVECTOR4 s1AOp(0.0f, 0.0f, 0.0f, 1.0f);
		float s1CScale = 1.0f;
		bool detailCombineSupported = false;

		// Stage 1 must want coordinates a bound shader can produce. The plain case is
		// coordinate set 0 passed straight through; everything else -- camera-space
		// generation, a texture matrix, and now the mesh's second coordinate set -- goes
		// through the texgen resolution above, which has already checked the format
		// really carries what the stage asks for.
		const bool s1CoordsCarried =
			(s1CoordSet == 0 && s1CoordGen == D3DTSS_TCI_PASSTHRU && s1XformFlags == D3DTTFF_DISABLE) ||
			(texgenRoutingOn && texGenSupported);

		if (!singleTexture &&
			s1CoordsCarried &&
			m_dwUnitDetailPS != 0 &&
			(m_shaderRoutingMask & (SHADER_ROUTE_DETAIL | SHADER_ROUTE_EVERYTHING)) != 0) {
			const DWORD cArg1 = TextureStageStates[1][D3DTSS_COLORARG1];
			const DWORD cArg2 = TextureStageStates[1][D3DTSS_COLORARG2];
			const DWORD aArg1 = TextureStageStates[1][D3DTSS_ALPHAARG1];
			const DWORD aArg2 = TextureStageStates[1][D3DTSS_ALPHAARG2];
			bool ok = true;

			switch (s1ColorOp) {
				case D3DTOP_DISABLE:      // stage off: result is the stage 0 output
					s1CArg2 = D3DXVECTOR4(0.0f, 1.0f, 0.0f, 0.0f);
					s1COp   = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 1.0f);
					break;
				case D3DTOP_SELECTARG1:
					ok = Map_Texture_Stage_Arg(cArg1, s1CArg1);
					s1COp = D3DXVECTOR4(0.0f, 0.0f, 1.0f, 0.0f);
					break;
				case D3DTOP_SELECTARG2:
					ok = Map_Texture_Stage_Arg(cArg2, s1CArg2);
					s1COp = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 1.0f);
					break;
				case D3DTOP_MODULATE4X:
				case D3DTOP_MODULATE2X:
				case D3DTOP_MODULATE:
					s1CScale = (s1ColorOp == D3DTOP_MODULATE4X) ? 4.0f
							 : (s1ColorOp == D3DTOP_MODULATE2X) ? 2.0f : 1.0f;
					ok = Map_Texture_Stage_Arg(cArg1, s1CArg1) &&
						 Map_Texture_Stage_Arg(cArg2, s1CArg2);
					s1COp = D3DXVECTOR4(1.0f, 0.0f, 0.0f, 0.0f);
					break;
				case D3DTOP_ADD:
					ok = Map_Texture_Stage_Arg(cArg1, s1CArg1) &&
						 Map_Texture_Stage_Arg(cArg2, s1CArg2);
					s1COp = D3DXVECTOR4(0.0f, 1.0f, 0.0f, 0.0f);
					break;
				default:
					ok = false;
					break;
			}

			if (ok) {
				switch (s1AlphaOp) {
					case D3DTOP_DISABLE:
						s1AArg2 = D3DXVECTOR4(0.0f, 1.0f, 0.0f, 0.0f);
						s1AOp   = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 1.0f);
						break;
					case D3DTOP_SELECTARG1:
						ok = Map_Texture_Stage_Arg(aArg1, s1AArg1);
						s1AOp = D3DXVECTOR4(0.0f, 0.0f, 1.0f, 0.0f);
						break;
					case D3DTOP_SELECTARG2:
						ok = Map_Texture_Stage_Arg(aArg2, s1AArg2);
						s1AOp = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 1.0f);
						break;
					case D3DTOP_MODULATE:
					case D3DTOP_MODULATE2X:
					case D3DTOP_MODULATE4X:
						ok = Map_Texture_Stage_Arg(aArg1, s1AArg1) &&
							 Map_Texture_Stage_Arg(aArg2, s1AArg2);
						s1AOp = D3DXVECTOR4(1.0f, 0.0f, 0.0f, 0.0f);
						break;
					case D3DTOP_ADD:
						ok = Map_Texture_Stage_Arg(aArg1, s1AArg1) &&
							 Map_Texture_Stage_Arg(aArg2, s1AArg2);
						s1AOp = D3DXVECTOR4(0.0f, 1.0f, 0.0f, 0.0f);
						break;
					default:
						ok = false;
						break;
				}
			}

			detailCombineSupported = ok;
		}

		// Some geometry is drawn with a programmable vertex shader the engine supplies
		// itself -- tree billboards bind tree_vs and upload its constants straight to
		// the device. Those draws must keep the shader the engine chose; binding the unit
		// shader over the top renders them with the wrong transform and inputs (this is
		// what turned foliage black once multi-texture passes became routable, since the
		// multi-texture rule had been incidentally keeping tree draws away from us).
		// A vertex shader handle is distinguishable from an FVF code by magnitude.
		// Every shader this wrapper binds itself must be listed here. Omitting one makes
		// the draw after it look engine-supplied, which both bars that draw from being
		// routed and stops the restore branch below from unbinding the shader -- so the
		// next mesh is transformed by whatever constants the previous one left behind.
		const bool foreignVertexShader =
			Vertex_Shader >= 0x10000 &&
			Vertex_Shader != m_dwUnitVS &&
			Vertex_Shader != m_dwUnitPrelitVS &&
			Vertex_Shader != m_dwUnitUv2VS &&
			Vertex_Shader != m_dwUnitPbrVS &&
			Vertex_Shader != m_dwTerrainVS &&
			Vertex_Shader != m_dwRoadVS &&
			Vertex_Shader != m_dwShadowDepthVS &&
			Vertex_Shader != m_dwShadowDepthParticleVS;

		// Routing categories are selectable at runtime (options.ini ShaderRouting) so the
		// pipeline split can be compared in game without a rebuild: EVERYTHING drops the
		// per-category restrictions (the configuration that demonstrably removes the
		// z-fighting, but renders detail/texgen passes wrong), OFF keeps every mesh on the
		// fixed-function pipeline, and the individual bits opt one category in at a time.
		const bool routeEverything = (m_shaderRoutingMask & SHADER_ROUTE_EVERYTHING) != 0;
		const bool routingDisabled = (m_shaderRoutingMask & SHADER_ROUTE_OFF) != 0;

		// Effect geometry now takes the shader by default, so this bit is the escape
		// hatch rather than the opt-in: setting it puts rotor discs, glows, beams, smoke
		// and tracks back on fixed function so the two can still be compared in game.
		//
		// The bit is the same one and keeps its name in options.ini. What changed is its
		// sense, which is worth saying plainly because a saved configuration carrying it
		// now means the opposite of what it used to.
		const bool keepEffectsOnFF = (m_shaderRoutingMask & SHADER_ROUTE_ADDITIVE) != 0;

		// A normal is only wanted for two things: the lit equation, and the texgen sources
		// derived from it. Geometry that has neither -- roads and tank tracks are pre-lit
		// DX8_FVF_XYZDUV1 with no NORMAL -- can be routed all the same, through a vertex
		// shader that declares no normal input. Declining it instead left those draws on
		// fixed function, where they never sampled the shadow map: a road crossed shadowed
		// ground at full brightness.
		const bool hasNormal = (curFVF & D3DFVF_NORMAL) != 0;
		const bool texGenNeedsNormal = texGenMode0 > 1.5f || texGenMode1 > 1.5f;
		// Lighting may well be left enabled on this geometry even though it carries no
		// normal -- roads are drawn that way. Fixed function then gets N.L == 0 for
		// every light and falls back to emissive plus ambient, which unit_prelit_vs
		// reproduces, so the state does not have to be off for this to be safe.
		const bool prelitNoNormal =
			!hasNormal && m_dwUnitPrelitVS != 0 && !texGenNeedsNormal;

		// The two-coordinate-set variant. Only the lit shader has one: a pass wanting
		// set 1 on geometry with no normal would need a pre-lit variant that does not
		// exist, and inventing the coordinates rather than declining the draw is exactly
		// the silent-wrong-output failure the format check upstream exists to prevent.
		// No such draw appears in any measured scene; if one does it stays on fixed
		// function and the census names it.
		const bool useUv2Shader =
			needsUvSet1 && hasNormal && uvSetCount >= 2 && m_dwUnitUv2VS != 0;

		// Soft-blended geometry -- blending on, no alpha test -- stays on fixed function
		// unless it belongs to a mesh that also has a depth-writing pass.
		//
		// The blend test alone is the obvious instrument and it is the wrong one. It does
		// describe effect geometry -- a rotor disc, a light shaft. It equally describes an
		// ordinary blended pass on an ordinary surface, and civilian buildings have
		// several, so writing it that way puts some passes of those meshes on the shader
		// and some on fixed function. The two pipelines do not compute identical depth, so
		// a mesh drawn by both z-fights with itself, in a pattern that crawls as the camera
		// moves. Measured on the civ_buildings replay with the blend test in place: six
		// meshes split across pipelines, every one of them a CB* civilian building, every
		// one for this reason.
		//
		// What separates the two cases is not the pass, it is the mesh the pass belongs to.
		// A building is a surface: something in it writes depth, and the blended pass is a
		// layer on top of that surface. A rotor disc, a beacon's light shaft, a smoke
		// puff -- these write no depth in any pass, because they are not surfaces at all.
		// So ask the mesh, not the blend. Being a property of the mesh, the answer is the
		// same for every pass of it, which is what makes the split impossible rather than
		// merely absent: an effect keeps all its passes on fixed function, and a surface
		// keeps all of its on the shader.
		//
		// Anything that is not a mesh-renderer draw leaves the flag false and so keeps the
		// old behaviour -- particle systems and decals are drawn soft-blended without ever
		// coming through the mesh renderer, and they stay where they were.
		const bool effectGeometryExcluded = softBlendedOverlay && !m_bMeshHasSolidPass;

		// The same reasoning, applied to additive passes: a blend that carries no coverage
		// on geometry that writes depth in no pass.
		//
		// Both of these used to *exclude* a draw from the programmable path. Neither does
		// any more -- they identify effect geometry rather than banish it, and what they
		// now decide is whether the sun and the clouds reach it. The names are kept
		// because what they describe has not changed, only what follows from it.
		const bool additiveExcluded = additiveBlend &&
			m_meshTechnique != MESH_TECHNIQUE_SURFACE;

		// What kind of thing is this?
		//
		// A declared technique answers it outright, which is the point of having one: the
		// asset was classified once, from its own material description, and the tests
		// below no longer have to be re-derived from render state on every draw.
		//
		// **Effect geometry now takes the shader too.** It was the largest single body of
		// fixed-function drawing left -- rotor discs, glows, light shafts, lasers, smoke,
		// tank tracks, decals -- and measured across a replay it is a third of every
		// remaining fixed-function draw on its own.
		//
		// It was excluded for a reason that no longer exists. The exclusion was written
		// when a helicopter rotor disc drew as nothing at all through this path, and the
		// cause of *that* turned out to be `unit_vs` forcing `output.color.a = 1` and
		// throwing away the vertex alpha in which a rotor's entire fade lives (fixed in
		// d1f008b27). Nothing else about an effect is beyond these shaders: the
		// frame-buffer blend is applied by hardware after the pixel shader, so an additive
		// or soft-blended pass composites identically whichever pipeline produced the
		// colour. What an effect must *not* get is the sun and the clouds, and that is one
		// constant (LightingParams.z), not a shader of its own.
		//
		// FIXED_FUNCTION remains the one technique that stays behind. It exists precisely
		// to name geometry that has to.
		//
		// A draw with no technique never came through the mesh renderer -- terrain, water,
		// the shroud, particles -- and there is no asset to ask, so those keep the
		// inference, minus the two exclusions this change removes. `reproducibleBlend`
		// stays: an unrecognised blend mode is a capability question, not a kind.
		const bool classifiedDraw = m_meshTechnique != MESH_TECHNIQUE_UNCLASSIFIED;
		const bool kindAllowsProgrammable = classifiedDraw
			? (m_meshTechnique != MESH_TECHNIQUE_FIXED_FUNCTION &&
			   !(keepEffectsOnFF && m_meshTechnique == MESH_TECHNIQUE_EFFECT))
			: (reproducibleBlend && !(keepEffectsOnFF &&
									  (effectGeometryExcluded || additiveExcluded)));

		// Is this draw an effect? Asked of the mesh where one said so, and inferred the
		// old way where nothing did -- a blend carrying no coverage on geometry that
		// writes depth in no pass. Read by the shader constants below, not by routing:
		// an effect is routed like anything else, it is just not lit by the sun.
		const bool effectDraw = classifiedDraw
			? (m_meshTechnique == MESH_TECHNIQUE_EFFECT)
			: (effectGeometryExcluded || additiveExcluded);

		const bool useUnitShader =
			// Context: which pass of the frame this is, and whether the path is usable
			// at all. Nothing here is a property of the mesh.
			!routingDisabled &&
			!m_bShadowDepthPass &&
			!m_bTerrainShaderPass &&
			!m_bRoadShaderPass &&
			!m_bWaterShaderPass &&
			m_dwUnitVS != 0 && m_dwUnitPS != 0 &&
			!(render_state_changed & (unsigned)VIEW_IDENTITY) &&
			(curFVF & D3DFVF_XYZ) && (hasNormal || prelitNoNormal) &&
			!foreignVertexShader &&
			// Kind: what the asset is.
			kindAllowsProgrammable &&
			// Capability: what these shaders can reproduce for this particular draw.
			// Unlike the kind, this is not a property of the asset -- the same mesh can
			// be drawn with a stage-1 combine one pass and without it the next.
			(routeEverything ||
			 ((render_state.Textures[0] != nullptr || untexturedDiffuseOnly) &&
			  (singleTexture || detailCombineSupported) &&
			  (!needsUvSet1 || useUv2Shader) &&
			  (!texgenActive || (texgenRoutingOn && texGenSupported))));


#ifdef RTS_DEBUG
		// Does the technique the asset declared agree with what this block decides for
		// itself? Nothing reads m_meshTechnique yet -- it is carried alongside the live
		// decision and checked against it, so the classifier can be trusted before
		// anything depends on it.
		//
		// Only the exclusions the classifier actually models are compared. It knows
		// nothing about which pass of the frame is being drawn, what ShaderRouting
		// permits, or whether a texgen can be reproduced, because none of those are
		// properties of the asset; those are dispatch concerns and are excluded from the
		// comparison rather than counted as disagreements.
		//
		// Draws with no position bit are the same kind of exclusion and are skipped for
		// the same reason.
		//
		// Sorting buffers used to land here too: they arrived with curFVF == 0, so the
		// live block saw no normal and read them as pre-lit while the classifier, looking
		// at the container's real vertex format, correctly called a sorted glow mesh a
		// surface -- every one of the 6264 disagreements in the first run. They now report
		// the format they are actually drawn with, which is the format the classifier was
		// reading all along, so the two agree by construction rather than by exemption.
		//
		// s_applyIsDraw, for the same reason the fixed-function attribution below carries
		// it: Apply_Render_State_Changes is public and most of its callers are not draws.
		// A caller that only wants the matrices flushed leaves this function holding one
		// draw's shader and material beside another draw's vertex buffer, and the live
		// technique inferred from that mixture describes neither of them.
		//
		// W3DProjectedShadowManager::flushDecals is the case that exposed it. It declares
		// FIXED_FUNCTION -- correctly, and see the comment there for why -- then calls
		// Apply_Render_State_Changes to force the view and projection matrices out, at a
		// point where its own multiplicative/additive shader is already set but its vertex
		// buffer is not: it binds that on the device itself, afterwards. So the live block
		// read the decal's blend against the *previous* draw's format and called it an
		// EFFECT, and the check reported that as the classifier disagreeing. One mismatch
		// per decal batch per frame -- 1800 a window for three hazard-field layers, and
		// zero before those layers existed only because nothing else in that save queued
		// a projected decal at all.
		//
		// This is not an exemption for flushDecals; nothing here mentions it. It is the
		// check declining to compare against state that no single draw ever held.
		if (s_applyIsDraw &&
			m_meshTechnique != MESH_TECHNIQUE_UNCLASSIFIED && (curFVF & D3DFVF_XYZ)) {
			// Mirrors the classifier's single mesh-level rule: a blend that carries no
			// coverage -- additive, or soft with no alpha test -- on a mesh that writes
			// depth nowhere. Both halves ask the mesh, so both are reproduced that way
			// here; comparing against the old per-pass additive test would report every
			// building glow as a disagreement when moving it is the point.
			const bool liveEffect =
				(softBlendedOverlay || additiveBlend) && !m_bMeshHasSolidPass;
			const bool livePrelit  = !liveEffect && reproducibleBlend && !hasNormal;
			const bool liveFixedFn = !liveEffect && !reproducibleBlend;

			MeshTechnique liveTechnique = MESH_TECHNIQUE_SURFACE;
			if (liveEffect)       liveTechnique = MESH_TECHNIQUE_EFFECT;
			else if (liveFixedFn) liveTechnique = MESH_TECHNIQUE_FIXED_FUNCTION;
			else if (livePrelit)  liveTechnique = MESH_TECHNIQUE_PRELIT;

			// The classifier calls a mesh pre-lit when its material has lighting off as
			// well as when it carries no normal; the live block has no equivalent test,
			// which is the gap that let a pre-lit cursor be shaded as a surface. Treat
			// that one direction as expected rather than as a disagreement -- it is the
			// defect being fixed, not a classifier error.
			const bool expectedPrelitGain =
				m_meshTechnique == MESH_TECHNIQUE_PRELIT &&
				liveTechnique == MESH_TECHNIQUE_SURFACE && !litMesh;

			if (m_meshTechnique != liveTechnique && !expectedPrelitGain)
				Debug_Note_Technique_Mismatch(m_meshTechnique, liveTechnique,
					render_state.Textures[0], curFVF);
			else
				Debug_Note_Technique_Agreement(m_meshTechnique, expectedPrelitGain);
		}
#endif

		// Which pipeline ends up drawing this pass: 1 = fixed function unless a branch
		// below claims it. Read by the split-pipeline watchdog in debug builds.
		//
		// Every programmable branch must claim it, including the ones the watchdog does
		// not look at (shadow depth 16, terrain 32, road 64, water 128). They did not,
		// which was harmless while only the watchdog read this -- it skips those passes
		// anyway -- and stopped being harmless the moment the fixed-function attribution
		// started reading it too: 551056 shader-drawn depth-pass draws per window were
		// reported as fixed function, 78% of a total whose whole purpose is to be a list
		// of work remaining.
		unsigned diagRouteBit = 1;
		// The same thing as a census bucket rather than a bit, split one level finer:
		// the two PBR paths are one pipeline but not one piece of evidence.
		unsigned diagCensusCat = 0;   // 0 = fixed function

		if (useShadowDepth) {
			// Force the full square shadow-map viewport right before the draw. The
			// camera's Apply set a screen-sized (16:9) viewport that gets re-applied
			// per object, leaving the bottom of the square map cleared and mis-sampled.
			// The SSR depth prepass is the exception: its target is the size of the
			// screen, so the camera's own viewport is already the right one and forcing
			// the square would squash the scene into a corner of it.
			if (!m_bDepthPrepass)
			{ GfxViewport svp = { 0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 0.0f, 1.0f };
			  Gfx->Set_Viewport(svp); }
			// Force the states the depth pass depends on, for the same reason as the
			// viewport above: the scene re-applies per-object state, so whatever the
			// normal path wanted (a blend mode, z-writes off for a translucent pass, a
			// colour-write mask left over from the alpha-mask pass) otherwise leaks in
			// here. Depth packing needs the plain nearest-wins opaque rules -- with
			// blending on, or z-writes off, the map ends up holding the last draw
			// rasterised rather than the one closest to the sun.
			Set_DX8_Render_State(D3DRS_COLORWRITEENABLE,
				D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
				D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
			// Blending off: packed depth must land in the target unmodified. The alpha
			// test, though, is deliberately left as the scene set it -- shadowdepth_ps
			// writes the caster's texture alpha, so the hardware cuts the transparent
			// texels out of a foliage billboard here exactly as it does on screen.
			Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, FALSE);
			Set_DX8_Render_State(D3DRS_ZENABLE, D3DZB_TRUE);
			Set_DX8_Render_State(D3DRS_ZWRITEENABLE, TRUE);
			Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
			// Every caster must reach the map whole. Culling would drop whichever facing
			// the sun disagrees with the camera about; wireframe would leave only edges;
			// a stencil test left enabled from the player-colour/occlusion work would
			// reject fragments outright -- and the shadow map's depth surface is D16,
			// with no stencil for it to test against.
			Set_DX8_Render_State(D3DRS_CULLMODE, D3DCULL_NONE);
			Set_DX8_Render_State(D3DRS_FILLMODE, D3DFILL_SOLID);
			Set_DX8_Render_State(D3DRS_STENCILENABLE, FALSE);
			Set_DX8_Render_State(D3DRS_ZBIAS, 0);
			// Bind the depth-packing shaders and feed the pass's view-projection (c0)
			// + this draw's World (c4). All geometry casts, so no texture/material
			// state is needed. Which projection depends on who is asking: the sun, for
			// the shadow map, or the camera, for the depth SSR marches against.
			if (!m_bUnitShaderBound) {
				s_dwOriginalPS = Pixel_Shader;
				m_bUnitShaderBound = true;
			}
			// A declared particle caster gets the sprite variant of the pair. Two things
			// separate it from every other caster and neither is expressible as a
			// constant: its opacity is in the vertex colour, which the shared shader
			// never reads, and it is genuinely translucent, which one depth per texel
			// cannot represent -- so that variant dithers coverage instead of thresholding
			// it. Only in the sun's pass: SSR marches against solid surfaces, and smoke
			// stippled into the camera depth would punch holes in the reflections behind
			// it.
			const bool particleCaster =
				m_bEffectCastsShadow && !m_bDepthPrepass &&
				m_dwShadowDepthParticleVS != 0 && m_dwShadowDepthParticlePS != 0;

			// A rotor disc is the other translucent caster, and the shared shader dithers
			// it for the same reason -- but through the plain vertex shader, not the
			// particle one. A mesh has no per-particle alpha to read, and reading COLOR0
			// off a vertex format that may not carry it is undefined, so the switch is a
			// constant rather than a second shader pair.
			//
			// What identifies one: soft-blended, depth-write off, and admitted to the pass
			// at all only by W3D_MESH_FLAG_CAST_SHADOW (see softBlendedCaster above, and
			// meshshadowname.h for the discs whose artist forgot to set it). That is the
			// exact set the flag exists to pick out. A soft-blended mesh that *writes*
			// depth is a solid surface drawn with blending and keeps the hard cut, and so
			// does anything in the camera depth prepass -- SSR marches against solid
			// surfaces, and a stippled rotor would punch holes in the reflections behind
			// it, the same reason particles stay out of it.
			const bool ditheredMeshCaster =
				softBlendedCaster && m_bMeshCastsShadow && !meshDepthWrite &&
				!m_bDepthPrepass;

			// x: alpha cutoff for a caster that is cut rather than dithered. Zero leaves
			// the hardware alpha test in sole charge, which is what opaque and cut-out
			// geometry want. Blended casters have no alpha test of their own, so they are
			// cut here instead.
			//
			// y is the particle variant's density ceiling: the fraction of the sun a fully
			// opaque sprite texel is allowed to take. Short of 1 on purpose. Smoke that
			// stops the sun outright reads as a hole in the ground rather than as smoke,
			// and the alpha these sprites carry is authored for compositing over a scene,
			// not for optical depth.
			//
			// Raised from the 0.7 it was first tried at. Measured against a tank battle,
			// that gave a mean darkening of about 9% of local brightness under a dust
			// trail -- present, but easy to miss on the thinner effects, which are most of
			// them. The headroom to 1.0 is what stops a dense plume going to a silhouette,
			// so there is room to spend some of it without losing that.
			//
			// z is the same ceiling for the shared shader's dither, and zero switches the
			// dither off. It is 1, not the sprites' 0.85, because a mesh's alpha *is* its
			// coverage rather than an artist's compositing weight. Most of the meshes this
			// path admits are not rotor discs at all: censused over the shipped set, every
			// other soft-blended flagged mesh -- TV dishes, ducts, warehouse roofs, a bank
			// -- carries a fully opaque alpha channel, and any ceiling below 1 would take
			// that fraction of their shadow away for nothing. At 1 they dither to solid and
			// come out exactly as they did under the cutoff, and only genuinely partial
			// alpha is affected.
			const float shadowAlphaCutoff =
				(softBlendedCaster && !ditheredMeshCaster) ? 0.45f : 0.0f;
			const float PARTICLE_SHADOW_DENSITY = 0.85f;
			const float MESH_SHADOW_DENSITY = 1.0f;
			const D3DXVECTOR4 shadowCastParams(shadowAlphaCutoff, PARTICLE_SHADOW_DENSITY,
											   ditheredMeshCaster ? MESH_SHADOW_DENSITY : 0.0f,
											   0.0f);
			Set_Pixel_Shader_Constant(0, &shadowCastParams, 1);
			Set_Vertex_Shader(particleCaster ? m_dwShadowDepthParticleVS : m_dwShadowDepthVS);
			Set_Pixel_Shader(particleCaster ? m_dwShadowDepthParticlePS : m_dwShadowDepthPS);
#ifdef RTS_DEBUG
			if (!m_bDepthPrepass)
				Debug_Note_Shadow_Caster_Draw(particleCaster);
			diagRouteBit = 16u;
#endif
			if (m_bDepthPrepass) {
				// Build the camera view-projection from the state the pipeline is
				// actually drawing with, rather than having the view push its own copy.
				// Two copies would be free to disagree, and the PBR shader reprojects
				// against this depth expecting an exact match -- a matrix that is merely
				// close puts every hit test a fraction of a pixel out. Stashed so that
				// shader is handed the very same one.
				D3DXMATRIX camView = *reinterpret_cast<const D3DXMATRIX*>(&render_state.view);
				const D3DXMATRIX camProj =
					*reinterpret_cast<const D3DXMATRIX*>(&DX8Transforms[D3DTS_PROJECTION]);
				D3DXMATRIX camVP;
				D3DXMatrixMultiply(&camVP, &camView, &camProj);
				memcpy(m_depthVP, &camVP, sizeof(m_depthVP));
				Set_Vertex_Shader_Constant(0, &camVP, 4);

				// The two projection elements the shader needs to turn the stored z/w
				// back into a view-space distance, handed over as they are. It used to
				// recover the near and far planes from them here and rebuild the
				// transform from those in the shader -- the same algebra with two extra
				// divisions in the middle, each of which goes through zero for
				// projections this code does not anticipate, and all of it landing where
				// the stored depth sits closest to 1.0 and tolerates error least.
				// Passing the coefficients removes the round trip entirely.
				// The projection in use is right-handed, so the inversion is
				//   ndcZ = -_33 - _43/viewZ  ->  viewZ = abs(_43 / (_33 + ndcZ))
				// which is what unit_pbr_ps and debugdepth_ps both compute, abs() included
				// so the expression survives a left-handed projection too. This comment
				// used to state the left-handed pair, and every consumer written from it
				// got a negative distance for every pixel; the depth inspector, built from
				// this line, came out uniformly white until it was checked against the
				// shader. Measured here: _33 = -1.006, _43 = -10.06, i.e. near 10, far 1677.
				// How far a reflection may travel before giving up and leaving the
				// cubemap in place. Long enough to cross a vehicle and reach the ground
				// beside it, short enough that the march stays fine-grained.
				const float SSR_MAX_RAY = 150.0f;
				Set_Ssr_Params(1.0f, SSR_MAX_RAY, camProj._33, camProj._43);
			}
			else
				Set_Vertex_Shader_Constant(0, reinterpret_cast<const D3DXMATRIX*>(m_sunVP), 4);
			D3DXMATRIX shadowWorld = *reinterpret_cast<const D3DXMATRIX*>(&render_state.world);
			Set_Vertex_Shader_Constant(4, &shadowWorld, 4);
		}
		else if (m_bShadowDepthPass) {
			// In the depth pass the render target is not a colour buffer, it is packed
			// depth -- so only the depth shaders may write to it. A draw that could not
			// be routed there would otherwise blast its own colour in as if it were a
			// depth value. Screen-space overlays are the ones that get here: they use
			// D3DFVF_XYZRHW, which does not set the D3DFVF_XYZ bit the routing requires.
			// One full-screen quad was covering the top 1440 rows of the map (the screen
			// height) with a constant, burying the real terrain depth underneath it.
			//
			// Masking colour and z made such a draw harmless. Suppressing it makes it
			// free, which is the same thing done properly: a draw that may write neither
			// colour nor depth has no effect a render target can record, so submitting it
			// only spends the pipeline on producing nothing. Measured on chinooks.rep,
			// that was 78934 draws per 600 frames -- a third of everything still reaching
			// the fixed-function pipeline, and none of it drawing anything.
			//
			// The masks are still written. They cost two cached state changes and they are
			// what keeps this safe if the suppression is ever bypassed -- Draw_Sorting_IB_VB
			// has its own path to the device, and a future one might too.
			// The masks are the decision, not a note about it: Draw() re-reads them for
			// every draw, so they suppress this one and every following one that inherits
			// them, and they stop suppressing the moment somebody writes them back.
			Set_DX8_Render_State(D3DRS_COLORWRITEENABLE, 0);
			Set_DX8_Render_State(D3DRS_ZWRITEENABLE, FALSE);
		}
		else if (useMaskShader) {
			if (!m_bUnitShaderBound) {
				s_dwOriginalPS = Pixel_Shader;
				m_bUnitShaderBound = true;
			}
			Set_Vertex_Shader(m_dwMaskVS);
			Set_Pixel_Shader(m_dwMaskPS);
#ifdef RTS_DEBUG
			diagRouteBit = 512u;
#endif

			D3DXMATRIX world = *reinterpret_cast<const D3DXMATRIX*>(&render_state.world);
			D3DXMATRIX view  = *reinterpret_cast<const D3DXMATRIX*>(&render_state.view);
			const D3DXMATRIX proj =
				*reinterpret_cast<const D3DXMATRIX*>(&DX8Transforms[D3DTS_PROJECTION]);
			D3DXMATRIX wvp;
			D3DXMatrixMultiply(&wvp, &world, &view);
			D3DXMatrixMultiply(&wvp, &wvp, &proj);
			Set_Vertex_Shader_Constant(0, &wvp, 4);

			// The two object->world rows the projection needs, in the same layout unit_vs
			// takes them: a column of the world matrix per constant, dotted against the
			// object-space position.
			const D3DXVECTOR4 worldAxisX(world._11, world._21, world._31, world._41);
			const D3DXVECTOR4 worldAxisY(world._12, world._22, world._32, world._42);
			Set_Vertex_Shader_Constant(4, &worldAxisX, 1);
			Set_Vertex_Shader_Constant(5, &worldAxisY, 1);
			Set_Vertex_Shader_Constant(6, &m_maskProj, 1);

			// The fixed-function path left stage 0's filter and addressing to whatever the
			// previous caller happened to leave on the device. State them: linear on a
			// smooth radial mask, and wrap, which is the D3D default it was inheriting.
			Set_Sampler(0, Get_Sampler(0)
				.With_Filter(SamplerStateClass::FILTER_LINEAR, SamplerStateClass::FILTER_LINEAR)
				.With_Address(SamplerStateClass::ADDRESS_WRAP, SamplerStateClass::ADDRESS_WRAP));
		}
		else if (useTerrainShader) {
			if (!m_bUnitShaderBound) {
				s_dwOriginalPS = Pixel_Shader;
				m_bUnitShaderBound = true;
			}
			Set_Vertex_Shader(m_dwTerrainVS);
			Set_Pixel_Shader(m_dwTerrainPS);
#ifdef RTS_DEBUG
			diagRouteBit = 32u;
#endif

			D3DXMATRIX world = *reinterpret_cast<const D3DXMATRIX*>(&render_state.world);
			D3DXMATRIX view  = *reinterpret_cast<const D3DXMATRIX*>(&render_state.view);
			const D3DXMATRIX proj =
				*reinterpret_cast<const D3DXMATRIX*>(&DX8Transforms[D3DTS_PROJECTION]);
			D3DXMATRIX wvp;
			D3DXMatrixMultiply(&wvp, &world, &view);
			D3DXMatrixMultiply(&wvp, &wvp, &proj);
			Set_Vertex_Shader_Constant(0, &wvp, 4);
			// Sun view-projection (VS c5) so the terrain can reproject + sample the
			// shadow map, and the shadow map itself on stage 5 (point + clamp).
			Set_Vertex_Shader_Constant(5, reinterpret_cast<const D3DXMATRIX*>(m_sunVP), 4);
			Set_Pixel_Shader_Constant(1, m_shadowParams, 1);   // bias + strength
			if (m_pShadowMap != nullptr) {
				Set_DX8_Texture(5, m_pShadowMap);
				s_shadowStage5Bound = true;
				Set_Sampler(5, Get_Sampler(5)
					.With_Filter(SamplerStateClass::FILTER_POINT, SamplerStateClass::FILTER_POINT)
					.With_Address(SamplerStateClass::ADDRESS_CLAMP, SamplerStateClass::ADDRESS_CLAMP));
			}

			// Smooth (bi/tri-linear) filtering + clamp, matching the fixed-function
			// terrain path. The base atlas texture's own filter may be point, which
			// looks jagged; this runs after the texture Apply so it wins.
			Set_Sampler(0, Get_Sampler(0)
				.With_Filter(SamplerStateClass::FILTER_LINEAR, SamplerStateClass::FILTER_LINEAR)
				.With_Mip_Filter(SamplerStateClass::FILTER_LINEAR)
				.With_Address(SamplerStateClass::ADDRESS_CLAMP, SamplerStateClass::ADDRESS_CLAMP));

			// Cloud/noise overlay layers: scroll offset (VS c4) + enable mask (PS c0).
			// These are (near-)constant across draws, so the redundant-set cache in
			// Set_*_Shader_Constant sends them once and then skips. A device reset
			// zeroes the device constants but Invalidate_Cached_Render_States leaves
			// the constant caches untouched, so after such a reset the cache still
			// matches while the device holds 0 -- and the overlays get stuck off
			// (terrain renders with no cloud/noise). Force these writes through the
			// device directly and keep the cache coherent so later cached sets work.
			// c4 is now the two cloud layers' world-space drift rather than one UV offset;
			// the shader divides each by its own projection period.
			D3DXVECTOR4 cloudOffset(m_cloudScrollAX, m_cloudScrollAY, m_cloudScrollBX, m_cloudScrollBY);
			GFXCALL(Set_Vertex_Shader_Constants(4, reinterpret_cast<const float*>(&cloudOffset), 1));
			Vertex_Shader_Constants[4] = *reinterpret_cast<const Vector4*>(&cloudOffset);
			D3DXVECTOR4 overlayEnable(m_terrainCloudEnable ? 1.0f : 0.0f,
									  m_terrainNoiseEnable ? 1.0f : 0.0f,
									  m_cloudStrength, 0.0f);
			GFXCALL(Set_Pixel_Shader_Constants(0, reinterpret_cast<const float*>(&overlayEnable), 1));
			Pixel_Shader_Constants[0] = *reinterpret_cast<const Vector4*>(&overlayEnable);
			// Stochastic tiling. The class table on stage 1 must be read exactly as it
			// was written -- its bytes are a width and two slot offsets, and a filtered
			// tap between two slots decodes to a class that does not exist.
			Set_Sampler(1, Get_Sampler(1)
				.With_Filter(SamplerStateClass::FILTER_POINT, SamplerStateClass::FILTER_POINT)
				.With_Mip_Filter(SamplerStateClass::FILTER_NONE)
				.With_Address(SamplerStateClass::ADDRESS_CLAMP, SamplerStateClass::ADDRESS_CLAMP));
			Set_Pixel_Shader_Constant(2, &m_terrainAtlasParams, 1);
			Set_Pixel_Shader_Constant(3, &m_terrainTilingParams, 1);

			// Procedural detail layer on stage 4. Filtered and mipped, unlike the class
			// table: its mip chain is what fades the relief out with distance instead of
			// letting the gradient alias into shimmer. WRAP because the field is built
			// periodic precisely so it can be projected across the whole map.
			Set_Sampler(4, Get_Sampler(4)
				.With_Filter(SamplerStateClass::FILTER_LINEAR, SamplerStateClass::FILTER_LINEAR)
				.With_Mip_Filter(SamplerStateClass::FILTER_LINEAR)
				.With_Address(SamplerStateClass::ADDRESS_WRAP, SamplerStateClass::ADDRESS_WRAP));
			Set_Pixel_Shader_Constant(4, &m_terrainDetailParams, 1);
			Set_Pixel_Shader_Constant(5, &m_terrainSunDir, 1);
			Set_Pixel_Shader_Constant(6, &m_terrainColourParams, 1);

			// Cloud/noise tile and wrap.
			Set_Sampler(2, Get_Sampler(2)
				.With_Filter(SamplerStateClass::FILTER_LINEAR, SamplerStateClass::FILTER_LINEAR)
				.With_Address(SamplerStateClass::ADDRESS_WRAP, SamplerStateClass::ADDRESS_WRAP));
			Set_Sampler(3, Get_Sampler(3)
				.With_Filter(SamplerStateClass::FILTER_LINEAR, SamplerStateClass::FILTER_LINEAR)
				.With_Address(SamplerStateClass::ADDRESS_WRAP, SamplerStateClass::ADDRESS_WRAP));
		}
		else if (useUiShader) {
			if (!m_bUnitShaderBound) {
				s_dwOriginalPS = Pixel_Shader;
				m_bUnitShaderBound = true;
			}
			Set_Vertex_Shader(m_dwUiVS);
			Set_Pixel_Shader(m_dwUiPS);
#ifdef RTS_DEBUG
			diagRouteBit = 256u;
#endif
			// Identity in practice -- Render2DClass nulls all three matrices and builds its
			// vertices in clip space -- but concatenated and passed rather than assumed, so
			// that a 2D drawer which does set up a projection routes here unchanged.
			D3DXMATRIX world = *reinterpret_cast<const D3DXMATRIX*>(&render_state.world);
			D3DXMATRIX view  = *reinterpret_cast<const D3DXMATRIX*>(&render_state.view);
			const D3DXMATRIX proj =
				*reinterpret_cast<const D3DXMATRIX*>(&DX8Transforms[D3DTS_PROJECTION]);
			D3DXMATRIX wvp;
			D3DXMatrixMultiply(&wvp, &world, &view);
			D3DXMatrixMultiply(&wvp, &wvp, &proj);
			Set_Vertex_Shader_Constant(0, &wvp, 4);

			// Whether stage 0 actually samples, asked of the combine rather than of the
			// binding. "Is a texture bound" is the wrong question: the 2D renderer leaves
			// the last texture bound on the device and turns *texturing* off in the
			// ShaderClass instead, so a line or a solid rectangle reaches here with a
			// perfectly valid texture attached that the fixed-function stage was ignoring.
			// Reading the binding drew the radar's camera box in whatever colour happened
			// to sit at its UV in the leftover atlas, which was black.
			const DWORD s0COp   = TextureStageStates[0][D3DTSS_COLOROP];
			const DWORD s0CArg1 = TextureStageStates[0][D3DTSS_COLORARG1] & D3DTA_SELECTMASK;
			const DWORD s0CArg2 = TextureStageStates[0][D3DTSS_COLORARG2] & D3DTA_SELECTMASK;
			bool colourUsesTexture;
			if (s0COp == D3DTOP_DISABLE)           colourUsesTexture = false;
			else if (s0COp == D3DTOP_SELECTARG1)   colourUsesTexture = (s0CArg1 == D3DTA_TEXTURE);
			else if (s0COp == D3DTOP_SELECTARG2)   colourUsesTexture = (s0CArg2 == D3DTA_TEXTURE);
			else colourUsesTexture = (s0CArg1 == D3DTA_TEXTURE || s0CArg2 == D3DTA_TEXTURE);

			const DWORD s0AOp   = TextureStageStates[0][D3DTSS_ALPHAOP];
			const DWORD s0AArg1 = TextureStageStates[0][D3DTSS_ALPHAARG1] & D3DTA_SELECTMASK;
			const DWORD s0AArg2 = TextureStageStates[0][D3DTSS_ALPHAARG2] & D3DTA_SELECTMASK;
			bool alphaUsesTexture;
			if (s0AOp == D3DTOP_DISABLE)           alphaUsesTexture = false;
			else if (s0AOp == D3DTOP_SELECTARG1)   alphaUsesTexture = (s0AArg1 == D3DTA_TEXTURE);
			else if (s0AOp == D3DTOP_SELECTARG2)   alphaUsesTexture = (s0AArg2 == D3DTA_TEXTURE);
			else alphaUsesTexture = (s0AArg1 == D3DTA_TEXTURE || s0AArg2 == D3DTA_TEXTURE);

			const bool haveTexture = render_state.Textures[0] != nullptr;
			// x gates the texture colour, z the texture alpha, y desaturates.
			const D3DXVECTOR4 uiCtl(
				(haveTexture && colourUsesTexture) ? 1.0f : 0.0f,
				m_uiGreyscale ? 1.0f : 0.0f,
				(haveTexture && alphaUsesTexture) ? 1.0f : 0.0f, 0.0f);
			Set_Pixel_Shader_Constant(0, &uiCtl, 1);
		}
		else if (useRoadShader) {
			if (!m_bUnitShaderBound) {
				s_dwOriginalPS = Pixel_Shader;
				m_bUnitShaderBound = true;
			}
			Set_Vertex_Shader(m_dwRoadVS);
			Set_Pixel_Shader(m_dwRoadPS);
#ifdef RTS_DEBUG
			diagRouteBit = 64u;
#endif

			D3DXMATRIX world = *reinterpret_cast<const D3DXMATRIX*>(&render_state.world);
			D3DXMATRIX view  = *reinterpret_cast<const D3DXMATRIX*>(&render_state.view);
			const D3DXMATRIX proj =
				*reinterpret_cast<const D3DXMATRIX*>(&DX8Transforms[D3DTS_PROJECTION]);
			D3DXMATRIX wvp;
			D3DXMatrixMultiply(&wvp, &world, &view);
			D3DXMatrixMultiply(&wvp, &wvp, &proj);
			Set_Vertex_Shader_Constant(0, &wvp, 4);
			// Sun view-projection (VS c5) and the shadow map on stage 5, exactly as the
			// terrain gets them -- this is what the fixed-function road path could not do.
			Set_Vertex_Shader_Constant(5, reinterpret_cast<const D3DXMATRIX*>(m_sunVP), 4);
			Set_Pixel_Shader_Constant(1, m_shadowParams, 1);   // bias + strength + texel
			if (m_pShadowMap != nullptr) {
				Set_DX8_Texture(5, m_pShadowMap);
				s_shadowStage5Bound = true;
				Set_Sampler(5, Get_Sampler(5)
					.With_Filter(SamplerStateClass::FILTER_POINT, SamplerStateClass::FILTER_POINT)
					.With_Address(SamplerStateClass::ADDRESS_CLAMP, SamplerStateClass::ADDRESS_CLAMP));
			}

			// The road texture is an atlas: clamp, and filter smoothly (the fixed-function
			// road path set trilinear here when the terrain was set to, point otherwise).
			Set_Sampler(0, Get_Sampler(0)
				.With_Filter(SamplerStateClass::FILTER_LINEAR, SamplerStateClass::FILTER_LINEAR)
				.With_Mip_Filter(SamplerStateClass::FILTER_LINEAR)
				.With_Address(SamplerStateClass::ADDRESS_CLAMP, SamplerStateClass::ADDRESS_CLAMP));

			// Cloud/noise overlays share the terrain's per-frame parameters: same textures,
			// same scroll, same projection. Written through the device for the same reason
			// the terrain path does -- see the note there about a device reset leaving the
			// redundant-set cache claiming values the device no longer holds.
			// c4 is now the two cloud layers' world-space drift rather than one UV offset;
			// the shader divides each by its own projection period.
			D3DXVECTOR4 cloudOffset(m_cloudScrollAX, m_cloudScrollAY, m_cloudScrollBX, m_cloudScrollBY);
			GFXCALL(Set_Vertex_Shader_Constants(4, reinterpret_cast<const float*>(&cloudOffset), 1));
			Vertex_Shader_Constants[4] = *reinterpret_cast<const Vector4*>(&cloudOffset);
			D3DXVECTOR4 overlayEnable(m_terrainCloudEnable ? 1.0f : 0.0f,
									  m_terrainNoiseEnable ? 1.0f : 0.0f,
									  m_cloudStrength, 0.0f);
			GFXCALL(Set_Pixel_Shader_Constants(0, reinterpret_cast<const float*>(&overlayEnable), 1));
			Pixel_Shader_Constants[0] = *reinterpret_cast<const Vector4*>(&overlayEnable);
			Set_Sampler(2, Get_Sampler(2)
				.With_Filter(SamplerStateClass::FILTER_LINEAR, SamplerStateClass::FILTER_LINEAR)
				.With_Address(SamplerStateClass::ADDRESS_WRAP, SamplerStateClass::ADDRESS_WRAP));
			Set_Sampler(3, Get_Sampler(3)
				.With_Filter(SamplerStateClass::FILTER_LINEAR, SamplerStateClass::FILTER_LINEAR)
				.With_Address(SamplerStateClass::ADDRESS_WRAP, SamplerStateClass::ADDRESS_WRAP));
		}
		else if (useWaterShader) {
			if (!m_bUnitShaderBound) {
				s_dwOriginalPS = Pixel_Shader;
				m_bUnitShaderBound = true;
			}
			Set_Vertex_Shader(m_dwWaterVS);
			Set_Pixel_Shader(m_dwWaterPS);
#ifdef RTS_DEBUG
			diagRouteBit = 128u;
#endif
#ifdef RTS_DEBUG
			++s_waterRoutedDraws;
#endif

			D3DXMATRIX world = *reinterpret_cast<const D3DXMATRIX*>(&render_state.world);
			D3DXMATRIX view  = *reinterpret_cast<const D3DXMATRIX*>(&render_state.view);
			const D3DXMATRIX proj =
				*reinterpret_cast<const D3DXMATRIX*>(&DX8Transforms[D3DTS_PROJECTION]);
			D3DXMATRIX wvp;
			D3DXMatrixMultiply(&wvp, &world, &view);
			D3DXMatrixMultiply(&wvp, &wvp, &proj);
			Set_Vertex_Shader_Constant(0, &wvp, 4);
			// World on its own as well, because the pixel shader needs the world position:
			// the depth lookup, the shroud and noise projections and the wave field are all
			// functions of it. The water builds its vertices in world space with an identity
			// world matrix, so this is identity today -- passed rather than assumed so the
			// grid-mesh water, which does carry a transform, can be routed here unchanged.
			Set_Vertex_Shader_Constant(4, &world, 4);

			// Camera position, from the inverse view, the same way the PBR path derives it.
			D3DXMATRIX viewInv;
			D3DXMatrixInverse(&viewInv, nullptr, &view);
			const D3DXVECTOR4 cameraPos(viewInv._41, viewInv._42, viewInv._43, 1.0f);

			// Whether the depth prepass actually ran this frame. Without it stage 7 holds
			// nothing meaningful and the shader has to fall back to treating the column as
			// deep -- which is the old constant-opacity look, and the right thing to
			// degrade to. Passed as part of the control vector rather than inferred from
			// SsrParams.x, which is the SSR *strength* and is zeroed by the routing mask
			// while the depth pass carries on running.
			Vector4 waterCtl = m_waterCtl;
			waterCtl.Z = (m_pSceneDepth != nullptr) ? 1.0f : 0.0f;

			Set_Pixel_Shader_Constant(0,  &waterCtl, 1);
			Set_Pixel_Shader_Constant(1,  &m_waterDepthCtl, 1);
			Set_Pixel_Shader_Constant(2,  &m_waterShallowTint, 1);
			Set_Pixel_Shader_Constant(3,  &m_waterDeepTint, 1);
			Set_Pixel_Shader_Constant(4,  &m_waterReflCtl, 1);
			Set_Pixel_Shader_Constant(5,  &m_waterSunDir, 1);
			Set_Pixel_Shader_Constant(6,  &m_waterSunCol, 1);
			Set_Pixel_Shader_Constant(7,  &m_waterWaveCtl, 1);
			Set_Pixel_Shader_Constant(8,  &m_waterShroudUV, 1);
			Set_Pixel_Shader_Constant(9,  &m_waterNoiseUV, 1);
			Set_Pixel_Shader_Constant(10, &cameraPos, 1);
			Set_Pixel_Shader_Constant(11, &m_waterBlendCtl, 1);
			Set_Pixel_Shader_Constant(12, reinterpret_cast<const D3DXMATRIX*>(m_sunVP), 4);
			Set_Pixel_Shader_Constant(16, m_shadowParams, 1);
			Set_Pixel_Shader_Constant(17, m_ssrParams, 1);
			// The very matrix the depth prepass rendered with. The shader reprojects this
			// pixel's world position through it to find its own screen UV, so any other
			// copy -- even one that is merely close -- puts the depth lookup a fraction of
			// a pixel out, and near a silhouette a fraction of a pixel is the difference
			// between the river bed and the tank standing in it.
			Set_Pixel_Shader_Constant(18,
				reinterpret_cast<const D3DXMATRIX*>(m_depthVP), 4);   // c18-21
			Set_Pixel_Shader_Constant(24, &m_waterFoamCtl, 1);
			Set_Pixel_Shader_Constant(25, &m_waterFoamCol, 1);
			// Refraction only claims to be available when the grab texture exists. The
			// shader multiplies the whole term out rather than branching around a sample,
			// so a missing grab has to say so here or it reads an unbound stage.
			Vector4 refractCtl = m_waterRefractCtl;
			refractCtl.Y = (m_pRefraction != nullptr) ? 1.0f : 0.0f;
			Set_Pixel_Shader_Constant(26, &refractCtl, 1);
			Set_Pixel_Shader_Constant(27, &m_waterAbsorb, 1);

			// Stage 1 is the refraction grab. It used to be the sparkle texture, which is
			// what paid for it: with a real sun glint and shoreline foam in the shader,
			// that overlay was drawing a second, dimmer version of the effect the glint
			// already produces, and the shader still derives an equivalent shimmer from the
			// noise texture on stage 2. The wrapper's texture cache is eight stages deep
			// (MAX_TEXTURE_STAGES), so a ninth would have to be written straight to the
			// device with nothing tracking it back off -- which is exactly the kind of
			// leaked binding the restore paths here exist to prevent.
			if (m_pRefraction != nullptr) {
				Set_DX8_Texture(1, m_pRefraction);
				// Same flag the PBR path raises for its ORM map: it is what makes the next
				// non-water draw put stage 1 back from the tracked texture state.
				s_pbrOrmBound = true;
				Set_Sampler(1, Get_Sampler(1)
					.With_Filter(SamplerStateClass::FILTER_LINEAR, SamplerStateClass::FILTER_LINEAR)
					.With_Mip_Filter(SamplerStateClass::FILTER_NONE)
					.With_Address(SamplerStateClass::ADDRESS_CLAMP, SamplerStateClass::ADDRESS_CLAMP));
			}

			// Stages 0, 2 and 3 (base, noise, edge ramp) are bound and filtered by the
			// water object itself, as they were on the fixed-function path. What follows is
			// only the stages the shader path adds.
			if (m_envCubeMap != nullptr) {
				Set_DX8_Texture(4, m_envCubeMap);
				s_pbrExtraStagesBound = true;
				Set_Sampler(4, Get_Sampler(4)
					.With_Filter(SamplerStateClass::FILTER_LINEAR, SamplerStateClass::FILTER_LINEAR)
					.With_Mip_Filter(SamplerStateClass::FILTER_LINEAR)
					.With_Address(SamplerStateClass::ADDRESS_CLAMP, SamplerStateClass::ADDRESS_CLAMP));
			}
			if (m_pShadowMap != nullptr) {
				Set_DX8_Texture(5, m_pShadowMap);
				s_shadowStage5Bound = true;
				Set_Sampler(5, Get_Sampler(5)
					.With_Filter(SamplerStateClass::FILTER_POINT, SamplerStateClass::FILTER_POINT)
					.With_Address(SamplerStateClass::ADDRESS_CLAMP, SamplerStateClass::ADDRESS_CLAMP));
			}
			// The shroud on 6, where the PBR path keeps the scene colour. Nothing samples
			// both, and putting it here leaves 0-3 exactly as the old path arranged them.
			// Bound whenever it exists, like the shadow map: the shader samples it in code
			// the compiler cannot skip, so a null stage would be an undefined read rather
			// than an unshrouded pixel. The water object binds a 1x1 white texture when the
			// map has no shroud.
			if (m_pWaterShroud != nullptr) {
				Set_DX8_Texture(6, m_pWaterShroud);
				s_pbrExtraStagesBound = true;
				Set_Sampler(6, Get_Sampler(6)
					.With_Filter(SamplerStateClass::FILTER_LINEAR, SamplerStateClass::FILTER_LINEAR)
					.With_Address(SamplerStateClass::ADDRESS_CLAMP, SamplerStateClass::ADDRESS_CLAMP));
			}
			if (m_pSceneDepth != nullptr) {
				// Point, as everywhere else packed depth is read: the three channels are
				// one number and interpolating them blends nonsense.
				Set_DX8_Texture(7, m_pSceneDepth);
				s_pbrExtraStagesBound = true;
				Set_Sampler(7, Get_Sampler(7)
					.With_Filter(SamplerStateClass::FILTER_POINT, SamplerStateClass::FILTER_POINT)
					.With_Address(SamplerStateClass::ADDRESS_CLAMP, SamplerStateClass::ADDRESS_CLAMP));
			}
		}
		else if (useUnitShader) {
			if (!m_bUnitShaderBound) {
				s_dwOriginalPS = Pixel_Shader;
				m_bUnitShaderBound = true;
			}

			// Material ambient/emissive (house-colour tint lives in the material
			// ambient; white for normal meshes). Computed first because it also gates
			// PBR below.
			D3DXVECTOR4 matAmbient(1.0f, 1.0f, 1.0f, 1.0f);
			D3DXVECTOR4 matDiffuse(1.0f, 1.0f, 1.0f, 1.0f);
			D3DXVECTOR4 matEmissive(0.0f, 0.0f, 0.0f, 0.0f);
			float matOpacity = 1.0f;
			// Read from the wrapper's own copy rather than with GetMaterial. The device no
			// longer has it -- a material is fixed-function vertex lighting and is not sent
			// unless a draw actually needs it -- and asking for it back was a round trip per
			// draw for a value we set ourselves.
			const D3DMATERIAL8 & mtl = CurrentMaterial;
			matAmbient  = D3DXVECTOR4(mtl.Ambient.r,  mtl.Ambient.g,  mtl.Ambient.b,  1.0f);
			matDiffuse  = D3DXVECTOR4(mtl.Diffuse.r,  mtl.Diffuse.g,  mtl.Diffuse.b,  1.0f);
			matEmissive = D3DXVECTOR4(mtl.Emissive.r, mtl.Emissive.g, mtl.Emissive.b, 0.0f);
			matOpacity  = mtl.Diffuse.a; // stealth/translucency rides in the material alpha

			// Diffuse (stealth) opacity handling. The fixed-function pipeline sourced the
			// vertex diffuse -- including the alpha that stealth translucency sets via the
			// material (DiffuseColorSource == MATERIAL) -- from the material for lit meshes.
			// Our shaders instead force lit alpha to 1 (M3) or pass a meaningless vertex
			// COLOR0 alpha through (PBR), so material-driven opacity was lost and stealthed
			// units rendered either fully opaque or (on the PBR path, where COLOR0.a is
			// undefined) vanished. Feed the material opacity in: lit meshes use it, pre-lit
			// meshes keep their genuine per-vertex alpha.
			//
			// Taken from the vertex material, which is what decides it: Apply() sets
			// D3DRS_LIGHTING from UseLighting, or forces it off for a null material and
			// under Is_Coloring_Enabled. Both of those are reproduced here. This is the
			// one fact in the whole routing block that separates a pre-lit mesh -- one
			// whose colour is already in its vertices or its material, like the W3D mouse
			// cursor -- from a mesh that expects to be lit, and reading it back off the
			// device left it a draw-order-dependent guess like the blend states above.
			// Gated the same way as the blend states: the vertex material describes the
			// draw only when the draw came from the mesh renderer.
			VertexMaterialClass * meshMaterial =
				const_cast<VertexMaterialClass*>(render_state.material);
			// (litMesh itself is classified with the blend states at the top of the
			// block, because the technique check needs it there too.)
			// Whether the diffuse alpha comes from the material or from the vertex. The
			// fixed-function pipeline takes it from the material only when told to, and
			// the engine says so per vertex material (VertexMaterialClass sets
			// D3DRS_DIFFUSEMATERIALSOURCE from its DiffuseColorSource); the default is
			// the vertex colour. Stealth translucency is the case that wants the
			// material, which is what the opacity is read for -- but substituting it for
			// every lit mesh throws away per-vertex alpha, and geometry that feathers
			// with it loses out. Bridges blend into the terrain that way and their pass
			// is alpha tested, so the wrong alpha does not just mis-blend, it discards
			// the wrong fragments and mottles the deck.
			const bool diffuseAlphaFromMaterial = litMesh &&
				(shaderDescribesDraw
					? meshMaterial->Get_Diffuse_Color_Source() == VertexMaterialClass::MATERIAL
					: RenderStates[D3DRS_DIFFUSEMATERIALSOURCE] == D3DMCS_MATERIAL);

			D3DXVECTOR4 alphaCtl(matOpacity, diffuseAlphaFromMaterial ? 1.0f : 0.0f, 0.0f, 0.0f);
			// House-colour meshes carry the team tint in a non-white material ambient
			// over a white texture; their procedurally-generated ORM reads that bright
			// texture as near-metallic, which PBR would render as dark metal instead of
			// a tint. So their authored map is not trusted (unless SHADER_ROUTE_PBR_TEAMCOLOR
			// says otherwise) -- but they are still shaded by PBR, on the neutral default
			// map, whose metallic is zero and so cannot make that mistake.
			const bool houseColoured =
				(matAmbient.x < 0.95f || matAmbient.y < 0.95f || matAmbient.z < 0.95f);

			// PBR path: every eligible object mesh runs the metallic-roughness shader, not
			// only the ones that ship an ORM sibling (<name>_orm). A mesh with no map of its
			// own is bound the neutral default (m_defaultOrmMap) instead, so it is shaded by
			// the same BRDF, under the same lights and the same cast shadows, as an HD unit --
			// with unoccluded, fully dielectric, mostly-rough values chosen to land close to
			// what the fixed-function pipeline drew.
			//
			// Before this the ORM maps decided which meshes got the new shading at all, and
			// since only the HD set ships them that left the great majority of faction units
			// and structures on the M3 shader: two renderers' worth of surface response
			// standing next to each other in the same frame.
			//
			// PBR needs texture stage 1 for the ORM map and reads the mesh's own coordinates,
			// so a pass that already carries a detail texture there or wants generated
			// coordinates keeps the plain unit shader.
			// Off unless SHADER_ROUTE_PBR is selected, so the default build renders exactly
			// what the milestone before it did and PBR can be A/B'd in game.
			const bool pbrRoutingOn = (m_shaderRoutingMask & SHADER_ROUTE_PBR) != 0;
			// Opt-out restoring the old gate: PBR only where an ORM map actually exists, so
			// the widened routing can be compared against what it replaced.
			const bool pbrAuthoredOnly = (m_shaderRoutingMask & SHADER_ROUTE_PBR_AUTHORED_ONLY) != 0;
			// The house-colour exclusion above is about procedurally generated ORM maps
			// misreading a white team-colour texture as metal. Where the map was authored
			// on purpose it should be obeyed instead -- and since every player-owned unit
			// carries a team tint, the exclusion otherwise keeps those maps off all of them,
			// which leaves it applying to almost nothing anyone looks at.
			const bool pbrTeamColour = (m_shaderRoutingMask & SHADER_ROUTE_PBR_TEAMCOLOR) != 0;
			// Translucent geometry is never a PBR surface. A metallic-roughness BRDF
			// describes light reflecting off an opaque solid; a blended sprite is an
			// effect whose appearance comes from its texture and its blend equation. The
			// plain unit shader already declines additive draws; PBR had no blend check of
			// any kind, and widening it to every mesh must not widen it to geometry that
			// was never a surface.
			//
			// Helicopter rotor discs are the case that exposed this: blended, no alpha
			// test, carrying an authored ORM map, so the gate claimed them -- and they
			// vanished completely. Measured on the actual draw: fvf 0x252, blend on,
			// SRCALPHA/INVSRCALPHA, alpha test off. Forcing the shader to output alpha 1
			// did not bring them back, so this is not the alpha or the blend maths; the
			// geometry is not surviving the programmable path at all. Whatever the vertex
			// stage does to it, an effect disc should never have been in there.
			//
			// PBR had no blend check of any kind. These conditions gate the default map
			// exactly as they gated the authored one -- widening PBR to every mesh must
			// not widen it to geometry that was never a surface. This matters more now
			// that effects reach the programmable path at all: the plain unit shader
			// used to decline them on the way in, and no longer does.
			// A base texture is required, because this shader has no way to do without one.
			//
			// unit_ps is told whether stage 0 is bound (TexCtl.x) and folds the sample to
			// white when it is not, which reproduces the fixed-function untextured pass
			// exactly: the colour then comes from the lit equation over the material.
			// unit_pbr_ps has no such control. It samples AlbedoSampler unconditionally,
			// so an unbound stage reads undefined -- black in practice -- and
			// `albedo = SrgbToLinear(albedoTex.rgb) * MatAmbient.rgb` carries that through
			// everything downstream. No amount of light brings a black albedo back.
			//
			// The goto move hint is exactly this. Measured: SCMOVEHINT.CYLINDER02-05, FVF
			// 0x12 -- position and normal only, no texture coordinates and no vertex
			// colour at all -- stage 0 SELECTARG2/DIFFUSE, no texture bound, drawn in two
			// passes whose vertex materials carry black and the player colour. Its whole
			// appearance is the material, and PBR multiplies the material by an albedo it
			// invented from an unbound sampler.
			//
			// Checked like the detail combine and the texgen sources: a pass the
			// programmable path cannot reproduce does not go to it. There is nothing to
			// reconstruct here either -- a metallic-roughness BRDF is defined over an
			// albedo map, and a mesh that has none is not a PBR surface however opaque and
			// well lit it is.
			// Only a SURFACE. PRELIT means the colour is already decided and no lighting
			// equation should touch it, which is the one thing a BRDF cannot honour --
			// unit_pbr_ps has no emissive term and reads the vertex colour only for its
			// alpha, so it re-derives the colour from albedo and light and discards
			// whatever the asset had put there. Draws with no technique keep the old
			// behaviour; the blend tests below are what stood in for this for them.
			const bool pbrKindAllows =
				m_meshTechnique == MESH_TECHNIQUE_SURFACE ||
				m_meshTechnique == MESH_TECHNIQUE_UNCLASSIFIED;

			const bool pbrEligible =
				pbrRoutingOn &&
				pbrKindAllows &&
				render_state.Textures[0] != nullptr &&
				singleTexture && !texgenActive &&
				// unit_pbr_vs carries one coordinate set, like unit_vs. A stage reading
				// the mesh's second one is not a surface this shader can shade.
				!needsUvSet1 &&
				(curFVF & D3DFVF_NORMAL) != 0 &&
				!additiveBlend && !softBlendedOverlay &&
				m_dwUnitPbrVS != 0 && m_dwUnitPbrPS != 0;

			// The resolver caches a nullptr for meshes without a map, so asking costs a hash
			// lookup after the first draw.
			TextureBaseClass* ormTex = nullptr;
			if (pbrEligible && (!houseColoured || pbrTeamColour) && s_ormResolver != nullptr) {
				ormTex = s_ormResolver(render_state.Textures[0]);
			}
			// No map of its own -- shade it by PBR anyway, on the neutral default.
			const bool useDefaultOrm =
				pbrEligible && ormTex == nullptr &&
				!pbrAuthoredOnly && m_defaultOrmMap != nullptr;
			const bool usePbr = (ormTex != nullptr) || useDefaultOrm;

			// Three variants: PBR, the detail (stage 1) combine, or the plain shader. The
			// detail variant is bound only when a second texture is really present -- the
			// single-texture shader must never sample a stage with no texture bound, which
			// is undefined and can produce values that survive a zero weight (a NaN times
			// zero is still NaN) and render the pixel black.
			const bool useDetailShader = !usePbr && !singleTexture && detailCombineSupported;
			diagRouteBit = usePbr ? 8u : (useDetailShader ? 4u : 2u);
			// 3 = PBR on the mesh's own map, 4 = PBR on the default, 5 = PBR on the
			// default and team-tinted (the meshes the house-colour rule used to hold
			// back from PBR entirely, so the category that has to be non-empty for the
			// faction units and buildings to have actually moved).
			diagCensusCat = usePbr ? (ormTex != nullptr ? 3u : (houseColoured ? 5u : 4u))
								   : (useDetailShader ? 2u : 1u);
			// The vertex shader is chosen by what the FVF supplies, not by what the draw
			// would like: geometry with no normal takes the pre-lit variant, a pass
			// reading the mesh's second coordinate set takes the two-set variant, and
			// each declares exactly the inputs its format provides.
			Set_Vertex_Shader(usePbr ? m_dwUnitPbrVS
									 : (useUv2Shader ? m_dwUnitUv2VS
													 : (hasNormal ? m_dwUnitVS : m_dwUnitPrelitVS)));
			Set_Pixel_Shader(usePbr ? m_dwUnitPbrPS
								    : (useDetailShader ? m_dwUnitDetailPS : m_dwUnitPS));

			// World/view/projection. row_major HLSL float4x4 with mul(v,M) takes the
			// D3D row-major matrices as-is; both unit shaders read the clip-space
			// transform at c0-3 and their object -> shading space matrix at c4-7.
			D3DXMATRIX world = *reinterpret_cast<const D3DXMATRIX*>(&render_state.world);
			D3DXMATRIX view  = *reinterpret_cast<const D3DXMATRIX*>(&render_state.view);
			const D3DXMATRIX proj =
				*reinterpret_cast<const D3DXMATRIX*>(&DX8Transforms[D3DTS_PROJECTION]);
			D3DXMATRIX worldView;
			D3DXMatrixMultiply(&worldView, &world, &view);
			D3DXMATRIX wvp;
			D3DXMatrixMultiply(&wvp, &worldView, &proj);
			// c0 = world*view*proj (position), the same for either shader.
			Set_Vertex_Shader_Constant(0, &wvp, 4);
			// c4 is the object -> shading space matrix, and the two shaders shade in
			// different spaces:
			//   M3  : camera space. unit_vs declares c8/c10/c12/c14 as camera-space
			//         directions toward the light, so the normal must reach that space
			//         too for N.L to be correct -- hence world*view. The directions
			//         themselves are rotated into it below; they do not arrive that way.
			//   PBR : world space. It reconstructs a view vector from a world-space
			//         camera position and indexes a cubemap baked in world space, so it
			//         needs the plain world matrix; the light directions are already
			//         world space and are handed over untouched. Feeding it world*view
			//         (as the M3 shader wants) leaves position and camera in different
			//         spaces, which makes every view-dependent term swing with the camera.
			Set_Vertex_Shader_Constant(4, usePbr ? &world : &worldView, 4);

			// Cloud shadow for meshes. The same field, drift and depth the ground gets --
			// a shadow that sweeps the terrain has to sweep what is standing on it, and
			// until now it stopped dead at the ground. The two shader families differ
			// only in where the constants land and in how they reach world space.
			if (m_pCloudMap != nullptr) {
				Set_DX8_Texture(2, m_pCloudMap);
				// Stage 2 is bound straight to the device here, so it joins the set that
				// gets taken off again -- a texture left on a stage a later fixed-function
				// draw never asked for is a bug this renderer has shipped twice.
				s_pbrExtraStagesBound = true;
				Set_Sampler(2, Get_Sampler(2)
					.With_Filter(SamplerStateClass::FILTER_LINEAR, SamplerStateClass::FILTER_LINEAR)
					.With_Mip_Filter(SamplerStateClass::FILTER_LINEAR)
					.With_Address(SamplerStateClass::ADDRESS_WRAP, SamplerStateClass::ADDRESS_WRAP));
			}
			const D3DXVECTOR4 cloudScroll(m_cloudScrollAX, m_cloudScrollAY,
										  m_cloudScrollBX, m_cloudScrollBY);
			const D3DXVECTOR4 cloudCtl(
				(m_terrainCloudEnable && m_pCloudMap != nullptr) ? 1.0f : 0.0f,
				m_cloudStrength, 0.0f, 0.0f);
			if (usePbr) {
				// PBR already carries the world position it needs.
				Set_Pixel_Shader_Constant(24, &cloudScroll, 1);
				Set_Pixel_Shader_Constant(25, &cloudCtl, 1);
			} else {
				// The M3 path has no world matrix, and needs only where the vertex lands
				// on the ground plane -- so it gets the two columns of world that give
				// that, rather than a whole matrix it would use twice. Row-vector
				// convention, so these are columns 0 and 1.
				const D3DXVECTOR4 worldAxisX(world._11, world._21, world._31, world._41);
				const D3DXVECTOR4 worldAxisY(world._12, world._22, world._32, world._42);
				Set_Vertex_Shader_Constant(22, &worldAxisX, 1);
				Set_Vertex_Shader_Constant(23, &worldAxisY, 1);
				Set_Pixel_Shader_Constant(10, &cloudScroll, 1);
				Set_Pixel_Shader_Constant(11, &cloudCtl, 1);
			}

			// Cast shadows for the M3 path. PBR reprojects per pixel from the world
			// position it already carries; this shader has only object->camera, so the
			// object->sun-clip matrix is combined here and the sun-space position comes
			// out of the vertex shader instead. Without this only PBR meshes -- the
			// minority, since PBR needs an ORM map -- received any shadow at all.
			if (!usePbr) {
				D3DXMATRIX worldSunVP;
				D3DXMatrixMultiply(&worldSunVP, &world,
					reinterpret_cast<const D3DXMATRIX*>(m_sunVP));
				Set_Vertex_Shader_Constant(32, &worldSunVP, 4);
				Set_Pixel_Shader_Constant(8, m_shadowParams, 1);   // bias + strength
				// Normal offset + the small bias that goes with it, but only for the
				// variant that has a normal to offset along. The pre-lit variant exists
				// precisely because its geometry carries none, so it keeps the blanket
				// bias -- it is ground decals, which is what that bias is sized for.
				const float meshShadow[4] = {
					hasNormal ? m_shadowMeshParams[0] : 0.0f,
					hasNormal ? m_shadowMeshParams[1] : m_shadowParams[0],
					0.0f, 0.0f };
				Set_Vertex_Shader_Constant(36, meshShadow, 1);
				Set_Pixel_Shader_Constant(9, meshShadow, 1);
				if (m_pShadowMap != nullptr) {
					Set_DX8_Texture(5, m_pShadowMap);
					s_shadowStage5Bound = true;
					Set_Sampler(5, Get_Sampler(5)
						.With_Filter(SamplerStateClass::FILTER_POINT, SamplerStateClass::FILTER_POINT)
						.With_Address(SamplerStateClass::ADDRESS_CLAMP, SamplerStateClass::ADDRESS_CLAMP));
				}
			}

			// Shared LightEnvironment gather: scene ambient (D3DRS_AMBIENT) + up to
			// four directional lights (disabled lights contribute nothing).
			//
			// These directions are in WORLD space, and the name of the accessor they came
			// through does not say so. Set_Light_Environment fills render_state.Lights from
			// LightEnvironmentClass::Get_Light_Direction, which returns InputLights[i] --
			// the untransformed input. The camera-space copies Pre_Render_Update makes live
			// in OutputLights[i] and are read by nothing outside that class. The fixed
			// function path is the confirmation: D3DLIGHT8 directionals are specified in
			// world space, and that is the same array. Rotate per consumer, below.
			DWORD ambientPacked = RenderStates[D3DRS_AMBIENT];
			D3DXVECTOR4 sceneAmbient(
				((ambientPacked >> 16) & 0xFF) / 255.0f,
				((ambientPacked >>  8) & 0xFF) / 255.0f,
				((ambientPacked      ) & 0xFF) / 255.0f,
				1.0f);
			D3DXVECTOR4 lightDir[4];
			D3DXVECTOR4 lightDiff[4];
			for (int li = 0; li < 4; ++li) {
				lightDir[li]  = D3DXVECTOR4(0.0f, 0.0f, 1.0f, 0.0f);
				lightDiff[li] = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
				if (render_state.LightEnable[li]) {
					lightDir[li].x = -render_state.Lights[li].Direction.x;
					lightDir[li].y = -render_state.Lights[li].Direction.y;
					lightDir[li].z = -render_state.Lights[li].Direction.z;
					lightDiff[li].x = render_state.Lights[li].Diffuse.r;
					lightDiff[li].y = render_state.Lights[li].Diffuse.g;
					lightDiff[li].z = render_state.Lights[li].Diffuse.b;
				}
			}

			if (usePbr) {
				if (ormTex != nullptr) {
					// Bind ORM to stage 1. Apply() (rather than Peek_D3D_Texture) triggers
					// the lazy texture load and updates the applied-texture cache, so the
					// map actually becomes resident instead of staying a null peek forever.
					ormTex->Apply(1);
				} else {
					// No authored map: the neutral default, bound straight to the device the
					// way the cubemap and the shadow map are. It is not a TextureClass and
					// has no asset-manager entry, so there is no Apply() to call and nothing
					// to load lazily -- it is one texel that exists for the whole session.
					Set_DX8_Texture(1, m_defaultOrmMap);
				}
				s_pbrOrmBound = true;
				Set_Sampler(1, Get_Sampler(1)
					.With_Filter(SamplerStateClass::FILTER_LINEAR, SamplerStateClass::FILTER_LINEAR)
					.With_Mip_Filter(SamplerStateClass::FILTER_LINEAR));
				// Shared environment cubemap on stage 4 (nothing else uses it, so a
				// direct bind cannot desync a shared stage). Linear + clamp.
				if (m_envCubeMap != nullptr) {
					Set_DX8_Texture(4, m_envCubeMap);
					s_pbrExtraStagesBound = true;
					// The cubemap carries cloud detail and ships a mip chain; without a
					// mip filter the minified case undersamples it and sparkles.
					Set_Sampler(4, Get_Sampler(4)
						.With_Filter(SamplerStateClass::FILTER_LINEAR, SamplerStateClass::FILTER_LINEAR)
						.With_Mip_Filter(SamplerStateClass::FILTER_LINEAR)
						.With_Address(SamplerStateClass::ADDRESS_CLAMP, SamplerStateClass::ADDRESS_CLAMP));
					// c22: mean cubemap colour, so the shader's irradiance tap can be
					// normalised to average 1.0 (see m_envAverage).
					Set_Pixel_Shader_Constant(22, m_envAverage, 1);
				}
				// Directional shadow map on stage 5 (point + clamp: packed depth must not
				// be interpolated; the shader PCFs). SunVP goes in c12-15 so the shader can
				// reproject world position into the sun's clip space and compare.
				if (m_pShadowMap != nullptr) {
					Set_DX8_Texture(5, m_pShadowMap);
					s_shadowStage5Bound = true;
					Set_Sampler(5, Get_Sampler(5)
						.With_Filter(SamplerStateClass::FILTER_POINT, SamplerStateClass::FILTER_POINT)
						.With_Address(SamplerStateClass::ADDRESS_CLAMP, SamplerStateClass::ADDRESS_CLAMP));
				}
				Set_Pixel_Shader_Constant(16, m_shadowParams, 1);   // bias + strength
				// c23: normal offset + the bias left over once the lookup is offset. The
				// PBR shader carries the world normal to the pixel, so it offsets there
				// rather than in its vertex shader as the M3 path does.
				Set_Pixel_Shader_Constant(23, m_shadowMeshParams, 1);
				// Screen-space reflections. Stage 6 carries the previous frame's scene
				// colour (what a ray that hits actually reads) and stage 7 this frame's
				// camera-view packed depth (what it tests against). Both are bound
				// whenever they exist rather than when the feature is on, for the same
				// reason as the shadow map: the shader samples them in code the compiler
				// cannot skip, and D3D9 leaves a read from an unbound stage undefined.
				// The strength in c17 is what actually switches the march off.
				if (m_pSceneColor != nullptr) {
					Set_DX8_Texture(6, m_pSceneColor);
					s_pbrExtraStagesBound = true;
					Set_Sampler(6, Get_Sampler(6)
						.With_Filter(SamplerStateClass::FILTER_LINEAR, SamplerStateClass::FILTER_LINEAR)
						.With_Address(SamplerStateClass::ADDRESS_CLAMP, SamplerStateClass::ADDRESS_CLAMP));
				}
				if (m_pSceneDepth != nullptr) {
					// Point filtering, as for the shadow map: packed depth is three bytes
					// of one number, and interpolating them blends nonsense.
					Set_DX8_Texture(7, m_pSceneDepth);
					s_pbrExtraStagesBound = true;
					Set_Sampler(7, Get_Sampler(7)
						.With_Filter(SamplerStateClass::FILTER_POINT, SamplerStateClass::FILTER_POINT)
						.With_Address(SamplerStateClass::ADDRESS_CLAMP, SamplerStateClass::ADDRESS_CLAMP));
				}
				Set_Pixel_Shader_Constant(17, m_ssrParams, 1);
				// The very matrix the depth prepass rendered with, so the shader's
				// reprojection cannot drift out of step with the depth it is reading.
				Set_Pixel_Shader_Constant(18,
					reinterpret_cast<const D3DXMATRIX*>(m_depthVP), 4);  // c18-21
				// SM3 pixel-shader lighting constants, all in world space (see c4 above).
				// The light directions gathered above are in world space (from LightEnvironment),
				// which matches what unit_pbr_ps expects.
				D3DXMATRIX viewInv;
				D3DXMatrixInverse(&viewInv, nullptr, &view);
				D3DXVECTOR4 cameraPos(viewInv._41, viewInv._42, viewInv._43, 1.0f);
				Set_Pixel_Shader_Constant(0, lightDir, 4);       // c0-3 directions (world space)
				Set_Pixel_Shader_Constant(4, lightDiff, 4);      // c4-7 diffuse
				Set_Pixel_Shader_Constant(8, &sceneAmbient, 1);
				Set_Pixel_Shader_Constant(9, &cameraPos, 1);
				Set_Pixel_Shader_Constant(10, &matAmbient, 1);
				Set_Pixel_Shader_Constant(11, &alphaCtl, 1);   // stealth opacity control
				Set_Pixel_Shader_Constant(12, reinterpret_cast<const D3DXMATRIX*>(m_sunVP), 4); // c12-15 SunVP

				// (The env cubemap used to snapshot light 0 here. It doesn't any more: W3D
				// gives every object its own LightEnvironment, so light 0 is only sometimes
				// the sun, and the bake lurched with draw order. W3DShaderManager reads the
				// map's global lighting instead -- the same source as the shadow frustum.)
			} else {
				// Non-PBR path: lighting and the texture combine come from the constants
				// the unit shaders read. Stage 1 only needs putting back when a previous
				// PBR draw actually left its ORM there.
				Restore_Stage1_After_Pbr();
				Restore_Pbr_Extra_Stages();
				// Soft particles. This must come *after* Restore_Pbr_Extra_Stages above:
				// that call puts stages 2, 4, 6 and 7 back to what render_state holds,
				// which for a sprite is nothing. Binding stage 7 before it meant the bind
				// was undone before the draw and the shader sampled an unbound stage --
				// black, every time, whatever was bound. The symptom was every particle
				// in the frame disappearing, and it survived three wrong diagnoses.
				//
				// Stage 7 is bound whenever a depth map exists, on the same reasoning as
				// the shadow map: the shader samples it in code the compiler cannot skip,
				// and a read from an unbound stage is undefined in D3D9. SoftCtl.x is what
				// actually switches the fade on.
				//
				// c12 is SunVP (c12-15) on the PBR path. Safe because this is written on
				// every non-PBR draw, immediately before it, but worth knowing.
				const bool softProjValid = (m_ssrParams[3] != 0.0f);
				const bool softOn = m_softParticles && (m_pSceneDepth != nullptr) && softProjValid
								  && (m_softParticleFade > 0.0f);
				if (m_pSceneDepth != nullptr) {
					Set_DX8_Texture(7, m_pSceneDepth);
					// Point filtering: packed depth is three bytes of one number and
					// interpolating them blends nonsense.
					Set_Sampler(7, Get_Sampler(7)
						.With_Filter(SamplerStateClass::FILTER_POINT, SamplerStateClass::FILTER_POINT)
						.With_Address(SamplerStateClass::ADDRESS_CLAMP, SamplerStateClass::ADDRESS_CLAMP));
				}
				D3DXVECTOR4 softCtl(softOn ? 1.0f : 0.0f,
									(m_softParticleFade > 0.0f) ? m_softParticleFade : 1.0f,
									m_ssrParams[2], m_ssrParams[3]);
				Set_Pixel_Shader_Constant(12, &softCtl, 1);
				Set_Vertex_Shader_Constant(16, &sceneAmbient, 1);
				for (int li = 0; li < 4; ++li) {
					// Transform world-space light direction to camera space for unit_vs
					D3DXVECTOR3 wrlDir(lightDir[li].x, lightDir[li].y, lightDir[li].z);
					D3DXVECTOR3 camDir;
					D3DXVec3TransformNormal(&camDir, &wrlDir, &view);
					D3DXVec3Normalize(&camDir, &camDir);
					D3DXVECTOR4 lightDirCam(camDir.x, camDir.y, camDir.z, 0.0f);
					Set_Vertex_Shader_Constant(8 + li * 2, &lightDirCam, 1);
					Set_Vertex_Shader_Constant(9 + li * 2, &lightDiff[li], 1);
				}

				// Lighting mode -- mirror what the fixed-function stage-0 colour combine
				// does with the vertex diffuse, not just D3DRS_LIGHTING:
				//   2 = texture only: stage 0 selects the texture alone (SELECTARG1, no
				//       diffuse), e.g. an unlit detail/overlay pass multiplied or added
				//       over the already-lit base in the frame buffer. Lighting it here
				//       would darken it a second time.
				//   1 = lit: stage 0 modulates the texture by the (lit) diffuse.
				//   0 = pre-lit: lighting disabled; pass the baked vertex colour through.
				const DWORD stage0Op   = TextureStageStates[0][D3DTSS_COLOROP];
				const DWORD stage0Arg1 = TextureStageStates[0][D3DTSS_COLORARG1] & D3DTA_SELECTMASK;
				const bool textureOnlyPass =
					(stage0Op == D3DTOP_SELECTARG1 && stage0Arg1 == D3DTA_TEXTURE);
				const float lightMode = textureOnlyPass ? 2.0f : (litMesh ? 1.0f : 0.0f);
				// y tells the no-normal shader where the ambient term comes from. Same
				// distinction as the diffuse alpha: the fixed-function pipeline takes it
				// from the material unless the material says to use the vertex colour.
				const float ambientFromVertex =
					RenderStates[D3DRS_AMBIENTMATERIALSOURCE] == D3DMCS_COLOR1 ? 1.0f : 0.0f;
				// z: effect geometry, which emits rather than reflects and so is shaded by
				// neither the sun nor the clouds. Both vertex shaders take it out through
				// the same shadowReceive gate the texture-only case already used, so a
				// laser is not dimmed by the shadow of the building it passes.
				// w: how much brighter than display white this draw may emit. Above 1 only
				// for additive effect geometry, and the two conditions are both load
				// bearing. "Effect" alone is too broad: smoke and dust are effects too, and
				// they occlude rather than emit, so a gain would make a dust cloud glow.
				// "Additive" alone is too broad the other way: an additive detail pass on a
				// building is a surface treatment, not a light source. Together they name
				// the thing that is actually light being added to the frame -- muzzle
				// flashes, tracers, beams, explosions -- which was authored at the ceiling
				// of a pipeline whose ceiling was 1.0, and has to be told it may go past it
				// because no asset says so.
				//
				// Left at 1 unless HDR is running. On an 8-bit target the hardware clamps
				// the result anyway, so a gain there would only crush the flash flat and
				// lose whatever gradient it had.
				const float effectGain =
					(effectDraw && additiveBlend) ? m_hdrEffectGain : 1.0f;
#ifdef RTS_DEBUG
				if (effectGain > 1.0f)
					++s_censusHdrEmissiveDraws;
#endif
				D3DXVECTOR4 lightingParams(lightMode, ambientFromVertex,
										   effectDraw ? 1.0f : 0.0f, effectGain);
				Set_Vertex_Shader_Constant(17, &lightingParams, 1);
				Set_Vertex_Shader_Constant(18, &matAmbient, 1);
				Set_Vertex_Shader_Constant(19, &matEmissive, 1);
				Set_Vertex_Shader_Constant(20, &matDiffuse, 1);

				// Stage 0's ALPHA combine, mirrored the same way the colour combine is
				// above. Assuming alpha is always texture*diffuse breaks any pass whose
				// alpha is the texture alone: additive effects (muzzle flashes and the
				// like) blend SRCALPHA/ONE, so multiplying in a material opacity the
				// fixed-function pipeline never applied to them makes them vanish.
				// At stage 0 CURRENT is defined to be the diffuse, so it counts as diffuse.
				const DWORD s0AOp   = TextureStageStates[0][D3DTSS_ALPHAOP];
				const DWORD s0AArg1 = TextureStageStates[0][D3DTSS_ALPHAARG1] & D3DTA_SELECTMASK;
				const DWORD s0AArg2 = TextureStageStates[0][D3DTSS_ALPHAARG2] & D3DTA_SELECTMASK;
				bool alphaUsesTexture = true;   // MODULATE and friends: texture * diffuse
				bool alphaUsesDiffuse = true;
				if (s0AOp == D3DTOP_DISABLE) {
					alphaUsesTexture = false;   // stage contributes nothing; diffuse survives
				}
				else if (s0AOp == D3DTOP_SELECTARG1 || s0AOp == D3DTOP_SELECTARG2) {
					const DWORD sel = (s0AOp == D3DTOP_SELECTARG1) ? s0AArg1 : s0AArg2;
					alphaUsesTexture = (sel == D3DTA_TEXTURE);
					alphaUsesDiffuse = (sel == D3DTA_DIFFUSE || sel == D3DTA_CURRENT);
				}

				// Texture control (pixel shader c1). x tells the shader whether a base
				// texture is bound at all; yz resolve the diffuse alpha (lit meshes take
				// it from the material, where stealth translucency lives, as the fixed-
				// function pipeline did, pre-lit meshes keep their vertex alpha, and
				// y=z=1 forces it to 1 when stage 0 does not source the diffuse at all);
				// w gates the texture alpha. They share a register because only registers
				// 0..7 are addressable by the SM2 unit shaders.
				D3DXVECTOR4 texCtl(
					render_state.Textures[0] != nullptr ? 1.0f : 0.0f,
					alphaUsesDiffuse ? matOpacity : 1.0f,
					(!alphaUsesDiffuse || diffuseAlphaFromMaterial) ? 1.0f : 0.0f,
					alphaUsesTexture ? 1.0f : 0.0f);
				Set_Pixel_Shader_Constant(1, &texCtl, 1);

				// Stage 1 (detail) combine, resolved above into per-argument source
				// selectors (texture / current / diffuse) plus a one-hot operation weight,
				// so the shader evaluates the same expression the fixed-function stage
				// would. The defaults make stage 1 a no-op for single-texture passes. The
				// modulate scale rides in the unused w of the first selector, which only
				// uses xyz, rather than taking a register of its own.
				s1CArg1.w = s1CScale;
				Set_Pixel_Shader_Constant(2, &s1CArg1, 1);
				Set_Pixel_Shader_Constant(3, &s1CArg2, 1);
				Set_Pixel_Shader_Constant(4, &s1COp, 1);
				Set_Pixel_Shader_Constant(5, &s1AArg1, 1);
				Set_Pixel_Shader_Constant(6, &s1AArg2, 1);
				Set_Pixel_Shader_Constant(7, &s1AOp, 1);

				// Texture coordinate generation. Modes and matrices are only meaningful
				// when this draw was claimed with texgen enabled; otherwise both stages
				// pass the mesh coordinates through, which is the shader default.
				const bool applyTexGen = texgenRoutingOn && texGenSupported;
				D3DXVECTOR4 texGenCtl(
					applyTexGen ? texGenMode0 : 0.0f,
					applyTexGen ? texGenMode1 : 0.0f,
					(applyTexGen && texGenMatrix0) ? 1.0f : 0.0f,
					(applyTexGen && texGenMatrix1) ? 1.0f : 0.0f);
				Set_Vertex_Shader_Constant(21, &texGenCtl, 1);

				if (applyTexGen && texGenMatrix0) {
					D3DXMATRIX texMat0;
					_Get_DX8_Transform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + 0),
									   *reinterpret_cast<D3DMATRIX*>(&texMat0));
					Set_Vertex_Shader_Constant(24, &texMat0, 4);
				}
				if (applyTexGen && texGenMatrix1) {
					D3DXMATRIX texMat1;
					_Get_DX8_Transform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + 1),
									   *reinterpret_cast<D3DMATRIX*>(&texMat1));
					Set_Vertex_Shader_Constant(28, &texMat1, 4);
				}
			}
		}
		else if (m_bUnitShaderBound) {
			// Non-mesh draw: restore the fixed-function pipeline. For DX8 buffers
			// the vertex-buffer block already re-applied the FVF; restore the pixel
			// shader, and reset the vertex shader when this draw carries an FVF.
			Restore_Stage1_After_Pbr();
			Restore_Stage5_After_Shadow();
			Restore_Pbr_Extra_Stages();
			// A draw that brought its own vertex shader keeps it -- only the pixel
			// shader goes back, since that geometry expects the fixed-function pixel
			// pipeline it would have had before we bound ours.
			Set_Pixel_Shader(s_dwOriginalPS);
			if (curFVF != 0 && !foreignVertexShader) {
				Set_Vertex_Shader(curFVF);
			}
			m_bUnitShaderBound = false;
		}

#ifdef RTS_DEBUG
		// Why this draw stayed on fixed function, if it did. Hoisted out of the block
		// below because the fixed-function attribution wants it for every pass of the
		// frame, not just the ones the split watchdog looks at.
		// A depth-pass draw the depth shaders declined is about to be dropped by Draw()
		// rather than submitted, so it is not a fixed-function draw and must not be
		// counted as one -- it is not a draw at all. Exactly the test Draw() makes, for
		// the same reason: the bound shader is the only thing that cannot disagree with
		// what the device is going to do.
		//
		// Counting these was worth 58984 of the 106067 in the first window after the
		// suppression landed, which read as the change having done nothing.
		const bool suppressedDraw = Is_Inert_Depth_Pass_Draw();

		// Everything below counts draws, so none of it runs for an application of state
		// that is not one. See the note in Draw() for what that excludes and why.
		if (s_applyIsDraw) {
		unsigned ffReason = 0;
		if (diagRouteBit == 1 && !suppressedDraw) {
			// Effect and additive are deliberately absent. They used to head this chain,
			// and they were the first thing it tested, so once they stopped excluding
			// anything every draw that failed a later gate still reported as "effect" --
			// 7514 sorted light fixtures declined for having no vertex format at all were
			// filed under a reason that no longer exists.
			if (!(curFVF & D3DFVF_XYZ) || !(hasNormal || prelitNoNormal)) ffReason = 3;
			else if (foreignVertexShader)                    ffReason = 4;
			else if (!(render_state.Textures[0] != nullptr || untexturedDiffuseOnly)) ffReason = 5;
			else if (!reproducibleBlend)                     ffReason = 6;
			else if (!(singleTexture || detailCombineSupported)) ffReason = 7;
			else if (texgenActive && !(texgenRoutingOn && texGenSupported)) ffReason = 8;
			// The context gates, which the split watchdog does not care about but this
			// does: they are the difference between "this draw needs a shader written
			// for it" and "this draw is in a pass that has no programmable path yet".
			// Without them everything that failed a context test reported as reason 0
			// and the largest groups in the table said nothing at all.
			else if (m_bShadowDepthPass)   ffReason = 9;

			if (ffReason == 9) Debug_Note_Depth_Pass_Stencil();
			else if (m_bTerrainShaderPass) ffReason = 10;
			else if (m_bRoadShaderPass)    ffReason = 11;
			else if (m_bWaterShaderPass)   ffReason = 12;
			else if (m_bMaskPass)          ffReason = 16;
			else if (render_state_changed & (unsigned)VIEW_IDENTITY) ffReason = 13;
			else if (m_meshTechnique == MESH_TECHNIQUE_FIXED_FUNCTION) ffReason = 14;
			else if (keepEffectsOnFF)      ffReason = 2;    // held back on purpose
			else if (routingDisabled || m_dwUnitVS == 0 || m_dwUnitPS == 0) ffReason = 15;

			// Attributed across the whole frame, including the terrain, road, water and
			// shadow passes the watchdog excludes -- those passes route *some* of their
			// draws and leave the rest behind (shorelines, extra blend tiles, the water
			// types the shader does not cover), and that remainder is exactly the work
			// this is meant to size.
			Debug_Note_FF_Draw(render_state.Textures[0], curFVF,
				(render_state_changed & (unsigned)VIEW_IDENTITY) != 0, ffReason);
		}
		else if (suppressedDraw) {
			Debug_Note_Suppressed_Draw();
		}
		else {
			Debug_Note_Routed_Draw();
		}

		// Name-substring watch. Every draw whose mesh matches, with everything that
		// decides its fate -- the recipe that has worked before, because deduplicating
		// by name hides passes and a per-session cap fills up before the thing you are
		// looking for is ever built.
		// Watch the one-mesh-one-pipeline invariant. Only mesh draws take part: the
		// shadow-depth pass, the terrain and now the roads are each drawn by a single
		// pipeline by construction.
		// The mask pass joins them: it redraws the entire scene through one shader by
		// construction, so every mesh in it legitimately routes somewhere other than where
		// the same mesh went a moment earlier in the real pass. Counting that as a split
		// would report the feature working as the invariant breaking.
		if (!m_bShadowDepthPass && !m_bTerrainShaderPass && !m_bRoadShaderPass &&
			!m_bWaterShaderPass && !m_bMaskPass) {
			Debug_Note_Mesh_Routing(diagRouteBit, ffReason);
			Debug_Note_Routing_Census(diagCensusCat);
			if (!classifiedDraw && (curFVF & D3DFVF_XYZ) &&
				!(render_state_changed & (unsigned)VIEW_IDENTITY))
				Debug_Note_Unclassified_Draw(render_state.Textures[0], curFVF,
					diagRouteBit != 1u, alphaBlendOn, softBlendedOverlay);
		}
		}
#endif
		// Debug visualization overrides, last of all: everything above has finished
		// deciding and binding, so what this recolours a draw by is what the device is
		// actually about to do.
		//
		// The 2D pass is excluded, and the fill mode is stated for it rather than left
		// alone: D3DRS_FILLMODE is device state, not per-draw state, so an excluded pass
		// does not merely miss the override -- it *inherits* whatever the last scene draw
		// left. Without this the control bar and the on-screen text draw in wireframe too,
		// and the banner naming the mode becomes unreadable.
		//
		// Identified by D3DFVF_XYZRHW, which is what every screen-space draw in the frame
		// still carries. That is a property of the interface being fixed function, so this
		// test is only correct for as long as that is true.
		//
		// Debug builds only: there is no reason to carry a per-draw branch into a release
		// build for a mode it can never enter.
#ifdef RTS_DEBUG
		// s_applyIsDraw and the suppression flag, for the same reason the census carries
		// them: most calls here are not draws, and recolouring state that no draw is
		// about to use paints nothing while still leaving the fill mode behind it.
		if (m_debugVisMode != DEBUG_VIS_OFF && s_applyIsDraw && !Is_Inert_Depth_Pass_Draw()) {
			// The excluded passes used to be identified by D3DFVF_XYZRHW, which was only
			// ever a proxy for "the interface" -- and it stops being one here, now that the
			// interface is drawn by a vertex shader from untransformed positions. Ask the
			// pass flags instead, which say what the draw is rather than what format it
			// happens to arrive in.
			//
			// The shadow-depth pass is excluded because its output is read back as data
			// rather than looked at: flat-shading it would write a constant colour where
			// packed depth belongs, and every shadow in the scene would move to wherever
			// that constant decodes to. The 2D pass is excluded for a plainer reason -- it
			// draws the control bar, the cursor and the banner naming the mode, and tinting
			// the interface flat would take away the legend for the colours.
			// The mask pass joins the list: it redraws the whole scene through one shader
			// by construction, so tinting it says nothing about routing and would paint
			// over the mode's own answer.
			const bool excludedPass = m_bShadowDepthPass || m_bMaskPass || m_bUiPass;
			// The fill mode is device state, not per-draw state, so an excluded pass does
			// not merely miss the override -- it *inherits* whatever the last scene draw
			// left. Stated for both cases rather than only the one that turns it on.
			if (m_debugVisMode == DEBUG_VIS_WIREFRAME)
				Set_DX8_Render_State(D3DRS_FILLMODE,
					excludedPass ? D3DFILL_SOLID : D3DFILL_WIREFRAME);
			if (!excludedPass)
				Apply_Debug_Draw_Override(diagRouteBit == 1u,
					(curFVF & D3DFVF_NORMAL) != 0, diagRouteBit);
		}
#endif
	}

	render_state_changed&=((unsigned)WORLD_IDENTITY|(unsigned)VIEW_IDENTITY);

	SNAPSHOT_SAY(("DX8Wrapper::Apply_Render_State_Changes() - finished"));
}

/*
** How many mip levels a texture of this size can actually have.
*/
static unsigned Max_Mip_Levels(unsigned width, unsigned height)
{
	unsigned levels = 1;
	while (width > 1 || height > 1) {
		if (width > 1) width >>= 1;
		if (height > 1) height >>= 1;
		++levels;
	}
	return levels;
}

void DX8Wrapper::Adjust_Texture_Requirements(unsigned & width, unsigned & height,
	WW3DFormat & format, unsigned & levels)
{
	// D3DXCheckTextureRequirements ran ahead of every D3DXCreateTexture in this file and
	// is the whole of what made those calls different from a plain CreateTexture. It
	// clamped the size to the device's limits, brought the aspect ratio inside what the
	// device allowed, substituted a format the device does not support, and clamped the
	// mip count to what the size actually has. All four questions are ones DX8Caps
	// already answers, which is why this is above the seam and not inside a backend.
	//
	// Two adjustments D3DX also made are deliberately NOT reproduced: rounding up to a
	// power of two, and forcing square, for devices that require them. DX8Caps does not
	// carry those bits, no D3D9 device this port has run on sets them, and the failure
	// mode is the better one -- such a device now fails the creation loudly instead of
	// silently getting a texture of a different size than the art was authored at.
	//
	// Measured, over D3DX's answer and over this one: 9125 2-D textures created across
	// civ_buildings.rep and not one came back different from what was asked for. See the
	// TEXTURE REQUIREMENTS census.
	if (width == 0) width = 1;
	if (height == 0) height = 1;

	if (CurrentCaps != nullptr) {
		const unsigned max_w = CurrentCaps->Get_Max_Texture_Width();
		const unsigned max_h = CurrentCaps->Get_Max_Texture_Height();
		if (max_w != 0 && width > max_w) width = max_w;
		if (max_h != 0 && height > max_h) height = max_h;

		// The ratio limits the long side, so the short side comes up to meet it rather
		// than the long side coming down: shrinking would throw away detail the caller
		// has already decided it wants.
		const unsigned max_ratio = CurrentCaps->Get_Max_Texture_Aspect_Ratio();
		if (max_ratio != 0) {
			while (width > height * max_ratio && (max_h == 0 || height < max_h)) height <<= 1;
			while (height > width * max_ratio && (max_w == 0 || width < max_w)) width <<= 1;
		}

		if (!CurrentCaps->Support_Texture_Format(format)) {
			const WW3DFormat substitute = Get_Valid_Texture_Format(format, true);
			format = CurrentCaps->Support_Texture_Format(substitute)
				? substitute : WW3D_FORMAT_A8R8G8B8;
		}
	}

	// Zero still means "all the way down to 1x1" and is left alone; both APIs read it
	// that way. Anything else is a count, and a count larger than the chain has is an
	// invalid call rather than a clamp under D3D9.
	const unsigned max_levels = Max_Mip_Levels(width, height);
	if (levels > max_levels) levels = max_levels;
}

unsigned DX8Wrapper::Texture_Pool_To_Usage(D3DPOOL pool, bool rendertarget)
{
	unsigned usage = rendertarget ? (unsigned)GFX_USAGE_RENDER_TARGET : (unsigned)GFX_USAGE_STATIC;
	// Placement first and exactly, because the engine's texture path depends on all
	// three homes existing separately -- see the note at GfxResourceUsage. Managed is
	// the absence of a placement bit, which is why it is the default arm here as it is
	// the default argument at every call site.
	switch (pool) {
	case D3DPOOL_SYSTEMMEM:
	case D3DPOOL_SCRATCH:	usage |= GFX_USAGE_STAGING; break;
	case D3DPOOL_DEFAULT:	usage |= GFX_USAGE_GPU_RESIDENT; break;
	default:				break;
	}
	return usage;
}

/*
** Make a texture, retrying once if the device could not.
**
** The ladder is what the engine has always done and it stays on the engine's side of the
** seam, because what it frees is the engine's: textures nothing has drawn with for five
** seconds, and the mesh cache. What it can no longer do is tell "out of video memory"
** from "this device cannot make that texture at all" -- creation across the seam reports
** failure and not an HRESULT, by design, see gfxdevice.h -- so a device that refuses
** outright now pays for one wasted flush before returning null. Measured: 0 of 9125
** creations failed for any reason over a full replay.
*/
static GfxTexture * Create_Texture_With_Retry(unsigned width, unsigned height,
	unsigned levels, WW3DFormat format, unsigned usage, const char * what)
{
	GfxTexture * texture = DX8Wrapper::Gfx->Create_Texture(width, height, levels, format, usage);
	if (texture != nullptr) return texture;

	WWDEBUG_SAY(("Error: Out of memory while creating %s. Trying to release assets...", what));
	// Free all textures that haven't been used in the last 5 seconds
	TextureClass::Invalidate_Old_Unused_Textures(5000);
	// Invalidate the mesh cache
	WW3D::_Invalidate_Mesh_Cache();

	texture = DX8Wrapper::Gfx->Create_Texture(width, height, levels, format, usage);
	if (texture != nullptr) {
		WWDEBUG_SAY(("...%s creation successful.", what));
	}
	else {
		StringClass format_name(0,true);
		Get_WW3D_Format_Name(format, format_name);
		WWDEBUG_SAY(("...%s creation failed. (%d x %d, format: %s, mips: %d",
			what, width, height, format_name.str(), levels));
	}
	return texture;
}

GfxTexture * DX8Wrapper::_Create_DX8_Texture
(
	unsigned int width,
	unsigned int height,
	WW3DFormat format,
	MipCountType mip_level_count,
	D3DPOOL pool,
	bool rendertarget
)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();

	// Paletted textures not supported!
	WWASSERT(format!=D3DFMT_P8);

	const unsigned req_w = width, req_h = height;
	const WW3DFormat req_fmt = format;

	unsigned use_w = width, use_h = height, use_levels = mip_level_count;
	WW3DFormat use_fmt = format;
	Adjust_Texture_Requirements(use_w, use_h, use_fmt, use_levels);

	GfxTexture * texture = Create_Texture_With_Retry(use_w, use_h, use_levels, use_fmt,
		Texture_Pool_To_Usage(pool, rendertarget),
		rendertarget ? "render target" : "texture");

#ifdef RTS_DEBUG
	Debug_Note_Texture_Made(req_w, req_h, req_fmt, mip_level_count, texture);
#endif
	// Just return the texture, no reduction allowed for render targets.
	return texture;
}

GfxTexture * DX8Wrapper::_Create_DX8_Texture
(
	GfxSurface *surface_handle,
	MipCountType mip_level_count
)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();

	WW3DSurfaceDescription surface_desc;
	if (!Gfx->Describe_Surface(surface_handle, surface_desc)) return nullptr;

	// This will make a texture in a different (but similar) format if the surface is not
	// in a supported texture format -- see Adjust_Texture_Requirements.
	GfxTexture * texture = _Create_DX8_Texture(surface_desc.Width, surface_desc.Height,
		surface_desc.Format, mip_level_count);
	if (texture == nullptr) return nullptr;

	// Copy the surface to the texture. GFX_COPY_HALVE is the box filter this asked for
	// outright before the seam existed; nothing is being halved here, the two surfaces
	// are the same size, and it is the filter name rather than the intent that reads oddly.
	GfxSurface * tex_surface = Gfx->Get_Texture_Surface_Level(texture, 0);
	if (tex_surface != nullptr) {
		Gfx->Copy_Surface_Rect(surface_handle, nullptr, tex_surface, nullptr, GFX_COPY_HALVE);
		Gfx->Release_Surface(tex_surface);
	}

	// Create mipmaps if needed
	if (mip_level_count!=MIP_LEVELS_1)
	{
		Gfx->Generate_Mips(texture, 0);
	}

	return texture;
}

/*!
 * KJM create depth stencil texture
 */
GfxTexture * DX8Wrapper::_Create_DX8_ZTexture
(
	unsigned int width,
	unsigned int height,
	WW3DZFormat zformat,
	MipCountType mip_level_count,
	D3DPOOL pool
)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();

	unsigned levels = mip_level_count;
	const unsigned max_levels = Max_Mip_Levels(width, height);
	if (levels > max_levels) levels = max_levels;

	const unsigned usage = Texture_Pool_To_Usage(pool, false);

	GfxTexture * texture = Gfx->Create_Depth_Texture(width, height, levels, zformat, usage);
	if (texture == nullptr) {
		// The same ladder as the colour path above, and for the same reason.
		WWDEBUG_SAY(("Error: Out of memory while creating depth texture. Trying to release assets..."));
		TextureClass::Invalidate_Old_Unused_Textures(5000);
		WW3D::_Invalidate_Mesh_Cache();
		texture = Gfx->Create_Depth_Texture(width, height, levels, zformat, usage);
		WWDEBUG_SAY(("...depth texture creation %s.", texture ? "successful" : "failed"));
		if (texture == nullptr) return nullptr;
	}

#ifdef RTS_DEBUG
	Debug_Note_Texture_Made_Other("z");
#endif
	Gfx->Reference_Texture(texture); // don't release this texture

	// Just return the texture, no reduction allowed for render targets.
	return texture;
}

/*!
 * KJM create cube map texture
 */
GfxTexture* DX8Wrapper::_Create_DX8_Cube_Texture
(
	unsigned int width,
	unsigned int height,
	WW3DFormat format,
	MipCountType mip_level_count,
	D3DPOOL pool,
	bool rendertarget
)
{
	WWASSERT(width==height);
	DX8_THREAD_ASSERT();
	DX8_Assert();

	// Paletted textures not supported!
	WWASSERT(format!=D3DFMT_P8);

	unsigned use_w = width, use_h = height, use_levels = mip_level_count;
	WW3DFormat use_fmt = format;
	Adjust_Texture_Requirements(use_w, use_h, use_fmt, use_levels);
	// A cube's faces are square by definition -- the assert above says the caller knows
	// that -- so the edge is whichever of the two survived, and they can only differ if
	// a device limit clamped one of them.
	const unsigned edge = use_w < use_h ? use_w : use_h;

	const unsigned usage = Texture_Pool_To_Usage(pool, rendertarget);
	const char * const what = rendertarget ? "cube render target" : "cube texture";

	GfxTexture * texture = Gfx->Create_Cube_Texture(edge, use_levels, use_fmt, usage);
	if (texture == nullptr) {
		WWDEBUG_SAY(("Error: Out of memory while creating %s. Trying to release assets...", what));
		TextureClass::Invalidate_Old_Unused_Textures(5000);
		WW3D::_Invalidate_Mesh_Cache();
		texture = Gfx->Create_Cube_Texture(edge, use_levels, use_fmt, usage);
		WWDEBUG_SAY(("...%s creation %s.", what, texture ? "successful" : "failed"));
	}

#ifdef RTS_DEBUG
	Debug_Note_Texture_Made_Other("c");
#endif
	return texture;
}

/*!
 * KJM create volume texture
 */
GfxTexture* DX8Wrapper::_Create_DX8_Volume_Texture
(
	unsigned int width,
	unsigned int height,
	unsigned int depth,
	WW3DFormat format,
	MipCountType mip_level_count,
	D3DPOOL pool
)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();

	// Paletted textures not supported!
	WWASSERT(format!=D3DFMT_P8);

	unsigned use_w = width, use_h = height, use_levels = mip_level_count;
	WW3DFormat use_fmt = format;
	Adjust_Texture_Requirements(use_w, use_h, use_fmt, use_levels);

	const unsigned usage = Texture_Pool_To_Usage(pool, false);

	GfxTexture * texture = Gfx->Create_Volume_Texture(use_w, use_h, depth, use_levels,
		use_fmt, usage);
	if (texture == nullptr) {
		WWDEBUG_SAY(("Error: Out of memory while creating volume texture. Trying to release assets..."));
		TextureClass::Invalidate_Old_Unused_Textures(5000);
		WW3D::_Invalidate_Mesh_Cache();
		texture = Gfx->Create_Volume_Texture(use_w, use_h, depth, use_levels, use_fmt, usage);
		WWDEBUG_SAY(("...volume texture creation %s.", texture ? "successful" : "failed"));
	}

#ifdef RTS_DEBUG
	Debug_Note_Texture_Made_Other("v");
#endif
	return texture;
}


GfxSurface * DX8Wrapper::_Create_DX8_Surface(unsigned int width, unsigned int height, WW3DFormat format)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();

	// Paletted surfaces not supported!
	WWASSERT(format!=D3DFMT_P8);

	// The two-pool ladder D3D9 needs when a driver refuses a format in system memory is
	// the backend's, not this function's -- it is already written there.
	GfxSurface * surface = Gfx->Create_Offscreen_Surface(width, height, format);
	Increment_DX8_CallCount();

	return surface;
}


GfxSurface * DX8Wrapper::_Create_DX8_Surface(const char *filename_)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();

	// The D3DX hack this comment used to describe -- make a texture from the file, check
	// its surface against the file data, copy or fall back -- is long gone: the body below
	// only decides whether the file exists at all (trying the .dds spelling of a .tga name)
	// and then hands the load to TextureLoader, which has its own decoders.

	{

		file_auto_ptr myfile(_TheFileFactory,filename_);
		// If file not found, create a surface with missing texture in it

		if (!myfile->Is_Available()) {
			// If file not found, try the dds format
			// else create a surface with missing texture in it
			char compressed_name[200];
			strlcpy(compressed_name,filename_, sizeof(compressed_name));
			char *ext = strstr(compressed_name, ".");
			if ( ext && (strlen(ext)==4) &&
				  ( (ext[1] == 't') || (ext[1] == 'T') ) &&
				  ( (ext[2] == 'g') || (ext[2] == 'G') ) &&
				  ( (ext[3] == 'a') || (ext[3] == 'A') ) ) {
				ext[1]='d';
				ext[2]='d';
				ext[3]='s';
			}
			file_auto_ptr myfile2(_TheFileFactory,compressed_name);
			if (!myfile2->Is_Available())
				return MissingTexture::_Create_Missing_Surface();
		}
	}

	StringClass filename_string(filename_,true);
	return TextureLoader::Load_Surface_Immediate(
		filename_string,
		WW3D_FORMAT_UNKNOWN,
		true);
}


/***********************************************************************************************
 * DX8Wrapper::_Update_Texture -- Copies a texture from system memory to video memory          *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   4/26/2001  hy : Created.                                                                  *
 *=============================================================================================*/
void DX8Wrapper::_Update_Texture(TextureClass *system, TextureClass *video)
{
	WWASSERT(system);
	WWASSERT(video);
	WWASSERT(system->Get_Pool()==TextureClass::POOL_SYSTEMMEM);
	WWASSERT(video->Get_Pool()==TextureClass::POOL_DEFAULT);
	GFXCALL(Update_Texture((GfxTexture*)system->Peek_D3D_Base_Texture(),
		(GfxTexture*)video->Peek_D3D_Base_Texture()));
}

void DX8Wrapper::Compute_Caps(WW3DFormat display_format)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();
	delete CurrentCaps;
	CurrentCaps=new DX8Caps(display_format);
}


void DX8Wrapper::Set_Light(unsigned index, const D3DLIGHT8* light)
{
	if (light) {
		render_state.Lights[index]=*light;
		render_state.LightEnable[index]=true;
	}
	else {
		render_state.LightEnable[index]=false;
	}
	render_state_changed|=(LIGHT0_CHANGED<<index);
}

void DX8Wrapper::Set_Light(unsigned index,const LightClass &light)
{
	D3DLIGHT8 dlight;
	Vector3 temp;
	memset(&dlight,0,sizeof(D3DLIGHT8));

	switch (light.Get_Type())
	{
	case LightClass::POINT:
		{
			dlight.Type=D3DLIGHT_POINT;
		}
		break;
	case LightClass::DIRECTIONAL:
		{
			dlight.Type=D3DLIGHT_DIRECTIONAL;
		}
		break;
	case LightClass::SPOT:
		{
			dlight.Type=D3DLIGHT_SPOT;
		}
		break;
	}

	light.Get_Diffuse(&temp);
	temp*=light.Get_Intensity();
	dlight.Diffuse.r=temp.X;
	dlight.Diffuse.g=temp.Y;
	dlight.Diffuse.b=temp.Z;
	dlight.Diffuse.a=1.0f;

	light.Get_Specular(&temp);
	temp*=light.Get_Intensity();
	dlight.Specular.r=temp.X;
	dlight.Specular.g=temp.Y;
	dlight.Specular.b=temp.Z;
	dlight.Specular.a=1.0f;

	light.Get_Ambient(&temp);
	temp*=light.Get_Intensity();
	dlight.Ambient.r=temp.X;
	dlight.Ambient.g=temp.Y;
	dlight.Ambient.b=temp.Z;
	dlight.Ambient.a=1.0f;

	temp=light.Get_Position();
	dlight.Position=*(D3DVECTOR*) &temp;

	light.Get_Spot_Direction(temp);
	dlight.Direction=*(D3DVECTOR*) &temp;

	dlight.Range=light.Get_Attenuation_Range();
	dlight.Falloff=light.Get_Spot_Exponent();
	dlight.Theta=light.Get_Spot_Angle();
	dlight.Phi=light.Get_Spot_Angle();

	// Inverse linear light 1/(1+D)
	double a,b;
	light.Get_Far_Attenuation_Range(a,b);
	dlight.Attenuation0=1.0f;
	if (fabs(a-b)<1e-5)
		// if the attenuation range is too small assume uniform with cutoff
		dlight.Attenuation1=0.0f;
	else
		// this will cause the light to drop to half intensity at the first far attenuation
		dlight.Attenuation1=(float) 1.0/a;
	dlight.Attenuation2=0.0f;

	Set_Light(index,&dlight);
}

//**********************************************************************************************
//! Set the light environment. This is a lighting model which used up to four
//! directional lights to produce the lighting.
/*! 5/27/02 KJM Added shader light environment support
*/
void DX8Wrapper::Set_Light_Environment(LightEnvironmentClass* light_env)
{
	// The site goes on the emitting function rather than on its callers, because there are
	// five of them across three subsystems -- the mesh renderer, the heightmap, ww3d's own
	// scene render and MeshClass -- and only two sat inside a site of any kind. The rest
	// wrote D3DRS_AMBIENT from nowhere, which the census reported as an "(unattributed)"
	// row of exactly two render words a frame with no calls against them.
	FF_SITE("DX8Wrapper::Set_Light_Environment");
	// Shader light environment support															*
	Light_Environment=light_env;

	if (light_env)
	{
		int light_count = light_env->Get_Light_Count();
		unsigned int color=Convert_Color(light_env->Get_Equivalent_Ambient(),0.0f);
		if (RenderStates[D3DRS_AMBIENT]!=color)
		{
			Set_DX8_Render_State(D3DRS_AMBIENT,color);
//buggy Radeon 9700 driver doesn't apply new ambient unless the material also changes.
#if 1
			render_state_changed|=MATERIAL_CHANGED;
#endif
		}

		D3DLIGHT8 light;
		int l=0;
		for (;l<light_count;++l) {

			::ZeroMemory(&light, sizeof(D3DLIGHT8));

			light.Type=D3DLIGHT_DIRECTIONAL;
			(Vector3&)light.Diffuse=light_env->Get_Light_Diffuse(l);
			Vector3 dir=-light_env->Get_Light_Direction(l);
			light.Direction=(const D3DVECTOR&)(dir);

			// (gth) TODO: put specular into LightEnvironment?  Much work to be done on lights :-)'
			if (l==0) {
				light.Specular.r = light.Specular.g = light.Specular.b = 1.0f;
			}

			if (light_env->isPointLight(l)) {
				light.Type = D3DLIGHT_POINT;
				(Vector3&)light.Diffuse=light_env->getPointDiffuse(l);
				(Vector3&)light.Ambient=light_env->getPointAmbient(l);
				light.Position = (const D3DVECTOR&)light_env->getPointCenter(l);
				light.Range = light_env->getPointOrad(l);

				// Inverse linear light 1/(1+D)
				double a,b;
				b = light_env->getPointOrad(l);
				a = light_env->getPointIrad(l);

//(gth) CNC3 Generals code for the attenuation factors is causing the lights to over-brighten
//I'm changing the Attenuation0 parameter to 1.0 to avoid this problem.
#if 0
				light.Attenuation0=0.01f;
#else
				light.Attenuation0=1.0f;
#endif
				if (fabs(a-b)<1e-5)
					// if the attenuation range is too small assume uniform with cutoff
					light.Attenuation1=0.0f;
				else
					// this will cause the light to drop to half intensity at the first far attenuation
					light.Attenuation1=(float) 0.1/a;

				light.Attenuation2=8.0f/(b*b);
			}

			Set_Light(l,&light);
		}

		for (;l<4;++l) {
			Set_Light(l,nullptr);
		}
	}
/*	else {
		for (int l=0;l<4;++l) {
			Set_Light(l,nullptr);
		}
	}
*/
}

GfxSurface * DX8Wrapper::_Get_DX8_Front_Buffer()
{
	DX8_THREAD_ASSERT();
	unsigned width=0, height=0;
	WW3DFormat display_format=WW3D_FORMAT_UNKNOWN;
	if (!Gfx->Get_Display_Mode(width,height,display_format)) return nullptr;

	DX8_Assert();
	GfxSurface * fb = Gfx->Create_Offscreen_Surface(width, height, WW3D_FORMAT_A8R8G8B8);
	Increment_DX8_CallCount();
	if (fb == nullptr) return nullptr;

	GFXCALL(Capture_Front_Buffer(fb));
	return fb;
}

SurfaceClass * DX8Wrapper::_Get_DX8_Back_Buffer(unsigned int num)
{
	DX8_THREAD_ASSERT();

	SurfaceClass *surf=nullptr;
	GfxSurface * bb=Gfx->Get_Back_Buffer(num);
	if (bb)
	{
		surf=NEW_REF(SurfaceClass,(bb));
		Gfx->Release_Surface(bb);
	}

	return surf;
}

SurfaceClass * DX8Wrapper::_Get_DX8_Render_Target()
{
	DX8_THREAD_ASSERT();

	SurfaceClass *surf=nullptr;
	GfxSurface * rt=Gfx->Get_Render_Target(0);
	if (rt)
	{
		surf=NEW_REF(SurfaceClass,(rt));
		Gfx->Release_Surface(rt);	//SurfaceClass took its own reference
	}

	return surf;
}


TextureClass *
DX8Wrapper::Create_Render_Target (int width, int height, WW3DFormat format)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();
	DX8_RECORD_DX8_CALLS();

	// Use the current display format if format isn't specified
	if (format==WW3D_FORMAT_UNKNOWN) {
		unsigned mode_width=0, mode_height=0;
		Gfx->Get_Display_Mode(mode_width,mode_height,format);
	}

	// If render target format isn't supported return null
	if (!Get_Current_Caps()->Support_Render_To_Texture_Format(format)) {
		WWDEBUG_SAY(("DX8Wrapper - Render target format is not supported"));
		return nullptr;
	}

	//
	//	Note: We're going to force the width and height to be powers of two and equal
	//
	const DX8Caps& dx8caps=*Get_Current_Caps();
	float poweroftwosize = width;
	if (height > 0 && height < width) {
		poweroftwosize = height;
	}
	poweroftwosize = ::Find_POT (poweroftwosize);

	if (poweroftwosize>dx8caps.Get_Max_Texture_Width()) {
		poweroftwosize=dx8caps.Get_Max_Texture_Width();
	}
	if (poweroftwosize>dx8caps.Get_Max_Texture_Height()) {
		poweroftwosize=dx8caps.Get_Max_Texture_Height();
	}

	width = height = poweroftwosize;

	//
	//	Attempt to create the render target
	//
	TextureClass * tex = NEW_REF(TextureClass,(width,height,format,MIP_LEVELS_1,TextureClass::POOL_DEFAULT,true));

	// 3dfx drivers are lying in the CheckDeviceFormat call and claiming
	// that they support render targets!
	if (tex->Peek_D3D_Base_Texture() == nullptr)
	{
		WWDEBUG_SAY(("DX8Wrapper - Render target creation failed!"));
		REF_PTR_RELEASE(tex);
	}

	return tex;
}

//**********************************************************************************************
//! Create render target with associated depth stencil buffer
/*! KJM
*/
void DX8Wrapper::Create_Render_Target
(
	int width,
	int height,
	WW3DFormat format,
	WW3DZFormat zformat,
	TextureClass** target,
	ZTextureClass** depth_buffer
)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();
	DX8_RECORD_DX8_CALLS();

	// Use the current display format if format isn't specified
	if (format==WW3D_FORMAT_UNKNOWN)
	{
		*target=nullptr;
		*depth_buffer=nullptr;
		return;
/*		D3DDISPLAYMODE mode;
		DX8CALL(GetDisplayMode(&mode));
		format=D3DFormat_To_WW3DFormat(mode.Format);*/
	}

	// If render target format isn't supported return null
	if (!Get_Current_Caps()->Support_Render_To_Texture_Format(format) ||
		 !Get_Current_Caps()->Support_Depth_Stencil_Format(zformat))
	{
		WWDEBUG_SAY(("DX8Wrapper - Render target with depth format is not supported"));
		return;
	}

	//	Note: We're going to force the width and height to be powers of two and equal
	const DX8Caps& dx8caps=*Get_Current_Caps();
	float poweroftwosize = width;
	if (height > 0 && height < width)
	{
		poweroftwosize = height;
	}
	poweroftwosize = ::Find_POT (poweroftwosize);

	if (poweroftwosize>dx8caps.Get_Max_Texture_Width())
	{
		poweroftwosize=dx8caps.Get_Max_Texture_Width();
	}

	if (poweroftwosize>dx8caps.Get_Max_Texture_Height())
	{
		poweroftwosize=dx8caps.Get_Max_Texture_Height();
	}

	width = height = poweroftwosize;

	//	Attempt to create the render target
	TextureClass* tex=NEW_REF(TextureClass,(width,height,format,MIP_LEVELS_1,TextureClass::POOL_DEFAULT,true));

	// 3dfx drivers are lying in the CheckDeviceFormat call and claiming
	// that they support render targets!
	if (tex->Peek_D3D_Base_Texture() == nullptr)
	{
		WWDEBUG_SAY(("DX8Wrapper - Render target creation failed!"));
		REF_PTR_RELEASE(tex);
	}

	*target=tex;

	// attempt to create the depth stencil buffer
	*depth_buffer=NEW_REF
	(
		ZTextureClass,
		(
			width,
			height,
			zformat,
			MIP_LEVELS_1,
			TextureClass::POOL_DEFAULT
		)
	);
}

/*!
 * Set render target
 * KM Added optional custom z target
 */
void DX8Wrapper::Set_Render_Target_With_Z
(
	TextureClass* texture,
	ZTextureClass* ztexture
)
{
	WWASSERT(texture!=nullptr);
	GfxSurface * d3d_surf = texture->Get_D3D_Surface_Level();
	WWASSERT(d3d_surf != nullptr);

	GfxSurface* d3d_zbuf=nullptr;
	if (ztexture!=nullptr)
	{

		d3d_zbuf=ztexture->Get_D3D_Surface_Level();
		WWASSERT(d3d_zbuf!=nullptr);
		Set_Render_Target(d3d_surf,d3d_zbuf);
		Gfx->Release_Surface(d3d_zbuf);
	}
	else
	{
		Set_Render_Target(d3d_surf,true);
	}
	Gfx->Release_Surface(d3d_surf);

	IsRenderToTexture = true;
}

void
DX8Wrapper::Set_Render_Target(GfxSurface *render_target, bool use_default_depth_buffer)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();

	//
	//	Should we restore the default render target set a new one?
	//
	if (render_target == nullptr || render_target == DefaultRenderTarget)
	{
		// If there is currently a custom render target, default must NOT be null.
		if (CurrentRenderTarget)
		{
			WWASSERT(DefaultRenderTarget!=nullptr);
		}

		//
		//	Restore the default render target
		//
		if (DefaultRenderTarget != nullptr)
		{
			Set_DX8_Render_Target(DefaultRenderTarget, DefaultDepthBuffer);
			Gfx->Release_Surface(DefaultRenderTarget);
			DefaultRenderTarget = nullptr;
			if (DefaultDepthBuffer)
			{
				Gfx->Release_Surface(DefaultDepthBuffer);
				DefaultDepthBuffer = nullptr;
			}
		}

		//
		//	Release our hold on the "current" render target
		//
		if (CurrentRenderTarget != nullptr)
		{
			Gfx->Release_Surface(CurrentRenderTarget);
			CurrentRenderTarget = nullptr;
		}

		if (CurrentDepthBuffer!=nullptr)
		{
			Gfx->Release_Surface(CurrentDepthBuffer);
			CurrentDepthBuffer=nullptr;
		}

	}
	else if (render_target != CurrentRenderTarget)
	{
		WWASSERT(DefaultRenderTarget==nullptr);

		//
		//	We'll need the depth buffer later...
		//
		if (DefaultDepthBuffer == nullptr)
		{
			DefaultDepthBuffer=Gfx->Get_Depth_Target();
		}

		//
		//	Get a pointer to the default render target (if necessary)
		//
		if (DefaultRenderTarget == nullptr)
		{
			DefaultRenderTarget=Gfx->Get_Render_Target(0);
		}

		//
		//	Release our hold on the old "current" render target
		//
		if (CurrentRenderTarget != nullptr)
		{
			Gfx->Release_Surface(CurrentRenderTarget);
			CurrentRenderTarget = nullptr;
		}

		if (CurrentDepthBuffer!=nullptr)
		{
			Gfx->Release_Surface(CurrentDepthBuffer);
			CurrentDepthBuffer=nullptr;
		}

		//
		//	Keep a copy of the current render target (for housekeeping)
		//
		CurrentRenderTarget = render_target;
		WWASSERT (CurrentRenderTarget != nullptr);
		if (CurrentRenderTarget != nullptr)
		{
			Gfx->Reference_Surface(CurrentRenderTarget);

			//
			//	Switch render targets
			//
			if (use_default_depth_buffer)
			{
				Set_DX8_Render_Target(CurrentRenderTarget, DefaultDepthBuffer);
			}
			else
			{
				Set_DX8_Render_Target(CurrentRenderTarget, nullptr);
			}
		}
	}

	//
	//	Free our hold on the depth buffer
	//
//	if (depth_buffer != nullptr) {
//		Gfx->Release_Surface(depth_buffer);
//		depth_buffer = nullptr;
//	}

	IsRenderToTexture = false;
}


//**********************************************************************************************
//! Set render target with depth stencil buffer
/*! KJM
*/
void DX8Wrapper::Set_Render_Target
(
	GfxSurface* render_target,
	GfxSurface* depth_buffer
)
{
	DX8_THREAD_ASSERT();
	DX8_Assert();

	//
	//	Should we restore the default render target set a new one?
	//
	if (render_target == nullptr || render_target == DefaultRenderTarget)
	{
		// If there is currently a custom render target, default must NOT be null.
		if (CurrentRenderTarget)
		{
			WWASSERT(DefaultRenderTarget!=nullptr);
		}

		//
		//	Restore the default render target
		//
		if (DefaultRenderTarget != nullptr)
		{
			Set_DX8_Render_Target(DefaultRenderTarget, DefaultDepthBuffer);
			Gfx->Release_Surface(DefaultRenderTarget);
			DefaultRenderTarget = nullptr;
			if (DefaultDepthBuffer)
			{
				Gfx->Release_Surface(DefaultDepthBuffer);
				DefaultDepthBuffer = nullptr;
			}
		}

		//
		//	Release our hold on the "current" render target
		//
		if (CurrentRenderTarget != nullptr)
		{
			Gfx->Release_Surface(CurrentRenderTarget);
			CurrentRenderTarget = nullptr;
		}

		if (CurrentDepthBuffer!=nullptr)
		{
			Gfx->Release_Surface(CurrentDepthBuffer);
			CurrentDepthBuffer=nullptr;
		}
	}
	else if (render_target != CurrentRenderTarget)
	{
		WWASSERT(DefaultRenderTarget==nullptr);

		//
		//	We'll need the depth buffer later...
		//
		if (DefaultDepthBuffer == nullptr)
		{
			DefaultDepthBuffer=Gfx->Get_Depth_Target();
		}

		//
		//	Get a pointer to the default render target (if necessary)
		//
		if (DefaultRenderTarget == nullptr)
		{
			DefaultRenderTarget=Gfx->Get_Render_Target(0);
		}

		//
		//	Release our hold on the old "current" render target
		//
		if (CurrentRenderTarget != nullptr)
		{
			Gfx->Release_Surface(CurrentRenderTarget);
			CurrentRenderTarget = nullptr;
		}

		if (CurrentDepthBuffer!=nullptr)
		{
			Gfx->Release_Surface(CurrentDepthBuffer);
			CurrentDepthBuffer=nullptr;
		}

		//
		//	Keep a copy of the current render target (for housekeeping)
		//
		CurrentRenderTarget = render_target;
		CurrentDepthBuffer = depth_buffer;
		WWASSERT (CurrentRenderTarget != nullptr);
		if (CurrentRenderTarget != nullptr)
		{
			Gfx->Reference_Surface(CurrentRenderTarget);
			Gfx->Reference_Surface(CurrentDepthBuffer);

			//
			//	Switch render targets
			//
			Set_DX8_Render_Target(CurrentRenderTarget, CurrentDepthBuffer);
		}
	}

	IsRenderToTexture=true;
}


void DX8Wrapper::Flush_DX8_Resource_Manager(unsigned int bytes)
{
	DX8_Assert();
	GFXCALL(Trim_Resource_Memory());
}

unsigned int DX8Wrapper::Get_Free_Texture_RAM()
{
	DX8_Assert();
	DX8_RECORD_DX8_CALLS();
	return Gfx->Get_Available_Texture_Memory();
}

// Converts a linear gamma ramp to one that is controlled by:
// Gamma - controls the curvature of the middle of the curve
// Bright - controls the minimum value of the curve
// Contrast - controls the difference between the maximum and the minimum of the curve
void DX8Wrapper::Set_Gamma(float gamma,float bright,float contrast,bool calibrate,bool uselimit)
{
	gamma=Bound(gamma,0.6f,6.0f);
	bright=Bound(bright,-0.5f,0.5f);
	contrast=Bound(contrast,0.5f,2.0f);
	float oo_gamma=1.0f/gamma;

	DX8_Assert();
	DX8_RECORD_DX8_CALLS();

	D3DGAMMARAMP ramp;
	float			 limit;

	// IML: I'm not really sure what the intent of the 'limit' variable is. It does not produce useful results for my purposes.
	if (uselimit) {
		limit=(contrast-1)/2*contrast;
	} else {
		limit = 0.0f;
	}

	// HY - arrived at this equation after much trial and error.
	for (int i=0; i<256; i++) {
		float in,out;
		in=i/256.0f;
		float x=in-limit;
		x=Bound(x,0.0f,1.0f);
		x=powf(x,oo_gamma);
		out=contrast*x+bright;
		out=Bound(out,0.0f,1.0f);
		ramp.red[i]=(WORD) (out*65535);
		ramp.green[i]=(WORD) (out*65535);
		ramp.blue[i]=(WORD) (out*65535);
	}

	if (Get_Current_Caps()->Support_Gamma())	{
		Gfx->Set_Gamma_Ramp(&ramp,calibrate);
	} else {
		HWND hwnd = GetDesktopWindow();
		HDC hdc = GetDC(hwnd);
		if (hdc)
		{
			SetDeviceGammaRamp (hdc, &ramp);
			ReleaseDC (hwnd, hdc);
		}
	}
}

namespace wrapper
{
void D3DMatrixIdentity(D3DMATRIX* dxm)
{
	memset(dxm, 0, sizeof(*dxm));
	dxm->_11 = 1.0f;
	dxm->_22 = 1.0f;
	dxm->_33 = 1.0f;
	dxm->_44 = 1.0f;
}
} // namespace wrapper

void DX8Wrapper::Set_World_Identity()
{
	if (render_state_changed&(unsigned)WORLD_IDENTITY)
		return;
	wrapper::D3DMatrixIdentity(&render_state.world);
	render_state_changed|=(unsigned)WORLD_CHANGED|(unsigned)WORLD_IDENTITY;
}

void DX8Wrapper::Set_View_Identity()
{
	if (render_state_changed&(unsigned)VIEW_IDENTITY)
		return;
	wrapper::D3DMatrixIdentity(&render_state.view);
	render_state_changed|=(unsigned)VIEW_CHANGED|(unsigned)VIEW_IDENTITY;
}

//**********************************************************************************************
//! Resets render device to default state
/*!
*/
void DX8Wrapper::Apply_Default_State()
{
	SNAPSHOT_SAY(("DX8Wrapper::Apply_Default_State()"));

	// only set states used in game
	Set_DX8_Render_State(D3DRS_ZENABLE, TRUE);
//	Set_DX8_Render_State(D3DRS_FILLMODE, D3DFILL_SOLID);
	Set_DX8_Render_State(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
	//Set_DX8_Render_State(D3DRS_LINEPATTERN, 0);
	Set_DX8_Render_State(D3DRS_ZWRITEENABLE, TRUE);
	Set_DX8_Render_State(D3DRS_ALPHATESTENABLE, FALSE);
	//Set_DX8_Render_State(D3DRS_LASTPIXEL, FALSE);
	Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_ONE);
	Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_ZERO);
	Set_DX8_Render_State(D3DRS_CULLMODE, D3DCULL_CW);
	Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	Set_DX8_Render_State(D3DRS_ALPHAREF, 0);
	Set_DX8_Render_State(D3DRS_ALPHAFUNC, D3DCMP_LESSEQUAL);
	Set_DX8_Render_State(D3DRS_DITHERENABLE, FALSE);
	Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, FALSE);
	Set_DX8_Render_State(D3DRS_FOGENABLE, FALSE);
	Set_DX8_Render_State(D3DRS_SPECULARENABLE, FALSE);
//	Set_DX8_Render_State(D3DRS_ZVISIBLE, FALSE);
//	Set_DX8_Render_State(D3DRS_FOGCOLOR, 0);
//	Set_DX8_Render_State(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
//	Set_DX8_Render_State(D3DRS_FOGSTART, 0);

//	Set_DX8_Render_State(D3DRS_FOGEND, WWMath::Float_As_Int(1.0f));
//	Set_DX8_Render_State(D3DRS_FOGDENSITY, WWMath::Float_As_Int(1.0f));

	//Set_DX8_Render_State(D3DRS_EDGEANTIALIAS, FALSE);
	Set_DX8_Render_State(D3DRS_ZBIAS, 0);
//	Set_DX8_Render_State(D3DRS_RANGEFOGENABLE, FALSE);
	Set_DX8_Render_State(D3DRS_STENCILENABLE, FALSE);
	Set_DX8_Render_State(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
	Set_DX8_Render_State(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
	Set_DX8_Render_State(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
	Set_DX8_Render_State(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
	Set_DX8_Render_State(D3DRS_STENCILREF, 0);
	Set_DX8_Render_State(D3DRS_STENCILMASK, 0xffffffff);
	Set_DX8_Render_State(D3DRS_STENCILWRITEMASK, 0xffffffff);
	Set_DX8_Render_State(D3DRS_TEXTUREFACTOR, 0);
/*	Set_DX8_Render_State(D3DRS_WRAP0, D3DWRAP_U| D3DWRAP_V);
	Set_DX8_Render_State(D3DRS_WRAP1, D3DWRAP_U| D3DWRAP_V);
	Set_DX8_Render_State(D3DRS_WRAP2, D3DWRAP_U| D3DWRAP_V);
	Set_DX8_Render_State(D3DRS_WRAP3, D3DWRAP_U| D3DWRAP_V);
	Set_DX8_Render_State(D3DRS_WRAP4, D3DWRAP_U| D3DWRAP_V);
	Set_DX8_Render_State(D3DRS_WRAP5, D3DWRAP_U| D3DWRAP_V);
	Set_DX8_Render_State(D3DRS_WRAP6, D3DWRAP_U| D3DWRAP_V);
	Set_DX8_Render_State(D3DRS_WRAP7, D3DWRAP_U| D3DWRAP_V);*/
	Set_DX8_Render_State(D3DRS_CLIPPING, TRUE);
	Set_DX8_Render_State(D3DRS_LIGHTING, FALSE);
	//Set_DX8_Render_State(D3DRS_AMBIENT, 0);
//	Set_DX8_Render_State(D3DRS_FOGVERTEXMODE, D3DFOG_NONE);
	Set_DX8_Render_State(D3DRS_COLORVERTEX, TRUE);
/*	Set_DX8_Render_State(D3DRS_LOCALVIEWER, TRUE);
	Set_DX8_Render_State(D3DRS_NORMALIZENORMALS, FALSE);
	Set_DX8_Render_State(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
	Set_DX8_Render_State(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_COLOR2);
	Set_DX8_Render_State(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL);
	Set_DX8_Render_State(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
	Set_DX8_Render_State(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);*/
	//Set_DX8_Render_State(D3DRS_CLIPPLANEENABLE, 0);
	Set_DX8_Render_State(D3DRS_SOFTWAREVERTEXPROCESSING, FALSE);
	//Set_DX8_Render_State(D3DRS_POINTSIZE, 0x3f800000);
	//Set_DX8_Render_State(D3DRS_POINTSIZE_MIN, 0);
	//Set_DX8_Render_State(D3DRS_POINTSPRITEENABLE, FALSE);
	//Set_DX8_Render_State(D3DRS_POINTSCALEENABLE, FALSE);
	//Set_DX8_Render_State(D3DRS_POINTSCALE_A, 0);
	//Set_DX8_Render_State(D3DRS_POINTSCALE_B, 0);
	//Set_DX8_Render_State(D3DRS_POINTSCALE_C, 0);
	//Set_DX8_Render_State(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
	//Set_DX8_Render_State(D3DRS_MULTISAMPLEMASK, 0xffffffff);
	//Set_DX8_Render_State(D3DRS_PATCHEDGESTYLE, D3DPATCHEDGE_DISCRETE);
	//Set_DX8_Render_State(D3DRS_PATCHSEGMENTS, 0x3f800000);
	//Set_DX8_Render_State(D3DRS_DEBUGMONITORTOKEN, D3DDMT_ENABLE);
	//Set_DX8_Render_State(D3DRS_POINTSIZE_MAX, Float_At_Int(64.0f));
	//Set_DX8_Render_State(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
	Set_DX8_Render_State(D3DRS_COLORWRITEENABLE, 0x0000000f);
	//Set_DX8_Render_State(D3DRS_TWEENFACTOR, 0);
	Set_DX8_Render_State(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	//Set_DX8_Render_State(D3DRS_POSITIONORDER, D3DORDER_CUBIC);
	//Set_DX8_Render_State(D3DRS_NORMALORDER, D3DORDER_LINEAR);

	// disable TSS stages
	int i;
	for (i=0; i<CurrentCaps->Get_Max_Textures_Per_Pass(); i++)
	{
		Set_DX8_Texture_Stage_State(i, D3DTSS_COLOROP, D3DTOP_DISABLE);
		Set_DX8_Texture_Stage_State(i, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		Set_DX8_Texture_Stage_State(i, D3DTSS_COLORARG2, D3DTA_DIFFUSE);

		Set_DX8_Texture_Stage_State(i, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		Set_DX8_Texture_Stage_State(i, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		Set_DX8_Texture_Stage_State(i, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

		/*Set_DX8_Texture_Stage_State(i, D3DTSS_BUMPENVMAT00, 0);
		Set_DX8_Texture_Stage_State(i, D3DTSS_BUMPENVMAT01, 0);
		Set_DX8_Texture_Stage_State(i, D3DTSS_BUMPENVMAT10, 0);
		Set_DX8_Texture_Stage_State(i, D3DTSS_BUMPENVMAT11, 0);
		Set_DX8_Texture_Stage_State(i, D3DTSS_BUMPENVLSCALE, 0);
		Set_DX8_Texture_Stage_State(i, D3DTSS_BUMPENVLOFFSET, 0);*/

		Set_DX8_Texture_Stage_State(i, D3DTSS_TEXCOORDINDEX, i);


		// This function states a starting model rather than editing one, which is why the
		// two addressing words go straight through instead of through Set_Sampler: a
		// default SamplerStateClass already reads WRAP on both axes, so Set_Sampler would
		// emit nothing and leave the tracked words holding zero, which is not a D3D
		// addressing mode. Samplers[] is restated beside them so the two models cannot
		// drift apart here.
		//
		// Nothing calls Apply_Default_State. It has no callers in either game and has not
		// had for as long as this branch goes back, so none of this runs; it is written to
		// be correct if it ever does rather than because it is load-bearing.
		Samplers[i] = SamplerStateClass();
		Set_DX8_Stage_State_Unguarded(i, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
		Set_DX8_Stage_State_Unguarded(i, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
		Set_DX8_Texture_Stage_State(i, D3DTSS_BORDERCOLOR, 0);
//		Set_DX8_Texture_Stage_State(i, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
//		Set_DX8_Texture_Stage_State(i, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
//		Set_DX8_Texture_Stage_State(i, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
//		Set_DX8_Texture_Stage_State(i, D3DTSS_MIPMAPLODBIAS, 0);
//		Set_DX8_Texture_Stage_State(i, D3DTSS_MAXMIPLEVEL, 0);
//		Set_DX8_Texture_Stage_State(i, D3DTSS_MAXANISOTROPY, 1);
		//Set_DX8_Texture_Stage_State(i, D3DTSS_ADDRESSW, D3DTADDRESS_WRAP);
		//Set_DX8_Texture_Stage_State(i, D3DTSS_COLORARG0, D3DTA_CURRENT);
		//Set_DX8_Texture_Stage_State(i, D3DTSS_ALPHAARG0, D3DTA_CURRENT);
		//Set_DX8_Texture_Stage_State(i, D3DTSS_RESULTARG, D3DTA_CURRENT);

		Set_DX8_Texture_Stage_State(i, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		Set_Texture(i,nullptr);
	}

//	DX8Wrapper::Set_Material(nullptr);
	VertexMaterialClass::Apply_Null();

	// Lights are not sent to the device at all, so there are none here to clear.

	// set up simple default TSS
	Vector4 vconst[MAX_VERTEX_SHADER_CONSTANTS];
	memset(vconst,0,sizeof(Vector4)*MAX_VERTEX_SHADER_CONSTANTS);
	Set_Vertex_Shader_Constant(0, vconst, MAX_VERTEX_SHADER_CONSTANTS);

	Vector4 pconst[MAX_PIXEL_SHADER_CONSTANTS];
	memset(pconst,0,sizeof(Vector4)*MAX_PIXEL_SHADER_CONSTANTS);
	Set_Pixel_Shader_Constant(0, pconst, MAX_PIXEL_SHADER_CONSTANTS);

	Set_Vertex_Shader(DX8_FVF_XYZNDUV2);
	Set_Pixel_Shader(0);

	ShaderClass::Invalidate();
}

const char* DX8Wrapper::Get_DX8_Render_State_Name(D3DRENDERSTATETYPE state)
{
	switch (state) {
	case D3DRS_ZENABLE                       : return "D3DRS_ZENABLE";
	case D3DRS_FILLMODE                      : return "D3DRS_FILLMODE";
	case D3DRS_SHADEMODE                     : return "D3DRS_SHADEMODE";
	case D3DRS_LINEPATTERN                   : return "D3DRS_LINEPATTERN";
	case D3DRS_ZWRITEENABLE                  : return "D3DRS_ZWRITEENABLE";
	case D3DRS_ALPHATESTENABLE               : return "D3DRS_ALPHATESTENABLE";
	case D3DRS_LASTPIXEL                     : return "D3DRS_LASTPIXEL";
	case D3DRS_SRCBLEND                      : return "D3DRS_SRCBLEND";
	case D3DRS_DESTBLEND                     : return "D3DRS_DESTBLEND";
	case D3DRS_CULLMODE                      : return "D3DRS_CULLMODE";
	case D3DRS_ZFUNC                         : return "D3DRS_ZFUNC";
	case D3DRS_ALPHAREF                      : return "D3DRS_ALPHAREF";
	case D3DRS_ALPHAFUNC                     : return "D3DRS_ALPHAFUNC";
	case D3DRS_DITHERENABLE                  : return "D3DRS_DITHERENABLE";
	case D3DRS_ALPHABLENDENABLE              : return "D3DRS_ALPHABLENDENABLE";
	case D3DRS_FOGENABLE                     : return "D3DRS_FOGENABLE";
	case D3DRS_SPECULARENABLE                : return "D3DRS_SPECULARENABLE";
	case D3DRS_ZVISIBLE                      : return "D3DRS_ZVISIBLE";
	case D3DRS_FOGCOLOR                      : return "D3DRS_FOGCOLOR";
	case D3DRS_FOGTABLEMODE                  : return "D3DRS_FOGTABLEMODE";
	case D3DRS_FOGSTART                      : return "D3DRS_FOGSTART";
	case D3DRS_FOGEND                        : return "D3DRS_FOGEND";
	case D3DRS_FOGDENSITY                    : return "D3DRS_FOGDENSITY";
	case D3DRS_EDGEANTIALIAS                 : return "D3DRS_EDGEANTIALIAS";
	case D3DRS_ZBIAS                         : return "D3DRS_ZBIAS";
	case D3DRS_RANGEFOGENABLE                : return "D3DRS_RANGEFOGENABLE";
	case D3DRS_STENCILENABLE                 : return "D3DRS_STENCILENABLE";
	case D3DRS_STENCILFAIL                   : return "D3DRS_STENCILFAIL";
	case D3DRS_STENCILZFAIL                  : return "D3DRS_STENCILZFAIL";
	case D3DRS_STENCILPASS                   : return "D3DRS_STENCILPASS";
	case D3DRS_STENCILFUNC                   : return "D3DRS_STENCILFUNC";
	case D3DRS_STENCILREF                    : return "D3DRS_STENCILREF";
	case D3DRS_STENCILMASK                   : return "D3DRS_STENCILMASK";
	case D3DRS_STENCILWRITEMASK              : return "D3DRS_STENCILWRITEMASK";
	case D3DRS_TEXTUREFACTOR                 : return "D3DRS_TEXTUREFACTOR";
	case D3DRS_WRAP0                         : return "D3DRS_WRAP0";
	case D3DRS_WRAP1                         : return "D3DRS_WRAP1";
	case D3DRS_WRAP2                         : return "D3DRS_WRAP2";
	case D3DRS_WRAP3                         : return "D3DRS_WRAP3";
	case D3DRS_WRAP4                         : return "D3DRS_WRAP4";
	case D3DRS_WRAP5                         : return "D3DRS_WRAP5";
	case D3DRS_WRAP6                         : return "D3DRS_WRAP6";
	case D3DRS_WRAP7                         : return "D3DRS_WRAP7";
	case D3DRS_CLIPPING                      : return "D3DRS_CLIPPING";
	case D3DRS_LIGHTING                      : return "D3DRS_LIGHTING";
	case D3DRS_AMBIENT                       : return "D3DRS_AMBIENT";
	case D3DRS_FOGVERTEXMODE                 : return "D3DRS_FOGVERTEXMODE";
	case D3DRS_COLORVERTEX                   : return "D3DRS_COLORVERTEX";
	case D3DRS_LOCALVIEWER                   : return "D3DRS_LOCALVIEWER";
	case D3DRS_NORMALIZENORMALS              : return "D3DRS_NORMALIZENORMALS";
	case D3DRS_DIFFUSEMATERIALSOURCE         : return "D3DRS_DIFFUSEMATERIALSOURCE";
	case D3DRS_SPECULARMATERIALSOURCE        : return "D3DRS_SPECULARMATERIALSOURCE";
	case D3DRS_AMBIENTMATERIALSOURCE         : return "D3DRS_AMBIENTMATERIALSOURCE";
	case D3DRS_EMISSIVEMATERIALSOURCE        : return "D3DRS_EMISSIVEMATERIALSOURCE";
	case D3DRS_VERTEXBLEND                   : return "D3DRS_VERTEXBLEND";
	case D3DRS_CLIPPLANEENABLE               : return "D3DRS_CLIPPLANEENABLE";
	case D3DRS_SOFTWAREVERTEXPROCESSING      : return "D3DRS_SOFTWAREVERTEXPROCESSING";
	case D3DRS_POINTSIZE                     : return "D3DRS_POINTSIZE";
	case D3DRS_POINTSIZE_MIN                 : return "D3DRS_POINTSIZE_MIN";
	case D3DRS_POINTSPRITEENABLE             : return "D3DRS_POINTSPRITEENABLE";
	case D3DRS_POINTSCALEENABLE              : return "D3DRS_POINTSCALEENABLE";
	case D3DRS_POINTSCALE_A                  : return "D3DRS_POINTSCALE_A";
	case D3DRS_POINTSCALE_B                  : return "D3DRS_POINTSCALE_B";
	case D3DRS_POINTSCALE_C                  : return "D3DRS_POINTSCALE_C";
	case D3DRS_MULTISAMPLEANTIALIAS          : return "D3DRS_MULTISAMPLEANTIALIAS";
	case D3DRS_MULTISAMPLEMASK               : return "D3DRS_MULTISAMPLEMASK";
	case D3DRS_PATCHEDGESTYLE                : return "D3DRS_PATCHEDGESTYLE";
	case D3DRS_PATCHSEGMENTS                 : return "D3DRS_PATCHSEGMENTS";
	case D3DRS_DEBUGMONITORTOKEN             : return "D3DRS_DEBUGMONITORTOKEN";
	case D3DRS_POINTSIZE_MAX                 : return "D3DRS_POINTSIZE_MAX";
	case D3DRS_INDEXEDVERTEXBLENDENABLE      : return "D3DRS_INDEXEDVERTEXBLENDENABLE";
	case D3DRS_COLORWRITEENABLE              : return "D3DRS_COLORWRITEENABLE";
	case D3DRS_TWEENFACTOR                   : return "D3DRS_TWEENFACTOR";
	case D3DRS_BLENDOP                       : return "D3DRS_BLENDOP";
//	case D3DRS_POSITIONORDER                 : return "D3DRS_POSITIONORDER";
//	case D3DRS_NORMALORDER                   : return "D3DRS_NORMALORDER";
	default											  : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Texture_Stage_State_Name(D3DTEXTURESTAGESTATETYPE state)
{
	switch (state) {
	case D3DTSS_COLOROP                   : return "D3DTSS_COLOROP";
	case D3DTSS_COLORARG1                 : return "D3DTSS_COLORARG1";
	case D3DTSS_COLORARG2                 : return "D3DTSS_COLORARG2";
	case D3DTSS_ALPHAOP                   : return "D3DTSS_ALPHAOP";
	case D3DTSS_ALPHAARG1                 : return "D3DTSS_ALPHAARG1";
	case D3DTSS_ALPHAARG2                 : return "D3DTSS_ALPHAARG2";
	case D3DTSS_BUMPENVMAT00              : return "D3DTSS_BUMPENVMAT00";
	case D3DTSS_BUMPENVMAT01              : return "D3DTSS_BUMPENVMAT01";
	case D3DTSS_BUMPENVMAT10              : return "D3DTSS_BUMPENVMAT10";
	case D3DTSS_BUMPENVMAT11              : return "D3DTSS_BUMPENVMAT11";
	case D3DTSS_TEXCOORDINDEX             : return "D3DTSS_TEXCOORDINDEX";
	case D3DTSS_ADDRESSU                  : return "D3DTSS_ADDRESSU";
	case D3DTSS_ADDRESSV                  : return "D3DTSS_ADDRESSV";
	case D3DTSS_BORDERCOLOR               : return "D3DTSS_BORDERCOLOR";
	case D3DTSS_MAGFILTER                 : return "D3DTSS_MAGFILTER";
	case D3DTSS_MINFILTER                 : return "D3DTSS_MINFILTER";
	case D3DTSS_MIPFILTER                 : return "D3DTSS_MIPFILTER";
	case D3DTSS_MIPMAPLODBIAS             : return "D3DTSS_MIPMAPLODBIAS";
	case D3DTSS_MAXMIPLEVEL               : return "D3DTSS_MAXMIPLEVEL";
	case D3DTSS_MAXANISOTROPY             : return "D3DTSS_MAXANISOTROPY";
	case D3DTSS_BUMPENVLSCALE             : return "D3DTSS_BUMPENVLSCALE";
	case D3DTSS_BUMPENVLOFFSET            : return "D3DTSS_BUMPENVLOFFSET";
	case D3DTSS_TEXTURETRANSFORMFLAGS     : return "D3DTSS_TEXTURETRANSFORMFLAGS";
	case D3DTSS_ADDRESSW                  : return "D3DTSS_ADDRESSW";
	case D3DTSS_COLORARG0                 : return "D3DTSS_COLORARG0";
	case D3DTSS_ALPHAARG0                 : return "D3DTSS_ALPHAARG0";
	case D3DTSS_RESULTARG                 : return "D3DTSS_RESULTARG";
	default										  : return "UNKNOWN";
	}
}

void DX8Wrapper::Get_DX8_Render_State_Value_Name(StringClass& name, D3DRENDERSTATETYPE state, unsigned value)
{
	switch (state) {
	case D3DRS_ZENABLE:
		name=Get_DX8_ZBuffer_Type_Name(value);
		break;

	case D3DRS_FILLMODE:
		name=Get_DX8_Fill_Mode_Name(value);
		break;

	case D3DRS_SHADEMODE:
		name=Get_DX8_Shade_Mode_Name(value);
		break;

	case D3DRS_LINEPATTERN:
	case D3DRS_FOGCOLOR:
	case D3DRS_ALPHAREF:
	case D3DRS_STENCILMASK:
	case D3DRS_STENCILWRITEMASK:
	case D3DRS_TEXTUREFACTOR:
	case D3DRS_AMBIENT:
	case D3DRS_CLIPPLANEENABLE:
	case D3DRS_MULTISAMPLEMASK:
		name.Format("0x%x",value);
		break;

	case D3DRS_ZWRITEENABLE:
	case D3DRS_ALPHATESTENABLE:
	case D3DRS_LASTPIXEL:
	case D3DRS_DITHERENABLE:
	case D3DRS_ALPHABLENDENABLE:
	case D3DRS_FOGENABLE:
	case D3DRS_SPECULARENABLE:
	case D3DRS_STENCILENABLE:
	case D3DRS_RANGEFOGENABLE:
	case D3DRS_EDGEANTIALIAS:
	case D3DRS_CLIPPING:
	case D3DRS_LIGHTING:
	case D3DRS_COLORVERTEX:
	case D3DRS_LOCALVIEWER:
	case D3DRS_NORMALIZENORMALS:
	case D3DRS_SOFTWAREVERTEXPROCESSING:
	case D3DRS_POINTSPRITEENABLE:
	case D3DRS_POINTSCALEENABLE:
	case D3DRS_MULTISAMPLEANTIALIAS:
	case D3DRS_INDEXEDVERTEXBLENDENABLE:
		name=value ? "TRUE" : "FALSE";
		break;

	case D3DRS_SRCBLEND:
	case D3DRS_DESTBLEND:
		name=Get_DX8_Blend_Name(value);
		break;

	case D3DRS_CULLMODE:
		name=Get_DX8_Cull_Mode_Name(value);
		break;

	case D3DRS_ZFUNC:
	case D3DRS_ALPHAFUNC:
	case D3DRS_STENCILFUNC:
		name=Get_DX8_Cmp_Func_Name(value);
		break;

	case D3DRS_ZVISIBLE:
		name="NOTSUPPORTED";
		break;

	case D3DRS_FOGTABLEMODE:
	case D3DRS_FOGVERTEXMODE:
		name=Get_DX8_Fog_Mode_Name(value);
		break;

	case D3DRS_FOGSTART:
	case D3DRS_FOGEND:
	case D3DRS_FOGDENSITY:
	case D3DRS_POINTSIZE:
	case D3DRS_POINTSIZE_MIN:
	case D3DRS_POINTSCALE_A:
	case D3DRS_POINTSCALE_B:
	case D3DRS_POINTSCALE_C:
	case D3DRS_PATCHSEGMENTS:
	case D3DRS_POINTSIZE_MAX:
	case D3DRS_TWEENFACTOR:
		name.Format("%f",*(float*)&value);
		break;

	case D3DRS_ZBIAS:
	case D3DRS_STENCILREF:
		name.Format("%d",value);
		break;

	case D3DRS_STENCILFAIL:
	case D3DRS_STENCILZFAIL:
	case D3DRS_STENCILPASS:
		name=Get_DX8_Stencil_Op_Name(value);
		break;

	case D3DRS_WRAP0:
	case D3DRS_WRAP1:
	case D3DRS_WRAP2:
	case D3DRS_WRAP3:
	case D3DRS_WRAP4:
	case D3DRS_WRAP5:
	case D3DRS_WRAP6:
	case D3DRS_WRAP7:
		name="0";
		if (value&D3DWRAP_U) name+="|D3DWRAP_U";
		if (value&D3DWRAP_V) name+="|D3DWRAP_V";
		if (value&D3DWRAP_W) name+="|D3DWRAP_W";
		break;

	case D3DRS_DIFFUSEMATERIALSOURCE:
	case D3DRS_SPECULARMATERIALSOURCE:
	case D3DRS_AMBIENTMATERIALSOURCE:
	case D3DRS_EMISSIVEMATERIALSOURCE:
		name=Get_DX8_Material_Source_Name(value);
		break;

	case D3DRS_VERTEXBLEND:
		name=Get_DX8_Vertex_Blend_Flag_Name(value);
		break;

	case D3DRS_PATCHEDGESTYLE:
		name=Get_DX8_Patch_Edge_Style_Name(value);
		break;

	case D3DRS_DEBUGMONITORTOKEN:
		name=Get_DX8_Debug_Monitor_Token_Name(value);
		break;

	case D3DRS_COLORWRITEENABLE:
		name="0";
		if (value&D3DCOLORWRITEENABLE_RED) name+="|D3DCOLORWRITEENABLE_RED";
		if (value&D3DCOLORWRITEENABLE_GREEN) name+="|D3DCOLORWRITEENABLE_GREEN";
		if (value&D3DCOLORWRITEENABLE_BLUE) name+="|D3DCOLORWRITEENABLE_BLUE";
		if (value&D3DCOLORWRITEENABLE_ALPHA) name+="|D3DCOLORWRITEENABLE_ALPHA";
		break;
	case D3DRS_BLENDOP:
		name=Get_DX8_Blend_Op_Name(value);
		break;
	default:
		name.Format("UNKNOWN (%d)",value);
		break;
	}
}

void DX8Wrapper::Get_DX8_Texture_Stage_State_Value_Name(StringClass& name, D3DTEXTURESTAGESTATETYPE state, unsigned value)
{
	switch (state) {
	case D3DTSS_COLOROP:
	case D3DTSS_ALPHAOP:
		name=Get_DX8_Texture_Op_Name(value);
		break;

	case D3DTSS_COLORARG0:
	case D3DTSS_COLORARG1:
	case D3DTSS_COLORARG2:
	case D3DTSS_ALPHAARG0:
	case D3DTSS_ALPHAARG1:
	case D3DTSS_ALPHAARG2:
	case D3DTSS_RESULTARG:
		name=Get_DX8_Texture_Arg_Name(value);
		break;

	case D3DTSS_ADDRESSU:
	case D3DTSS_ADDRESSV:
	case D3DTSS_ADDRESSW:
		name=Get_DX8_Texture_Address_Name(value);
		break;

	case D3DTSS_MAGFILTER:
	case D3DTSS_MINFILTER:
	case D3DTSS_MIPFILTER:
		name=Get_DX8_Texture_Filter_Name(value);
		break;

	case D3DTSS_TEXTURETRANSFORMFLAGS:
		name=Get_DX8_Texture_Transform_Flag_Name(value);
		break;

	// Floating point values
	case D3DTSS_MIPMAPLODBIAS:
	case D3DTSS_BUMPENVMAT00:
	case D3DTSS_BUMPENVMAT01:
	case D3DTSS_BUMPENVMAT10:
	case D3DTSS_BUMPENVMAT11:
	case D3DTSS_BUMPENVLSCALE:
	case D3DTSS_BUMPENVLOFFSET:
		name.Format("%f",*(float*)&value);
		break;

	case D3DTSS_TEXCOORDINDEX:
		if ((value&0xffff0000)==D3DTSS_TCI_CAMERASPACENORMAL) {
			name.Format("D3DTSS_TCI_CAMERASPACENORMAL|%d",value&0xffff);
		}
		else if ((value&0xffff0000)==D3DTSS_TCI_CAMERASPACEPOSITION) {
			name.Format("D3DTSS_TCI_CAMERASPACEPOSITION|%d",value&0xffff);
		}
		else if ((value&0xffff0000)==D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR) {
			name.Format("D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR|%d",value&0xffff);
		}
		else {
			name.Format("%d",value);
		}
		break;

	// Integer value
	case D3DTSS_MAXMIPLEVEL:
	case D3DTSS_MAXANISOTROPY:
		name.Format("%d",value);
		break;
	// Hex values
	case D3DTSS_BORDERCOLOR:
		name.Format("0x%x",value);
		break;

	default:
		name.Format("UNKNOWN (%d)",value);
		break;
	}
}

const char* DX8Wrapper::Get_DX8_Texture_Op_Name(unsigned value)
{
	switch (value) {
	case D3DTOP_DISABLE                      : return "D3DTOP_DISABLE";
	case D3DTOP_SELECTARG1                   : return "D3DTOP_SELECTARG1";
	case D3DTOP_SELECTARG2                   : return "D3DTOP_SELECTARG2";
	case D3DTOP_MODULATE                     : return "D3DTOP_MODULATE";
	case D3DTOP_MODULATE2X                   : return "D3DTOP_MODULATE2X";
	case D3DTOP_MODULATE4X                   : return "D3DTOP_MODULATE4X";
	case D3DTOP_ADD                          : return "D3DTOP_ADD";
	case D3DTOP_ADDSIGNED                    : return "D3DTOP_ADDSIGNED";
	case D3DTOP_ADDSIGNED2X                  : return "D3DTOP_ADDSIGNED2X";
	case D3DTOP_SUBTRACT                     : return "D3DTOP_SUBTRACT";
	case D3DTOP_ADDSMOOTH                    : return "D3DTOP_ADDSMOOTH";
	case D3DTOP_BLENDDIFFUSEALPHA            : return "D3DTOP_BLENDDIFFUSEALPHA";
	case D3DTOP_BLENDTEXTUREALPHA            : return "D3DTOP_BLENDTEXTUREALPHA";
	case D3DTOP_BLENDFACTORALPHA             : return "D3DTOP_BLENDFACTORALPHA";
	case D3DTOP_BLENDTEXTUREALPHAPM          : return "D3DTOP_BLENDTEXTUREALPHAPM";
	case D3DTOP_BLENDCURRENTALPHA            : return "D3DTOP_BLENDCURRENTALPHA";
	case D3DTOP_PREMODULATE                  : return "D3DTOP_PREMODULATE";
	case D3DTOP_MODULATEALPHA_ADDCOLOR       : return "D3DTOP_MODULATEALPHA_ADDCOLOR";
	case D3DTOP_MODULATECOLOR_ADDALPHA       : return "D3DTOP_MODULATECOLOR_ADDALPHA";
	case D3DTOP_MODULATEINVALPHA_ADDCOLOR    : return "D3DTOP_MODULATEINVALPHA_ADDCOLOR";
	case D3DTOP_MODULATEINVCOLOR_ADDALPHA    : return "D3DTOP_MODULATEINVCOLOR_ADDALPHA";
	case D3DTOP_BUMPENVMAP                   : return "D3DTOP_BUMPENVMAP";
	case D3DTOP_BUMPENVMAPLUMINANCE          : return "D3DTOP_BUMPENVMAPLUMINANCE";
	case D3DTOP_DOTPRODUCT3                  : return "D3DTOP_DOTPRODUCT3";
	case D3DTOP_MULTIPLYADD                  : return "D3DTOP_MULTIPLYADD";
	case D3DTOP_LERP                         : return "D3DTOP_LERP";
	default										     : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Texture_Arg_Name(unsigned value)
{
	switch (value) {
	case D3DTA_CURRENT			: return "D3DTA_CURRENT";
	case D3DTA_DIFFUSE			: return "D3DTA_DIFFUSE";
	case D3DTA_SELECTMASK		: return "D3DTA_SELECTMASK";
	case D3DTA_SPECULAR			: return "D3DTA_SPECULAR";
	case D3DTA_TEMP				: return "D3DTA_TEMP";
	case D3DTA_TEXTURE			: return "D3DTA_TEXTURE";
	case D3DTA_TFACTOR			: return "D3DTA_TFACTOR";
	case D3DTA_ALPHAREPLICATE	: return "D3DTA_ALPHAREPLICATE";
	case D3DTA_COMPLEMENT		: return "D3DTA_COMPLEMENT";
	default					      : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Texture_Filter_Name(unsigned value)
{
	switch (value) {
	case D3DTEXF_NONE				: return "D3DTEXF_NONE";
	case D3DTEXF_POINT			: return "D3DTEXF_POINT";
	case D3DTEXF_LINEAR			: return "D3DTEXF_LINEAR";
	case D3DTEXF_ANISOTROPIC	: return "D3DTEXF_ANISOTROPIC";
	case D3DTEXF_FLATCUBIC		: return "D3DTEXF_FLATCUBIC";
	case D3DTEXF_GAUSSIANCUBIC	: return "D3DTEXF_GAUSSIANCUBIC";
	default					      : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Texture_Address_Name(unsigned value)
{
	switch (value) {
	case D3DTADDRESS_WRAP		: return "D3DTADDRESS_WRAP";
	case D3DTADDRESS_MIRROR		: return "D3DTADDRESS_MIRROR";
	case D3DTADDRESS_CLAMP		: return "D3DTADDRESS_CLAMP";
	case D3DTADDRESS_BORDER		: return "D3DTADDRESS_BORDER";
	case D3DTADDRESS_MIRRORONCE: return "D3DTADDRESS_MIRRORONCE";
	default					      : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Texture_Transform_Flag_Name(unsigned value)
{
	switch (value) {
	case D3DTTFF_DISABLE			: return "D3DTTFF_DISABLE";
	case D3DTTFF_COUNT1			: return "D3DTTFF_COUNT1";
	case D3DTTFF_COUNT2			: return "D3DTTFF_COUNT2";
	case D3DTTFF_COUNT3			: return "D3DTTFF_COUNT3";
	case D3DTTFF_COUNT4			: return "D3DTTFF_COUNT4";
	case D3DTTFF_PROJECTED		: return "D3DTTFF_PROJECTED";
	default					      : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_ZBuffer_Type_Name(unsigned value)
{
	switch (value) {
	case D3DZB_FALSE				: return "D3DZB_FALSE";
	case D3DZB_TRUE				: return "D3DZB_TRUE";
	case D3DZB_USEW				: return "D3DZB_USEW";
	default					      : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Fill_Mode_Name(unsigned value)
{
	switch (value) {
	case D3DFILL_POINT			: return "D3DFILL_POINT";
	case D3DFILL_WIREFRAME		: return "D3DFILL_WIREFRAME";
	case D3DFILL_SOLID			: return "D3DFILL_SOLID";
	default					      : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Shade_Mode_Name(unsigned value)
{
	switch (value) {
	case D3DSHADE_FLAT			: return "D3DSHADE_FLAT";
	case D3DSHADE_GOURAUD		: return "D3DSHADE_GOURAUD";
	case D3DSHADE_PHONG			: return "D3DSHADE_PHONG";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Blend_Name(unsigned value)
{
	switch (value) {
	case D3DBLEND_ZERO                : return "D3DBLEND_ZERO";
	case D3DBLEND_ONE                 : return "D3DBLEND_ONE";
	case D3DBLEND_SRCCOLOR            : return "D3DBLEND_SRCCOLOR";
	case D3DBLEND_INVSRCCOLOR         : return "D3DBLEND_INVSRCCOLOR";
	case D3DBLEND_SRCALPHA            : return "D3DBLEND_SRCALPHA";
	case D3DBLEND_INVSRCALPHA         : return "D3DBLEND_INVSRCALPHA";
	case D3DBLEND_DESTALPHA           : return "D3DBLEND_DESTALPHA";
	case D3DBLEND_INVDESTALPHA        : return "D3DBLEND_INVDESTALPHA";
	case D3DBLEND_DESTCOLOR           : return "D3DBLEND_DESTCOLOR";
	case D3DBLEND_INVDESTCOLOR        : return "D3DBLEND_INVDESTCOLOR";
	case D3DBLEND_SRCALPHASAT         : return "D3DBLEND_SRCALPHASAT";
	case D3DBLEND_BOTHSRCALPHA        : return "D3DBLEND_BOTHSRCALPHA";
	case D3DBLEND_BOTHINVSRCALPHA     : return "D3DBLEND_BOTHINVSRCALPHA";
	default									 : return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Cull_Mode_Name(unsigned value)
{
	switch (value) {
	case D3DCULL_NONE				: return "D3DCULL_NONE";
	case D3DCULL_CW				: return "D3DCULL_CW";
	case D3DCULL_CCW				: return "D3DCULL_CCW";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Cmp_Func_Name(unsigned value)
{
	switch (value) {
	case D3DCMP_NEVER          : return "D3DCMP_NEVER";
	case D3DCMP_LESS           : return "D3DCMP_LESS";
	case D3DCMP_EQUAL          : return "D3DCMP_EQUAL";
	case D3DCMP_LESSEQUAL      : return "D3DCMP_LESSEQUAL";
	case D3DCMP_GREATER        : return "D3DCMP_GREATER";
	case D3DCMP_NOTEQUAL       : return "D3DCMP_NOTEQUAL";
	case D3DCMP_GREATEREQUAL   : return "D3DCMP_GREATEREQUAL";
	case D3DCMP_ALWAYS         : return "D3DCMP_ALWAYS";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Fog_Mode_Name(unsigned value)
{
	switch (value) {
	case D3DFOG_NONE				: return "D3DFOG_NONE";
	case D3DFOG_EXP				: return "D3DFOG_EXP";
	case D3DFOG_EXP2				: return "D3DFOG_EXP2";
	case D3DFOG_LINEAR			: return "D3DFOG_LINEAR";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Stencil_Op_Name(unsigned value)
{
	switch (value) {
	case D3DSTENCILOP_KEEP		: return "D3DSTENCILOP_KEEP";
	case D3DSTENCILOP_ZERO		: return "D3DSTENCILOP_ZERO";
	case D3DSTENCILOP_REPLACE	: return "D3DSTENCILOP_REPLACE";
	case D3DSTENCILOP_INCRSAT	: return "D3DSTENCILOP_INCRSAT";
	case D3DSTENCILOP_DECRSAT	: return "D3DSTENCILOP_DECRSAT";
	case D3DSTENCILOP_INVERT	: return "D3DSTENCILOP_INVERT";
	case D3DSTENCILOP_INCR		: return "D3DSTENCILOP_INCR";
	case D3DSTENCILOP_DECR		: return "D3DSTENCILOP_DECR";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Material_Source_Name(unsigned value)
{
	switch (value) {
	case D3DMCS_MATERIAL			: return "D3DMCS_MATERIAL";
	case D3DMCS_COLOR1			: return "D3DMCS_COLOR1";
	case D3DMCS_COLOR2			: return "D3DMCS_COLOR2";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Vertex_Blend_Flag_Name(unsigned value)
{
	switch (value) {
	case D3DVBF_DISABLE			: return "D3DVBF_DISABLE";
	case D3DVBF_1WEIGHTS			: return "D3DVBF_1WEIGHTS";
	case D3DVBF_2WEIGHTS			: return "D3DVBF_2WEIGHTS";
	case D3DVBF_3WEIGHTS			: return "D3DVBF_3WEIGHTS";
	case D3DVBF_TWEENING			: return "D3DVBF_TWEENING";
	case D3DVBF_0WEIGHTS			: return "D3DVBF_0WEIGHTS";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Patch_Edge_Style_Name(unsigned value)
{
	switch (value) {
	case D3DPATCHEDGE_DISCRETE	: return "D3DPATCHEDGE_DISCRETE";
   case D3DPATCHEDGE_CONTINUOUS:return "D3DPATCHEDGE_CONTINUOUS";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Debug_Monitor_Token_Name(unsigned value)
{
	switch (value) {
	case D3DDMT_ENABLE			: return "D3DDMT_ENABLE";
	case D3DDMT_DISABLE			: return "D3DDMT_DISABLE";
	default							: return "UNKNOWN";
	}
}

const char* DX8Wrapper::Get_DX8_Blend_Op_Name(unsigned value)
{
	switch (value) {
	case D3DBLENDOP_ADD			: return "D3DBLENDOP_ADD";
	case D3DBLENDOP_SUBTRACT	: return "D3DBLENDOP_SUBTRACT";
	case D3DBLENDOP_REVSUBTRACT: return "D3DBLENDOP_REVSUBTRACT";
	case D3DBLENDOP_MIN			: return "D3DBLENDOP_MIN";
	case D3DBLENDOP_MAX			: return "D3DBLENDOP_MAX";
	default							: return "UNKNOWN";
	}
}


//============================================================================
// DX8Wrapper::getBackBufferFormat
//============================================================================

WW3DFormat	DX8Wrapper::getBackBufferFormat()
{
	return SwapChain.BackBufferFormat;
}

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

// The Direct3D 11 backend. See gfxdevice_d3d11.h for what is different about it and why.
//
// This is the only translation unit in the tree that includes <d3d11.h>. It gets d3d9.h
// too, whether it likes it or not: dx8wrapper.h is this library's precompiled header and
// pulls in d3d9_compat.h. The two coexist -- checked, not assumed -- because
// d3d9_compat.h's CreateTexture / CreateVertexBuffer / CreateIndexBuffer macros are
// fixed-arity and D3D11 spells its own creation calls CreateTexture2D and CreateBuffer.
// What this file does not do is name a D3D9 type, which is why the state words it
// translates are written out as numbers below rather than taken from that header.
//
// d3d11.dll and dxgi.dll are loaded by hand rather than imported, so an executable built
// with this backend compiled in still starts on a machine that has neither. That mirrors
// what the D3D9 adapter already does with d3d9.dll, and it is what makes "both backends
// stay compiled in" cost nothing at startup.

#include "gfxdevice_d3d11.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#include "WWDebug/wwdebug.h"
#include "WWLib/ffactory.h"
#include "WWLib/WWFILE.h"

// Written by gfxdevice_png.cpp, which owns the one copy of stb_image_write in this
// library. Declared here rather than included so that this file pulls in no more headers.
bool Gfx_Write_Png_RGB(const char * path, unsigned width, unsigned height,
	const unsigned char * rgb);

// Defined in dx8wrapper.cpp. Counting device calls here rather than in the wrapper is
// the only way the count can be right -- one interface call is not always one device
// call, and which it is differs between the two backends. Declared as a free function
// rather than reached through DX8Wrapper because that class's header is this library's
// precompiled header and pulls in d3d9_compat.h with it.
void DX8Wrapper_Increment_Call_Count();

#ifdef RTS_DEBUG
struct D3D11ProfileStats {
	LONGLONG t_draw_indexed;
	LONGLONG t_draw;
	LONGLONG t_draw_up;
	LONGLONG t_prepare_draw;
	LONGLONG t_input_layout;
	LONGLONG t_apply_states;
	LONGLONG t_upload_constants;
	LONGLONG t_raw_draw_call;
	LONGLONG t_present;
	LONGLONG t_drain_debug;
	LONGLONG t_swap_present;
	LONGLONG t_set_rt;
	LONGLONG t_clear;
	LONGLONG t_set_texture;
	LONGLONG t_set_vs_const;
	LONGLONG t_set_ps_const;
	LONGLONG t_set_frame_const;
	LONGLONG t_map_buffer;
	LONGLONG t_unmap_buffer;
	LONGLONG t_begin_scene;
	LONGLONG t_render_span;
	LONGLONG t_post_span;
	LONGLONG t_logic_span;

	LONGLONG t_copy_surface;
	LONGLONG t_copy_surface_rect;
	LONGLONG t_map_surface;
	LONGLONG t_unmap_surface;
	LONGLONG t_map_texture;
	LONGLONG t_unmap_texture;
	LONGLONG t_readback_subresource;
	LONGLONG t_set_vs;
	LONGLONG t_set_ps;
	LONGLONG t_set_stream;
	LONGLONG t_set_ib;
	LONGLONG t_set_vp;
	LONGLONG t_set_rs;
	LONGLONG t_set_tss;

	LONGLONG t_dispatch;
	LONGLONG t_map_buffer_read;

	LONGLONG t_pass_backbuffer;
	LONGLONG t_pass_shadow;
	LONGLONG t_pass_depthprepass;
	LONGLONG t_pass_other;

	unsigned n_draw_indexed;
	unsigned n_draw;
	unsigned n_draw_up;
	unsigned n_upload_vs;
	unsigned n_upload_ps;
	unsigned n_upload_frame;
	unsigned n_set_rt;
	unsigned n_clear;
	unsigned n_set_texture;
	unsigned n_set_vs_const;
	unsigned n_set_ps_const;
	unsigned n_set_frame_const;
	unsigned n_map_buffer;
	unsigned n_unmap_buffer;

	unsigned n_copy_surface;
	unsigned n_copy_surface_rect;
	unsigned n_map_surface;
	unsigned n_unmap_surface;
	unsigned n_map_texture;
	unsigned n_unmap_texture;
	unsigned n_readback_subresource;
	unsigned n_set_vs;
	unsigned n_set_ps;
	unsigned n_set_stream;
	unsigned n_set_ib;
	unsigned n_set_vp;
	unsigned n_set_rs;
	unsigned n_set_tss;
	unsigned n_begin_scene;
	unsigned n_end_scene;
	unsigned n_dispatch;
	unsigned n_map_buffer_read;

	unsigned n_draws_backbuffer;
	unsigned n_draws_shadow;
	unsigned n_draws_depthprepass;
	unsigned n_draws_other;

	void Reset() { memset(this, 0, sizeof(*this)); }
};
static D3D11ProfileStats s_d3d11_prof;
static LARGE_INTEGER s_tick_begin;
static LARGE_INTEGER s_tick_end;
static LARGE_INTEGER s_tick_present;

enum CurrentPassType { PASS_NONE, PASS_BACKBUFFER, PASS_SHADOW, PASS_DEPTHPREPASS, PASS_OTHER };
static CurrentPassType s_current_pass = PASS_NONE;
static LARGE_INTEGER s_tick_pass_start;
static unsigned s_current_pass_draws = 0;

static void Flush_Pass_Timing(LARGE_INTEGER now)
{
	if (s_tick_pass_start.QuadPart != 0) {
		LONGLONG dt = now.QuadPart - s_tick_pass_start.QuadPart;
		switch (s_current_pass) {
		case PASS_BACKBUFFER:
			s_d3d11_prof.t_pass_backbuffer += dt;
			s_d3d11_prof.n_draws_backbuffer += s_current_pass_draws;
			break;
		case PASS_SHADOW:
			s_d3d11_prof.t_pass_shadow += dt;
			s_d3d11_prof.n_draws_shadow += s_current_pass_draws;
			break;
		case PASS_DEPTHPREPASS:
			s_d3d11_prof.t_pass_depthprepass += dt;
			s_d3d11_prof.n_draws_depthprepass += s_current_pass_draws;
			break;
		default:
			s_d3d11_prof.t_pass_other += dt;
			s_d3d11_prof.n_draws_other += s_current_pass_draws;
			break;
		}
	}
	s_tick_pass_start = now;
	s_current_pass_draws = 0;
}

struct D3D11TimerScope {
	LONGLONG & target;
	LARGE_INTEGER start;
	D3D11TimerScope(LONGLONG & t) : target(t) {
		QueryPerformanceCounter(&start);
	}
	~D3D11TimerScope() {
		LARGE_INTEGER end;
		QueryPerformanceCounter(&end);
		target += (end.QuadPart - start.QuadPart);
	}
};
#define PROFILE_D3D11_SCOPE(field) D3D11TimerScope _scope_##field(s_d3d11_prof.field)
#else
#define PROFILE_D3D11_SCOPE(field)
#endif

// ---------------------------------------------------------------------------
// D3D9 state words, spelled out.
//
// The seam passes the engine's own state numbers, which are D3D9's -- that is the
// contract in gfxdevice.h. This backend therefore has to know them, and it knows them
// from this list rather than from d3d9.h: including that header here is precisely what
// this file is arranged to avoid. The numbers are fixed by a shipped API and cannot
// change.
// ---------------------------------------------------------------------------

enum {
	RS_ZENABLE = 7, RS_FILLMODE = 8, RS_SHADEMODE = 9, RS_LINEPATTERN_D3D8 = 10,
	RS_ZWRITEENABLE = 14, RS_ALPHATESTENABLE = 15, RS_LASTPIXEL = 16,
	RS_SRCBLEND = 19, RS_DESTBLEND = 20, RS_CULLMODE = 22, RS_ZFUNC = 23,
	RS_ALPHAREF = 24, RS_ALPHAFUNC = 25, RS_DITHERENABLE = 26,
	RS_ALPHABLENDENABLE = 27, RS_FOGENABLE = 28, RS_SPECULARENABLE = 29,
	RS_FOGCOLOR = 34, RS_FOGTABLEMODE = 35, RS_FOGSTART = 36, RS_FOGEND = 37,
	RS_FOGDENSITY = 38, RS_RANGEFOGENABLE = 48,
	RS_STENCILENABLE = 52, RS_STENCILFAIL = 53, RS_STENCILZFAIL = 54,
	RS_STENCILPASS = 55, RS_STENCILFUNC = 56, RS_STENCILREF = 57,
	RS_STENCILMASK = 58, RS_STENCILWRITEMASK = 59, RS_TEXTUREFACTOR = 60,
	RS_WRAP0 = 128,
	RS_CLIPPING = 136, RS_LIGHTING = 137, RS_AMBIENT = 139, RS_FOGVERTEXMODE = 140,
	RS_COLORVERTEX = 141, RS_LOCALVIEWER = 142, RS_NORMALIZENORMALS = 143,
	RS_DIFFUSEMATERIALSOURCE = 145, RS_SPECULARMATERIALSOURCE = 146,
	RS_AMBIENTMATERIALSOURCE = 147, RS_EMISSIVEMATERIALSOURCE = 148,
	RS_VERTEXBLEND = 151, RS_CLIPPLANEENABLE = 152,
	RS_POINTSIZE = 154, RS_POINTSIZE_MIN = 155, RS_POINTSPRITEENABLE = 156,
	RS_POINTSCALEENABLE = 157, RS_POINTSCALE_A = 158, RS_POINTSCALE_B = 159,
	RS_POINTSCALE_C = 160, RS_MULTISAMPLEANTIALIAS = 161, RS_MULTISAMPLEMASK = 162,
	RS_PATCHEDGESTYLE_D3D8 = 163, RS_PATCHSEGMENTS_D3D8 = 164,
	RS_DEBUGMONITORTOKEN = 165, RS_POINTSIZE_MAX = 166,
	RS_INDEXEDVERTEXBLENDENABLE = 167, RS_COLORWRITEENABLE = 168,
	RS_TWEENFACTOR = 170, RS_BLENDOP = 171, RS_POSITIONDEGREE = 172,
	RS_NORMALDEGREE = 173, RS_SCISSORTESTENABLE = 174,
	RS_SLOPESCALEDEPTHBIAS = 175, RS_ANTIALIASEDLINEENABLE = 176,
	RS_MINTESSELLATIONLEVEL = 178, RS_MAXTESSELLATIONLEVEL = 179,
	RS_ADAPTIVETESS_X = 180, RS_ADAPTIVETESS_Y = 181, RS_ADAPTIVETESS_Z = 182,
	RS_ADAPTIVETESS_W = 183, RS_ENABLEADAPTIVETESSELLATION = 184,
	RS_TWOSIDEDSTENCILMODE = 185, RS_CCW_STENCILFAIL = 186,
	RS_CCW_STENCILZFAIL = 187, RS_CCW_STENCILPASS = 188, RS_CCW_STENCILFUNC = 189,
	RS_COLORWRITEENABLE1 = 190, RS_COLORWRITEENABLE2 = 191, RS_COLORWRITEENABLE3 = 192,
	RS_BLENDFACTOR = 193, RS_SRGBWRITEENABLE = 194, RS_DEPTHBIAS = 195,
	RS_SEPARATEALPHABLENDENABLE = 206, RS_SRCBLENDALPHA = 207,
	RS_DESTBLENDALPHA = 208, RS_BLENDOPALPHA = 209,
	// The five d3d9_compat.h keeps alive as dummy slots so the engine's tracked array
	// still has somewhere to put a D3D8 state word. All five are absorbed here, exactly
	// as the D3D9 backend absorbs them.
	RS_COMPAT_LINEPATTERN = 220, RS_COMPAT_SOFTWAREVERTEXPROCESSING = 221,
	RS_COMPAT_ZVISIBLE = 222, RS_COMPAT_PATCHSEGMENTS = 223, RS_COMPAT_ZBIAS = 224,
	RS_COMPAT_EDGEANTIALIAS = 225, RS_COMPAT_PATCHEDGESTYLE = 226,
	RS_COUNT = 256
};

// The ten D3D8 texture stage states that became D3D9 sampler states, at the numbers
// d3d9_compat.h keeps them at. Everything else arriving at Set_Texture_Stage_State is a
// fixed-function combiner word with no D3D11 meaning.
enum {
	TSS_ADDRESSU = 13, TSS_ADDRESSV = 14, TSS_BORDERCOLOR = 15,
	TSS_MAGFILTER = 16, TSS_MINFILTER = 17, TSS_MIPFILTER = 18,
	TSS_MIPMAPLODBIAS = 19, TSS_MAXMIPLEVEL = 20, TSS_MAXANISOTROPY = 21,
	TSS_ADDRESSW = 25,
	// The engine's own word, not D3D's: see D3DTSS_COMPAREFUNC in gfxstatewords.h.
	TSS_COMPAREFUNC = 29
};

// D3DPRIMITIVETYPE.
enum {
	PT_POINTLIST = 1, PT_LINELIST = 2, PT_LINESTRIP = 3,
	PT_TRIANGLELIST = 4, PT_TRIANGLESTRIP = 5, PT_TRIANGLEFAN = 6
};

// D3DFVF bits. The engine's vertex formats are all built from these.
enum {
	FVF_XYZ = 0x002, FVF_XYZRHW = 0x004, FVF_NORMAL = 0x010, FVF_PSIZE = 0x020,
	FVF_DIFFUSE = 0x040, FVF_SPECULAR = 0x080,
	FVF_TEXCOUNT_MASK = 0xf00, FVF_TEXCOUNT_SHIFT = 8
};

// D3D9 numbers its transform slots 0..31 and then puts D3DTS_WORLD at 256, with three
// more world matrices after it. Compacted onto one array by Transform_Slot below.
#define GFX_TRANSFORM_SLOTS 36
#define GFX_MAX_STAGES 8
#define GFX_VS_CONSTANTS 96
#define GFX_PS_CONSTANTS 32

// The b1 buffer, C2 of the clustered-lighting plan. Sixteen vec4 is generous for the
// three the plan names today (ClusterParams, ClusterDepth, CameraForward) with room for
// frame-global data that migrates here later -- see the comment on
// GfxDeviceClass::Set_Frame_Constants for why anything migrates here at all.
#define GFX_FRAME_CONSTANTS 32

// The two halves of the pixel stage's t register file have to meet exactly: the texture
// stages own 0..GFX_MAX_STAGES-1 and the shader buffers own the rest. A gap wastes a
// register; an overlap means Set_Texture and Set_Pixel_Buffer fight over one slot and
// whichever wrote last wins silently, which is the failure this arrangement exists to
// prevent. GFX_MAX_STAGES tracks the engine's MAX_TEXTURE_STAGES, so raising that means
// raising GFX_FIRST_PIXEL_BUFFER_SLOT with it -- and this is what says so.
static_assert(GFX_FIRST_PIXEL_BUFFER_SLOT == GFX_MAX_STAGES,
	"the shader-buffer slots must start exactly where the texture stages end");

// ---------------------------------------------------------------------------
// What was swallowed and what was dropped.
//
// "Absorb, do not assert" is the contract, and an absorbed write that leaves no trace is
// indistinguishable from one that never happened. These are the trace. They are debug
// only and are printed on the same 600-frame window as every other census in this tree,
// so the figures can be read beside them.
// ---------------------------------------------------------------------------

#ifdef RTS_DEBUG
// The absorbed counters, the per-word histograms and the shared-predicate cross-check were
// here. They asked one question -- does swallowing these writes change the picture -- and
// Phase 9 answered it with the only instrument that could: it made the D3D9 backend drop
// exactly the same set and diffed D3D9 against itself. 0 differing pixels on four scenes,
// with a positive control on every group. Retired with the comparator, because a count of
// swallowed writes that nothing can be compared against is a line printing into a log
// nobody can interpret. The named list is in the Phase 9 investigation.
static unsigned s_dropped_no_vertex_shader = 0;
static unsigned s_dropped_trianglefan = 0;
static unsigned s_dropped_no_input_layout = 0;
static unsigned s_dropped_signature_mismatch = 0;
static unsigned s_draws = 0;
// The render-target hazard. s_srv_forced_unbound is how often a texture had to leave a
// shader-resource slot because it became the render target; s_srv_rebound is how often it
// went back; s_draws_with_null_slot is the one that matters -- a draw submitted while the
// wrapper believed a texture was bound and the device had NULL there.
static unsigned s_srv_forced_unbound = 0;
static unsigned s_srv_rebound = 0;
// Two different things, and the first draft of this instrument conflated them.
//
// s_draws_target_conflict is draws submitted while a slot really is the render target.
// That is legal, unavoidable and true of D3D9 too -- the shadow depth pass binds the
// shadow map and then draws several hundred times with last frame's copy of it still in a
// stage -- so it is exposure, not a defect, and it does not go to zero.
//
// s_draws_rescued is the one that measures the bug: draws that sampled through a slot this
// backend put back on its own initiative, between the resource ceasing to be a target and
// the wrapper next writing that stage. Without the fix every one of those draws sampled
// zero. It should be non-zero if the bug was ever real.
static unsigned s_draws_target_conflict = 0;
// ...and the subset of those where the bound pixel shader declares a texture in a
// register that is one of the nulled slots. Only these can tell the two APIs apart.
static unsigned s_draws_target_conflict_read = 0;
// The control for the line above: draws reaching that test with no pixel shader whose
// registers could be read at all. A zero narrowed figure means nothing without it.
static unsigned s_draws_target_conflict_noshader = 0;
// The control for the narrowing itself: how many pixel shaders the RDEF parse found any
// texture register in at all. If that were zero the narrowed draw count would read zero
// whatever the truth was, and the parser would be reporting its own failure as a result.
static unsigned s_ps_created = 0;
static unsigned s_ps_with_textures = 0;
static unsigned s_ps_texture_union = 0;
static unsigned s_draws_rescued = 0;
// The sampler census. 87% of the D3D11/D3D9 difference is texture sampling, and the two
// candidates are mip selection and the anisotropic implementation; these say which
// samplers the frame is actually built out of rather than which ones it could be.
// s_sampler_stage_no_sampler is the one with a hypothesis behind it: a stage that never
// received a D3DSAMP write gets no sampler object at all here and falls back to D3D11's
// own default, which mipmaps -- where D3D9's device default for MIPFILTER is NONE.
static unsigned s_sampler_textured_stages = 0;
static unsigned s_sampler_stage_mip_none = 0;
static unsigned s_sampler_stage_mip_point = 0;
static unsigned s_sampler_stage_mip_linear = 0;
static unsigned s_sampler_stage_aniso = 0;
static unsigned s_sampler_stage_no_sampler = 0;
// Every distinct sampler description this backend has created, kept here rather than read
// off the cache because the report is a static member and has no device to ask. Ten fields
// in declaration order: address u/v/w, mag, min, mip, lod bias (as a float's bit pattern),
// max lod, max anisotropy, border.
static unsigned s_sampler_keys[64][11];
static unsigned s_sampler_key_uses[64];
static int s_sampler_key_count = 0;
// The SRV/UAV hazard, which is the render-target hazard above in its other clothes.
//
// s_buffer_srv_forced_unbound is how often a buffer had to leave a read slot -- on the
// pixel stage or the compute stage -- because it was about to be bound for writing;
// s_buffer_uav_forced_unbound is the same in the other direction. Both should be small
// and non-zero in a frame that dispatches: a grid is cleared and filled by the compute
// stage and then read by every lit pixel shader, so it changes hands twice a frame.
//
// Two zeroes is the instrument not running, and that matters more here than it did for
// textures: D3D11 nulls the conflicting binding on its own and says so only to the debug
// layer, so the failure mode with these at zero is not a crash, it is a buffer that reads
// entirely as zeroes. For a cluster grid that is "no lights near this pixel", everywhere,
// which is a picture nobody would look at twice.
static unsigned s_buffer_srv_forced_unbound = 0;
static unsigned s_buffer_uav_forced_unbound = 0;
static unsigned s_csMade = 0, s_csFreed = 0;
#define ABSORB(counter) do { ++(counter); } while (0)
#else
#define ABSORB(counter) do { } while (0)
#endif


// ---------------------------------------------------------------------------
// A trace of the first frames, for when the backend does not survive one.
//
// W3D_D3D11_TRACE=N logs every entry into this backend for the first N presents and then
// goes quiet. It is the instrument for the one failure mode a census cannot describe: the
// run that stops before the first census window, where the debug log is the measurement
// and it ends mid-sentence.
// ---------------------------------------------------------------------------

#ifdef RTS_DEBUG
static int s_trace_frames = -1;			// -1 = not yet read from the environment
static int s_trace_remaining = 0;

static bool Tracing()
{
	if (s_trace_frames < 0) {
		const char * n = getenv("W3D_D3D11_TRACE");
		s_trace_frames = (n != nullptr) ? atoi(n) : 0;
		s_trace_remaining = s_trace_frames;
	}
	return s_trace_remaining > 0;
}
#define TRACE(what) do { if (Tracing()) WWDEBUG_SAY(("D3D11 TRACE: %s", what)); } while (0)
#else
#define TRACE(what) do { } while (0)
#endif

// ---------------------------------------------------------------------------
// Formats
// ---------------------------------------------------------------------------

// WW3DFormat is the engine's own vocabulary and the mapping to DXGI is by name, one row
// at a time, rather than by the arithmetic the D3D9 conversion can get away with.
//
// The rows that are not a straight rename are the ones worth reading. D3D9's colour
// formats name their channels in memory order most-significant first (A8R8G8B8 is BGRA in
// bytes) and DXGI names them least-significant first, so A8R8G8B8 is B8G8R8A8_UNORM and
// not R8G8B8A8_UNORM -- getting that backwards swaps red and blue in every texture in the
// game, which looks like a bug in the shaders. The 16-bit and paletted formats have no
// DXGI counterpart at all and return UNKNOWN; the caller's creation then fails and the
// engine's own format fallback runs, which is what it is for.
static DXGI_FORMAT WW3D_To_DXGI(WW3DFormat format)
{
	switch (format) {
	case WW3D_FORMAT_A8R8G8B8:			return DXGI_FORMAT_B8G8R8A8_UNORM;
	case WW3D_FORMAT_X8R8G8B8:			return DXGI_FORMAT_B8G8R8X8_UNORM;
	case WW3D_FORMAT_R5G6B5:			return DXGI_FORMAT_B5G6R5_UNORM;
	case WW3D_FORMAT_A1R5G5B5:			return DXGI_FORMAT_B5G5R5A1_UNORM;
	case WW3D_FORMAT_A4R4G4B4:			return DXGI_FORMAT_B4G4R4A4_UNORM;
	case WW3D_FORMAT_A8:				return DXGI_FORMAT_A8_UNORM;
	case WW3D_FORMAT_L8:				return DXGI_FORMAT_R8_UNORM;
	case WW3D_FORMAT_A8L8:				return DXGI_FORMAT_R8G8_UNORM;
	case WW3D_FORMAT_U8V8:				return DXGI_FORMAT_R8G8_SNORM;
	case WW3D_FORMAT_DXT1:				return DXGI_FORMAT_BC1_UNORM;
	case WW3D_FORMAT_DXT2:				return DXGI_FORMAT_BC2_UNORM;
	case WW3D_FORMAT_DXT3:				return DXGI_FORMAT_BC2_UNORM;
	case WW3D_FORMAT_DXT4:				return DXGI_FORMAT_BC3_UNORM;
	case WW3D_FORMAT_DXT5:				return DXGI_FORMAT_BC3_UNORM;
	case WW3D_FORMAT_A16B16G16R16F:		return DXGI_FORMAT_R16G16B16A16_FLOAT;
	case WW3D_FORMAT_R32F:				return DXGI_FORMAT_R32_FLOAT;
	// R8G8B8 is 24 bits per pixel and DXGI has no 24-bit format at all. The engine's own
	// converter already promotes it wherever it matters; here it simply has no answer.
	default:							return DXGI_FORMAT_UNKNOWN;
	}
}

static WW3DFormat DXGI_To_WW3D(DXGI_FORMAT format)
{
	switch (format) {
	case DXGI_FORMAT_B8G8R8A8_UNORM:		return WW3D_FORMAT_A8R8G8B8;
	case DXGI_FORMAT_B8G8R8X8_UNORM:		return WW3D_FORMAT_X8R8G8B8;
	case DXGI_FORMAT_B5G6R5_UNORM:			return WW3D_FORMAT_R5G6B5;
	case DXGI_FORMAT_B5G5R5A1_UNORM:		return WW3D_FORMAT_A1R5G5B5;
	case DXGI_FORMAT_B4G4R4A4_UNORM:		return WW3D_FORMAT_A4R4G4B4;
	case DXGI_FORMAT_A8_UNORM:				return WW3D_FORMAT_A8;
	case DXGI_FORMAT_R8_UNORM:				return WW3D_FORMAT_L8;
	case DXGI_FORMAT_R8G8_UNORM:			return WW3D_FORMAT_A8L8;
	case DXGI_FORMAT_R8G8_SNORM:			return WW3D_FORMAT_U8V8;
	case DXGI_FORMAT_BC1_UNORM:				return WW3D_FORMAT_DXT1;
	case DXGI_FORMAT_BC2_UNORM:				return WW3D_FORMAT_DXT3;
	case DXGI_FORMAT_BC3_UNORM:				return WW3D_FORMAT_DXT5;
	case DXGI_FORMAT_R16G16B16A16_FLOAT:	return WW3D_FORMAT_A16B16G16R16F;
	case DXGI_FORMAT_R32_FLOAT:				return WW3D_FORMAT_R32F;
	default:								return WW3D_FORMAT_UNKNOWN;
	}
}

/*
** A depth format has three DXGI spellings, not one, and which of the three is wanted
** depends on what the resource is for. A plain depth buffer is created in the typed
** format. One the shadow map samples afterwards has to be created *typeless* and then
** viewed twice -- as a depth-stencil view in the typed format to write it, and as a
** shader-resource view in the colour format to read it. D3D11 refuses a resource that is
** both a typed depth format and a shader resource, which is the failure that looks like
** "the shadow map is empty".
*/
struct DepthFormats
{
	DXGI_FORMAT	typeless;
	DXGI_FORMAT	depth;
	DXGI_FORMAT	shader;
};

static DepthFormats WW3DZ_To_DXGI(WW3DZFormat format)
{
	DepthFormats f;
	switch (format) {
	case WW3D_ZFORMAT_D16:
	case WW3D_ZFORMAT_D16_LOCKABLE:
	case WW3D_ZFORMAT_D15S1:
		f.typeless = DXGI_FORMAT_R16_TYPELESS;
		f.depth    = DXGI_FORMAT_D16_UNORM;
		f.shader   = DXGI_FORMAT_R16_UNORM;
		return f;
	case WW3D_ZFORMAT_D32:
		f.typeless = DXGI_FORMAT_R32_TYPELESS;
		f.depth    = DXGI_FORMAT_D32_FLOAT;
		f.shader   = DXGI_FORMAT_R32_FLOAT;
		return f;
	default:
		// D24S8, D24X8 and D24X4S4 all land here. There is one 24-bit depth format in
		// DXGI and it carries eight stencil bits whether or not they were asked for,
		// which costs nothing and is what every driver would have given D3D9 anyway.
		f.typeless = DXGI_FORMAT_R24G8_TYPELESS;
		f.depth    = DXGI_FORMAT_D24_UNORM_S8_UINT;
		f.shader   = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		return f;
	}
}

static bool Depth_Format_Has_Stencil(WW3DZFormat format)
{
	return format == WW3D_ZFORMAT_D15S1 || format == WW3D_ZFORMAT_D24S8 ||
		format == WW3D_ZFORMAT_D24X4S4;
}

static bool DXGI_Format_Has_Stencil(DXGI_FORMAT format)
{
	return format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
		format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
}

/// Bytes per pixel, for the uncompressed formats the CPU-side paths touch. Zero means
/// "not a format this code can walk a row of", which every caller checks.
static unsigned Bytes_Per_Pixel(DXGI_FORMAT format)
{
	switch (format) {
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R32_FLOAT:			return 4;
	case DXGI_FORMAT_R16G16B16A16_FLOAT:	return 8;
	case DXGI_FORMAT_B5G6R5_UNORM:
	case DXGI_FORMAT_B5G5R5A1_UNORM:
	case DXGI_FORMAT_B4G4R4A4_UNORM:
	case DXGI_FORMAT_R8G8_UNORM:
	case DXGI_FORMAT_R8G8_SNORM:			return 2;
	case DXGI_FORMAT_A8_UNORM:
	case DXGI_FORMAT_R8_UNORM:				return 1;
	default:								return 0;
	}
}

// ---------------------------------------------------------------------------
// Handles
//
// Everything the seam hands out as an opaque GfxTexture / GfxSurface / GfxVertexBuffer /
// GfxIndexBuffer is one of these, cast. They carry their own reference count rather than
// borrowing the underlying COM object's, because a GfxSurface is frequently a *view* of
// one mip level of a texture the engine also holds separately, and the two lifetimes are
// not the same one.
// ---------------------------------------------------------------------------

struct D3D11Texture
{
	long					refs;
	ID3D11Resource *		resource;
	ID3D11ShaderResourceView * srv;
	ID3D11UnorderedAccessView * uav;
	unsigned				width, height, depth, levels, faces;
	DXGI_FORMAT				dxgi;			// what the resource actually is
	WW3DFormat				ww;				// UNKNOWN for a depth texture
	WW3DZFormat				wwz;			// UNKNOWN for a colour texture
	unsigned				usage;
	unsigned				lod;			// the texture-reduction level, as a resource minimum
	bool					cube, volume, is_depth, mappable;
	// Whether the resource was actually created with the flags GenerateMips needs. It is
	// asked for wherever it might be wanted and dropped if the driver refuses, so the
	// answer is a property of this texture and not of its format.
	bool					can_generate_mips;
	// Map on a resource D3D11 will not map lands here: one scratch allocation per
	// subresource, handed to the caller, copied back on Unmap. It is this resource's
	// CPU-side copy -- zeroed when first allocated, holding every CPU write since -- which
	// is what makes GFX_MAP_WRITE's "whatever is not written stays" true without a
	// read-back per lock. box_* is the sub-rectangle the caller asked for, so Unmap sends
	// up what was written rather than the whole level.
	struct Scratch
	{
		unsigned char * data;
		unsigned row_pitch, slice_pitch;
		bool has_box;
		unsigned box_left, box_top, box_right, box_bottom;
	};
	Scratch *				scratch;		// [levels * faces]
};

struct D3D11Surface
{
	long					refs;
	ID3D11Texture2D *		texture;		// owns a reference
	unsigned				subresource;
	unsigned				width, height;
	DXGI_FORMAT				dxgi;			// the resource's own format
	DXGI_FORMAT				view_format;	// typed, for a typeless depth resource
	WW3DFormat				ww;
	WW3DMultiSampleType		multisample;
	bool					is_depth;
	ID3D11RenderTargetView * rtv;			// made on first use
	ID3D11DepthStencilView * dsv;			// made on first use
	ID3D11ShaderResourceView * srv;			// made on first use
	unsigned char *			scratch;		// Map on a surface D3D11 will not map
	unsigned				scratch_pitch;
	ID3D11Texture2D *		readback;		// staging copy, for a mapped read
	D3D11Texture *			parent_texture;
};

struct D3D11Buffer
{
	long					refs;
	ID3D11Buffer *			buffer;
	unsigned				size;
	unsigned				fvf;			// vertex buffers only
	unsigned				usage;
	bool					is_dynamic;
	// Dynamic buffers are created with D3D11_USAGE_DYNAMIC and mapped directly with DISCARD
	// or NO_OVERWRITE, requiring no CPU shadow. Default/static buffers keep a shadow.
	unsigned char *			shadow;
	unsigned				map_offset, map_size;
	bool					mapped;

	// The shader-buffer half. Null on a vertex or index buffer, which is every buffer this
	// backend made before the clustered-lighting work: those are bound through the input
	// assembler and have no view at all.
	ID3D11ShaderResourceView *	srv;
	ID3D11UnorderedAccessView *	uav;		// only when GFX_BUFFER_UAV was asked for
	unsigned				stride, count;
	bool					uint_view;		// a typed R32_UINT view, not a structured one
	// The staging copy a GFX_MAP_READ lands in, made on first use. A buffer a compute
	// shader writes cannot be mapped for reading -- it is D3D11_USAGE_DEFAULT -- so the
	// read is a CopyResource into this and a map of that. Same arrangement as
	// D3D11Surface::readback, and it stalls the pipeline just as hard.
	ID3D11Buffer *			readback;
	bool					mapped_readback;
};

namespace {
#ifdef RTS_DEBUG
// Which buffers are still alive when the device goes away.
//
// Phase 8 attributed seven leaked shader handles by matching sizeof(D3D11VertexShader)
// and sizeof(D3D11PixelShader) against the block sizes in the engine's leak report, and
// said the nine that remained were "D3D11Buffer objects and their shadow arrays". That
// was an inference from two numbers. This is the question asked directly: every buffer
// this backend makes is registered here and struck off when it is freed, so what is left
// at shutdown can be listed with its size, its usage and its vertex format instead of
// being recognised by the size of its allocation.
// `kind` was a bool meaning "index buffer" until a third kind of buffer existed. A
// structured buffer reported as a vertex buffer is exactly the sort of mis-attribution
// this register was written to stop.
struct LiveBuffer { const D3D11Buffer * b; unsigned size, usage; const char * kind; };
static LiveBuffer s_liveBuffers[64];
static int s_liveBufferCount = 0;
static unsigned s_buffersMade = 0, s_buffersFreed = 0, s_liveOverflow = 0;
// The same question for the other four things this backend allocates. Counters only:
// what matters is whether made and freed agree, and a disagreement names the kind.
static unsigned s_vsMade = 0, s_vsFreed = 0;
static unsigned s_psMade = 0, s_psFreed = 0;
static unsigned s_texMade = 0, s_texFreed = 0;
static unsigned s_surfMade = 0, s_surfFreed = 0;

// And which ones, for the two kinds whose counts do not balance. Registered by pointer so
// the survivors can be listed with their dimensions and format instead of being guessed at
// from a block size -- which is how the last attribution of these came out wrong.
struct LiveRes { const void * p; const char * site; };
static LiveRes s_liveTex[2048];
static int s_liveTexCount = 0;
static LiveRes s_liveSurf[4096];
static int s_liveSurfCount = 0;
static unsigned s_resOverflow = 0;

static void Note_Res_Made(LiveRes * table, int & count, int cap, const void * p,
	const char * site)
{
	if (count >= cap) { ++s_resOverflow; return; }
	LiveRes & e = table[count++];
	e.p = p; e.site = site;
}

static void Note_Res_Freed(LiveRes * table, int & count, const void * p)
{
	for (int i = 0; i < count; ++i) {
		if (table[i].p == p) { table[i] = table[--count]; return; }
	}
}

void Note_Buffer_Made(const D3D11Buffer * b, unsigned size, unsigned usage, const char * kind)
{
	++s_buffersMade;
	if (s_liveBufferCount >= 64) { ++s_liveOverflow; return; }
	LiveBuffer & e = s_liveBuffers[s_liveBufferCount++];
	e.b = b; e.size = size; e.usage = usage; e.kind = kind;
}

void Note_Buffer_Freed(const D3D11Buffer * b)
{
	++s_buffersFreed;
	for (int i = 0; i < s_liveBufferCount; ++i) {
		if (s_liveBuffers[i].b == b) {
			s_liveBuffers[i] = s_liveBuffers[--s_liveBufferCount];
			return;
		}
	}
}
#endif

}


/*
** One shader stage's signature: what it reads or writes, and in which register.
**
** The register is the field that matters, and it is the one ps_3_0 did not have. Model 3
** matches a vertex shader's outputs to a pixel shader's inputs by semantic alone; model 4
** matches by semantic *and* register, and fxc assigns registers in struct declaration
** order. So a pixel shader declaring a subset of its vertex shader's outputs, or the same
** set in a different order, compiles clean at both models and cannot be linked to that
** vertex shader at model 4 -- D3D11 refuses the pair and draws nothing.
**
** shader_signature_check.py reads the same two chunks out of the shipped
** blobs and says which pairs those are.
*/
struct SignatureElement { char semantic[32]; unsigned index; unsigned reg; };

struct Signature
{
	SignatureElement	elements[32];
	unsigned			count;
};

struct D3D11VertexShader
{
	ID3D11VertexShader *	shader;
	unsigned char *			bytecode;		// kept: CreateInputLayout needs it
	unsigned				bytecode_size;
	// What it reads. An input layout has to satisfy every element of this or
	// CreateInputLayout refuses -- which is the difference that matters, because D3D9's
	// FVF path simply defaulted whatever the vertex format did not carry and never failed.
	Signature				inputs;
	// What it hands on, for the linkage check against whatever pixel shader is bound.
	Signature				outputs;
};

struct D3D11PixelShader
{
	ID3D11PixelShader *		shader;
	Signature				inputs;
	// Which t registers it declares a texture in, for the render-target hazard census.
	unsigned				texture_mask;
};

// No signature and no bytecode kept. A compute shader is never linked to another stage --
// there is no input layout to build and no interpolator alignment to check -- so the two
// reasons D3D11VertexShader holds its bytes do not arise.
struct D3D11ComputeShader
{
	ID3D11ComputeShader *	shader;
};

struct D3D11Query { int unused; };

// ---------------------------------------------------------------------------
// The state caches.
//
// D3D9 writes state one word at a time; D3D11 has four immutable objects. So the words
// accumulate here, an object is materialised at the draw that needs it, and it is cached
// on the words that fed it. Each group carries a dirty flag so that the common case --
// a draw that changed nothing in a group -- costs a branch rather than a hash.
// ---------------------------------------------------------------------------

template <class KEY, class OBJ, int N> struct StateCache
{
	KEY		keys[N];
	OBJ *	objects[N];
	int		count;
	int		last_index;

	StateCache() : count(0), last_index(-1) { }

	OBJ * Find(const KEY & key)
	{
		if (last_index >= 0 && last_index < count) {
			if (memcmp(&keys[last_index], &key, sizeof(KEY)) == 0) return objects[last_index];
		}
		for (int i = 0; i < count; ++i) {
			if (i != last_index && memcmp(&keys[i], &key, sizeof(KEY)) == 0) {
				last_index = i;
				return objects[i];
			}
		}
		return nullptr;
	}

	void Add(const KEY & key, OBJ * object)
	{
		if (count >= N) return;			// full; the object leaks into the device's own cache
		keys[count] = key;
		objects[count] = object;
		last_index = count;
		++count;
	}

	void Release_All()
	{
		for (int i = 0; i < count; ++i) {
			if (objects[i] != nullptr) objects[i]->Release();
		}
		count = 0;
		last_index = -1;
	}
};

struct BlendKey
{
	unsigned enable, src, dest, op, write_mask, separate_alpha, src_alpha, dest_alpha, op_alpha;
};

struct DepthKey
{
	unsigned z_enable, z_write, z_func;
	unsigned stencil_enable, stencil_func, stencil_mask, stencil_write_mask;
	unsigned stencil_pass, stencil_fail, stencil_zfail;
};

struct RasterKey
{
	unsigned cull, fill, depth_bias, slope_bias, scissor, multisample;
};

struct SamplerKey
{
	unsigned address_u, address_v, address_w, mag, min, mip, lod_bias, max_lod, max_aniso, border, compare;
};

struct LayoutKey
{
	unsigned		fvf;
	const void *	vertex_shader;
};

// ---------------------------------------------------------------------------
// The device's private state, all of it.
// ---------------------------------------------------------------------------

struct GfxD3D11Impl
{
	HMODULE						d3d11_library;
	HMODULE						dxgi_library;
	ID3D11Device *				device;
	ID3D11DeviceContext *		context;
	IDXGISwapChain *			swap_chain;
	IDXGIAdapter *				adapter;
	// The debug layer's own message queue, when the machine has one. Draining it into
	// the game log is what turns "the frame is wrong" into a line naming the bind that
	// was wrong, which is the only reason the debug layer is worth asking for.
	ID3D11InfoQueue *			info_queue;
	D3D_FEATURE_LEVEL			feature_level;
	GfxSwapChainDesc			desc;

	// The back buffer and the depth buffer, as the engine sees them. These wrapper
	// objects outlive a swap-chain resize: Reset_Swap_Chain swaps the texture inside them
	// rather than replacing them, so a reference the engine is still holding stays valid.
	D3D11Surface *				back_buffer;
	D3D11Surface *				depth_buffer;

	// What is bound now.
	D3D11Surface *				current_rt;
	D3D11Surface *				current_ds;

	// Tracked D3D9 state.
	unsigned					rs[RS_COUNT];
	unsigned					tss[GFX_MAX_STAGES][32];
	bool						blend_dirty, depth_dirty, raster_dirty;
	bool						sampler_dirty[GFX_MAX_STAGES];
	// Whether a sampler object was ever built and bound for this stage. False means the
	// stage is sampling through D3D11's own default sampler, which is not D3D9's.
	bool						sampler_applied[GFX_MAX_STAGES];

	StateCache<BlendKey, ID3D11BlendState, 128>			blend_cache;
	StateCache<DepthKey, ID3D11DepthStencilState, 128>	depth_cache;
	StateCache<RasterKey, ID3D11RasterizerState, 128>	raster_cache;
	StateCache<SamplerKey, ID3D11SamplerState, 128>		sampler_cache;
	StateCache<LayoutKey, ID3D11InputLayout, 128>		layout_cache;

	// Bindings.
	D3D11VertexShader *			vertex_shader;
	D3D11PixelShader *			pixel_shader;
	unsigned					fvf;				// when no vertex shader is bound
	D3D11Buffer *				stream0;
	unsigned					stream0_stride;
	D3D11Buffer *				index_buffer;
	int							base_vertex_index;
	D3D11Texture *				textures[GFX_MAX_STAGES];
	// The stages whose shader-resource slot this backend has nulled behind the wrapper's
	// back, one bit each.
	//
	// D3D11 will not have one resource bound as a render target and as a shader resource
	// at the same time: OMSetRenderTargets nulls the shader-resource slot and says so only
	// to the debug layer. That alone would be harmless -- except that the redundancy check
	// lives one level up, in DX8Wrapper::Set_Texture ("if (Textures[stage]==texture)
	// return"), so the wrapper still believes the texture is bound and never sends it
	// again. The slot stays NULL and the shader samples zero. The backend is the only
	// place that knows, so it remembers here and re-binds at the next draw, once the
	// resource has stopped being a target.
	unsigned					srv_unbound_mask;
	// The stages this backend has put back on its own initiative, cleared when the wrapper
	// next writes that stage. Purely a measurement: it is the window in which, without the
	// fix above, the slot would still have been NULL and the shader would have sampled
	// zero. Nothing reads it but the census.
	unsigned					srv_rescued_mask;

	// The compute stage, and the shader buffers bound to it and to the pixel stage.
	//
	// These are kept for exactly one reason and it is not redundancy filtering: it is the
	// SRV/UAV hazard. To null the read binding of a buffer that is about to be written,
	// this backend has to be able to answer "which slots is that buffer in", and D3D11
	// will not be asked -- there is no cheap Get for a shader-resource slot. So the
	// bindings are mirrored here. See Unbind_Buffer_From_Read_Slots and its opposite
	// number, and the section comment above them for what happens without them.
	D3D11ComputeShader *		compute_shader = nullptr;
	D3D11Buffer *				cs_buffers[GFX_COMPUTE_BUFFER_SLOTS] = {};
	D3D11Buffer *				cs_rw_buffers[GFX_COMPUTE_RW_SLOTS] = {};
	D3D11Buffer *				ps_buffers[GFX_PIXEL_BUFFER_SLOTS] = {};
	D3D11Texture *				cs_textures[GFX_COMPUTE_BUFFER_SLOTS] = {};
	D3D11Texture *				cs_rw_textures[GFX_COMPUTE_RW_SLOTS] = {};

	// Constants. One buffer per stage at the register offsets the shaders already declare;
	// Phase 3.8 established that register(cN) survives to Shader Model 4 unchanged, so
	// there is nothing to reflect and nothing to renumber.
	float						vs_constants[GFX_VS_CONSTANTS * 4];
	float						ps_constants[GFX_PS_CONSTANTS * 4];
	ID3D11Buffer *				vs_constant_buffer;
	ID3D11Buffer *				ps_constant_buffer;
	bool						vs_constants_dirty, ps_constants_dirty;

	// b1, C2 of the clustered-lighting plan: a second constant buffer, written once a
	// frame instead of once a draw, because the two above are full (see
	// GfxDeviceClass::Set_Frame_Constants). Bound on VS, PS and CS alike -- CSSetShader
	// has no per-draw setup step to bind it from, so if it is not bound here it is not
	// bound anywhere a compute shader can see.
	float						frame_constants[GFX_FRAME_CONSTANTS * 4];
	ID3D11Buffer *				frame_constant_buffer;
	bool						frame_constants_dirty;
	// Debug only: where Debug_Read_Vertex_Constants copies the constant buffer to so it
	// can be mapped for reading. Made on first use, never in a release build.
	ID3D11Buffer *				debug_constant_staging;

	// Redundant state filtering cache.
	ID3D11InputLayout *			current_layout;
	D3D11_PRIMITIVE_TOPOLOGY	current_topology;
	ID3D11Buffer *				current_vb0;
	UINT						current_stride0;
	ID3D11Buffer *				current_ib;
	ID3D11BlendState *			current_blend_state;
	unsigned					current_blend_factor;
	ID3D11DepthStencilState *	current_depth_state;
	unsigned					current_stencil_ref;
	ID3D11RasterizerState *		current_raster_state;
	ID3D11SamplerState *		current_ps_samplers[GFX_MAX_STAGES];

	// The stream that supplies whatever a vertex shader declares and the vertex format
	// does not carry. D3D9's FVF path defaulted those registers to (0,0,0,1) and never
	// failed; D3D11's CreateInputLayout refuses the pair outright. Binding a stride-zero
	// buffer holding exactly that value at slot 1 is what makes the same 25 pairs legal
	// here, and it is why the input layout is built from the shader's signature rather
	// than from the vertex format alone.
	ID3D11Buffer *				zero_stream;

	// Draw_Up's ring. Phase 4.0 left one call site with the reasoning that a D3D11
	// backend maps a dynamic ring, copies and draws; this is that.
	ID3D11Buffer *				up_buffer;
	unsigned					up_capacity;
	unsigned					up_offset;

	// Blit / StretchRect GPU pipeline objects
	ID3D11VertexShader *		blit_vs;
	ID3D11PixelShader *			blit_ps;
	ID3D11Buffer *				blit_cb;
	ID3D11SamplerState *		blit_sampler_point;
	ID3D11SamplerState *		blit_sampler_linear;
	ID3D11BlendState *			blit_blend_state;
	ID3D11DepthStencilState *	blit_depth_state;
	ID3D11RasterizerState *		blit_raster_state;

	// Intermediate texture cache for blit sources that cannot be sampled directly
	ID3D11Texture2D *			blit_cache_tex;
	ID3D11ShaderResourceView *	blit_cache_srv;
	unsigned					blit_cache_width;
	unsigned					blit_cache_height;
	DXGI_FORMAT					blit_cache_format;

	GfxViewport					viewport;

	// The transforms the wrapper sent, kept rather than used.
	//
	// There is no fixed-function pipeline for one to drive, so nothing here rasterises
	// anything -- but Get_Transform is not only the audit's read-back, whatever
	// gfxdevice.h says. DX8Wrapper::Bind_Ui_Shader_World reads the view and projection
	// back on the *render path* to build a world-view-projection constant, and a backend
	// that answers false there sends the shadow decals down their fixed-function fallback:
	// 1676 draws a window arriving with no vertex shader, on a backend that cannot make
	// one. Sixteen floats each, for the slots D3D9 numbers 0..31 plus D3DTS_WORLD, which
	// it numbers 256.
	float						transforms[GFX_TRANSFORM_SLOTS][16];
	bool						transform_set[GFX_TRANSFORM_SLOTS];

	void Invalidate_Bound_States()
	{
		current_layout = (ID3D11InputLayout *)(intptr_t)-1;
		current_topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		current_vb0 = (ID3D11Buffer *)(intptr_t)-1;
		current_stride0 = 0xFFFFFFFF;
		current_ib = (ID3D11Buffer *)(intptr_t)-1;
		current_blend_state = (ID3D11BlendState *)(intptr_t)-1;
		current_blend_factor = 0xFFFFFFFF;
		current_depth_state = (ID3D11DepthStencilState *)(intptr_t)-1;
		current_stencil_ref = 0xFFFFFFFF;
		current_raster_state = (ID3D11RasterizerState *)(intptr_t)-1;
		for (unsigned i = 0; i < GFX_MAX_STAGES; ++i) {
			current_ps_samplers[i] = (ID3D11SamplerState *)(intptr_t)-1;
		}
	}

	GfxD3D11Impl()
	{
		memset(this, 0, sizeof(*this));
		Invalidate_Bound_States();
	}
};

// ---------------------------------------------------------------------------
// State translation
// ---------------------------------------------------------------------------

// D3D9's blend factors 1..11 and 14..15 are numerically D3D11's, which is not a
// coincidence -- D3D11 kept the numbering. Only D3DBLEND_BOTHSRCALPHA (12) and
// BOTHINVSRCALPHA (13) have no counterpart; they set the colour and alpha factors as a
// pair, which D3D11 expresses with a separate alpha blend instead.
static D3D11_BLEND To_Blend(unsigned d3d9)
{
	switch (d3d9) {
	case 12: return D3D11_BLEND_SRC_ALPHA;
	case 13: return D3D11_BLEND_INV_SRC_ALPHA;
	default: break;
	}
	if (d3d9 >= 1 && d3d9 <= 11) return (D3D11_BLEND)d3d9;
	if (d3d9 == 14 || d3d9 == 15) return (D3D11_BLEND)d3d9;
	return D3D11_BLEND_ONE;
}

// The alpha half will not take a colour factor. D3D9 applied one factor to both halves,
// so the colour spellings have to be folded to their alpha equivalents on the way -- and
// SRC_COLOR folded to SRC_ALPHA is what D3D9's hardware did with them anyway.
static D3D11_BLEND To_Alpha_Blend(unsigned d3d9)
{
	switch (d3d9) {
	case 3:  return D3D11_BLEND_SRC_ALPHA;			// SRCCOLOR
	case 4:  return D3D11_BLEND_INV_SRC_ALPHA;		// INVSRCCOLOR
	case 9:  return D3D11_BLEND_DEST_ALPHA;			// DESTCOLOR
	case 10: return D3D11_BLEND_INV_DEST_ALPHA;		// INVDESTCOLOR
	default: return To_Blend(d3d9);
	}
}

static D3D11_BLEND_OP To_Blend_Op(unsigned d3d9)
{
	if (d3d9 >= 1 && d3d9 <= 5) return (D3D11_BLEND_OP)d3d9;
	return D3D11_BLEND_OP_ADD;
}

// D3DCMPFUNC 1..8 is D3D11_COMPARISON_FUNC 1..8, same order.
static D3D11_COMPARISON_FUNC To_Comparison(unsigned d3d9)
{
	if (d3d9 >= 1 && d3d9 <= 8) return (D3D11_COMPARISON_FUNC)d3d9;
	return D3D11_COMPARISON_ALWAYS;
}

// D3DSTENCILOP 1..8 is D3D11_STENCIL_OP 1..8, same order.
static D3D11_STENCIL_OP To_Stencil_Op(unsigned d3d9)
{
	if (d3d9 >= 1 && d3d9 <= 8) return (D3D11_STENCIL_OP)d3d9;
	return D3D11_STENCIL_OP_KEEP;
}

// D3DCULL_NONE/CW/CCW is 1/2/3 and D3D11_CULL_NONE/FRONT/BACK is 1/2/3, and with
// FrontCounterClockwise left FALSE the two agree on which winding is which: D3D9's
// default D3DCULL_CCW and D3D11's default CULL_BACK cull the same triangles. Getting
// this backwards turns the world inside out, so it is stated rather than assumed.
static D3D11_CULL_MODE To_Cull(unsigned d3d9)
{
	if (d3d9 >= 1 && d3d9 <= 3) return (D3D11_CULL_MODE)d3d9;
	return D3D11_CULL_NONE;
}

static D3D11_TEXTURE_ADDRESS_MODE To_Address(unsigned d3d9)
{
	if (d3d9 >= 1 && d3d9 <= 5) return (D3D11_TEXTURE_ADDRESS_MODE)d3d9;
	return D3D11_TEXTURE_ADDRESS_WRAP;
}

// D3DTEXF_NONE 0, POINT 1, LINEAR 2, ANISOTROPIC 3.
static D3D11_FILTER To_Filter(unsigned mag, unsigned min, unsigned mip)
{
	if (mag == 3 || min == 3) return D3D11_FILTER_ANISOTROPIC;
	unsigned bits = 0;
	if (min == 2) bits |= 0x10;
	if (mag == 2) bits |= 0x04;
	if (mip == 2) bits |= 0x01;
	return (D3D11_FILTER)bits;
}

/// D3D9's transform slot number onto this backend's array, or -1 for one it does not
/// keep. 0..31 are view, projection and the eight texture matrices; D3DTS_WORLD is 256
/// and is followed by three more world matrices for vertex blending.
static int Transform_Slot(unsigned which)
{
	if (which < 32) return (int)which;
	if (which >= 256 && which < 260) return 32 + (int)(which - 256);
	return -1;
}

static D3D11_PRIMITIVE_TOPOLOGY To_Topology(unsigned d3d9)
{
	switch (d3d9) {
	case PT_POINTLIST:		return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
	case PT_LINELIST:		return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
	case PT_LINESTRIP:		return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
	case PT_TRIANGLELIST:	return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	case PT_TRIANGLESTRIP:	return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
	// D3DPT_TRIANGLEFAN has no D3D11 topology. Nothing in this engine draws one -- the
	// vertex-layout census reports no fan on either replay -- so this is a dropped draw
	// with a count rather than an index-rewriting path nothing would exercise.
	default:				return D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	}
}

/// How many vertices or indices a primitive count means, which is the number D3D11's
/// draw calls take where D3D9's took the count of primitives.
static unsigned Vertices_For(unsigned primitive_type, unsigned primitive_count)
{
	switch (primitive_type) {
	case PT_POINTLIST:		return primitive_count;
	case PT_LINELIST:		return primitive_count * 2;
	case PT_LINESTRIP:		return primitive_count + 1;
	case PT_TRIANGLELIST:	return primitive_count * 3;
	case PT_TRIANGLESTRIP:	return primitive_count + 2;
	case PT_TRIANGLEFAN:	return primitive_count + 2;
	default:				return 0;
	}
}

// ---------------------------------------------------------------------------
// The vertex shader's input signature, out of the bytecode.
//
// D3D11 builds an input layout from a vertex format *and* a compiled vertex shader's
// input signature together, and refuses the pair if the layout does not supply every
// element the signature declares. So a backend has to know what the shader declares, and
// the answer is in the bytecode: a DXBC container carries an ISGN chunk listing them.
//
// Parsed here rather than read out of D3DReflect, which would mean shipping and loading
// d3dcompiler_47.dll at runtime for a fact that is fifty lines of container walking. The
// tree already parses this container -- dxbc_cbuffer.py reads RDEF the same
// way -- so the format is not new here.
// ---------------------------------------------------------------------------

static bool Parse_Signature(const unsigned char * bytecode, unsigned size,
	const char * fourcc, Signature & out)
{
	out.count = 0;
	if (bytecode == nullptr || size < 32) return false;
	if (memcmp(bytecode, "DXBC", 4) != 0) return false;

	const unsigned chunk_count = *(const unsigned *)(bytecode + 28);
	if (chunk_count == 0 || chunk_count > 32) return false;
	if (size < 32 + chunk_count * 4) return false;
	const unsigned * offsets = (const unsigned *)(bytecode + 32);

	for (unsigned c = 0; c < chunk_count; ++c) {
		const unsigned offset = offsets[c];
		if (offset + 8 > size) continue;
		const unsigned char * chunk = bytecode + offset;
		// ISGN is the input signature and OSGN the output one. The ...1 variants (ISG1/OSG1)
		// are the same thing with a 28-byte element (adding min_precision in SM5/D3D11.1).
		bool match = (memcmp(chunk, fourcc, 4) == 0);
		bool is_variant1 = false;
		if (!match && fourcc[0] == 'I' && memcmp(chunk, "ISG1", 4) == 0) {
			match = true;
			is_variant1 = true;
		} else if (!match && fourcc[0] == 'O' && memcmp(chunk, "OSG1", 4) == 0) {
			match = true;
			is_variant1 = true;
		}
		if (!match) continue;

		const unsigned chunk_size = *(const unsigned *)(chunk + 4);
		const unsigned char * data = chunk + 8;
		if (offset + 8 + chunk_size > size || chunk_size < 8) return false;

		const unsigned element_count = *(const unsigned *)(data + 0);
		if (element_count > 32) return false;
		const unsigned elem_stride = is_variant1 ? 28 : 24;
		for (unsigned e = 0; e < element_count; ++e) {
			const unsigned char * element = data + 8 + e * elem_stride;
			if ((unsigned)(element + elem_stride - data) > chunk_size) return false;
			// Six words per element: the name's offset from the start of the chunk's data,
			// the semantic index, the system-value kind, the component type, the register,
			// and the two masks.
			const unsigned name_offset = *(const unsigned *)(element + 0);
			const unsigned semantic_index = *(const unsigned *)(element + 4);
			const unsigned reg = *(const unsigned *)(element + 16);
			if (name_offset >= chunk_size) return false;
			const char * name = (const char *)(data + name_offset);

			SignatureElement & in = out.elements[out.count];
			strncpy(in.semantic, name, sizeof(in.semantic) - 1);
			in.semantic[sizeof(in.semantic) - 1] = '\0';
			in.index = semantic_index;
			in.reg = reg;
			++out.count;
		}
		return true;
	}
	// A shader with no such chunk reads or writes nothing through it, which is legal and
	// means there is nothing to satisfy.
	return true;
}

// Which t registers a shader actually declares a texture in, one bit each.
//
// The render-target hazard census counts *bindings*: a draw is flagged when a slot the
// backend had to null still points at the current render target. That is the right
// question for the binding, and the wrong one for the draw, because the two APIs only
// diverge if the shader on that draw reads the slot. D3D9 hands back whatever is in the
// surface; D3D11 hands back zero from a NULL slot -- but only to a shader that fetches.
// A draw whose pixel shader declares no texture at that register cannot tell them apart
// and is exposure, not a defect. This is what narrows one to the other.
//
// RDEF's resource-binding table, which is where a t register is written down: eight
// words per entry, the second being the input type (2 is a texture) and the sixth the
// bind point.
static unsigned Parse_Texture_Mask(const unsigned char * bytecode, unsigned size)
{
	if (bytecode == nullptr || size < 32) return 0;
	if (memcmp(bytecode, "DXBC", 4) != 0) return 0;

	const unsigned chunk_count = *(const unsigned *)(bytecode + 28);
	if (chunk_count == 0 || chunk_count > 32) return 0;
	if (size < 32 + chunk_count * 4) return 0;
	const unsigned * offsets = (const unsigned *)(bytecode + 32);

	for (unsigned c = 0; c < chunk_count; ++c) {
		const unsigned offset = offsets[c];
		if (offset + 8 > size) continue;
		const unsigned char * chunk = bytecode + offset;
		if (memcmp(chunk, "RDEF", 4) != 0) continue;

		const unsigned chunk_size = *(const unsigned *)(chunk + 4);
		const unsigned char * data = chunk + 8;
		if (offset + 8 + chunk_size > size || chunk_size < 28) return 0;

		const unsigned bind_count = *(const unsigned *)(data + 8);
		const unsigned bind_offset = *(const unsigned *)(data + 12);
		if (bind_count > 64) return 0;

		unsigned mask = 0;
		for (unsigned e = 0; e < bind_count; ++e) {
			const unsigned entry = bind_offset + e * 32;
			if (entry + 32 > chunk_size) return mask;
			const unsigned type = *(const unsigned *)(data + entry + 4);
			const unsigned bind_point = *(const unsigned *)(data + entry + 20);
			// 2 is D3D_SIT_TEXTURE. Samplers (3) are a separate table in D3D11 and are
			// not what a NULL shader-resource slot returns zero through.
			if (type == 2 && bind_point < 32) mask |= (1u << bind_point);
		}
		return mask;
	}
	// No RDEF is no declared texture, which is the honest answer for a shader that
	// samples nothing.
	return 0;
}

/// A system value -- SV_Position and the rest -- is placed in a slot of its own and takes
/// no part in the register matching that goes wrong between these two stages.
static bool Is_System_Value(const char * semantic)
{
	return (semantic[0] == 'S' || semantic[0] == 's') &&
		(semantic[1] == 'V' || semantic[1] == 'v') && semantic[2] == '_';
}

// ---------------------------------------------------------------------------
// FVF to input elements
// ---------------------------------------------------------------------------

struct FvfElement
{
	const char *	semantic;
	unsigned		index;
	DXGI_FORMAT		format;
	unsigned		offset;
};

/// Decode a D3D9 flexible vertex format into the elements it lays out, in memory order.
/// Returns the number written, and the stride through the last argument.
static unsigned Decode_Fvf(unsigned fvf, FvfElement * out, unsigned max_out, unsigned & stride)
{
	unsigned n = 0;
	unsigned offset = 0;

	#define EMIT(sem, idx, fmt, bytes) \
		do { \
			if (n < max_out) { out[n].semantic = (sem); out[n].index = (idx); \
				out[n].format = (fmt); out[n].offset = offset; ++n; } \
			offset += (bytes); \
		} while (0)

	if (fvf & FVF_XYZRHW) {
		// A pre-transformed position. Phase 4.0 converted every draw that used one, so
		// nothing should reach here -- but the decode is written rather than asserted,
		// because a wrong stride is a silently scrambled mesh and a missing element is a
		// loud failure at CreateInputLayout.
		EMIT("POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 16);
	} else if (fvf & FVF_XYZ) {
		EMIT("POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 12);
	}
	if (fvf & FVF_NORMAL)	EMIT("NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 12);
	if (fvf & FVF_PSIZE)	EMIT("PSIZE", 0, DXGI_FORMAT_R32_FLOAT, 4);
	// D3DCOLOR is BGRA in memory, which is B8G8R8A8_UNORM and not R8G8B8A8_UNORM. The
	// wrong one of those swaps red and blue in every vertex colour in the game.
	if (fvf & FVF_DIFFUSE)	EMIT("COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 4);
	if (fvf & FVF_SPECULAR)	EMIT("COLOR", 1, DXGI_FORMAT_B8G8R8A8_UNORM, 4);

	const unsigned tex_count = (fvf & FVF_TEXCOUNT_MASK) >> FVF_TEXCOUNT_SHIFT;
	for (unsigned t = 0; t < tex_count && t < 8; ++t) {
		// Two bits per set, at bit 16 + 2*set, saying how many floats it carries: 0 means
		// two, 1 means three, 2 means four and 3 means one. That numbering is D3DFVF's and
		// is not in ascending order, which is exactly the sort of thing to write down.
		const unsigned size_bits = (fvf >> (16 + t * 2)) & 3;
		switch (size_bits) {
		case 0:  EMIT("TEXCOORD", t, DXGI_FORMAT_R32G32_FLOAT, 8); break;
		case 1:  EMIT("TEXCOORD", t, DXGI_FORMAT_R32G32B32_FLOAT, 12); break;
		case 2:  EMIT("TEXCOORD", t, DXGI_FORMAT_R32G32B32A32_FLOAT, 16); break;
		default: EMIT("TEXCOORD", t, DXGI_FORMAT_R32_FLOAT, 4); break;
		}
	}

	#undef EMIT
	stride = offset;
	return n;
}

// ---------------------------------------------------------------------------
// Half-float, for reading an HDR scene buffer back on the CPU.
//
// The frame dump is how this whole series produces evidence, and under HDR the surface it
// dumps is R16G16B16A16_FLOAT. There is no D3DX to convert it and no reason to link a
// library for sixteen lines.
// ---------------------------------------------------------------------------

static float Half_To_Float(unsigned short h)
{
	const unsigned sign = (h >> 15) & 1;
	const unsigned exponent = (h >> 10) & 0x1f;
	const unsigned mantissa = h & 0x3ff;

	unsigned bits;
	if (exponent == 0) {
		if (mantissa == 0) {
			bits = sign << 31;					// signed zero
		} else {
			// Subnormal: renormalise into a single-precision exponent.
			unsigned e = 1;
			unsigned m = mantissa;
			while ((m & 0x400) == 0) { m <<= 1; ++e; }
			m &= 0x3ff;
			bits = (sign << 31) | ((127 - 15 - e + 1) << 23) | (m << 13);
		}
	} else if (exponent == 0x1f) {
		bits = (sign << 31) | (0xff << 23) | (mantissa << 13);	// infinity or NaN
	} else {
		bits = (sign << 31) | ((exponent - 15 + 127) << 23) | (mantissa << 13);
	}

	float result;
	memcpy(&result, &bits, sizeof(result));
	return result;
}

static unsigned char Float_To_Byte(float v)
{
	if (!(v > 0.0f)) return 0;				// also catches NaN
	if (v >= 1.0f) return 255;
	return (unsigned char)(v * 255.0f + 0.5f);
}

// ---------------------------------------------------------------------------
// The adapter
// ---------------------------------------------------------------------------

typedef HRESULT (WINAPI * CreateDXGIFactory1Type)(REFIID, void **);
typedef HRESULT (WINAPI * D3D11CreateDeviceType)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE,
	UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *,
	ID3D11DeviceContext **);

struct GfxAdapterD3D11::Impl
{
	HMODULE					d3d11_library;
	HMODULE					dxgi_library;
	IDXGIFactory1 *			factory;
	D3D11CreateDeviceType	create_device;
	// A device kept only to answer capability questions.
	//
	// The engine asks what an adapter supports before it creates anything, which is the
	// shape D3D9 had: IDirect3D9 answered CheckDeviceMultiSampleType with no device in
	// existence. D3D11 has no adapter-level query at all -- multisampling is
	// ID3D11Device::CheckMultisampleQualityLevels and nothing else -- so answering
	// honestly means having a device. This one is created on the first question, cached,
	// and released with the adapter; it draws nothing.
	ID3D11Device *			caps_device;
	unsigned				caps_adapter;

	Impl() : d3d11_library(nullptr), dxgi_library(nullptr), factory(nullptr),
		create_device(nullptr), caps_device(nullptr), caps_adapter(0) { }
};

GfxAdapterD3D11::GfxAdapterD3D11()
	: m_impl(new Impl)
{
	m_impl->dxgi_library = LoadLibrary("dxgi.dll");
	m_impl->d3d11_library = LoadLibrary("d3d11.dll");
	if (m_impl->dxgi_library == nullptr || m_impl->d3d11_library == nullptr) {
		WWDEBUG_SAY(("D3D11: dxgi.dll or d3d11.dll is missing on this machine."));
		return;
	}

	CreateDXGIFactory1Type create_factory = (CreateDXGIFactory1Type)
		GetProcAddress(m_impl->dxgi_library, "CreateDXGIFactory1");
	m_impl->create_device = (D3D11CreateDeviceType)
		GetProcAddress(m_impl->d3d11_library, "D3D11CreateDevice");
	if (create_factory == nullptr || m_impl->create_device == nullptr) {
		WWDEBUG_SAY(("D3D11: dxgi.dll or d3d11.dll did not export what is needed."));
		return;
	}

	if (FAILED(create_factory(__uuidof(IDXGIFactory1), (void **)&m_impl->factory))) {
		WWDEBUG_SAY(("D3D11: CreateDXGIFactory1 failed."));
		m_impl->factory = nullptr;
	}
}

GfxAdapterD3D11::~GfxAdapterD3D11()
{
	if (m_impl->caps_device != nullptr) m_impl->caps_device->Release();
	if (m_impl->factory != nullptr) m_impl->factory->Release();
	if (m_impl->d3d11_library != nullptr) FreeLibrary(m_impl->d3d11_library);
	if (m_impl->dxgi_library != nullptr) FreeLibrary(m_impl->dxgi_library);
	delete m_impl;
}

bool GfxAdapterD3D11::Is_Valid() const
{
	return m_impl->factory != nullptr && m_impl->create_device != nullptr;
}

namespace
{
	/// The adapter at this ordinal, or null. The caller releases it.
	IDXGIAdapter1 * Get_Adapter(IDXGIFactory1 * factory, unsigned index)
	{
		if (factory == nullptr) return nullptr;
		IDXGIAdapter1 * adapter = nullptr;
		if (FAILED(factory->EnumAdapters1(index, &adapter))) return nullptr;
		return adapter;
	}

	/// The first output of an adapter, which is the display the engine means when it asks
	/// about display modes. The caller releases it.
	IDXGIOutput * Get_Output(IDXGIFactory1 * factory, unsigned index)
	{
		IDXGIAdapter1 * adapter = Get_Adapter(factory, index);
		if (adapter == nullptr) return nullptr;
		IDXGIOutput * output = nullptr;
		if (FAILED(adapter->EnumOutputs(0, &output))) output = nullptr;
		adapter->Release();
		return output;
	}
}

unsigned GfxAdapterD3D11::Get_Adapter_Count()
{
	if (!Is_Valid()) return 0;
	unsigned count = 0;
	for (;;) {
		IDXGIAdapter1 * adapter = Get_Adapter(m_impl->factory, count);
		if (adapter == nullptr) break;
		adapter->Release();
		++count;
		if (count > 16) break;
	}
	return count;
}

bool GfxAdapterD3D11::Get_Adapter_Info(unsigned adapter_index, GfxAdapterInfo & info)
{
	memset(&info, 0, sizeof(info));
	IDXGIAdapter1 * adapter = Get_Adapter(m_impl->factory, adapter_index);
	if (adapter == nullptr) return false;

	DXGI_ADAPTER_DESC1 desc;
	const bool ok = SUCCEEDED(adapter->GetDesc1(&desc));
	adapter->Release();
	if (!ok) return false;

	// DXGI describes an adapter in wide characters and names no driver at all; the
	// engine's blacklists are written against a four-part driver version that DXGI does
	// not report. Reporting zeros there is honest -- a blacklist entry that cannot match
	// is better than one that matches the wrong thing.
	WideCharToMultiByte(CP_ACP, 0, desc.Description, -1, info.Description,
		sizeof(info.Description) - 1, nullptr, nullptr);
	strncpy(info.Driver, "d3d11", sizeof(info.Driver) - 1);
	strncpy(info.DriverVersion, "0.0.0.0", sizeof(info.DriverVersion) - 1);
	sprintf(info.DeviceIdentifier, "D3D11-%04X-%04X-%08X",
		desc.VendorId, desc.DeviceId, desc.SubSysId);
	info.VendorId = desc.VendorId;
	info.DeviceId = desc.DeviceId;
	info.SubSystemId = desc.SubSysId;
	info.Revision = desc.Revision;
	return true;
}

bool GfxAdapterD3D11::Get_Current_Display_Mode(unsigned adapter_index, GfxDisplayMode & mode)
{
	memset(&mode, 0, sizeof(mode));
	mode.Format = WW3D_FORMAT_UNKNOWN;

	IDXGIOutput * output = Get_Output(m_impl->factory, adapter_index);
	if (output != nullptr) {
		DXGI_OUTPUT_DESC desc;
		if (SUCCEEDED(output->GetDesc(&desc))) {
			mode.Width = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
			mode.Height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
		}
		output->Release();
	}
	if (mode.Width == 0) {
		mode.Width = GetSystemMetrics(SM_CXSCREEN);
		mode.Height = GetSystemMetrics(SM_CYSCREEN);
	}
	// A windowed device shares the desktop's format, and under DXGI the desktop is
	// always 32-bit. The refresh rate is reported as the adapter default rather than
	// enumerated: nothing windowed reads it.
	mode.RefreshRate = 0;
	mode.Format = WW3D_FORMAT_X8R8G8B8;
	return mode.Width != 0;
}

unsigned GfxAdapterD3D11::Get_Display_Mode_Count(unsigned adapter_index, WW3DFormat format)
{
	const DXGI_FORMAT dxgi = WW3D_To_DXGI(format);
	if (dxgi == DXGI_FORMAT_UNKNOWN) return 0;
	IDXGIOutput * output = Get_Output(m_impl->factory, adapter_index);
	if (output == nullptr) return 0;
	UINT count = 0;
	// DXGI enumerates only the formats a scan-out can actually be in, which is the
	// 32-bit one; asking about a 16-bit back buffer correctly yields nothing.
	output->GetDisplayModeList(dxgi == DXGI_FORMAT_B8G8R8X8_UNORM
		? DXGI_FORMAT_B8G8R8A8_UNORM : dxgi, 0, &count, nullptr);
	output->Release();
	return count;
}

bool GfxAdapterD3D11::Get_Display_Mode(unsigned adapter_index, WW3DFormat format,
	unsigned index, GfxDisplayMode & mode)
{
	memset(&mode, 0, sizeof(mode));
	mode.Format = WW3D_FORMAT_UNKNOWN;

	const DXGI_FORMAT dxgi = WW3D_To_DXGI(format);
	if (dxgi == DXGI_FORMAT_UNKNOWN) return false;
	IDXGIOutput * output = Get_Output(m_impl->factory, adapter_index);
	if (output == nullptr) return false;

	const DXGI_FORMAT enumerated = (dxgi == DXGI_FORMAT_B8G8R8X8_UNORM)
		? DXGI_FORMAT_B8G8R8A8_UNORM : dxgi;
	UINT count = 0;
	output->GetDisplayModeList(enumerated, 0, &count, nullptr);
	if (index >= count) { output->Release(); return false; }

	DXGI_MODE_DESC * modes = new DXGI_MODE_DESC[count];
	const bool ok = SUCCEEDED(output->GetDisplayModeList(enumerated, 0, &count, modes));
	output->Release();
	if (ok) {
		mode.Width = modes[index].Width;
		mode.Height = modes[index].Height;
		mode.RefreshRate = modes[index].RefreshRate.Denominator != 0
			? modes[index].RefreshRate.Numerator / modes[index].RefreshRate.Denominator : 0;
		mode.Format = format;
	}
	delete [] modes;
	return ok;
}

bool GfxAdapterD3D11::Supports_Display_Format(unsigned, WW3DFormat,
	WW3DFormat back_buffer, bool)
{
	// DXGI presents from a 32-bit back buffer and nothing else, whatever the display is
	// in. A 16-bit mode is not a supported configuration here rather than a slower one.
	return back_buffer == WW3D_FORMAT_X8R8G8B8 || back_buffer == WW3D_FORMAT_A8R8G8B8;
}

bool GfxAdapterD3D11::Supports_Depth_Stencil_Format(unsigned, WW3DFormat,
	WW3DFormat, WW3DZFormat depth)
{
	return WW3DZ_To_DXGI(depth).depth != DXGI_FORMAT_UNKNOWN;
}

ID3D11Device * GfxAdapterD3D11::Caps_Device(unsigned adapter_index)
{
	if (!Is_Valid()) return nullptr;
	if (m_impl->caps_device != nullptr && m_impl->caps_adapter == adapter_index)
		return m_impl->caps_device;
	if (m_impl->caps_device != nullptr) {
		m_impl->caps_device->Release();
		m_impl->caps_device = nullptr;
	}

	IDXGIAdapter1 * adapter = Get_Adapter(m_impl->factory, adapter_index);
	if (adapter == nullptr) return nullptr;

	static const D3D_FEATURE_LEVEL levels[] = {
		D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0
	};
	ID3D11Device * device = nullptr;
	ID3D11DeviceContext * context = nullptr;
	const HRESULT hr = m_impl->create_device(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
		levels, (UINT)(sizeof(levels) / sizeof(levels[0])), D3D11_SDK_VERSION,
		&device, nullptr, &context);
	adapter->Release();
	if (FAILED(hr)) return nullptr;
	if (context != nullptr) context->Release();

	m_impl->caps_device = device;
	m_impl->caps_adapter = adapter_index;
	return device;
}

bool GfxAdapterD3D11::Supports_Multisample(unsigned adapter_index, WW3DFormat format, bool,
	WW3DMultiSampleType samples)
{
	if (samples == WW3D_MULTISAMPLE_NONE) return true;
	const DXGI_FORMAT dxgi = WW3D_To_DXGI(format);
	if (dxgi == DXGI_FORMAT_UNKNOWN) return false;
	ID3D11Device * device = Caps_Device(adapter_index);
	if (device == nullptr) return false;
	// Quality levels, not a boolean: CheckMultisampleQualityLevels succeeds and returns
	// zero for a count the device cannot do, so the count is the answer.
	UINT quality = 0;
	if (FAILED(device->CheckMultisampleQualityLevels(dxgi, (UINT)samples, &quality)))
		return false;
	return quality > 0;
}

bool GfxAdapterD3D11::Supports_Depth_Multisample(unsigned adapter_index, WW3DZFormat format,
	bool, WW3DMultiSampleType samples)
{
	if (samples == WW3D_MULTISAMPLE_NONE) return true;
	const DXGI_FORMAT dxgi = WW3DZ_To_DXGI(format).depth;
	if (dxgi == DXGI_FORMAT_UNKNOWN) return false;
	ID3D11Device * device = Caps_Device(adapter_index);
	if (device == nullptr) return false;
	UINT quality = 0;
	if (FAILED(device->CheckMultisampleQualityLevels(dxgi, (UINT)samples, &quality)))
		return false;
	return quality > 0;
}

bool GfxAdapterD3D11::Supports_Hardware_Transform_And_Lighting(unsigned)
{
	// There is no fixed-function transform to do in hardware or anywhere else. True is
	// nonetheless the right answer: the engine reads this to decide whether vertices need
	// software processing, and under D3D11 they never do.
	return true;
}

bool GfxAdapterD3D11::Supports_Texture_Format(unsigned adapter_index, WW3DFormat,
	WW3DFormat format, GfxFormatCapability capability)
{
	// Asked before a device exists, and D3D11 has no adapter-level format query -- only
	// ID3D11Device::CheckFormatSupport. Answering from the format alone is what is
	// available, and for the eleven formats this engine asks about it is also correct:
	// every D3D11 device supports all of them for sampling, and the two the engine ever
	// renders into (A8R8G8B8 and A16B16G16R16F) are render-target and blendable on every
	// feature level 10 device.
	(void)adapter_index;
	const DXGI_FORMAT dxgi = WW3D_To_DXGI(format);
	if (dxgi == DXGI_FORMAT_UNKNOWN) return false;
	switch (capability) {
	case GFX_FORMAT_RENDER_TARGET:
	case GFX_FORMAT_BLENDABLE:
		return dxgi == DXGI_FORMAT_B8G8R8A8_UNORM || dxgi == DXGI_FORMAT_B8G8R8X8_UNORM ||
			dxgi == DXGI_FORMAT_R16G16B16A16_FLOAT || dxgi == DXGI_FORMAT_R8_UNORM ||
			dxgi == DXGI_FORMAT_R8G8_UNORM;
	default:
		return true;
	}
}

bool GfxAdapterD3D11::Supports_Depth_Texture_Format(unsigned, WW3DFormat, WW3DZFormat format)
{
	// A depth buffer the shadow map samples afterwards. Every D3D11 device can do this
	// for the 16- and 24-bit formats; it is the typeless creation that makes it work and
	// that is this backend's business, not the caller's.
	return WW3DZ_To_DXGI(format).shader != DXGI_FORMAT_UNKNOWN;
}

// What this backend reports it can do. Every field here feeds a decision somewhere, and
// a wrong one does not throw -- it draws the wrong thing somewhere far from the lie.
static void Fill_D3D11_Caps(unsigned adapter_ordinal, GfxDeviceCaps & caps)
{
	memset(&caps, 0, sizeof(caps));
	caps.AdapterOrdinal = adapter_ordinal;

	caps.HardwareTransformAndLighting = true;
	caps.NPatches = false;
	caps.FullScreenGamma = false;
	caps.CubeMaps = true;
	caps.ColorWriteEnable = true;
	caps.BumpEnvmap = false;
	caps.BumpEnvmapLuminance = false;
	caps.ModulateAlphaAddColor = false;
	caps.DotProduct3 = false;
	// False on purpose, and it is the whole of what this backend needs for the snow.
	// D3D11 does not expand a point into a screen-facing quad, and W3DSnowManager reads
	// this to pick renderAsQuads -- which draws the same snowfall through unit_vs with a
	// vertex shader, and so through this backend at all.
	caps.PointSprites = false;

	caps.LinearFilter = true;
	caps.MipLinearFilter = true;
	caps.AnisotropicFilter = true;

	// Feature level 10 and up guarantee 8192; 11 guarantees 16384. Reported rather than
	// left zero because Adjust_Texture_Requirements -- the engine's own texture policy
	// since Phase 4.2 -- reads all three, and a zero here makes every texture allocation
	// quietly wrong.
	caps.MaxTextureWidth = 16384;
	caps.MaxTextureHeight = 16384;
	caps.MaxVolumeExtent = 2048;
	caps.MaxTextureAspectRatio = 0;			// unlimited
	caps.MaxSimultaneousTextures = 8;

	// Deliberately the versions the D3D9 backend reports rather than the ones the
	// hardware would claim. These feed the routing block, and the point of this phase is
	// a census that can be read beside D3D9's -- a backend that reported model 4 here
	// would take different routing decisions and produce a different census for a reason
	// that has nothing to do with whether it draws correctly. The bytecode that actually
	// loads is model 4; what the engine decides with is unchanged.
	caps.VertexShaderVersion = (3 << 8) | 0;
	caps.PixelShaderVersion = (3 << 8) | 0;

}

bool GfxAdapterD3D11::Query_Capabilities(unsigned adapter_index, GfxDeviceCaps & caps)
{
	IDXGIAdapter1 * adapter = Get_Adapter(m_impl->factory, adapter_index);
	if (adapter == nullptr) return false;
	adapter->Release();
	Fill_D3D11_Caps(adapter_index, caps);
	return true;
}

// ---------------------------------------------------------------------------
// Making the device
// ---------------------------------------------------------------------------

namespace
{
	/// Make the back-buffer and depth-buffer wrappers for a freshly created or resized
	/// swap chain. Returns false if either could not be made, which is fatal either way.
	bool Attach_Swap_Chain_Surfaces(GfxD3D11Impl * impl);
	void Release_Swap_Chain_Surfaces(GfxD3D11Impl * impl);
	bool Create_Device_Resources(GfxD3D11Impl * impl);
}

GfxDeviceClass * GfxAdapterD3D11::Create_Device(unsigned adapter_index, GfxSwapChainDesc & desc)
{
	if (!Is_Valid()) return nullptr;

	IDXGIAdapter1 * adapter = Get_Adapter(m_impl->factory, adapter_index);
	if (adapter == nullptr) return nullptr;

	UINT flags = 0;
#ifdef RTS_DEBUG
	// The debug layer is what turns "the frame is black" into a line saying which bind
	// was wrong. It is only present if the machine has the Graphics Tools feature, so
	// creation is tried with it and again without. W3D_D3D11_NODEBUG turns it off, which
	// is worth having when the question is whether the layer itself is in the way.
	if (getenv("W3D_D3D11_NODEBUG") == nullptr) flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	// Feature level 11.0 is the floor. Every shader here compiles at model 5, which
	// requires 11.0, and D3D11 refuses to create SM 5.0 shaders on lower levels.
	static const D3D_FEATURE_LEVEL levels[] = {
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0
	};

	ID3D11Device * device = nullptr;
	ID3D11DeviceContext * context = nullptr;
	D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;

	HRESULT hr = m_impl->create_device(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
		levels, (UINT)(sizeof(levels) / sizeof(levels[0])), D3D11_SDK_VERSION, &device, &level, &context);
	if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG) != 0) {
		WWDEBUG_SAY(("D3D11: no debug layer on this machine (0x%08x); retrying without it.",
			(unsigned)hr));
		flags &= ~D3D11_CREATE_DEVICE_DEBUG;
		hr = m_impl->create_device(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
			levels, 3, D3D11_SDK_VERSION, &device, &level, &context);
	}
	if (FAILED(hr)) {
		WWDEBUG_SAY(("D3D11: D3D11CreateDevice failed (0x%08x).", (unsigned)hr));
		adapter->Release();
		return nullptr;
	}

	GfxD3D11Impl * impl = new GfxD3D11Impl;
	impl->device = device;
	impl->context = context;
	if ((flags & D3D11_CREATE_DEVICE_DEBUG) != 0) {
		if (SUCCEEDED(device->QueryInterface(__uuidof(ID3D11InfoQueue),
				(void **)&impl->info_queue))) {
			// Explicitly not breaking. The debug layer will raise an exception on a
			// severity it is told to break on, and a renderer that dies at the first
			// warning cannot be measured at all -- the messages are wanted in the log,
			// not in a crash dump.
			impl->info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, FALSE);
			impl->info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, FALSE);
			impl->info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, FALSE);
		} else {
			impl->info_queue = nullptr;
		}
	}
	impl->adapter = adapter;			// keeps the reference EnumAdapters1 took
	impl->feature_level = level;
	impl->desc = desc;

	DXGI_SWAP_CHAIN_DESC scd;
	memset(&scd, 0, sizeof(scd));
	scd.BufferDesc.Width = desc.Width;
	scd.BufferDesc.Height = desc.Height;
	// One back-buffer format, whatever was asked for. DXGI presents from a 32-bit buffer
	// and Supports_Display_Format above already declined everything else.
	scd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	scd.BufferDesc.RefreshRate.Numerator = desc.RefreshRate;
	scd.BufferDesc.RefreshRate.Denominator = desc.RefreshRate != 0 ? 1 : 0;
	// The back buffer carries the sample count, the way D3D9's did, because that is what
	// the engine reads to decide the whole multisampled arrangement: W3DShaderManager
	// describes the current render target -- the back buffer -- and builds its scene
	// target to match, drawing into a multisampled colour surface and resolving into the
	// texture the post-process chain samples.
	//
	// DXGI_SWAP_EFFECT_DISCARD is what makes this legal at all: the flip models refuse a
	// multisampled back buffer outright, the BitBlt model does not.
	scd.SampleDesc.Count = (desc.MultiSample != WW3D_MULTISAMPLE_NONE)
		? (UINT)desc.MultiSample : 1;
	scd.SampleDesc.Quality = 0;
	scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
	scd.BufferCount = desc.BackBufferCount != 0 ? desc.BackBufferCount : 1;
	scd.OutputWindow = (HWND)desc.Window;
	scd.Windowed = desc.Windowed ? TRUE : FALSE;
	scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	hr = m_impl->factory->CreateSwapChain(device, &scd, &impl->swap_chain);
	if (FAILED(hr) && (scd.BufferUsage & DXGI_USAGE_SHADER_INPUT) != 0) {
		scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		hr = m_impl->factory->CreateSwapChain(device, &scd, &impl->swap_chain);
	}
	if (FAILED(hr) && scd.SampleDesc.Count > 1) {
		// Say so rather than falling back silently: a run that asked for 8x and measured
		// 1x is the kind of thing this tree has mistaken for a rendering change before.
		WWDEBUG_SAY(("D3D11: CreateSwapChain refused %u samples (0x%08x); falling back to "
			"one. The frame will differ from D3D9 on every silhouette edge.",
			scd.SampleDesc.Count, (unsigned)hr));
		scd.SampleDesc.Count = 1;
		impl->desc.MultiSample = WW3D_MULTISAMPLE_NONE;
		hr = m_impl->factory->CreateSwapChain(device, &scd, &impl->swap_chain);
	}
	if (FAILED(hr)) {
		WWDEBUG_SAY(("D3D11: CreateSwapChain failed (0x%08x).", (unsigned)hr));
		delete impl;
		context->Release();
		device->Release();
		adapter->Release();
		return nullptr;
	}

	// DXGI otherwise takes Alt+Enter for itself and switches the swap chain to fullscreen
	// behind the engine's back, which leaves the engine's own idea of the mode wrong and
	// the harness looking at a window that is no longer the size it asked for.
	m_impl->factory->MakeWindowAssociation((HWND)desc.Window,
		DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);

	if (!Attach_Swap_Chain_Surfaces(impl) || !Create_Device_Resources(impl)) {
		WWDEBUG_SAY(("D3D11: could not make the back buffer, the depth buffer or the "
			"device's own constant and zero-stream buffers."));
		impl->swap_chain->Release();
		delete impl;
		context->Release();
		device->Release();
		adapter->Release();
		return nullptr;
	}

	// The nibbles are the version in *hexadecimal* -- D3D_FEATURE_LEVEL_11_0 is 0xb000, so
	// the major nibble is 0xb and means eleven. Printed with %x this line has said
	// "feature level b_0" in every run this tree has ever made, and Phase 5's write-up
	// read it as 11_0 and quoted it as evidence. %u is the whole fix.
	WWDEBUG_SAY(("D3D11: device created at feature level %u_%u, %ux%u %s.",
		((unsigned)level >> 12) & 0xf, ((unsigned)level >> 8) & 0xf,
		desc.Width, desc.Height, desc.Windowed ? "windowed" : "fullscreen"));

	return new GfxDeviceD3D11(impl);
}

GfxAdapterClass * Gfx_Create_Adapter_D3D11()
{
	GfxAdapterD3D11 * adapter = new GfxAdapterD3D11;
	if (!adapter->Is_Valid()) {
		delete adapter;
		return nullptr;
	}
	return adapter;
}

// ---------------------------------------------------------------------------
// Resource helpers
// ---------------------------------------------------------------------------

namespace
{
#ifdef RTS_DEBUG
	static const char * s_surfaceSite = "?";
#endif

	void Unbind_Texture_From_Write_Slots(GfxD3D11Impl * impl, D3D11Texture * t);
	void Unbind_Texture_Everywhere(GfxD3D11Impl * impl, D3D11Texture * t);

	D3D11Surface * New_Surface()
	{
		D3D11Surface * s = new D3D11Surface;
#ifdef RTS_DEBUG
		++s_surfMade;
		Note_Res_Made(s_liveSurf, s_liveSurfCount, 4096, s, s_surfaceSite);
#endif
		memset(s, 0, sizeof(*s));
		s->refs = 1;
		s->ww = WW3D_FORMAT_UNKNOWN;
		s->multisample = WW3D_MULTISAMPLE_NONE;
		return s;
	}

	/// Wrap one subresource of a Texture2D. Takes its own reference on the texture.
	D3D11Surface * Wrap_Surface(ID3D11Texture2D * texture, unsigned subresource,
		DXGI_FORMAT view_format, bool is_depth)
	{
		if (texture == nullptr) return nullptr;
		D3D11_TEXTURE2D_DESC desc;
		texture->GetDesc(&desc);

		D3D11Surface * s = New_Surface();
		texture->AddRef();
		s->texture = texture;
		s->subresource = subresource;
		// A mip level is half the size of the one above it, floored at one.
		unsigned level = subresource % (desc.MipLevels != 0 ? desc.MipLevels : 1);
		s->width = desc.Width >> level;
		s->height = desc.Height >> level;
		if (s->width == 0) s->width = 1;
		if (s->height == 0) s->height = 1;
		s->dxgi = desc.Format;
		s->view_format = view_format != DXGI_FORMAT_UNKNOWN ? view_format : desc.Format;
		s->ww = DXGI_To_WW3D(s->view_format);
		s->multisample = (WW3DMultiSampleType)(desc.SampleDesc.Count > 1
			? desc.SampleDesc.Count : 0);
		s->is_depth = is_depth;
		return s;
	}

	void Free_Texture(D3D11Texture * t);

	void Free_Surface(D3D11Surface * s)
	{
#ifdef RTS_DEBUG
		if (s != nullptr) { ++s_surfFreed; Note_Res_Freed(s_liveSurf, s_liveSurfCount, s); }
#endif
		if (s == nullptr) return;
		if (s->parent_texture != nullptr) {
			if (--s->parent_texture->refs <= 0) Free_Texture(s->parent_texture);
			s->parent_texture = nullptr;
		}
		if (s->rtv != nullptr) s->rtv->Release();
		if (s->dsv != nullptr) s->dsv->Release();
		if (s->srv != nullptr) s->srv->Release();
		if (s->readback != nullptr) s->readback->Release();
		if (s->texture != nullptr) s->texture->Release();
		delete [] s->scratch;
		delete s;
	}

	ID3D11RenderTargetView * Get_RTV(ID3D11Device * device, D3D11Surface * s)
	{
		if (s == nullptr || s->is_depth || s->texture == nullptr) return nullptr;
		if (s->rtv != nullptr) return s->rtv;
		D3D11_TEXTURE2D_DESC td;
		s->texture->GetDesc(&td);
		if ((td.BindFlags & D3D11_BIND_RENDER_TARGET) == 0) return nullptr;
		D3D11_RENDER_TARGET_VIEW_DESC desc;
		memset(&desc, 0, sizeof(desc));
		desc.Format = s->view_format;
		// A multisampled resource takes a TEXTURE2DMS view, which has no mip slice --
		// a multisampled texture has one level by definition. Creating a plain TEXTURE2D
		// view on one is refused, and a null render-target view draws nothing.
		if (s->multisample != WW3D_MULTISAMPLE_NONE) {
			desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
		} else {
			desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			desc.Texture2D.MipSlice = s->subresource;
		}
		if (FAILED(device->CreateRenderTargetView(s->texture, &desc, &s->rtv))) {
			s->rtv = nullptr;
		}
		return s->rtv;
	}

	ID3D11DepthStencilView * Get_DSV(ID3D11Device * device, D3D11Surface * s)
	{
		if (s == nullptr || !s->is_depth) return nullptr;
		if (s->dsv != nullptr) return s->dsv;
		D3D11_DEPTH_STENCIL_VIEW_DESC desc;
		memset(&desc, 0, sizeof(desc));
		desc.Format = s->view_format;
		if (s->multisample != WW3D_MULTISAMPLE_NONE) {
			desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
		} else {
			desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			desc.Texture2D.MipSlice = s->subresource;
		}
		if (FAILED(device->CreateDepthStencilView(s->texture, &desc, &s->dsv))) {
			s->dsv = nullptr;
		}
		return s->dsv;
	}

	ID3D11ShaderResourceView * Get_SRV(ID3D11Device * device, D3D11Surface * s)
	{
		if (s == nullptr || s->is_depth || s->texture == nullptr) return nullptr;
		if (s->srv != nullptr) return s->srv;
		D3D11_TEXTURE2D_DESC td;
		s->texture->GetDesc(&td);
		if ((td.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0) return nullptr;
		D3D11_SHADER_RESOURCE_VIEW_DESC desc;
		memset(&desc, 0, sizeof(desc));
		desc.Format = s->view_format;
		if (s->multisample != WW3D_MULTISAMPLE_NONE) {
			desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
		} else {
			desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			desc.Texture2D.MostDetailedMip = s->subresource;
			desc.Texture2D.MipLevels = 1;
		}
		if (FAILED(device->CreateShaderResourceView(s->texture, &desc, &s->srv))) {
			s->srv = nullptr;
		}
		return s->srv;
	}

	void Free_Texture(D3D11Texture * t)
	{
#ifdef RTS_DEBUG
		if (t != nullptr) { ++s_texFreed; Note_Res_Freed(s_liveTex, s_liveTexCount, t); }
#endif
		if (t == nullptr) return;
		if (t->srv != nullptr) t->srv->Release();
		if (t->uav != nullptr) t->uav->Release();
		if (t->resource != nullptr) t->resource->Release();
		if (t->scratch != nullptr) {
			const unsigned n = t->levels * t->faces;
			for (unsigned i = 0; i < n; ++i) delete [] t->scratch[i].data;
			delete [] t->scratch;
		}
		delete t;
	}

	bool Attach_Swap_Chain_Surfaces(GfxD3D11Impl * impl)
	{
		ID3D11Texture2D * back = nullptr;
		if (FAILED(impl->swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&back)))
			return false;

		if (impl->back_buffer == nullptr) {
#ifdef RTS_DEBUG
			s_surfaceSite = "swap chain back buffer";
#endif
			impl->back_buffer = Wrap_Surface(back, 0, DXGI_FORMAT_UNKNOWN, false);
		} else {
			// A resize keeps the wrapper object and swaps the texture inside it, so that
			// a reference the engine is still holding across the reset stays valid and
			// starts pointing at the new buffer rather than at a dead one.
			if (impl->back_buffer->rtv != nullptr) {
				impl->back_buffer->rtv->Release();
				impl->back_buffer->rtv = nullptr;
			}
			if (impl->back_buffer->srv != nullptr) {
				impl->back_buffer->srv->Release();
				impl->back_buffer->srv = nullptr;
			}
			if (impl->back_buffer->texture != nullptr) impl->back_buffer->texture->Release();
			back->AddRef();
			impl->back_buffer->texture = back;
			D3D11_TEXTURE2D_DESC d;
			back->GetDesc(&d);
			impl->back_buffer->width = d.Width;
			impl->back_buffer->height = d.Height;
			impl->back_buffer->dxgi = d.Format;
			impl->back_buffer->view_format = d.Format;
			impl->back_buffer->ww = DXGI_To_WW3D(d.Format);
		}
		back->Release();
		if (impl->back_buffer == nullptr) return false;

		// The depth buffer. D3D9 made this for the engine as part of the swap chain
		// (EnableAutoDepthStencil); DXGI has no such thing, so it is an ordinary texture
		// created here and bound alongside the back buffer.
		const DepthFormats zf = WW3DZ_To_DXGI(impl->desc.DepthStencilFormat);
		D3D11_TEXTURE2D_DESC dd;
		memset(&dd, 0, sizeof(dd));
		dd.Width = impl->back_buffer->width;
		dd.Height = impl->back_buffer->height;
		dd.MipLevels = 1;
		dd.ArraySize = 1;
		dd.Format = zf.typeless;
		// A depth buffer's sample count has to equal its colour target's, so this is the
		// back buffer's and not a constant.
		{
			D3D11_TEXTURE2D_DESC bd;
			impl->back_buffer->texture->GetDesc(&bd);
			dd.SampleDesc = bd.SampleDesc;
		}
		dd.Usage = D3D11_USAGE_DEFAULT;
		dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;

		ID3D11Texture2D * depth = nullptr;
		if (FAILED(impl->device->CreateTexture2D(&dd, nullptr, &depth))) return false;

		if (impl->depth_buffer == nullptr) {
#ifdef RTS_DEBUG
			s_surfaceSite = "swap chain depth buffer";
#endif
			impl->depth_buffer = Wrap_Surface(depth, 0, zf.depth, true);
		} else {
			if (impl->depth_buffer->dsv != nullptr) {
				impl->depth_buffer->dsv->Release();
				impl->depth_buffer->dsv = nullptr;
			}
			if (impl->depth_buffer->texture != nullptr) impl->depth_buffer->texture->Release();
			depth->AddRef();
			impl->depth_buffer->texture = depth;
			impl->depth_buffer->width = dd.Width;
			impl->depth_buffer->height = dd.Height;
			impl->depth_buffer->dxgi = dd.Format;
			impl->depth_buffer->view_format = zf.depth;
		}
		depth->Release();
		if (impl->depth_buffer == nullptr) return false;

		impl->current_rt = impl->back_buffer;
		impl->current_ds = impl->depth_buffer;
		return true;
	}

	void Release_Swap_Chain_Surfaces(GfxD3D11Impl * impl)
	{
		if (impl->back_buffer != nullptr) {
			if (impl->back_buffer->rtv != nullptr) {
				impl->back_buffer->rtv->Release();
				impl->back_buffer->rtv = nullptr;
			}
			if (impl->back_buffer->srv != nullptr) {
				impl->back_buffer->srv->Release();
				impl->back_buffer->srv = nullptr;
			}
			if (impl->back_buffer->texture != nullptr) {
				impl->back_buffer->texture->Release();
				impl->back_buffer->texture = nullptr;
			}
		}
		if (impl->depth_buffer != nullptr) {
			if (impl->depth_buffer->dsv != nullptr) {
				impl->depth_buffer->dsv->Release();
				impl->depth_buffer->dsv = nullptr;
			}
			if (impl->depth_buffer->texture != nullptr) {
				impl->depth_buffer->texture->Release();
				impl->depth_buffer->texture = nullptr;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// The device: lifetime
// ---------------------------------------------------------------------------

#ifdef RTS_DEBUG
// The live-object census, factored out of the destructor so it can be asked twice.
//
// Phase 11 found one object alive when the graphics device is destroyed -- a 64x64 BC3
// texture with five levels, the projected shadow decal's shadow.tga -- and deliberately
// did not call it a leak, because the census could not tell a leak from a teardown-order
// artefact: W3DShadowTextureManager, which owns that texture, is destroyed AFTER the
// device. Its count does not scale with anything, which is what a correctly released
// cache looks like as well as what a one-off leak looks like.
//
// The counters are file statics and outlive the device, so the way to tell the two apart
// is to ask again once the engine itself has finished shutting down. WinMain does that,
// after GameMain returns and before the memory manager's own report.
void Gfx_Report_Live_Objects(const char * when)
{
	WWDEBUG_SAY(("D3D11 LIVE OBJECTS -- %s", when));
	// What is still alive, named rather than inferred from the
	// engine leak report's block sizes. A buffer here is one the engine never released:
	// this backend frees a D3D11Buffer only from Release_Vertex_Buffer /
	// Release_Index_Buffer, so the register can only be non-empty because a caller kept it.
	WWDEBUG_SAY(("D3D11 LIVE OBJECTS: %u buffers made, %u freed, %d still "
		"alive (%u could not be registered -- the register holds 64). A non-zero \"still "
		"alive\" is the engine's, not this backend's: nothing here can free one on its own.",
		s_buffersMade, s_buffersFreed, s_liveBufferCount, s_liveOverflow));
	WWDEBUG_SAY(("D3D11 LIVE OBJECTS, the other five kinds (made/freed, sizeof): vertex "
		"shaders %u/%u (%u bytes), pixel shaders %u/%u (%u), compute shaders %u/%u (%u), "
		"textures %u/%u (%u), surfaces %u/%u (%u), buffers (%u). A kind whose two figures "
		"differ is the one to look for in the engine's leak report, and its sizeof says "
		"which block.",
		s_vsMade, s_vsFreed, (unsigned)sizeof(D3D11VertexShader),
		s_psMade, s_psFreed, (unsigned)sizeof(D3D11PixelShader),
		s_csMade, s_csFreed, (unsigned)sizeof(D3D11ComputeShader),
		s_texMade, s_texFreed, (unsigned)sizeof(D3D11Texture),
		s_surfMade, s_surfFreed, (unsigned)sizeof(D3D11Surface),
		(unsigned)sizeof(D3D11Buffer)));
	for (int i = 0; i < s_liveTexCount; ++i) {
		const D3D11Texture * t = (const D3D11Texture *)s_liveTex[i].p;
		WWDEBUG_SAY(("    live texture %ux%u ww=%d dxgi=%d levels=%u usage=0x%x refs=%ld "
			"cube=%d volume=%d depth=%d", t->width, t->height, (int)t->ww, (int)t->dxgi,
			t->levels, t->usage, t->refs, (int)t->cube, (int)t->volume, (int)t->is_depth));
}
for (int i = 0; i < s_liveSurfCount; ++i) {
	const D3D11Surface * sf = (const D3D11Surface *)s_liveSurf[i].p;
	WWDEBUG_SAY(("    live surface %ux%u ww=%d dxgi=%d sub=%u refs=%ld depth=%d "
		"rtv=%d dsv=%d scratch=%d, made by %s", sf->width, sf->height, (int)sf->ww,
		(int)sf->dxgi, sf->subresource, sf->refs, (int)sf->is_depth,
		sf->rtv != nullptr ? 1 : 0, sf->dsv != nullptr ? 1 : 0,
		sf->scratch != nullptr ? 1 : 0, s_liveSurf[i].site));
}
if (s_resOverflow != 0)
	WWDEBUG_SAY(("    %u resources could not be registered -- the lists are full, so the "
		"survivors above are a lower bound.", s_resOverflow));
{
	unsigned shadowBytes = 0;
	for (int i = 0; i < s_liveBufferCount; ++i) {
		shadowBytes += s_liveBuffers[i].size;
		WWDEBUG_SAY(("    live %s buffer %u bytes, usage 0x%x, fvf 0x%x",
			s_liveBuffers[i].kind, s_liveBuffers[i].size,
			s_liveBuffers[i].usage,
			s_liveBuffers[i].b != nullptr ? s_liveBuffers[i].b->fvf : 0u));
	}
	if (s_liveBufferCount != 0)
		WWDEBUG_SAY(("    %d objects of %u bytes each plus %u bytes of shadow -- which is "
			"%d blocks in the engine's leak report, two per buffer.",
			s_liveBufferCount, (unsigned)sizeof(D3D11Buffer), shadowBytes,
			s_liveBufferCount * 2));
}
}
#endif

GfxDeviceD3D11::~GfxDeviceD3D11()
{
	if (m_impl == nullptr) return;


	m_impl->blend_cache.Release_All();
	m_impl->depth_cache.Release_All();
	m_impl->raster_cache.Release_All();
	m_impl->sampler_cache.Release_All();
	m_impl->layout_cache.Release_All();

	if (m_impl->debug_constant_staging != nullptr)
		m_impl->debug_constant_staging->Release();
	if (m_impl->vs_constant_buffer != nullptr) m_impl->vs_constant_buffer->Release();
	if (m_impl->ps_constant_buffer != nullptr) m_impl->ps_constant_buffer->Release();
	if (m_impl->frame_constant_buffer != nullptr) m_impl->frame_constant_buffer->Release();
	if (m_impl->zero_stream != nullptr) m_impl->zero_stream->Release();
	if (m_impl->up_buffer != nullptr) m_impl->up_buffer->Release();

	if (m_impl->blit_vs != nullptr) m_impl->blit_vs->Release();
	if (m_impl->blit_ps != nullptr) m_impl->blit_ps->Release();
	if (m_impl->blit_cb != nullptr) m_impl->blit_cb->Release();
	if (m_impl->blit_sampler_point != nullptr) m_impl->blit_sampler_point->Release();
	if (m_impl->blit_sampler_linear != nullptr) m_impl->blit_sampler_linear->Release();
	if (m_impl->blit_blend_state != nullptr) m_impl->blit_blend_state->Release();
	if (m_impl->blit_depth_state != nullptr) m_impl->blit_depth_state->Release();
	if (m_impl->blit_raster_state != nullptr) m_impl->blit_raster_state->Release();
	if (m_impl->blit_cache_srv != nullptr) m_impl->blit_cache_srv->Release();
	if (m_impl->blit_cache_tex != nullptr) m_impl->blit_cache_tex->Release();

	Release_Swap_Chain_Surfaces(m_impl);
	Free_Surface(m_impl->back_buffer);
	Free_Surface(m_impl->depth_buffer);

	if (m_impl->swap_chain != nullptr) {
		// A swap chain must not be released while it is fullscreen, or DXGI leaves the
		// display in the mode the game chose and the desktop never comes back.
		m_impl->swap_chain->SetFullscreenState(FALSE, nullptr);
		m_impl->swap_chain->Release();
	}
	if (m_impl->context != nullptr) {
		m_impl->context->ClearState();
		m_impl->context->Flush();
		m_impl->context->Release();
	}
	if (m_impl->info_queue != nullptr) m_impl->info_queue->Release();
	if (m_impl->device != nullptr) m_impl->device->Release();
	if (m_impl->adapter != nullptr) m_impl->adapter->Release();

#ifdef RTS_DEBUG
	Gfx_Report_Live_Objects("at device teardown");
#endif

	delete m_impl;
	m_impl = nullptr;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void GfxDeviceD3D11::Begin_Scene()
{
	TRACE("Begin_Scene");
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_begin_scene);
	++s_d3d11_prof.n_begin_scene;
	LARGE_INTEGER now_b;
	QueryPerformanceCounter(&now_b);
	if (s_tick_present.QuadPart != 0) {
		s_d3d11_prof.t_logic_span += (now_b.QuadPart - s_tick_present.QuadPart);
		s_tick_present.QuadPart = 0;
	}
	s_tick_begin = now_b;
	s_tick_pass_start = now_b;
	s_current_pass = (m_impl->current_rt == m_impl->back_buffer || m_impl->current_rt == nullptr) ? PASS_BACKBUFFER : PASS_OTHER;
	s_current_pass_draws = 0;
#endif
	// D3D11 has no scene bracket. What D3D9 needed BeginScene for -- telling the runtime
	// that draws are coming -- is implicit in the immediate context.
	//
	// The one thing that does have to happen once is binding the targets, because D3D11
	// starts every frame with nothing bound where D3D9 kept the swap chain's own buffers
	// bound by default. Set_Render_Target does the work; this makes sure it has run.
	if (m_impl->current_rt == nullptr) m_impl->current_rt = m_impl->back_buffer;
	if (m_impl->current_ds == nullptr) m_impl->current_ds = m_impl->depth_buffer;
	ID3D11RenderTargetView * rtv = Get_RTV(m_impl->device, m_impl->current_rt);
	ID3D11DepthStencilView * dsv = Get_DSV(m_impl->device, m_impl->current_ds);
	m_impl->context->OMSetRenderTargets(1, &rtv, dsv);
	DX8Wrapper_Increment_Call_Count();
}

void GfxDeviceD3D11::End_Scene()
{
	TRACE("End_Scene");
#ifdef RTS_DEBUG
	++s_d3d11_prof.n_end_scene;
	LARGE_INTEGER now_e;
	QueryPerformanceCounter(&now_e);
	if (s_tick_begin.QuadPart != 0) {
		s_d3d11_prof.t_render_span += (now_e.QuadPart - s_tick_begin.QuadPart);
	}
	s_tick_end = now_e;
	Flush_Pass_Timing(now_e);
	s_tick_pass_start.QuadPart = 0;
	s_current_pass = PASS_NONE;
#endif
	DX8Wrapper_Increment_Call_Count();
}

namespace
{
	/// Everything the debug layer has to say since the last frame, into the game log. It
	/// repeats itself heavily -- one wrong bind is one message per draw -- so this stops
	/// after a handful per frame and says how many it dropped.
	void Drain_Debug_Messages(GfxD3D11Impl * impl)
	{
		if (impl->info_queue == nullptr) return;
#ifdef RTS_DEBUG
		PROFILE_D3D11_SCOPE(t_drain_debug);
#endif
		const UINT64 total = impl->info_queue->GetNumStoredMessages();
		UINT64 printed = 0;
		for (UINT64 i = 0; i < total && printed < 8; ++i) {
			SIZE_T length = 0;
			if (FAILED(impl->info_queue->GetMessage(i, nullptr, &length))) continue;
			D3D11_MESSAGE * message = (D3D11_MESSAGE *)new char[length];
			if (SUCCEEDED(impl->info_queue->GetMessage(i, message, &length))) {
				if (message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING) {
					WWDEBUG_SAY(("D3D11 DEBUG [%d/%d]: %.*s", (int)message->Severity,
						(int)message->ID, (int)message->DescriptionByteLength,
						message->pDescription));
					++printed;
				}
			}
			delete [] (char *)message;
		}
		if (total > 0) {
			if (printed >= 8)
				WWDEBUG_SAY(("D3D11 DEBUG: ...and %d more messages this frame.",
					(int)(total - printed)));
			impl->info_queue->ClearStoredMessages();
		}
	}
}

GfxDeviceStatus GfxDeviceD3D11::Present()
{
	TRACE("Present");
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_present);
	LARGE_INTEGER now_p;
	QueryPerformanceCounter(&now_p);
	if (s_tick_end.QuadPart != 0) {
		s_d3d11_prof.t_post_span += (now_p.QuadPart - s_tick_end.QuadPart);
	}
#endif
	if (m_impl->swap_chain == nullptr) return GFX_DEVICE_ERROR;
	Drain_Debug_Messages(m_impl);
#ifdef RTS_DEBUG
	if (Tracing()) { --s_trace_remaining; WWDEBUG_SAY(("D3D11 TRACE: ---- present, %d frames left ----", s_trace_remaining)); }
#endif

	HRESULT hr;
	{
#ifdef RTS_DEBUG
		PROFILE_D3D11_SCOPE(t_swap_present);
#endif
		hr = m_impl->swap_chain->Present(
			m_impl->desc.SwapInterval > 0 ? (UINT)m_impl->desc.SwapInterval : 0, 0);
	}

#ifdef RTS_DEBUG
	QueryPerformanceCounter(&s_tick_present);
#endif

	// DXGI_STATUS_OCCLUDED is a success code and means the window is hidden -- alt-tabbed
	// away, or covered. It is emphatically not a lost device, and treating it as one
	// makes an alt-tab look like a driver failure and starts a reset loop that never ends.
	if (hr == DXGI_STATUS_OCCLUDED) return GFX_DEVICE_OK;
	if (SUCCEEDED(hr)) return GFX_DEVICE_OK;
	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
		return GFX_DEVICE_LOST;
	return GFX_DEVICE_ERROR;
}

GfxDeviceStatus GfxDeviceD3D11::Get_Device_Status()
{
	TRACE("Get_Device_Status");
	// D3D11 has no device-lost concept for the ordinary cases D3D9 had one for: an
	// alt-tab does not lose a D3D11 device and there is nothing to reset. Only a driver
	// reset or a removed adapter loses one, and that is not recoverable here.
	if (m_impl->device == nullptr) return GFX_DEVICE_ERROR;
	const HRESULT hr = m_impl->device->GetDeviceRemovedReason();
	if (SUCCEEDED(hr)) return GFX_DEVICE_OK;
	return GFX_DEVICE_LOST;
}

void GfxDeviceD3D11::Clear(bool clear_color, bool clear_z, bool clear_stencil,
	unsigned argb, float z, unsigned stencil)
{
	TRACE("Clear");
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_clear);
	++s_d3d11_prof.n_clear;
#endif
	if (clear_color) {
		ID3D11RenderTargetView * rtv = Get_RTV(m_impl->device, m_impl->current_rt);
		if (rtv != nullptr) {
			const float rgba[4] = {
				((argb >> 16) & 0xff) / 255.0f,
				((argb >>  8) & 0xff) / 255.0f,
				((argb >>  0) & 0xff) / 255.0f,
				((argb >> 24) & 0xff) / 255.0f
			};
			m_impl->context->ClearRenderTargetView(rtv, rgba);
			DX8Wrapper_Increment_Call_Count();
		}
	}
	if (clear_z || clear_stencil) {
		ID3D11DepthStencilView * dsv = Get_DSV(m_impl->device, m_impl->current_ds);
		if (dsv != nullptr) {
			UINT flags = 0;
			if (clear_z) flags |= D3D11_CLEAR_DEPTH;
			if (clear_stencil && Has_Stencil_Target()) flags |= D3D11_CLEAR_STENCIL;
			if (flags != 0) {
				m_impl->context->ClearDepthStencilView(dsv, flags, z, (UINT8)stencil);
				DX8Wrapper_Increment_Call_Count();
			}
		}
	}
}

bool GfxDeviceD3D11::Has_Stencil_Target()
{
	TRACE("Has_Stencil_Target");
	if (m_impl->current_ds == nullptr) return false;
	return DXGI_Format_Has_Stencil(m_impl->current_ds->view_format);
}

// ---------------------------------------------------------------------------
// Render state
//
// Recording, not setting. What each word feeds is decided at the draw.
// ---------------------------------------------------------------------------

void GfxDeviceD3D11::Set_Render_State(unsigned state, unsigned value)
{
	TRACE("Set_Render_State");
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_set_rs);
	++s_d3d11_prof.n_set_rs;
#endif
	if (state >= RS_COUNT) return;
	if (m_impl->rs[state] == value) {
		// The wrapper already skips redundant writes; this catches the ones that reach
		// here anyway and keeps a dirty flag from being raised for nothing.
		return;
	}
	m_impl->rs[state] = value;

	switch (state) {
	case RS_ALPHABLENDENABLE: case RS_SRCBLEND: case RS_DESTBLEND: case RS_BLENDOP:
	case RS_COLORWRITEENABLE: case RS_SEPARATEALPHABLENDENABLE:
	case RS_SRCBLENDALPHA: case RS_DESTBLENDALPHA: case RS_BLENDOPALPHA:
	case RS_BLENDFACTOR:
		m_impl->blend_dirty = true;
		return;

	case RS_ZENABLE: case RS_ZWRITEENABLE: case RS_ZFUNC:
	case RS_STENCILENABLE: case RS_STENCILFUNC: case RS_STENCILREF:
	case RS_STENCILMASK: case RS_STENCILWRITEMASK:
	case RS_STENCILPASS: case RS_STENCILFAIL: case RS_STENCILZFAIL:
		m_impl->depth_dirty = true;
		return;

	case RS_CULLMODE: case RS_FILLMODE: case RS_DEPTHBIAS:
	case RS_SLOPESCALEDEPTHBIAS: case RS_SCISSORTESTENABLE:
	case RS_MULTISAMPLEANTIALIAS:
		m_impl->raster_dirty = true;
		return;

	case RS_COMPAT_ZBIAS:
		// D3D8's integer ZBIAS, which the D3D9 backend converts into a float depth bias.
		// D3D11's is an integer again, in units of the depth buffer's smallest
		// representable value -- so the conversion is the other way round from D3D9's and
		// the sign is the same: a larger bias pushes towards the viewer.
		m_impl->raster_dirty = true;
		return;

	// Alpha test. It is not a D3D11 state at all -- Phase 2 moved it into clip() at c28
	// and turned the hardware stage off -- so these three arrive only as the engine
	// keeping its own tracked array in step, and there is nothing to do with them.
	case RS_ALPHATESTENABLE: case RS_ALPHAREF: case RS_ALPHAFUNC:
		return;

	default:
		// Everything else. Fog, line patterns, point scaling, edge antialiasing, software
		// vertex processing, the patch and tessellation words, tween factor, vertex
		// blending, and every fixed-function lighting and material word: none of them has
		// a D3D11 meaning, and gfxdevice.h makes absorbing them this backend's job rather
		// than making the caller know which they are.
		return;
	}
}

void GfxDeviceD3D11::Set_Texture_Stage_State(unsigned stage, unsigned state, unsigned value)
{
	TRACE("Set_Texture_Stage_State");
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_set_tss);
	++s_d3d11_prof.n_set_tss;
#endif
	if (stage >= GFX_MAX_STAGES || state >= 32) return;
	if (m_impl->tss[stage][state] == value) return;
	m_impl->tss[stage][state] = value;

	switch (state) {
	case TSS_ADDRESSU: case TSS_ADDRESSV: case TSS_ADDRESSW: case TSS_BORDERCOLOR:
	case TSS_MAGFILTER: case TSS_MINFILTER: case TSS_MIPFILTER:
	case TSS_MIPMAPLODBIAS: case TSS_MAXMIPLEVEL: case TSS_MAXANISOTROPY:
	case TSS_COMPAREFUNC:
		m_impl->sampler_dirty[stage] = true;
		return;
	default:
		// A fixed-function combiner word. There is no combiner, and the measurement that
		// says this is safe was taken before the backend was written: dropping every one
		// of these at the call -- keeping the deferral bookkeeping, which is the part
		// that was load-bearing -- moved zero pixels on civ_buildings.
		return;
	}
}

void GfxDeviceD3D11::Set_Clip_Plane(unsigned, const float *)
{
	TRACE("Set_Clip_Plane");
	// D3D11 has no fixed clip planes; the equivalent is SV_ClipDistance written by the
	// vertex shader. Nothing here needs one.
	//
	// The one caller anybody could name is the water reflection pass, and it does not call
	// this: W3DWater.cpp's clip-plane lines are commented out inside a
	// CLIP_GEOMETRY_TO_PLANE block whose #define is itself commented out, and what runs
	// instead is the alpha-test hack written beside them. So D3D9 does not clip the
	// reflection to the water plane either, and this backend loses nothing by absorbing a
	// call that is never made. The counter that said so read 0 in every census window of
	// every run since the backend existed, and it was retired with the rest of the
	// absorbed-write instrument in Phase 10.
}

bool GfxDeviceD3D11::Get_Render_State(unsigned state, unsigned & value)
{
	TRACE("Get_Render_State");
	// The device-state audit's read-back. This backend keeps the words itself rather than
	// handing them to an API that would forget them, so it can answer -- and answering is
	// what lets the audit run under D3D11 at all.
	if (state >= RS_COUNT) return false;
	value = m_impl->rs[state];
	return true;
}

bool GfxDeviceD3D11::Get_Texture_Stage_State(unsigned stage, unsigned state, unsigned & value)
{
	TRACE("Get_Texture_Stage_State");
	if (stage >= GFX_MAX_STAGES || state >= 32) return false;
	value = m_impl->tss[stage][state];
	return true;
}

// ---------------------------------------------------------------------------
// Fixed-function residue
// ---------------------------------------------------------------------------

void GfxDeviceD3D11::Set_Transform(unsigned which, const float * matrix4x4)
{
	TRACE("Set_Transform");
	// Nothing is *driven* by this: there is no fixed-function transform to feed, and every
	// draw that positioned itself with D3DTS_WORLD went with the shadow volumes in Phase
	// 4.1. So the fixed-function half is absorbed.

	// The value is kept anyway, because a caller reads it back. See the note on
	// GfxD3D11Impl::transforms: this is what the shadow decals' vertex shader is built
	// from, and a backend that forgets sends them to a fixed-function pipeline it has not
	// got.
	const int slot = Transform_Slot(which);
	if (slot < 0 || matrix4x4 == nullptr) return;
	memcpy(m_impl->transforms[slot], matrix4x4, 16 * sizeof(float));
	m_impl->transform_set[slot] = true;
}

bool GfxDeviceD3D11::Get_Transform(unsigned which, float * matrix4x4)
{
	TRACE("Get_Transform");
	const int slot = Transform_Slot(which);
	if (slot < 0 || matrix4x4 == nullptr || !m_impl->transform_set[slot]) return false;
	memcpy(matrix4x4, m_impl->transforms[slot], 16 * sizeof(float));
	return true;
}

// ---------------------------------------------------------------------------
// Bindings
//
// The render-target hazard lives here, in both directions. See srv_unbound_mask on
// GfxD3D11Impl for what it is and why the backend, rather than the wrapper, has to own it.
// ---------------------------------------------------------------------------

namespace
{
	/// The resource a surface lives in, or null. A GfxSurface is a view of one subresource
	/// of a texture, and the hazard is about the resource.
	ID3D11Resource * Surface_Resource(const D3D11Surface * s)
	{
		return (s != nullptr) ? (ID3D11Resource *)s->texture : nullptr;
	}

	/// Whether a resource is bound as the colour or the depth target right now.
	bool Is_Bound_As_Target(const GfxD3D11Impl * impl, const ID3D11Resource * resource)
	{
		if (resource == nullptr) return false;
		return resource == Surface_Resource(impl->current_rt)
			|| resource == Surface_Resource(impl->current_ds);
	}

	/// Take one stage's texture out of both shader stages and record that it happened.
	void Force_Unbind_Stage(GfxD3D11Impl * impl, unsigned stage)
	{
		ID3D11ShaderResourceView * none = nullptr;
		impl->context->PSSetShaderResources(stage, 1, &none);
		impl->context->VSSetShaderResources(stage, 1, &none);
		impl->srv_unbound_mask |= (1u << stage);
		ABSORB(s_srv_forced_unbound);
	}

	/// Everything the incoming target conflicts with, unbound before OMSetRenderTargets
	/// gets to do it silently.
	void Unbind_Conflicting_Textures(GfxD3D11Impl * impl, const D3D11Surface * rt,
		const D3D11Surface * ds)
	{
		const ID3D11Resource * colour = Surface_Resource(rt);
		const ID3D11Resource * depth = Surface_Resource(ds);
		if (colour == nullptr && depth == nullptr) return;
		for (unsigned s = 0; s < GFX_MAX_STAGES; ++s) {
			if ((impl->srv_unbound_mask & (1u << s)) != 0) continue;		// already out
			const D3D11Texture * t = impl->textures[s];
			if (t == nullptr || t->resource == nullptr) continue;
			if (t->resource != colour && t->resource != depth) continue;
			Force_Unbind_Stage(impl, s);
		}
	}

	/// The other half: a slot nulled above goes back the moment its texture stops being a
	/// target. Nothing above this can do it, because nothing above this knows the slot was
	/// ever cleared.
	void Restore_Unbound_Textures(GfxD3D11Impl * impl)
	{
		for (unsigned s = 0; s < GFX_MAX_STAGES; ++s) {
			if ((impl->srv_unbound_mask & (1u << s)) == 0) continue;
			D3D11Texture * t = impl->textures[s];
			ID3D11Resource * res = (t != nullptr) ? t->resource : nullptr;
			if (Is_Bound_As_Target(impl, res)) continue;	// still a target; cannot go back
			ID3D11ShaderResourceView * srv = (t != nullptr) ? t->srv : nullptr;
			impl->context->PSSetShaderResources(s, 1, &srv);
			impl->context->VSSetShaderResources(s, 1, &srv);
			impl->srv_unbound_mask &= ~(1u << s);
			if (srv != nullptr) impl->srv_rescued_mask |= (1u << s);
			ABSORB(s_srv_rebound);
		}
	}
}

void GfxDeviceD3D11::Set_Texture(unsigned stage, GfxTexture * texture)
{
	TRACE("Set_Texture");
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_set_texture);
	++s_d3d11_prof.n_set_texture;
#endif
	if (stage >= GFX_MAX_STAGES) return;
	D3D11Texture * t = (D3D11Texture *)texture;
	if (m_impl->textures[stage] == t && (m_impl->srv_unbound_mask & (1u << stage)) == 0) return;
	m_impl->textures[stage] = t;
	m_impl->srv_unbound_mask &= ~(1u << stage);
	// The wrapper has spoken for this stage, so the rescue window closes: from here the
	// slot would have been right with or without the fix.
	m_impl->srv_rescued_mask &= ~(1u << stage);

	Unbind_Texture_From_Write_Slots(m_impl, t);

	// The same hazard from the other side. Binding a texture that is the current target
	// would make D3D11 unbind the *target* instead, which is the worse half of the trade.
	// Null the slot and remember, exactly as Set_Render_Target does; the next draw after
	// the resource stops being a target puts it back.
	if (t != nullptr && Is_Bound_As_Target(m_impl, t->resource)) {
		Force_Unbind_Stage(m_impl, stage);
		DX8Wrapper_Increment_Call_Count();
		return;
	}
	ID3D11ShaderResourceView * srv = (t != nullptr) ? t->srv : nullptr;
	// Bound to both stages. A pixel shader is the usual reader, but vs_3_0 could fetch a
	// texture and the engine's slot numbering is shared between the two -- Phase 3.8's
	// note is explicit that s3 becomes t3 and s3, not t0 and s0.
	m_impl->context->PSSetShaderResources(stage, 1, &srv);
	m_impl->context->VSSetShaderResources(stage, 1, &srv);
	DX8Wrapper_Increment_Call_Count();
}

void GfxDeviceD3D11::Set_Vertex_Shader(GfxShaderHandle shader)
{
	TRACE("Set_Vertex_Shader");
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_set_vs);
	++s_d3d11_prof.n_set_vs;
#endif
	// Below 0x10000 the handle is a vertex format code and not a shader. Under D3D9 that
	// binds an FVF and clears the vertex shader; here there is nothing to bind it to, so
	// it is recorded and the draw that follows is dropped with a count. That count is the
	// VERTEX LAYOUT CENSUS's "reached a device with no vertex shader" line seen from the
	// other side, and it reads 0 on civ_buildings.
	if (shader < 0x10000) {
		m_impl->fvf = (unsigned)shader;
		if (m_impl->vertex_shader != nullptr) {
			m_impl->vertex_shader = nullptr;
			m_impl->context->VSSetShader(nullptr, nullptr, 0);
			DX8Wrapper_Increment_Call_Count();
		}
		return;
	}

	D3D11VertexShader * vs = (D3D11VertexShader *)shader;
	if (m_impl->vertex_shader == vs) return;
	m_impl->vertex_shader = vs;
	m_impl->context->VSSetShader(vs->shader, nullptr, 0);
	DX8Wrapper_Increment_Call_Count();
}

void GfxDeviceD3D11::Set_Pixel_Shader(GfxShaderHandle shader)
{
	TRACE("Set_Pixel_Shader");
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_set_ps);
	++s_d3d11_prof.n_set_ps;
#endif
	D3D11PixelShader * ps = (D3D11PixelShader *)shader;
	if (m_impl->pixel_shader == ps) return;
	m_impl->pixel_shader = ps;
	m_impl->context->PSSetShader(ps != nullptr ? ps->shader : nullptr, nullptr, 0);
	DX8Wrapper_Increment_Call_Count();
}

GfxShaderHandle GfxDeviceD3D11::Create_Vertex_Shader(const void * bytecode, unsigned size)
{
	TRACE("Create_Vertex_Shader");
	if (bytecode == nullptr || size == 0) return 0;

	D3D11VertexShader * vs = new D3D11VertexShader;
#ifdef RTS_DEBUG
	++s_vsMade;
#endif
	memset(vs, 0, sizeof(*vs));
	if (FAILED(m_impl->device->CreateVertexShader(bytecode, size, nullptr, &vs->shader))) {
#ifdef RTS_DEBUG
		++s_vsFreed;
#endif
		delete vs;
		return 0;
	}
	// The bytecode is kept for the life of the shader because CreateInputLayout wants it
	// again -- an input layout is built from a vertex format and a shader signature
	// together, and the signature is only available as the bytes it was compiled into.
	vs->bytecode = new unsigned char[size];
	memcpy(vs->bytecode, bytecode, size);
	vs->bytecode_size = size;
	Parse_Signature(vs->bytecode, size, "ISGN", vs->inputs);
	Parse_Signature(vs->bytecode, size, "OSGN", vs->outputs);
	return (GfxShaderHandle)vs;
}

GfxShaderHandle GfxDeviceD3D11::Create_Pixel_Shader(const void * bytecode, unsigned size)
{
	TRACE("Create_Pixel_Shader");
	if (bytecode == nullptr || size == 0) return 0;
	D3D11PixelShader * ps = new D3D11PixelShader;
#ifdef RTS_DEBUG
	++s_psMade;
#endif
	memset(ps, 0, sizeof(*ps));
	if (FAILED(m_impl->device->CreatePixelShader(bytecode, size, nullptr, &ps->shader))) {
#ifdef RTS_DEBUG
		++s_psFreed;
#endif
		delete ps;
		return 0;
	}
	// Its input signature, for the linkage check at the draw. Kept rather than the
	// bytecode: a pixel shader's bytes are needed for nothing else once it is created.
	Parse_Signature((const unsigned char *)bytecode, size, "ISGN", ps->inputs);
	ps->texture_mask = Parse_Texture_Mask((const unsigned char *)bytecode, size);
#ifdef RTS_DEBUG
	++s_ps_created;
	if (ps->texture_mask != 0) ++s_ps_with_textures;
	s_ps_texture_union |= ps->texture_mask;
#endif
	return (GfxShaderHandle)ps;
}

void GfxDeviceD3D11::Release_Vertex_Shader(GfxShaderHandle shader)
{
	TRACE("Release_Vertex_Shader");
	if (shader == 0 || shader < 0x10000) return;
	D3D11VertexShader * vs = (D3D11VertexShader *)shader;
	if (m_impl->vertex_shader == vs) m_impl->vertex_shader = nullptr;
	if (vs->shader != nullptr) vs->shader->Release();
	delete [] vs->bytecode;
#ifdef RTS_DEBUG
	++s_vsFreed;
#endif
	delete vs;
}

void GfxDeviceD3D11::Release_Pixel_Shader(GfxShaderHandle shader)
{
	TRACE("Release_Pixel_Shader");
	if (shader == 0) return;
	D3D11PixelShader * ps = (D3D11PixelShader *)shader;
	if (m_impl->pixel_shader == ps) m_impl->pixel_shader = nullptr;
	if (ps->shader != nullptr) ps->shader->Release();
#ifdef RTS_DEBUG
	++s_psFreed;
#endif
	delete ps;
}

void GfxDeviceD3D11::Set_Vertex_Shader_Constants(unsigned reg, const float * data,
	unsigned vec4_count)
{
	TRACE("Set_Vertex_Shader_Constants");
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_set_vs_const);
	++s_d3d11_prof.n_set_vs_const;
#endif
	if (data == nullptr || reg >= GFX_VS_CONSTANTS) return;
	if (reg + vec4_count > GFX_VS_CONSTANTS) vec4_count = GFX_VS_CONSTANTS - reg;
	const size_t bytes = vec4_count * 4 * sizeof(float);
	float * dst = &m_impl->vs_constants[reg * 4];
	if (memcmp(dst, data, bytes) != 0) {
		memcpy(dst, data, bytes);
		m_impl->vs_constants_dirty = true;
	}
}

void GfxDeviceD3D11::Set_Pixel_Shader_Constants(unsigned reg, const float * data,
	unsigned vec4_count)
{
	TRACE("Set_Pixel_Shader_Constants");
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_set_ps_const);
	++s_d3d11_prof.n_set_ps_const;
#endif
	if (data == nullptr || reg >= GFX_PS_CONSTANTS) return;
	if (reg + vec4_count > GFX_PS_CONSTANTS) vec4_count = GFX_PS_CONSTANTS - reg;
	const size_t bytes = vec4_count * 4 * sizeof(float);
	float * dst = &m_impl->ps_constants[reg * 4];
	if (memcmp(dst, data, bytes) != 0) {
		memcpy(dst, data, bytes);
		m_impl->ps_constants_dirty = true;
	}
}

void GfxDeviceD3D11::Set_Frame_Constants(const float * data, unsigned vec4_count)
{
	Set_Frame_Constants_At(0, data, vec4_count);
}

void GfxDeviceD3D11::Set_Frame_Constants_At(unsigned offset, const float * data, unsigned vec4_count)
{
	TRACE("Set_Frame_Constants_At");
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_set_frame_const);
	++s_d3d11_prof.n_set_frame_const;
#endif
	if (data == nullptr || vec4_count == 0 || offset >= GFX_FRAME_CONSTANTS) return;
	if (offset + vec4_count > GFX_FRAME_CONSTANTS) vec4_count = GFX_FRAME_CONSTANTS - offset;
	const size_t bytes = vec4_count * 4 * sizeof(float);
	if (memcmp(&m_impl->frame_constants[offset * 4], data, bytes) != 0) {
		memcpy(&m_impl->frame_constants[offset * 4], data, bytes);
		m_impl->frame_constants_dirty = true;
	}
}

void GfxDeviceD3D11::Set_Vertex_Stream(unsigned stream, GfxVertexBuffer * buffer,
	unsigned stride)
{
	TRACE("Set_Vertex_Stream");
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_set_stream);
	++s_d3d11_prof.n_set_stream;
#endif
	// Recorded, not bound. Slot 1 carries the stream that fills in whatever a vertex
	// shader declares and the vertex format does not, and one IASetVertexBuffers at the
	// draw sets both.
	if (stream != 0) return;
	m_impl->stream0 = (D3D11Buffer *)buffer;
	m_impl->stream0_stride = stride;
}

bool GfxDeviceD3D11::Get_Vertex_Stream(unsigned stream, GfxVertexBuffer ** buffer,
	unsigned * offset, unsigned * stride)
{
	TRACE("Get_Vertex_Stream");
	if (stream != 0) return false;
	if (buffer != nullptr) {
		// A reference, which the caller releases. D3D9's GetStreamSource does this because
		// it is COM and every Get_ does; the seam never said so, and a backend that hands
		// back a borrowed pointer instead is freed out from under the next draw. That is
		// what it did: Get_Vertex_Stream, Release_Vertex_Buffer, Set_Vertex_Stream,
		// Draw_Indexed, crash -- on the first frame of the first run of this backend.
		if (m_impl->stream0 != nullptr) ++m_impl->stream0->refs;
		*buffer = (GfxVertexBuffer *)m_impl->stream0;
	}
	if (offset != nullptr) *offset = 0;
	if (stride != nullptr) *stride = m_impl->stream0_stride;
	return true;
}

bool GfxDeviceD3D11::Debug_Peek_Base_Vertex_Index(int & out)
{
	out = m_impl->base_vertex_index;
	return true;
}

void GfxDeviceD3D11::Set_Index_Buffer(GfxIndexBuffer * buffer, int base_vertex_index)
{
	TRACE("Set_Index_Buffer");
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_set_ib);
	++s_d3d11_prof.n_set_ib;
#endif
	m_impl->index_buffer = (D3D11Buffer *)buffer;
	m_impl->base_vertex_index = base_vertex_index;
	ID3D11Buffer * ib = (m_impl->index_buffer != nullptr)
		? m_impl->index_buffer->buffer : nullptr;
	if (ib != m_impl->current_ib) {
		m_impl->context->IASetIndexBuffer(ib, DXGI_FORMAT_R16_UINT, 0);
		m_impl->current_ib = ib;
		DX8Wrapper_Increment_Call_Count();
	}
}

// ---------------------------------------------------------------------------
// The device's own buffers
//
// Three, made once: one constant buffer per shader stage at the sizes the engine already
// shadows, and the stride-zero stream that supplies whatever a vertex format does not
// carry. None of them depends on the swap chain, so none is remade by a reset.
// ---------------------------------------------------------------------------

namespace
{
	static bool Load_Shader_Blob(const char * path, void ** out_data, unsigned * out_size)
	{
		*out_data = nullptr;
		*out_size = 0;

		if (_TheFileFactory != nullptr) {
			FileClass * file = _TheFileFactory->Get_File(path);
			if (file != nullptr) {
				if (file->Open(FileClass::READ)) {
					int sz = file->Size();
					if (sz > 0) {
						char * buf = new char[sz];
						if (file->Read(buf, sz) == sz) {
							file->Close();
							_TheFileFactory->Return_File(file);
							*out_data = buf;
							*out_size = (unsigned)sz;
							return true;
						}
						delete [] buf;
					}
					file->Close();
				}
				_TheFileFactory->Return_File(file);
			}
		}

		// Direct filesystem fallback
		FILE * f = fopen(path, "rb");
		if (!f) {
			char exe_path[MAX_PATH];
			if (GetModuleFileNameA(NULL, exe_path, MAX_PATH)) {
				char * p = strrchr(exe_path, '\\');
				if (!p) p = strrchr(exe_path, '/');
				if (p) {
					*p = '\0';
					char full[MAX_PATH];
					snprintf(full, sizeof(full), "%s\\%s", exe_path, path);
					f = fopen(full, "rb");
				}
			}
		}
		if (f) {
			fseek(f, 0, SEEK_END);
			long sz = ftell(f);
			fseek(f, 0, SEEK_SET);
			if (sz > 0) {
				char * buf = new char[sz];
				if (fread(buf, 1, (size_t)sz, f) == (size_t)sz) {
					fclose(f);
					*out_data = buf;
					*out_size = (unsigned)sz;
					return true;
				}
				delete [] buf;
			}
			fclose(f);
		}
		return false;
	}

	static bool Ensure_Blit_Shaders(GfxD3D11Impl * impl)
	{
		if (impl == nullptr || impl->device == nullptr) return false;
		if (impl->blit_vs != nullptr && impl->blit_ps != nullptr) return true;

		if (impl->blit_vs == nullptr) {
			void * vs_data = nullptr;
			unsigned vs_size = 0;
			if (Load_Shader_Blob("shaders\\gpu_blit_vs.sm4", &vs_data, &vs_size)) {
				HRESULT hr = impl->device->CreateVertexShader(vs_data, vs_size, nullptr, &impl->blit_vs);
				delete [] (char *)vs_data;
				if (FAILED(hr)) {
					WWDEBUG_SAY(("D3D11: Failed to create blit vertex shader (0x%08x)", (unsigned)hr));
				}
			} else {
				WWDEBUG_SAY(("D3D11: Failed to load shaders\\gpu_blit_vs.sm4"));
			}
		}

		if (impl->blit_ps == nullptr) {
			void * ps_data = nullptr;
			unsigned ps_size = 0;
			if (Load_Shader_Blob("shaders\\gpu_blit_ps.sm4", &ps_data, &ps_size)) {
				HRESULT hr = impl->device->CreatePixelShader(ps_data, ps_size, nullptr, &impl->blit_ps);
				delete [] (char *)ps_data;
				if (FAILED(hr)) {
					WWDEBUG_SAY(("D3D11: Failed to create blit pixel shader (0x%08x)", (unsigned)hr));
				}
			} else {
				WWDEBUG_SAY(("D3D11: Failed to load shaders\\gpu_blit_ps.sm4"));
			}
		}

		return (impl->blit_vs != nullptr && impl->blit_ps != nullptr);
	}

	bool Create_Device_Resources(GfxD3D11Impl * impl)
	{
		D3D11_BUFFER_DESC desc;
		memset(&desc, 0, sizeof(desc));
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		// Ninety-six vectors and thirty-two, which is what Vertex_Shader_Constants[96]
		// and Pixel_Shader_Constants[32] already hold. Phase 3.8 established that
		// register(cN) survives to Shader Model 4 -- fxc places each constant in the
		// automatic $Globals buffer at exactly that register's byte offset -- so the
		// buffer is a straight re-declaration of the register file and nothing has to be
		// reflected or renumbered.
		desc.ByteWidth = GFX_VS_CONSTANTS * 4 * sizeof(float);
		if (FAILED(impl->device->CreateBuffer(&desc, nullptr, &impl->vs_constant_buffer)))
			return false;

		desc.ByteWidth = GFX_PS_CONSTANTS * 4 * sizeof(float);
		if (FAILED(impl->device->CreateBuffer(&desc, nullptr, &impl->ps_constant_buffer)))
			return false;

		// b1, C2 of the clustered-lighting plan. Same dynamic/CPU-write arrangement as the
		// two above, at the next slot along, and bound on all three stages a shader can run
		// on -- see the comment on GfxDeviceClass::Set_Frame_Constants for why the compute
		// binding is not optional.
		desc.ByteWidth = GFX_FRAME_CONSTANTS * 4 * sizeof(float);
		if (FAILED(impl->device->CreateBuffer(&desc, nullptr, &impl->frame_constant_buffer)))
			return false;

		impl->context->VSSetConstantBuffers(0, 1, &impl->vs_constant_buffer);
		impl->context->PSSetConstantBuffers(0, 1, &impl->ps_constant_buffer);
		impl->context->VSSetConstantBuffers(1, 1, &impl->frame_constant_buffer);
		impl->context->PSSetConstantBuffers(1, 1, &impl->frame_constant_buffer);
		impl->context->CSSetConstantBuffers(1, 1, &impl->frame_constant_buffer);

#ifdef RTS_DEBUG
		WWDEBUG_SAY(("D3D11: frame constant buffer created at b1, %u bytes (%u vec4) -- "
			"bound on VS, PS and CS. the clustered lighting plan C2.",
			(unsigned)(GFX_FRAME_CONSTANTS * 4 * sizeof(float)), (unsigned)GFX_FRAME_CONSTANTS));
#endif

		// (0, 0, 0, 1) -- what D3D9 left in a vertex shader input register the vertex
		// declaration did not write. Read through a stride of zero, one copy of it fills
		// in every missing element of every vertex of every draw.
		static const float zero[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		D3D11_SUBRESOURCE_DATA initial;
		memset(&initial, 0, sizeof(initial));
		initial.pSysMem = zero;

		memset(&desc, 0, sizeof(desc));
		desc.ByteWidth = sizeof(zero);
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		if (FAILED(impl->device->CreateBuffer(&desc, &initial, &impl->zero_stream)))
			return false;

		// D3D9's own defaults for the words this backend materialises state objects from,
		// so that a draw arriving before the engine has written any of them is rasterised
		// the way D3D9 would have rasterised it rather than with a zeroed description.
		impl->rs[RS_ZENABLE] = 1;
		impl->rs[RS_ZWRITEENABLE] = 1;
		impl->rs[RS_ZFUNC] = 4;					// D3DCMP_LESSEQUAL
		impl->rs[RS_CULLMODE] = 3;				// D3DCULL_CCW
		impl->rs[RS_FILLMODE] = 3;				// D3DFILL_SOLID
		impl->rs[RS_SRCBLEND] = 2;				// D3DBLEND_ONE
		impl->rs[RS_DESTBLEND] = 1;				// D3DBLEND_ZERO
		impl->rs[RS_BLENDOP] = 1;				// D3DBLENDOP_ADD
		impl->rs[RS_SRCBLENDALPHA] = 2;
		impl->rs[RS_DESTBLENDALPHA] = 1;
		impl->rs[RS_BLENDOPALPHA] = 1;
		impl->rs[RS_COLORWRITEENABLE] = 0xf;
		impl->rs[RS_STENCILFUNC] = 8;			// D3DCMP_ALWAYS
		impl->rs[RS_STENCILPASS] = 1;			// D3DSTENCILOP_KEEP
		impl->rs[RS_STENCILFAIL] = 1;
		impl->rs[RS_STENCILZFAIL] = 1;
		impl->rs[RS_STENCILMASK] = 0xffffffff;
		impl->rs[RS_STENCILWRITEMASK] = 0xffffffff;
		// D3D9's device default for D3DRS_MULTISAMPLEANTIALIAS is TRUE, and nothing in
		// this engine ever writes it -- the one line that would is commented out in
		// Set_Default_Global_Render_States. So a zero here is not "the engine asked for
		// no multisampling", it is this table never having been told what D3D9 starts
		// with, and D3D11_RASTERIZER_DESC::MultisampleEnable is the one field in that
		// description with no D3D9 render state behind it to correct it later.
		impl->rs[RS_MULTISAMPLEANTIALIAS] = 1;
		for (unsigned s = 0; s < GFX_MAX_STAGES; ++s) {
			impl->tss[s][TSS_ADDRESSU] = 1;		// D3DTADDRESS_WRAP
			impl->tss[s][TSS_ADDRESSV] = 1;
			impl->tss[s][TSS_ADDRESSW] = 1;
			impl->tss[s][TSS_MAGFILTER] = 1;	// D3DTEXF_POINT
			impl->tss[s][TSS_MINFILTER] = 1;
			impl->tss[s][TSS_MIPFILTER] = 0;	// D3DTEXF_NONE
			impl->tss[s][TSS_MAXANISOTROPY] = 1;
			impl->sampler_dirty[s] = true;
		}
		impl->blend_dirty = true;
		impl->depth_dirty = true;
		impl->raster_dirty = true;
		impl->vs_constants_dirty = true;
		impl->ps_constants_dirty = true;
		impl->frame_constants_dirty = true;

		// GPU Blit / StretchRect pipeline resources
		Ensure_Blit_Shaders(impl);

		D3D11_BUFFER_DESC cbd;
		memset(&cbd, 0, sizeof(cbd));
		cbd.ByteWidth = 16;
		cbd.Usage = D3D11_USAGE_DYNAMIC;
		cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(impl->device->CreateBuffer(&cbd, nullptr, &impl->blit_cb)))
			return false;

		D3D11_SAMPLER_DESC samp;
		memset(&samp, 0, sizeof(samp));
		samp.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		samp.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samp.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samp.ComparisonFunc = D3D11_COMPARISON_NEVER;
		samp.MinLOD = 0.0f;
		samp.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(impl->device->CreateSamplerState(&samp, &impl->blit_sampler_point)))
			return false;

		samp.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
		if (FAILED(impl->device->CreateSamplerState(&samp, &impl->blit_sampler_linear)))
			return false;

		D3D11_BLEND_DESC bd;
		memset(&bd, 0, sizeof(bd));
		bd.RenderTarget[0].BlendEnable = FALSE;
		bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(impl->device->CreateBlendState(&bd, &impl->blit_blend_state)))
			return false;

		D3D11_DEPTH_STENCIL_DESC dsd;
		memset(&dsd, 0, sizeof(dsd));
		dsd.DepthEnable = FALSE;
		dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		dsd.DepthFunc = D3D11_COMPARISON_ALWAYS;
		if (FAILED(impl->device->CreateDepthStencilState(&dsd, &impl->blit_depth_state)))
			return false;

		D3D11_RASTERIZER_DESC rd;
		memset(&rd, 0, sizeof(rd));
		rd.FillMode = D3D11_FILL_SOLID;
		rd.CullMode = D3D11_CULL_NONE;
		rd.DepthClipEnable = FALSE;
		if (FAILED(impl->device->CreateRasterizerState(&rd, &impl->blit_raster_state)))
			return false;

		return true;
	}
}

// ---------------------------------------------------------------------------
// Materialising the state objects
//
// Everything above only records. This is where the recorded words become the four
// immutable objects D3D11 draws with, once per distinct combination and then never again.
// ---------------------------------------------------------------------------

namespace
{
	void Apply_Blend_State(GfxD3D11Impl * impl)
	{
		BlendKey key;
		memset(&key, 0, sizeof(key));
		key.enable = impl->rs[RS_ALPHABLENDENABLE];
		key.src = impl->rs[RS_SRCBLEND];
		key.dest = impl->rs[RS_DESTBLEND];
		key.op = impl->rs[RS_BLENDOP];
		key.write_mask = impl->rs[RS_COLORWRITEENABLE];
		key.separate_alpha = impl->rs[RS_SEPARATEALPHABLENDENABLE];
		key.src_alpha = impl->rs[RS_SRCBLENDALPHA];
		key.dest_alpha = impl->rs[RS_DESTBLENDALPHA];
		key.op_alpha = impl->rs[RS_BLENDOPALPHA];

		ID3D11BlendState * state = impl->blend_cache.Find(key);
		if (state == nullptr) {
			D3D11_BLEND_DESC desc;
			memset(&desc, 0, sizeof(desc));
			desc.AlphaToCoverageEnable = FALSE;
			desc.IndependentBlendEnable = FALSE;
			D3D11_RENDER_TARGET_BLEND_DESC & rt = desc.RenderTarget[0];
			rt.BlendEnable = key.enable ? TRUE : FALSE;
			rt.SrcBlend = To_Blend(key.src);
			rt.DestBlend = To_Blend(key.dest);
			rt.BlendOp = To_Blend_Op(key.op);
			if (key.separate_alpha) {
				rt.SrcBlendAlpha = To_Alpha_Blend(key.src_alpha);
				rt.DestBlendAlpha = To_Alpha_Blend(key.dest_alpha);
				rt.BlendOpAlpha = To_Blend_Op(key.op_alpha);
			} else {
				// D3D9 with separate alpha blending off applies the colour factors to the
				// alpha channel too. D3D11 always has an alpha half and will not take a
				// colour factor in it, so the same factor is folded to its alpha spelling
				// rather than left at a default that would blend alpha differently.
				rt.SrcBlendAlpha = To_Alpha_Blend(key.src);
				rt.DestBlendAlpha = To_Alpha_Blend(key.dest);
				rt.BlendOpAlpha = To_Blend_Op(key.op);
			}
			// D3DCOLORWRITEENABLE's RED/GREEN/BLUE/ALPHA bits are 1/2/4/8, and
			// D3D11_COLOR_WRITE_ENABLE's are the same four bits in the same order.
			rt.RenderTargetWriteMask = (UINT8)(key.write_mask & 0xf);

			if (FAILED(impl->device->CreateBlendState(&desc, &state))) return;
			impl->blend_cache.Add(key, state);
		}

		// The blend factor is a render state in D3D9 and an argument to the bind in
		// D3D11, which is why it is not part of the key: a factor change would otherwise
		// make a new state object for state that did not change.
		const unsigned bf = impl->rs[RS_BLENDFACTOR];
		const float factor[4] = {
			((bf >> 16) & 0xff) / 255.0f, ((bf >> 8) & 0xff) / 255.0f,
			((bf >> 0) & 0xff) / 255.0f,  ((bf >> 24) & 0xff) / 255.0f
		};
		if (state != impl->current_blend_state || bf != impl->current_blend_factor) {
			impl->context->OMSetBlendState(state, factor, 0xffffffff);
			impl->current_blend_state = state;
			impl->current_blend_factor = bf;
			DX8Wrapper_Increment_Call_Count();
		}
	}

	void Apply_Depth_State(GfxD3D11Impl * impl)
	{
		DepthKey key;
		memset(&key, 0, sizeof(key));
		key.z_enable = impl->rs[RS_ZENABLE];
		key.z_write = impl->rs[RS_ZWRITEENABLE];
		key.z_func = impl->rs[RS_ZFUNC];
		key.stencil_enable = impl->rs[RS_STENCILENABLE];
		key.stencil_func = impl->rs[RS_STENCILFUNC];
		key.stencil_mask = impl->rs[RS_STENCILMASK];
		key.stencil_write_mask = impl->rs[RS_STENCILWRITEMASK];
		key.stencil_pass = impl->rs[RS_STENCILPASS];
		key.stencil_fail = impl->rs[RS_STENCILFAIL];
		key.stencil_zfail = impl->rs[RS_STENCILZFAIL];

		ID3D11DepthStencilState * state = impl->depth_cache.Find(key);
		if (state == nullptr) {
			D3D11_DEPTH_STENCIL_DESC desc;
			memset(&desc, 0, sizeof(desc));
			// D3DZB_FALSE is 0, TRUE is 1 and USEW is 2; anything non-zero is the depth
			// test on, and W-buffering is not a thing D3D11 has.
			desc.DepthEnable = key.z_enable != 0 ? TRUE : FALSE;
			desc.DepthWriteMask = key.z_write != 0
				? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
			desc.DepthFunc = To_Comparison(key.z_func);
			desc.StencilEnable = key.stencil_enable != 0 ? TRUE : FALSE;
			desc.StencilReadMask = (UINT8)(key.stencil_mask & 0xff);
			desc.StencilWriteMask = (UINT8)(key.stencil_write_mask & 0xff);
			desc.FrontFace.StencilFunc = To_Comparison(key.stencil_func);
			desc.FrontFace.StencilPassOp = To_Stencil_Op(key.stencil_pass);
			desc.FrontFace.StencilFailOp = To_Stencil_Op(key.stencil_fail);
			desc.FrontFace.StencilDepthFailOp = To_Stencil_Op(key.stencil_zfail);
			// D3D9's single-sided stencil applies the same operations to both faces;
			// two-sided stencil went with the shadow volumes in Phase 4.1.
			desc.BackFace = desc.FrontFace;

			if (FAILED(impl->device->CreateDepthStencilState(&desc, &state))) return;
			impl->depth_cache.Add(key, state);
		}

		// Like the blend factor, the stencil reference is an argument to the bind here
		// and a render state there, so it stays out of the key.
		const unsigned sref = impl->rs[RS_STENCILREF];
		if (state != impl->current_depth_state || sref != impl->current_stencil_ref) {
			impl->context->OMSetDepthStencilState(state, sref);
			impl->current_depth_state = state;
			impl->current_stencil_ref = sref;
			DX8Wrapper_Increment_Call_Count();
		}
	}

	void Apply_Raster_State(GfxD3D11Impl * impl)
	{
		RasterKey key;
		memset(&key, 0, sizeof(key));
		key.cull = impl->rs[RS_CULLMODE];
		key.fill = impl->rs[RS_FILLMODE];
		key.depth_bias = impl->rs[RS_DEPTHBIAS];
		key.slope_bias = impl->rs[RS_SLOPESCALEDEPTHBIAS];
		key.scissor = impl->rs[RS_SCISSORTESTENABLE];
		key.multisample = impl->rs[RS_MULTISAMPLEANTIALIAS];

		ID3D11RasterizerState * state = impl->raster_cache.Find(key);
		if (state == nullptr) {
			D3D11_RASTERIZER_DESC desc;
			memset(&desc, 0, sizeof(desc));
			// D3DFILL_POINT has no D3D11 spelling; wireframe and solid are 2 and 3 in
			// both. Point fill falls to solid, which is what a debug view asking for it
			// would rather have than nothing.
			desc.FillMode = (key.fill == 2) ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
			desc.CullMode = To_Cull(key.cull);
			desc.FrontCounterClockwise = FALSE;
			// D3D9's depth bias is a float in depth-buffer units; D3D11's is an integer
			// count of the depth buffer's smallest representable value. The engine writes
			// the float's bit pattern into the state word, so it is read back as a float
			// and scaled -- 2^24 for the 24-bit depth buffer this game runs with.
			float bias = 0.0f;
			memcpy(&bias, &key.depth_bias, sizeof(bias));
			desc.DepthBias = (INT)(bias * 16777216.0f);
			memcpy(&bias, &key.slope_bias, sizeof(bias));
			desc.SlopeScaledDepthBias = bias;
			desc.DepthBiasClamp = 0.0f;
			// D3DRS_CLIPPING off means "do not clip", which D3D11 spells as depth clip
			// off. The engine leaves it on everywhere; it is read rather than assumed.
			desc.DepthClipEnable = TRUE;
			desc.ScissorEnable = key.scissor != 0 ? TRUE : FALSE;
			desc.MultisampleEnable = key.multisample != 0 ? TRUE : FALSE;
			desc.AntialiasedLineEnable = FALSE;

			if (FAILED(impl->device->CreateRasterizerState(&desc, &state))) return;
#ifdef RTS_DEBUG
			// Every distinct rasterizer description this backend ever builds, once each.
			// Five of these fields have no D3D9 render state behind them, so nothing
			// downstream can correct a wrong default and no census counts them: the only
			// way to know what they hold is to print them. There are a handful of
			// descriptions in a whole run, so this is a handful of lines.
			WWDEBUG_SAY(("D3D11 RASTERIZER: fill %u cull %u ccw %d depthBias %d slope %g "
				"clamp %g depthClip %d scissor %d multisample %d aaLine %d",
				(unsigned)desc.FillMode, (unsigned)desc.CullMode,
				(int)desc.FrontCounterClockwise, (int)desc.DepthBias,
				(double)desc.SlopeScaledDepthBias, (double)desc.DepthBiasClamp,
				(int)desc.DepthClipEnable, (int)desc.ScissorEnable,
				(int)desc.MultisampleEnable, (int)desc.AntialiasedLineEnable));
#endif
			impl->raster_cache.Add(key, state);
		}
		if (state != impl->current_raster_state) {
			impl->context->RSSetState(state);
			impl->current_raster_state = state;
			DX8Wrapper_Increment_Call_Count();
		}
	}

	void Apply_Sampler_State(GfxD3D11Impl * impl, unsigned stage)
	{
		SamplerKey key;
		memset(&key, 0, sizeof(key));
		key.address_u = impl->tss[stage][TSS_ADDRESSU];
		key.address_v = impl->tss[stage][TSS_ADDRESSV];
		key.address_w = impl->tss[stage][TSS_ADDRESSW];
		key.mag = impl->tss[stage][TSS_MAGFILTER];
		key.min = impl->tss[stage][TSS_MINFILTER];
		key.mip = impl->tss[stage][TSS_MIPFILTER];
		key.lod_bias = impl->tss[stage][TSS_MIPMAPLODBIAS];
		key.max_lod = impl->tss[stage][TSS_MAXMIPLEVEL];
		key.max_aniso = impl->tss[stage][TSS_MAXANISOTROPY];
		key.border = impl->tss[stage][TSS_BORDERCOLOR];
		key.compare = impl->tss[stage][TSS_COMPAREFUNC];

#ifdef RTS_DEBUG
		// Which sampler descriptions the frame is actually built out of, and how much of
		// it each one draws. Eleven fields, in SamplerKey's declaration order.
		{
			const unsigned fields[11] = { key.address_u, key.address_v, key.address_w,
				key.mag, key.min, key.mip, key.lod_bias, key.max_lod, key.max_aniso,
				key.border, key.compare };
			int slot = -1;
			for (int i = 0; i < s_sampler_key_count; ++i) {
				if (memcmp(s_sampler_keys[i], fields, sizeof(fields)) == 0) { slot = i; break; }
			}
			if (slot < 0 && s_sampler_key_count < 64) {
				slot = s_sampler_key_count++;
				memcpy(s_sampler_keys[slot], fields, sizeof(fields));
				s_sampler_key_uses[slot] = 0;
			}
			if (slot >= 0) ++s_sampler_key_uses[slot];
		}
#endif

		ID3D11SamplerState * state = impl->sampler_cache.Find(key);
		if (state == nullptr) {
			D3D11_SAMPLER_DESC desc;
			memset(&desc, 0, sizeof(desc));
			desc.Filter = To_Filter(key.mag, key.min, key.mip);
			desc.AddressU = To_Address(key.address_u);
			desc.AddressV = To_Address(key.address_v);
			desc.AddressW = To_Address(key.address_w != 0 ? key.address_w : 1);
			memcpy(&desc.MipLODBias, &key.lod_bias, sizeof(float));
			desc.MaxAnisotropy = key.max_aniso != 0 ? key.max_aniso : 1;
			// A comparison sampler is the same filter with the comparison bit set
			// (D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT is 0x94 = 0x14 | 0x80, and
			// likewise for every other entry including anisotropic). A shader declaring
			// SamplerComparisonState must be bound one of these and nothing else.
			if (key.compare >= 1 && key.compare <= 8) {
				desc.Filter = (D3D11_FILTER)(desc.Filter | 0x80);
				desc.ComparisonFunc = (D3D11_COMPARISON_FUNC)key.compare;
			} else {
				desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
			}
			desc.MinLOD = (float)key.max_lod;
			// D3DTEXF_NONE on the mip filter means no mipmapping at all, which D3D11
			// expresses as a point mip filter clamped to the top level rather than as a
			// filter mode of its own. Leaving MaxLOD open here would sample the mip chain
			// on exactly the draws that asked not to.
			desc.MaxLOD = (key.mip == 0) ? (float)key.max_lod : D3D11_FLOAT32_MAX;
			desc.BorderColor[0] = ((key.border >> 16) & 0xff) / 255.0f;
			desc.BorderColor[1] = ((key.border >>  8) & 0xff) / 255.0f;
			desc.BorderColor[2] = ((key.border >>  0) & 0xff) / 255.0f;
			desc.BorderColor[3] = ((key.border >> 24) & 0xff) / 255.0f;

			if (FAILED(impl->device->CreateSamplerState(&desc, &state))) return;
			impl->sampler_cache.Add(key, state);
		}
		if (state != impl->current_ps_samplers[stage]) {
			impl->context->PSSetSamplers(stage, 1, &state);
			impl->context->VSSetSamplers(stage, 1, &state);
			impl->current_ps_samplers[stage] = state;
			DX8Wrapper_Increment_Call_Count();
		}
		impl->sampler_applied[stage] = true;
	}

	/// The input layout for one (vertex format x vertex shader) pair. The census says
	/// this cache holds about thirty entries on shipped content.
	ID3D11InputLayout * Get_Input_Layout(GfxD3D11Impl * impl)
	{
		LayoutKey key;
		memset(&key, 0, sizeof(key));
		key.fvf = impl->fvf;
		key.vertex_shader = impl->vertex_shader;

		ID3D11InputLayout * layout = impl->layout_cache.Find(key);
		if (layout != nullptr) return layout;

		FvfElement fvf_elements[16];
		unsigned stride = 0;
		const unsigned fvf_count = Decode_Fvf(key.fvf, fvf_elements, 16, stride);

		D3D11_INPUT_ELEMENT_DESC elements[32];
		unsigned n = 0;
		for (unsigned i = 0; i < fvf_count && n < 32; ++i, ++n) {
			memset(&elements[n], 0, sizeof(elements[n]));
			elements[n].SemanticName = fvf_elements[i].semantic;
			elements[n].SemanticIndex = fvf_elements[i].index;
			elements[n].Format = fvf_elements[i].format;
			elements[n].InputSlot = 0;
			elements[n].AlignedByteOffset = fvf_elements[i].offset;
			elements[n].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		}

		// Anything the shader declares and the format does not carry comes off the zero
		// stream. Without this every pair whose shader reads more than its vertex format
		// supplies -- unit_prelit_vs against a format with no normal, ui_vs against one
		// with a single texture coordinate set -- fails CreateInputLayout outright, where
		// D3D9's FVF path silently defaulted the register and drew.
		const D3D11VertexShader * vs = impl->vertex_shader;
		if (vs != nullptr) {
			for (unsigned i = 0; i < vs->inputs.count && n < 32; ++i) {
				const SignatureElement & wanted = vs->inputs.elements[i];
				if (Is_System_Value(wanted.semantic)) continue;
				bool supplied = false;
				for (unsigned j = 0; j < fvf_count; ++j) {
					if (fvf_elements[j].index == wanted.index &&
						_stricmp(fvf_elements[j].semantic, wanted.semantic) == 0) {
						supplied = true;
						break;
					}
				}
				if (supplied) continue;
				memset(&elements[n], 0, sizeof(elements[n]));
				elements[n].SemanticName = wanted.semantic;
				elements[n].SemanticIndex = wanted.index;
				elements[n].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
				elements[n].InputSlot = 1;
				elements[n].AlignedByteOffset = 0;
				elements[n].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
				++n;
			}
		}

		if (n == 0 || vs == nullptr) return nullptr;

		HRESULT hr = impl->device->CreateInputLayout(elements, n, vs->bytecode,
			vs->bytecode_size, &layout);
		if (FAILED(hr)) {
			WWDEBUG_SAY(("D3D11: CreateInputLayout failed (0x%08x) for FVF 0x%x against a "
				"shader declaring %u inputs. Draws on this pair will be dropped.",
				(unsigned)hr, key.fvf, vs->inputs.count));
			layout = nullptr;
		}
		// Cached either way, including the failure: a pair that cannot be made once
		// cannot be made ever, and retrying it per draw would log it 800000 times.
		impl->layout_cache.Add(key, layout);
		return layout;
	}

	void Upload_Constants(GfxD3D11Impl * impl)
	{
#ifdef RTS_DEBUG
		PROFILE_D3D11_SCOPE(t_upload_constants);
#endif
		if (impl->vs_constants_dirty && impl->vs_constant_buffer != nullptr) {
#ifdef RTS_DEBUG
			++s_d3d11_prof.n_upload_vs;
#endif
			D3D11_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(impl->context->Map(impl->vs_constant_buffer, 0,
					D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
				memcpy(mapped.pData, impl->vs_constants, sizeof(impl->vs_constants));
				impl->context->Unmap(impl->vs_constant_buffer, 0);
			}
			impl->vs_constants_dirty = false;
		}
		if (impl->ps_constants_dirty && impl->ps_constant_buffer != nullptr) {
#ifdef RTS_DEBUG
			++s_d3d11_prof.n_upload_ps;
#endif
			D3D11_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(impl->context->Map(impl->ps_constant_buffer, 0,
					D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
				memcpy(mapped.pData, impl->ps_constants, sizeof(impl->ps_constants));
				impl->context->Unmap(impl->ps_constant_buffer, 0);
			}
			impl->ps_constants_dirty = false;
		}
		if (impl->frame_constants_dirty && impl->frame_constant_buffer != nullptr) {
#ifdef RTS_DEBUG
			++s_d3d11_prof.n_upload_frame;
#endif
			D3D11_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(impl->context->Map(impl->frame_constant_buffer, 0,
					D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
				memcpy(mapped.pData, impl->frame_constants, sizeof(impl->frame_constants));
				impl->context->Unmap(impl->frame_constant_buffer, 0);
			}
			impl->frame_constants_dirty = false;
		}
	}

#ifdef RTS_DEBUG
	static const D3D11VertexShader * s_last_link_vs = (const D3D11VertexShader *)(intptr_t)-1;
	static const D3D11PixelShader * s_last_link_ps = (const D3D11PixelShader *)(intptr_t)-1;
	static bool s_last_link_result = true;

	/// Whether the bound pair can be linked: every non-system input the pixel shader
	/// declares must be written by the vertex shader *into the same register*.
	///
	/// This is a measurement and not a gate. D3D11 refuses the draw itself, silently, so
	/// the only thing counting it changes is whether anyone can see it happening -- and it
	/// is the single largest reason this backend's frame is not the game's frame yet. The
	/// pairs it fires on are listed by shader_signature_check.py, which reads
	/// the shipped blobs and needs no run at all.
	bool Signatures_Link(const GfxD3D11Impl * impl)
	{
		const D3D11VertexShader * vs = impl->vertex_shader;
		const D3D11PixelShader * ps = impl->pixel_shader;
		if (vs == nullptr || ps == nullptr) return true;
		if (vs == s_last_link_vs && ps == s_last_link_ps) return s_last_link_result;

		s_last_link_vs = vs;
		s_last_link_ps = ps;
		s_last_link_result = true;

		for (unsigned i = 0; i < ps->inputs.count; ++i) {
			const SignatureElement & in = ps->inputs.elements[i];
			if (Is_System_Value(in.semantic)) continue;
			bool matched = false;
			for (unsigned j = 0; j < vs->outputs.count; ++j) {
				const SignatureElement & out = vs->outputs.elements[j];
				if (out.index != in.index) continue;
				if (_stricmp(out.semantic, in.semantic) != 0) continue;
				matched = (out.reg == in.reg);
				break;
			}
			if (!matched) {
				s_last_link_result = false;
				return false;
			}
		}
		return true;
	}
#endif

	/// Everything a draw needs, in one place. False means the draw cannot be made and
	/// must be dropped -- which is a counted outcome and not an error.
	bool Prepare_Draw(GfxD3D11Impl * impl, unsigned primitive_type)
	{
#ifdef RTS_DEBUG
		PROFILE_D3D11_SCOPE(t_prepare_draw);
		++s_draws;
		if (!Signatures_Link(impl)) ++s_dropped_signature_mismatch;
#endif
		if (impl->vertex_shader == nullptr) {
			// The draw the VERTEX LAYOUT CENSUS calls "reached a device with no vertex
			// shader". D3D11 cannot make one: there is no fixed-function pipeline behind
			// the vertex format to run it.
			ABSORB(s_dropped_no_vertex_shader);
			return false;
		}

		const D3D11_PRIMITIVE_TOPOLOGY topology = To_Topology(primitive_type);
		if (topology == D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED) {
			ABSORB(s_dropped_trianglefan);
			return false;
		}

		ID3D11InputLayout * layout;
		{
#ifdef RTS_DEBUG
			PROFILE_D3D11_SCOPE(t_input_layout);
#endif
			layout = Get_Input_Layout(impl);
			if (layout == nullptr) {
				ABSORB(s_dropped_no_input_layout);
				return false;
			}
			if (layout != impl->current_layout) {
				impl->context->IASetInputLayout(layout);
				impl->current_layout = layout;
			}
		}
		if (topology != impl->current_topology) {
			impl->context->IASetPrimitiveTopology(topology);
			impl->current_topology = topology;
		}

		ID3D11Buffer * b0 = (impl->stream0 != nullptr) ? impl->stream0->buffer : nullptr;
		if (b0 != impl->current_vb0 || impl->stream0_stride != impl->current_stride0) {
			ID3D11Buffer * buffers[2];
			buffers[0] = b0;
			buffers[1] = impl->zero_stream;
			// Stride zero on slot 1 makes every vertex read the same sixteen bytes, which is
			// the whole trick: one (0,0,0,1) supplies every element the vertex format does
			// not carry, at the value D3D9 defaulted an unwritten input register to.
			const UINT strides[2] = { impl->stream0_stride, 0 };
			const UINT offsets[2] = { 0, 0 };
			impl->context->IASetVertexBuffers(0, 2, buffers, strides, offsets);
			impl->current_vb0 = b0;
			impl->current_stride0 = impl->stream0_stride;
		}

		{
#ifdef RTS_DEBUG
			PROFILE_D3D11_SCOPE(t_apply_states);
#endif
			if (impl->blend_dirty)  { Apply_Blend_State(impl);  impl->blend_dirty = false; }
			if (impl->depth_dirty)  { Apply_Depth_State(impl);  impl->depth_dirty = false; }
			if (impl->raster_dirty) { Apply_Raster_State(impl); impl->raster_dirty = false; }
			for (unsigned s = 0; s < GFX_MAX_STAGES; ++s) {
				if (impl->sampler_dirty[s]) {
					Apply_Sampler_State(impl, s);
					impl->sampler_dirty[s] = false;
				}
			}
		}

		// Anything the render-target hazard took out of a slot goes back here, now that
		// the resource may have stopped being a target. The common case is a mask of zero
		// and one branch.
		if (impl->srv_unbound_mask != 0) Restore_Unbound_Textures(impl);
#ifdef RTS_DEBUG
		// What survives the restore is a texture that really is the current target, which
		// no shader may read under either API -- exposure, not a defect. The rescued mask
		// is the defect: without the fix those slots would still be NULL here.
		if (impl->srv_unbound_mask != 0) {
			++s_draws_target_conflict;
			// Narrowed by what the shader on this draw can actually fetch. A conflicting
			// slot the pixel shader declares no texture in is a binding the shader never
			// looks at, and the two APIs' disagreement about what a NULL slot returns
			// cannot reach a pixel.
			if (impl->pixel_shader == nullptr) ++s_draws_target_conflict_noshader;
			else if ((impl->srv_unbound_mask & impl->pixel_shader->texture_mask) != 0)
				++s_draws_target_conflict_read;
		}
		if (impl->srv_rescued_mask != 0) ++s_draws_rescued;
#endif
#ifdef RTS_DEBUG
		// The sampler census, taken per *stage that has a texture on it* rather than per
		// draw, because a draw sampling four textures is four sampling decisions.
		for (unsigned s = 0; s < GFX_MAX_STAGES; ++s) {
			if (impl->textures[s] == nullptr) continue;
			++s_sampler_textured_stages;
			if (!impl->sampler_applied[s]) { ++s_sampler_stage_no_sampler; continue; }
			const unsigned mag = impl->tss[s][TSS_MAGFILTER];
			const unsigned min = impl->tss[s][TSS_MINFILTER];
			if (mag == 3 || min == 3) { ++s_sampler_stage_aniso; continue; }
			switch (impl->tss[s][TSS_MIPFILTER]) {
			case 2:  ++s_sampler_stage_mip_linear; break;
			case 1:  ++s_sampler_stage_mip_point;  break;
			default: ++s_sampler_stage_mip_none;   break;
			}
		}
#endif

		Upload_Constants(impl);
		DX8Wrapper_Increment_Call_Count();
		return true;
	}
}

// ---------------------------------------------------------------------------
// Draws
// ---------------------------------------------------------------------------

void GfxDeviceD3D11::Draw_Indexed(unsigned primitive_type, int base_vertex_index,
	unsigned min_vertex_index, unsigned vertex_count,
	unsigned start_index, unsigned primitive_count)
{
	TRACE("Draw_Indexed");
	(void)min_vertex_index;
	(void)vertex_count;
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_draw_indexed);
	++s_d3d11_prof.n_draw_indexed;
	++s_current_pass_draws;
#endif
	if (!Prepare_Draw(m_impl, primitive_type)) return;
	{
#ifdef RTS_DEBUG
		PROFILE_D3D11_SCOPE(t_raw_draw_call);
#endif
		m_impl->context->DrawIndexed(Vertices_For(primitive_type, primitive_count),
			start_index, base_vertex_index);
	}
}

void GfxDeviceD3D11::Draw(unsigned primitive_type, unsigned start_vertex,
	unsigned primitive_count)
{
	TRACE("Draw");
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_draw);
	++s_d3d11_prof.n_draw;
	++s_current_pass_draws;
#endif
	if (!Prepare_Draw(m_impl, primitive_type)) return;
	{
#ifdef RTS_DEBUG
		PROFILE_D3D11_SCOPE(t_raw_draw_call);
#endif
		m_impl->context->Draw(Vertices_For(primitive_type, primitive_count), start_vertex);
	}
}

void GfxDeviceD3D11::Draw_Up(unsigned primitive_type, unsigned primitive_count,
	const void * vertex_data, unsigned vertex_stride)
{
	TRACE("Draw_Up");
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_draw_up);
	++s_d3d11_prof.n_draw_up;
	++s_current_pass_draws;
#endif
	// The twenty lines Phase 4.0 left one call site for: map a dynamic ring, copy, draw.
	//
	// Phase 5's prompt expected something free to come with them: under D3D9 this is
	// DrawPrimitiveUP, which nulls stream 0 behind the caller's back while the engine's
	// base-vertex global stays where it was, and the DIRECT-DEVICE DRAWS census reports
	// the repair for that as "after screenQuad x559 wrong base 526". Staging into a real
	// buffer does remove that cause -- and the counter still reads 526 here, measured.
	//
	// Because it was never measuring this. It compares the wrapper's own tracked bindings
	// against what Get_Vertex_Stream reports, and what puts them out of step is
	// screenQuad binding its own stream directly, which happens on either backend and has
	// nothing to do with how the draw is then submitted. A prediction worth writing down
	// as wrong, since the alternative is a later session reading 526 as a regression.
	if (vertex_data == nullptr || vertex_stride == 0) return;

	const unsigned vertices = Vertices_For(primitive_type, primitive_count);
	const unsigned bytes = vertices * vertex_stride;
	if (bytes == 0) return;

	if (m_impl->up_buffer == nullptr || m_impl->up_capacity < bytes) {
		if (m_impl->up_buffer != nullptr) m_impl->up_buffer->Release();
		m_impl->up_buffer = nullptr;
		unsigned capacity = 64 * 1024;
		while (capacity < bytes) capacity *= 2;

		D3D11_BUFFER_DESC desc;
		memset(&desc, 0, sizeof(desc));
		desc.ByteWidth = capacity;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(m_impl->device->CreateBuffer(&desc, nullptr, &m_impl->up_buffer))) {
			m_impl->up_buffer = nullptr;
			return;
		}
		m_impl->up_capacity = capacity;
		m_impl->up_offset = 0;
	}

	// Append until the ring is full, then discard and start again. Appending is what lets
	// a frame's worth of these coexist without each one throwing away the last.
	D3D11_MAP map = D3D11_MAP_WRITE_NO_OVERWRITE;
	if (m_impl->up_offset + bytes > m_impl->up_capacity) {
		map = D3D11_MAP_WRITE_DISCARD;
		m_impl->up_offset = 0;
	}

	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(m_impl->context->Map(m_impl->up_buffer, 0, map, 0, &mapped))) return;
	memcpy((unsigned char *)mapped.pData + m_impl->up_offset, vertex_data, bytes);
	m_impl->context->Unmap(m_impl->up_buffer, 0);

	const unsigned first_vertex = m_impl->up_offset / vertex_stride;
	m_impl->up_offset += bytes;
	// Keep the ring's next write aligned to a whole vertex, so that first_vertex above
	// stays exact for the draw after this one.
	m_impl->up_offset = ((m_impl->up_offset + vertex_stride - 1) / vertex_stride) * vertex_stride;

	// The ring stands in for stream 0 for this draw only. Prepare_Draw binds whatever
	// Set_Vertex_Stream last recorded, so it is swapped in around the call and back after.
	D3D11Buffer * saved_stream = m_impl->stream0;
	const unsigned saved_stride = m_impl->stream0_stride;

	D3D11Buffer ring;
	memset(&ring, 0, sizeof(ring));
	ring.buffer = m_impl->up_buffer;
	m_impl->stream0 = &ring;
	m_impl->stream0_stride = vertex_stride;

	if (Prepare_Draw(m_impl, primitive_type)) {
		m_impl->context->Draw(vertices, first_vertex);
	}

	m_impl->stream0 = saved_stream;
	m_impl->stream0_stride = saved_stride;
}

// ---------------------------------------------------------------------------
// Targets
// ---------------------------------------------------------------------------

bool GfxDeviceD3D11::Set_Render_Target(GfxSurface * color, GfxSurface * depth)
{
	TRACE("Set_Render_Target");
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_set_rt);
	++s_d3d11_prof.n_set_rt;
	LARGE_INTEGER now_rt;
	QueryPerformanceCounter(&now_rt);
	Flush_Pass_Timing(now_rt);
#endif
	D3D11Surface * rt = (D3D11Surface *)color;
	D3D11Surface * ds = (D3D11Surface *)depth;
	if (rt == nullptr) rt = m_impl->back_buffer;

	// Before OMSetRenderTargets, not after: left to itself D3D11 nulls the conflicting
	// shader-resource slots and tells only the debug layer, and the wrapper's redundancy
	// check then never sends the texture again.
	Unbind_Conflicting_Textures(m_impl, rt, ds);

#ifdef RTS_DEBUG
	if (rt == nullptr || rt == m_impl->back_buffer) {
		s_current_pass = PASS_BACKBUFFER;
	} else if (ds != nullptr && ds->width == ds->height && (ds->width == 1024 || ds->width == 2048 || ds->width == 4096)) {
		s_current_pass = PASS_SHADOW;
	} else if (m_impl->back_buffer != nullptr && rt->width == m_impl->back_buffer->width && rt->height == m_impl->back_buffer->height) {
		s_current_pass = PASS_DEPTHPREPASS;
	} else {
		s_current_pass = PASS_OTHER;
	}
#endif

	m_impl->current_rt = rt;
	m_impl->current_ds = ds;

	ID3D11Resource * rt_res = (rt != nullptr) ? rt->texture : nullptr;
	ID3D11Resource * ds_res = (ds != nullptr) ? ds->texture : nullptr;
	for (unsigned s = 0; s < GFX_MAX_STAGES; ++s) {
		if (m_impl->textures[s] != nullptr) {
			ID3D11Resource * tex_res = m_impl->textures[s]->resource;
			if ((rt_res != nullptr && tex_res == rt_res) ||
			    (ds_res != nullptr && tex_res == ds_res)) {
				m_impl->textures[s] = nullptr;
				ID3D11ShaderResourceView * null_srv = nullptr;
				m_impl->context->PSSetShaderResources(s, 1, &null_srv);
				m_impl->context->VSSetShaderResources(s, 1, &null_srv);
			}
		}
	}

	ID3D11RenderTargetView * rtv = Get_RTV(m_impl->device, rt);
	ID3D11DepthStencilView * dsv = Get_DSV(m_impl->device, ds);
	m_impl->context->OMSetRenderTargets(1, &rtv, dsv);
	DX8Wrapper_Increment_Call_Count();

	// D3D9 resets the viewport to the whole of a newly bound render target and the engine
	// relies on that -- several passes bind a target and draw without setting one. D3D11
	// keeps whatever viewport was last set, which on a smaller target means drawing off
	// the end of it and on a larger one means drawing into a corner.
	if (rt != nullptr) {
		GfxViewport vp;
		vp.X = 0;
		vp.Y = 0;
		vp.Width = rt->width;
		vp.Height = rt->height;
		vp.MinZ = 0.0f;
		vp.MaxZ = 1.0f;
		Set_Viewport(vp);
	}
	return rtv != nullptr;
}

GfxSurface * GfxDeviceD3D11::Get_Render_Target(unsigned index)
{
	TRACE("Get_Render_Target");
	if (index != 0) return nullptr;
	D3D11Surface * s = (m_impl->current_rt != nullptr) ? m_impl->current_rt : m_impl->back_buffer;
	if (s == nullptr) return nullptr;
	++s->refs;					// the caller releases it, as it always has
	return (GfxSurface *)s;
}

GfxSurface * GfxDeviceD3D11::Get_Depth_Target()
{
	TRACE("Get_Depth_Target");
	D3D11Surface * s = (m_impl->current_ds != nullptr) ? m_impl->current_ds : m_impl->depth_buffer;
	if (s == nullptr) return nullptr;
	++s->refs;
	return (GfxSurface *)s;
}

GfxSurface * GfxDeviceD3D11::Get_Back_Buffer(unsigned index)
{
	TRACE("Get_Back_Buffer");
	if (index != 0 || m_impl->back_buffer == nullptr) return nullptr;
	++m_impl->back_buffer->refs;
	return (GfxSurface *)m_impl->back_buffer;
}

void GfxDeviceD3D11::Set_Viewport(const GfxViewport & viewport)
{
	TRACE("Set_Viewport");
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_set_vp);
	++s_d3d11_prof.n_set_vp;
#endif
	if (memcmp(&m_impl->viewport, &viewport, sizeof(viewport)) == 0) return;
	m_impl->viewport = viewport;
	D3D11_VIEWPORT vp;
	vp.TopLeftX = (float)viewport.X;
	vp.TopLeftY = (float)viewport.Y;
	vp.Width = (float)viewport.Width;
	vp.Height = (float)viewport.Height;
	vp.MinDepth = viewport.MinZ;
	vp.MaxDepth = viewport.MaxZ;
	m_impl->context->RSSetViewports(1, &vp);
	DX8Wrapper_Increment_Call_Count();
}

bool GfxDeviceD3D11::Get_Viewport(GfxViewport & viewport)
{
	TRACE("Get_Viewport");
	viewport = m_impl->viewport;
	return true;
}

// ---------------------------------------------------------------------------
// Buffers
//
// Every buffer here is D3D11_USAGE_DEFAULT with a system-memory shadow; see the note on
// D3D11Buffer for why a first backend does it this way rather than splitting static from
// dynamic.
// ---------------------------------------------------------------------------

namespace
{
	D3D11Buffer * Create_Buffer(GfxD3D11Impl * impl, unsigned size, UINT bind, unsigned usage)
	{
		if (size == 0) return nullptr;

		const bool is_dyn = (usage & GFX_USAGE_DYNAMIC) != 0;

		D3D11_BUFFER_DESC desc;
		memset(&desc, 0, sizeof(desc));
		desc.ByteWidth = size;
		desc.Usage = is_dyn ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
		desc.BindFlags = bind;
		desc.CPUAccessFlags = is_dyn ? D3D11_CPU_ACCESS_WRITE : 0;

		ID3D11Buffer * buffer = nullptr;
		if (FAILED(impl->device->CreateBuffer(&desc, nullptr, &buffer))) return nullptr;

		D3D11Buffer * b = new D3D11Buffer;
		memset(b, 0, sizeof(*b));
		b->refs = 1;
		b->buffer = buffer;
		b->size = size;
		b->usage = usage;
		b->is_dynamic = is_dyn;
		if (!is_dyn) {
			b->shadow = new unsigned char[size];
			memset(b->shadow, 0, size);
		}
#ifdef RTS_DEBUG
		Note_Buffer_Made(b, size, usage,
			(bind == D3D11_BIND_INDEX_BUFFER) ? "index" : "vertex");
#endif
		return b;
	}

	void Free_Buffer(D3D11Buffer * b)
	{
		if (b == nullptr) return;
#ifdef RTS_DEBUG
		Note_Buffer_Freed(b);
#endif
		// The views before the resource they view. Null on a vertex or index buffer, which
		// is what the release ladder looked like before shader buffers existed.
		if (b->srv != nullptr) b->srv->Release();
		if (b->uav != nullptr) b->uav->Release();
		if (b->readback != nullptr) b->readback->Release();
		if (b->buffer != nullptr) b->buffer->Release();
		delete [] b->shadow;
		delete b;
	}

	bool Map_Buffer(GfxD3D11Impl * impl, D3D11Buffer * b, unsigned offset, unsigned size, GfxMapMode mode, void ** data)
	{
#ifdef RTS_DEBUG
		PROFILE_D3D11_SCOPE(t_map_buffer);
		++s_d3d11_prof.n_map_buffer;
#endif
		if (b == nullptr || data == nullptr) return false;
		if (offset > b->size) return false;
		// A size of zero means "the rest of it", which is what D3D9's Lock means by it and
		// what several callers rely on.
		if (size == 0 || offset + size > b->size) size = b->size - offset;

		if (b->is_dynamic) {
			D3D11_MAP map_type = D3D11_MAP_WRITE_NO_OVERWRITE;
			if (mode == GFX_MAP_WRITE_DISCARD || offset == 0) {
				map_type = D3D11_MAP_WRITE_DISCARD;
			}
			D3D11_MAPPED_SUBRESOURCE ms;
			if (FAILED(impl->context->Map(b->buffer, 0, map_type, 0, &ms))) {
				return false;
			}
			b->map_offset = offset;
			b->map_size = size;
			b->mapped = true;
			*data = (unsigned char *)ms.pData + offset;
			return true;
		}

		if (b->shadow == nullptr) return false;
		b->map_offset = offset;
		b->map_size = size;
		b->mapped = true;
		*data = b->shadow + offset;
		return true;
	}

	void Unmap_Buffer(GfxD3D11Impl * impl, D3D11Buffer * b)
	{
#ifdef RTS_DEBUG
		PROFILE_D3D11_SCOPE(t_unmap_buffer);
		++s_d3d11_prof.n_unmap_buffer;
#endif
		if (b == nullptr || !b->mapped) return;
		b->mapped = false;

		if (b->is_dynamic) {
			impl->context->Unmap(b->buffer, 0);
			return;
		}

		if (b->map_size == 0) return;

		D3D11_BOX box;
		box.left = b->map_offset;
		box.right = b->map_offset + b->map_size;
		box.top = 0;
		box.bottom = 1;
		box.front = 0;
		box.back = 1;
		impl->context->UpdateSubresource(b->buffer, 0, &box,
			b->shadow + b->map_offset, 0, 0);
	}
}

GfxVertexBuffer * GfxDeviceD3D11::Create_Vertex_Buffer(unsigned size_in_bytes, unsigned fvf,
	unsigned usage)
{
	TRACE("Create_Vertex_Buffer");
	D3D11Buffer * b = Create_Buffer(m_impl, size_in_bytes, D3D11_BIND_VERTEX_BUFFER, usage);
	if (b != nullptr) b->fvf = fvf;
	return (GfxVertexBuffer *)b;
}

GfxIndexBuffer * GfxDeviceD3D11::Create_Index_Buffer(unsigned index_count, unsigned usage)
{
	TRACE("Create_Index_Buffer");
	return (GfxIndexBuffer *)Create_Buffer(m_impl,
		(unsigned)sizeof(unsigned short) * index_count, D3D11_BIND_INDEX_BUFFER, usage);
}

void GfxDeviceD3D11::Release_Vertex_Buffer(GfxVertexBuffer * buffer)
{
	TRACE("Release_Vertex_Buffer");
	D3D11Buffer * b = (D3D11Buffer *)buffer;
	if (b == nullptr) return;
	if (m_impl->stream0 == b) m_impl->stream0 = nullptr;
	if (--b->refs <= 0) Free_Buffer(b);
}

void GfxDeviceD3D11::Release_Index_Buffer(GfxIndexBuffer * buffer)
{
	TRACE("Release_Index_Buffer");
	D3D11Buffer * b = (D3D11Buffer *)buffer;
	if (b == nullptr) return;
	if (m_impl->index_buffer == b) m_impl->index_buffer = nullptr;
	if (--b->refs <= 0) Free_Buffer(b);
}

bool GfxDeviceD3D11::Map_Vertex_Buffer(GfxVertexBuffer * buffer, unsigned offset_in_bytes,
	unsigned size_in_bytes, GfxMapMode mode, void ** data)
{
	TRACE("Map_Vertex_Buffer");
	// Qualified, here and in the three below: GfxDeviceD3D11 now has its own Map_Buffer and
	// Unmap_Buffer -- the shader-buffer pair -- and an unqualified call from inside the
	// class finds those members and stops looking, whatever their arity.
	return ::Map_Buffer(m_impl, (D3D11Buffer *)buffer, offset_in_bytes, size_in_bytes, mode, data);
}

void GfxDeviceD3D11::Unmap_Vertex_Buffer(GfxVertexBuffer * buffer)
{
	TRACE("Unmap_Vertex_Buffer");
	::Unmap_Buffer(m_impl, (D3D11Buffer *)buffer);
}

bool GfxDeviceD3D11::Map_Index_Buffer(GfxIndexBuffer * buffer, unsigned offset_in_bytes,
	unsigned size_in_bytes, GfxMapMode mode, void ** data)
{
	TRACE("Map_Index_Buffer");
	return ::Map_Buffer(m_impl, (D3D11Buffer *)buffer, offset_in_bytes, size_in_bytes, mode, data);
}

void GfxDeviceD3D11::Unmap_Index_Buffer(GfxIndexBuffer * buffer)
{
	TRACE("Unmap_Index_Buffer");
	::Unmap_Buffer(m_impl, (D3D11Buffer *)buffer);
}

// ---------------------------------------------------------------------------
// Compute, and the buffers it reads and writes
//
// THE SRV/UAV HAZARD, which is the whole reason this section is longer than the four
// D3D11 calls it wraps.
//
// D3D11 will not have one resource bound for reading and for writing at the same time,
// and it does not say so: CSSetUnorderedAccessViews nulls any shader-resource slot
// holding the same resource, and CSSetShaderResources / PSSetShaderResources null the
// unordered-access slot, and both report it to the debug layer and to nothing else. A
// null buffer SRV does not fault -- it reads as **zero**, for every element, forever.
//
// That is the same shape as the render-target hazard above and it is worse in one
// respect. A texture stage that reads zero draws a black patch somebody notices. A
// cluster grid that reads zero says "no lights are near this pixel", which is a perfectly
// ordinary thing for a cluster grid to say, and the frame it produces is the frame you
// would get with the feature working and no lights in the scene. There is no picture to
// look at that distinguishes them; only the counters below and the self-test do.
//
// So this backend nulls the conflicting binding itself, before D3D11 does it silently,
// and counts it. The difference from the render-target rule is deliberate: that one
// *restores* the texture afterwards, because the wrapper's redundancy check would
// otherwise never re-send a texture it believes is still bound. Here the record is
// cleared along with the device binding, so the next Set_ of that slot is not filtered
// out as redundant and simply re-binds. That works because the caller of these is one
// piece of clustered-lighting code that re-binds what it needs each frame, where
// Set_Texture's callers are the whole engine.
// ---------------------------------------------------------------------------

namespace
{
	/// Take a buffer out of every slot it is bound in for reading, on either stage.
	/// Called just before it is bound for writing.
	void Unbind_Buffer_From_Read_Slots(GfxD3D11Impl * impl, D3D11Buffer * b)
	{
		if (b == nullptr) return;
		ID3D11ShaderResourceView * none = nullptr;
		for (unsigned i = 0; i < GFX_COMPUTE_BUFFER_SLOTS; ++i) {
			if (impl->cs_buffers[i] != b) continue;
			impl->context->CSSetShaderResources(i, 1, &none);
			impl->cs_buffers[i] = nullptr;
			ABSORB(s_buffer_srv_forced_unbound);
		}
		for (unsigned i = 0; i < GFX_PIXEL_BUFFER_SLOTS; ++i) {
			if (impl->ps_buffers[i] != b) continue;
			impl->context->PSSetShaderResources(GFX_FIRST_PIXEL_BUFFER_SLOT + i, 1, &none);
			impl->ps_buffers[i] = nullptr;
			ABSORB(s_buffer_srv_forced_unbound);
		}
	}

	/// ...and the other direction: out of every write slot, before it is bound for reading.
	void Unbind_Buffer_From_Write_Slots(GfxD3D11Impl * impl, D3D11Buffer * b)
	{
		if (b == nullptr) return;
		ID3D11UnorderedAccessView * none = nullptr;
		// -1 means "leave the append/consume counter alone", which is what every caller
		// that is not using one passes. These buffers have none.
		const UINT keep_counter = (UINT)-1;
		for (unsigned i = 0; i < GFX_COMPUTE_RW_SLOTS; ++i) {
			if (impl->cs_rw_buffers[i] != b) continue;
			impl->context->CSSetUnorderedAccessViews(i, 1, &none, &keep_counter);
			impl->cs_rw_buffers[i] = nullptr;
			ABSORB(s_buffer_uav_forced_unbound);
		}
	}

	/// Every slot a buffer about to be destroyed is bound in, on the way out. The same
	/// service Release_Texture performs for a texture, and for the same reason: a released
	/// resource left in a slot is a dangling binding the next draw or dispatch uses.
	void Unbind_Buffer_Everywhere(GfxD3D11Impl * impl, D3D11Buffer * b)
	{
		Unbind_Buffer_From_Read_Slots(impl, b);
		Unbind_Buffer_From_Write_Slots(impl, b);
	}

	void Unbind_Texture_From_Read_Slots(GfxD3D11Impl * impl, D3D11Texture * t)
	{
		if (t == nullptr) return;
		ID3D11ShaderResourceView * none = nullptr;
		for (unsigned i = 0; i < GFX_COMPUTE_BUFFER_SLOTS; ++i) {
			if (impl->cs_textures[i] != t) continue;
			impl->context->CSSetShaderResources(i, 1, &none);
			impl->cs_textures[i] = nullptr;
		}
		for (unsigned s = 0; s < GFX_MAX_STAGES; ++s) {
			if (impl->textures[s] != t) continue;
			impl->textures[s] = nullptr;
			impl->srv_unbound_mask &= ~(1u << s);
			impl->srv_rescued_mask &= ~(1u << s);
			impl->context->PSSetShaderResources(s, 1, &none);
			impl->context->VSSetShaderResources(s, 1, &none);
		}
	}

	void Unbind_Texture_From_Write_Slots(GfxD3D11Impl * impl, D3D11Texture * t)
	{
		if (t == nullptr) return;
		ID3D11UnorderedAccessView * none = nullptr;
		const UINT keep_counter = (UINT)-1;
		for (unsigned i = 0; i < GFX_COMPUTE_RW_SLOTS; ++i) {
			if (impl->cs_rw_textures[i] != t) continue;
			impl->context->CSSetUnorderedAccessViews(i, 1, &none, &keep_counter);
			impl->cs_rw_textures[i] = nullptr;
		}
	}

	void Unbind_Texture_Everywhere(GfxD3D11Impl * impl, D3D11Texture * t)
	{
		Unbind_Texture_From_Read_Slots(impl, t);
		Unbind_Texture_From_Write_Slots(impl, t);
	}
}

GfxShaderHandle GfxDeviceD3D11::Create_Compute_Shader(const void * bytecode, unsigned size)
{
	TRACE("Create_Compute_Shader");
	if (bytecode == nullptr || size == 0) return 0;

	D3D11ComputeShader * cs = new D3D11ComputeShader;
#ifdef RTS_DEBUG
	++s_csMade;
#endif
	memset(cs, 0, sizeof(*cs));
	if (FAILED(m_impl->device->CreateComputeShader(bytecode, size, nullptr, &cs->shader))) {
#ifdef RTS_DEBUG
		++s_csFreed;
#endif
		delete cs;
		return 0;
	}
	return (GfxShaderHandle)cs;
}

void GfxDeviceD3D11::Release_Compute_Shader(GfxShaderHandle shader)
{
	TRACE("Release_Compute_Shader");
	if (shader == 0) return;
	D3D11ComputeShader * cs = (D3D11ComputeShader *)shader;
	if (m_impl->compute_shader == cs) m_impl->compute_shader = nullptr;
	if (cs->shader != nullptr) cs->shader->Release();
#ifdef RTS_DEBUG
	++s_csFreed;
#endif
	delete cs;
}

void GfxDeviceD3D11::Set_Compute_Shader(GfxShaderHandle shader)
{
	TRACE("Set_Compute_Shader");
	// No FVF escape hatch here, unlike Set_Vertex_Shader: there has never been a
	// fixed-function compute stage for a small handle to mean.
	D3D11ComputeShader * cs = (D3D11ComputeShader *)shader;
	if (m_impl->compute_shader == cs) return;
	m_impl->compute_shader = cs;
	m_impl->context->CSSetShader(cs != nullptr ? cs->shader : nullptr, nullptr, 0);
	DX8Wrapper_Increment_Call_Count();
}

void GfxDeviceD3D11::Dispatch(unsigned x, unsigned y, unsigned z)
{
	TRACE("Dispatch");
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_dispatch);
	++s_d3d11_prof.n_dispatch;
#endif
	// Nothing like Prepare_Draw runs first, and that is not an omission. Everything
	// Prepare_Draw materialises -- blend, depth, raster, samplers, the input layout -- is
	// state for a rasteriser this has none of. A dispatch's whole state is the shader, its
	// buffers and its constants, and all three are set by their own calls.
	//
	// ...except that Set_Frame_Constants only STAGES b1; the copy into the constant buffer
	// happens in Upload_Constants, which until C6 was reached from Prepare_Draw and from
	// one debug helper and from nowhere else. A dispatch issued before the frame's first
	// draw therefore read whatever b1 held at the end of the previous frame -- and the
	// cluster builder is issued before the shadow-map pass, so "before the first draw" is
	// where it lives. It happened to work only because the shadow pass draws first, which
	// is incidental and not a property anything guarantees.
	//
	// Flushed here rather than in the caller, because it is not the caller's business: the
	// contract "the constants you set are the constants the shader sees" is the same one
	// Prepare_Draw honours for a draw, and every future compute caller wants it too. It
	// costs one memcmp-guarded flag test on a dispatch that changed nothing.
	Upload_Constants(m_impl);
	if (m_impl->compute_shader == nullptr) return;
	if (x == 0 || y == 0 || z == 0) return;
	m_impl->context->Dispatch(x, y, z);
	DX8Wrapper_Increment_Call_Count();
}

GfxBuffer * GfxDeviceD3D11::Create_Structured_Buffer(unsigned stride, unsigned count,
	unsigned usage)
{
	TRACE("Create_Structured_Buffer");
	if (stride == 0 || count == 0) return nullptr;

	const bool wants_uav = (usage & GFX_BUFFER_UAV) != 0;
	const bool wants_dynamic = (usage & GFX_BUFFER_DYNAMIC) != 0;
	const bool uint_view = (usage & GFX_BUFFER_UINT) != 0;

	// Said rather than silently dropped, because the two ways this can be asked for
	// wrongly both produce a working-looking buffer that is not what was wanted.
	if (wants_uav && wants_dynamic) {
		WWDEBUG_SAY(("D3D11: a buffer cannot be both GFX_BUFFER_DYNAMIC and GFX_BUFFER_UAV "
			"-- D3D11 has no usage that is CPU-written every frame and GPU-written. Ask for "
			"one or the other; they are two buffers."));
		return nullptr;
	}
	if (uint_view && stride != 4) {
		WWDEBUG_SAY(("D3D11: GFX_BUFFER_UINT asks for a typed view of 32-bit words and the "
			"stride is %u. That view has no way to describe an element of another size.",
			stride));
		return nullptr;
	}

	D3D11_BUFFER_DESC desc;
	memset(&desc, 0, sizeof(desc));
	desc.ByteWidth = stride * count;
	desc.Usage = wants_dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE
		| (wants_uav ? D3D11_BIND_UNORDERED_ACCESS : 0);
	desc.CPUAccessFlags = wants_dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
	// STRUCTURED and a stride, or neither. A typed R32_UINT view is the other kind of
	// buffer entirely and D3D11 refuses a resource that claims to be both.
	if (!uint_view) {
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = stride;
	}

	ID3D11Buffer * buffer = nullptr;
	if (FAILED(m_impl->device->CreateBuffer(&desc, nullptr, &buffer))) return nullptr;

	D3D11Buffer * b = new D3D11Buffer;
	memset(b, 0, sizeof(*b));
	b->refs = 1;
	b->buffer = buffer;
	b->size = desc.ByteWidth;
	b->usage = usage;
	b->is_dynamic = wants_dynamic;
	b->stride = stride;
	b->count = count;
	b->uint_view = uint_view;
	// No shadow. The vertex and index path keeps one so that a partial CPU write to a
	// static buffer can be replayed through UpdateSubresource; a shader buffer is either
	// dynamic and mapped directly, or GPU-written and never CPU-written at all. A shadow
	// for the light-index list would be 5.6 MB of system memory nothing would ever read.
#ifdef RTS_DEBUG
	// Registered before the views, not after: the two failure paths below free the buffer
	// through Free_Buffer, which strikes it off this register, and striking off something
	// that was never registered leaves made and freed disagreeing for a reason that has
	// nothing to do with a leak.
	Note_Buffer_Made(b, desc.ByteWidth, usage, uint_view ? "uint shader" : "structured");
#endif

	D3D11_SHADER_RESOURCE_VIEW_DESC srvd;
	memset(&srvd, 0, sizeof(srvd));
	srvd.Format = uint_view ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_UNKNOWN;
	srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srvd.Buffer.FirstElement = 0;
	srvd.Buffer.NumElements = count;
	if (FAILED(m_impl->device->CreateShaderResourceView(buffer, &srvd, &b->srv))) {
		Free_Buffer(b);
		return nullptr;
	}

	if (wants_uav) {
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavd;
		memset(&uavd, 0, sizeof(uavd));
		uavd.Format = uint_view ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_UNKNOWN;
		uavd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavd.Buffer.FirstElement = 0;
		uavd.Buffer.NumElements = count;
		if (FAILED(m_impl->device->CreateUnorderedAccessView(buffer, &uavd, &b->uav))) {
			Free_Buffer(b);
			return nullptr;
		}
	}

	return (GfxBuffer *)b;
}

void GfxDeviceD3D11::Release_Buffer(GfxBuffer * buffer)
{
	TRACE("Release_Buffer");
	D3D11Buffer * b = (D3D11Buffer *)buffer;
	if (b == nullptr) return;
	if (--b->refs > 0) return;
	// Out of every slot before it is freed. A released resource left in a shader-resource
	// or unordered-access slot is a dangling binding the next dispatch or draw uses --
	// the same service Release_Texture performs for the texture stages.
	Unbind_Buffer_Everywhere(m_impl, b);
	Free_Buffer(b);
}

bool GfxDeviceD3D11::Map_Buffer(GfxBuffer * buffer, GfxMapMode mode, void ** data)
{
	TRACE("Map_Buffer");
	D3D11Buffer * b = (D3D11Buffer *)buffer;
	if (b == nullptr || data == nullptr || b->mapped) return false;

	if (mode == GFX_MAP_READ) {
#ifdef RTS_DEBUG
		PROFILE_D3D11_SCOPE(t_map_buffer_read);
		++s_d3d11_prof.n_map_buffer_read;
#endif
		// A staging copy, because the buffer itself is D3D11_USAGE_DEFAULT and the CPU
		// cannot see one. Made on first use and kept, so an oracle that reads the grid
		// every frame does not allocate every frame.
		//
		// BindFlags 0 and no structure stride even when the source has one: CopyResource
		// between two buffers asks only that they be the same size, and a staging buffer
		// is not allowed the structured flag because nothing binds it.
		if (b->readback == nullptr) {
			D3D11_BUFFER_DESC desc;
			memset(&desc, 0, sizeof(desc));
			desc.ByteWidth = b->size;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			if (FAILED(m_impl->device->CreateBuffer(&desc, nullptr, &b->readback))) {
				b->readback = nullptr;
				return false;
			}
		}
		// This stalls until the GPU has finished everything queued ahead of it. That is
		// the price of reading a GPU-written buffer on the CPU and there is no cheaper
		// way; it is why the seam says nothing on the render path may call this.
		m_impl->context->CopyResource(b->readback, b->buffer);
		D3D11_MAPPED_SUBRESOURCE ms;
		if (FAILED(m_impl->context->Map(b->readback, 0, D3D11_MAP_READ, 0, &ms))) return false;
		b->mapped = true;
		b->mapped_readback = true;
		*data = ms.pData;
		return true;
	}

	// The write half. Only a dynamic buffer takes one: a GPU-written buffer has no CPU
	// path in and answering with a shadow that never reaches it would be a write that
	// silently does nothing.
	if (!b->is_dynamic) return false;
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_map_buffer);
	++s_d3d11_prof.n_map_buffer;
#endif
	D3D11_MAPPED_SUBRESOURCE ms;
	if (FAILED(m_impl->context->Map(b->buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return false;
	b->mapped = true;
	b->mapped_readback = false;
	*data = ms.pData;
	return true;
}

void GfxDeviceD3D11::Unmap_Buffer(GfxBuffer * buffer)
{
	TRACE("Unmap_Buffer");
	D3D11Buffer * b = (D3D11Buffer *)buffer;
	if (b == nullptr || !b->mapped) return;
	b->mapped = false;
	if (b->mapped_readback) {
		m_impl->context->Unmap(b->readback, 0);
		b->mapped_readback = false;
		return;
	}
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_unmap_buffer);
	++s_d3d11_prof.n_unmap_buffer;
#endif
	m_impl->context->Unmap(b->buffer, 0);
}

void GfxDeviceD3D11::Set_Compute_Buffer(unsigned slot, GfxBuffer * buffer)
{
	TRACE("Set_Compute_Buffer");
	if (slot >= GFX_COMPUTE_BUFFER_SLOTS) return;
	D3D11Buffer * b = (D3D11Buffer *)buffer;
	if (m_impl->cs_buffers[slot] == b && m_impl->cs_textures[slot] == nullptr) return;
	// Reading it and writing it cannot both stand. See the hazard note above this section.
	Unbind_Buffer_From_Write_Slots(m_impl, b);
	m_impl->cs_buffers[slot] = b;
	m_impl->cs_textures[slot] = nullptr;
	ID3D11ShaderResourceView * srv = (b != nullptr) ? b->srv : nullptr;
	m_impl->context->CSSetShaderResources(slot, 1, &srv);
	DX8Wrapper_Increment_Call_Count();
}

void GfxDeviceD3D11::Set_Compute_RW_Buffer(unsigned slot, GfxBuffer * buffer)
{
	TRACE("Set_Compute_RW_Buffer");
	if (slot >= GFX_COMPUTE_RW_SLOTS) return;
	D3D11Buffer * b = (D3D11Buffer *)buffer;
	if (b != nullptr && b->uav == nullptr) {
		// A buffer created without GFX_BUFFER_UAV has no view to bind, and binding
		// nothing would leave the dispatch writing into whatever was there before.
		WWDEBUG_SAY(("D3D11: Set_Compute_RW_Buffer on a buffer created without "
			"GFX_BUFFER_UAV (usage 0x%x). Nothing is bound at u%u.", b->usage, slot));
		return;
	}
	if (m_impl->cs_rw_buffers[slot] == b && m_impl->cs_rw_textures[slot] == nullptr) return;
	Unbind_Buffer_From_Read_Slots(m_impl, b);
	m_impl->cs_rw_buffers[slot] = b;
	m_impl->cs_rw_textures[slot] = nullptr;
	ID3D11UnorderedAccessView * uav = (b != nullptr) ? b->uav : nullptr;
	const UINT keep_counter = (UINT)-1;
	m_impl->context->CSSetUnorderedAccessViews(slot, 1, &uav, &keep_counter);
	DX8Wrapper_Increment_Call_Count();
}

void GfxDeviceD3D11::Set_Pixel_Buffer(unsigned slot, GfxBuffer * buffer)
{
	TRACE("Set_Pixel_Buffer");
	// Below the first buffer slot is the mesh's texture stage range, which Set_Texture
	// owns and whose redundancy check knows nothing about buffers. A buffer bound there
	// would be replaced by the next texture at that stage with nothing to say so.
	//
	// Said once rather than never and rather than every frame. Never is how a shader ends
	// up sampling a slot nothing was ever put in -- which reads as zero, which is the
	// failure this whole section exists to make visible -- and every frame is a log nobody
	// can read the rest of.
	if (slot < GFX_FIRST_PIXEL_BUFFER_SLOT || slot - GFX_FIRST_PIXEL_BUFFER_SLOT >= GFX_PIXEL_BUFFER_SLOTS) {
		static bool warned = false;
		if (!warned) {
			warned = true;
			WWDEBUG_SAY(("D3D11: Set_Pixel_Buffer refused slot t%u. The pixel stage's buffer "
				"slots are t%u..t%u; below that is the texture stage range, where a buffer "
				"would be silently replaced by the next Set_Texture for that stage.",
				slot, (unsigned)GFX_FIRST_PIXEL_BUFFER_SLOT,
				(unsigned)(GFX_FIRST_PIXEL_BUFFER_SLOT + GFX_PIXEL_BUFFER_SLOTS - 1)));
		}
		return;
	}
	const unsigned index = slot - GFX_FIRST_PIXEL_BUFFER_SLOT;
	D3D11Buffer * b = (D3D11Buffer *)buffer;
	if (m_impl->ps_buffers[index] == b) return;
	Unbind_Buffer_From_Write_Slots(m_impl, b);
	m_impl->ps_buffers[index] = b;
	ID3D11ShaderResourceView * srv = (b != nullptr) ? b->srv : nullptr;
	m_impl->context->PSSetShaderResources(slot, 1, &srv);
	DX8Wrapper_Increment_Call_Count();
}

void GfxDeviceD3D11::Set_Compute_Texture(unsigned slot, GfxTexture * texture)
{
	TRACE("Set_Compute_Texture");
	if (slot >= GFX_COMPUTE_BUFFER_SLOTS) return;
	D3D11Texture * t = (D3D11Texture *)texture;
	if (m_impl->cs_textures[slot] == t && m_impl->cs_buffers[slot] == nullptr) return;
	Unbind_Texture_From_Write_Slots(m_impl, t);
	m_impl->cs_textures[slot] = t;
	m_impl->cs_buffers[slot] = nullptr;
	ID3D11ShaderResourceView * srv = (t != nullptr) ? t->srv : nullptr;
	m_impl->context->CSSetShaderResources(slot, 1, &srv);
	DX8Wrapper_Increment_Call_Count();
}

void GfxDeviceD3D11::Set_Compute_RW_Texture(unsigned slot, GfxTexture * texture)
{
	TRACE("Set_Compute_RW_Texture");
	if (slot >= GFX_COMPUTE_RW_SLOTS) return;
	D3D11Texture * t = (D3D11Texture *)texture;
	if (t != nullptr && t->uav == nullptr) {
		WWDEBUG_SAY(("D3D11: Set_Compute_RW_Texture on a texture created without "
			"GFX_USAGE_UAV (usage 0x%x). Nothing is bound at u%u.", t->usage, slot));
		return;
	}
	if (m_impl->cs_rw_textures[slot] == t && m_impl->cs_rw_buffers[slot] == nullptr) return;
	Unbind_Texture_From_Read_Slots(m_impl, t);
	m_impl->cs_rw_textures[slot] = t;
	m_impl->cs_rw_buffers[slot] = nullptr;
	ID3D11UnorderedAccessView * uav = (t != nullptr) ? t->uav : nullptr;
	const UINT keep_counter = (UINT)-1;
	m_impl->context->CSSetUnorderedAccessViews(slot, 1, &uav, &keep_counter);
	DX8Wrapper_Increment_Call_Count();
}

void GfxDeviceD3D11::Clear_RW_Buffer_UInt(GfxBuffer * buffer, unsigned value)
{
	TRACE("Clear_RW_Buffer_UInt");
	D3D11Buffer * b = (D3D11Buffer *)buffer;
	if (b == nullptr || b->uav == nullptr) return;
	if (!b->uint_view) {
		// Refused rather than attempted. ClearUnorderedAccessViewUint is defined against a
		// typed or raw view; against a structured one the result is the driver's business,
		// and a clear that does nothing leaves the previous frame's contents in place --
		// which for a cluster grid is lights that are no longer there, drawn confidently.
		WWDEBUG_SAY(("D3D11: Clear_RW_Buffer_UInt on a structured buffer (usage 0x%x). "
			"Create it with GFX_BUFFER_UINT if it is to be cleared this way.", b->usage));
		return;
	}
	const UINT values[4] = { value, value, value, value };
	m_impl->context->ClearUnorderedAccessViewUint(b->uav, values);
	DX8Wrapper_Increment_Call_Count();
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

namespace
{
	unsigned Full_Mip_Chain(unsigned width, unsigned height)
	{
		unsigned levels = 1;
		while (width > 1 || height > 1) {
			if (width > 1) width >>= 1;
			if (height > 1) height >>= 1;
			++levels;
		}
		return levels;
	}

	/// True where D3D11 will let a texture in this format be a render target, which is
	/// what GenerateMips needs alongside a shader-resource view. The block-compressed
	/// formats cannot, and never need to: nothing in the engine asks for mips on one.
	bool Can_Generate_Mips(DXGI_FORMAT format)
	{
		return format == DXGI_FORMAT_B8G8R8A8_UNORM ||
			format == DXGI_FORMAT_B8G8R8X8_UNORM ||
			format == DXGI_FORMAT_R8G8B8A8_UNORM;
	}

	D3D11Texture * New_Texture(unsigned levels, unsigned faces)
	{
		D3D11Texture * t = new D3D11Texture;
#ifdef RTS_DEBUG
		++s_texMade;
		Note_Res_Made(s_liveTex, s_liveTexCount, 2048, t, "Create_Texture");
#endif
		memset(t, 0, sizeof(*t));
		t->refs = 1;
		t->levels = levels;
		t->faces = faces;
		t->ww = WW3D_FORMAT_UNKNOWN;
		t->wwz = WW3D_ZFORMAT_UNKNOWN;
		t->scratch = new D3D11Texture::Scratch[levels * faces];
		memset(t->scratch, 0, sizeof(D3D11Texture::Scratch) * levels * faces);
		return t;
	}
}

GfxTexture * GfxDeviceD3D11::Create_Texture(unsigned width, unsigned height, unsigned levels,
	WW3DFormat format, unsigned usage)
{
	TRACE("Create_Texture");
	const DXGI_FORMAT dxgi = WW3D_To_DXGI(format);
	if (dxgi == DXGI_FORMAT_UNKNOWN || width == 0 || height == 0) return nullptr;
	if (levels == 0) levels = Full_Mip_Chain(width, height);

	D3D11_TEXTURE2D_DESC desc;
	memset(&desc, 0, sizeof(desc));
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = levels;
	desc.ArraySize = 1;
	desc.Format = dxgi;
	desc.SampleDesc.Count = 1;

	const bool staging = (usage & GFX_USAGE_STAGING) != 0;
	if (staging) {
		// The system-memory home: the source of an Update_Texture and the surface a
		// readback lands in. Nothing draws from one, so it binds to nothing.
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
	} else {
		// Everything else is D3D11_USAGE_DEFAULT. D3DPOOL_MANAGED -- what a plain static
		// texture is under D3D9 -- has no counterpart at all, and the engine's own
		// DX8TextureTrackerClass restore path is what covers the difference.
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		if (usage & GFX_USAGE_RENDER_TARGET) desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
		if (levels > 1 && Can_Generate_Mips(dxgi)) {
			// Five callers build a texture atlas a tile at a time and then ask for the
			// mips to be filled in. GenerateMips needs both flags on the resource and
			// cannot be added afterwards, so a mipped texture in a format that can carry
			// them gets them.
			desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
			desc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
		}
	}

	ID3D11Texture2D * texture = nullptr;
	HRESULT hr = m_impl->device->CreateTexture2D(&desc, nullptr, &texture);
	if (FAILED(hr) && (desc.MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS) != 0) {
		// Say which texture lost its mip flags and what the device said, because
		// "the driver refused" is a guess until an HRESULT says so.
		WWDEBUG_SAY(("D3D11: CreateTexture2D refused %ux%u %u-level DXGI %d with "
			"RENDER_TARGET|GENERATE_MIPS (hr 0x%08X); retrying without them.",
			width, height, levels, (int)dxgi, (unsigned)hr));
		desc.MiscFlags &= ~D3D11_RESOURCE_MISC_GENERATE_MIPS;
		desc.BindFlags &= ~D3D11_BIND_RENDER_TARGET;
		hr = m_impl->device->CreateTexture2D(&desc, nullptr, &texture);
	}
	if (FAILED(hr)) return nullptr;
	const bool mips_available = (desc.MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS) != 0;

	D3D11Texture * t = New_Texture(levels, 1);
	t->resource = texture;
	t->width = width;
	t->height = height;
	t->depth = 1;
	t->dxgi = dxgi;
	t->ww = format;
	t->usage = usage;
	t->mappable = staging;
	t->can_generate_mips = mips_available;

	if (!staging) {
		D3D11_SHADER_RESOURCE_VIEW_DESC srv;
		memset(&srv, 0, sizeof(srv));
		srv.Format = dxgi;
		srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv.Texture2D.MipLevels = levels;
		if (FAILED(m_impl->device->CreateShaderResourceView(texture, &srv, &t->srv))) {
			t->srv = nullptr;
		}
	}
	return (GfxTexture *)t;
}

GfxTexture * GfxDeviceD3D11::Create_Cube_Texture(unsigned edge_length, unsigned levels,
	WW3DFormat format, unsigned usage)
{
	TRACE("Create_Cube_Texture");
	const DXGI_FORMAT dxgi = WW3D_To_DXGI(format);
	if (dxgi == DXGI_FORMAT_UNKNOWN || edge_length == 0) return nullptr;
	if (levels == 0) levels = Full_Mip_Chain(edge_length, edge_length);

	D3D11_TEXTURE2D_DESC desc;
	memset(&desc, 0, sizeof(desc));
	desc.Width = edge_length;
	desc.Height = edge_length;
	desc.MipLevels = levels;
	desc.ArraySize = 6;
	desc.Format = dxgi;
	desc.SampleDesc.Count = 1;

	const bool staging = (usage & GFX_USAGE_STAGING) != 0;
	if (staging) {
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
	} else {
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
		// The same two flags Create_Texture asks for, and for the same reason. This is
		// the one cube texture the engine builds a face at a time and then asks to have
		// filtered down -- the environment map, which W3DShaderManager bakes on the CPU
		// at 256 and mips because a reflection vector can sweep a whole face across one
		// pixel. Without them GenerateMips is refused and the lower mips stay empty.
		if (levels > 1 && Can_Generate_Mips(dxgi)) {
			desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
			desc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
		}
	}

	ID3D11Texture2D * texture = nullptr;
	HRESULT hr = m_impl->device->CreateTexture2D(&desc, nullptr, &texture);
	if (FAILED(hr) && (desc.MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS) != 0) {
		WWDEBUG_SAY(("D3D11: CreateTexture2D refused a %u cube, %u levels, DXGI %d with "
			"RENDER_TARGET|GENERATE_MIPS (hr 0x%08X); retrying without them.",
			edge_length, levels, (int)dxgi, (unsigned)hr));
		desc.MiscFlags &= ~D3D11_RESOURCE_MISC_GENERATE_MIPS;
		desc.BindFlags &= ~D3D11_BIND_RENDER_TARGET;
		hr = m_impl->device->CreateTexture2D(&desc, nullptr, &texture);
	}
	if (FAILED(hr)) return nullptr;

	D3D11Texture * t = New_Texture(levels, 6);
	t->resource = texture;
	t->width = edge_length;
	t->height = edge_length;
	t->depth = 1;
	t->dxgi = dxgi;
	t->ww = format;
	t->usage = usage;
	t->cube = true;
	t->mappable = staging;
	t->can_generate_mips = (desc.MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS) != 0;

	if (!staging) {
		D3D11_SHADER_RESOURCE_VIEW_DESC srv;
		memset(&srv, 0, sizeof(srv));
		srv.Format = dxgi;
		srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
		srv.TextureCube.MipLevels = levels;
		if (FAILED(m_impl->device->CreateShaderResourceView(texture, &srv, &t->srv))) {
			t->srv = nullptr;
		}
	}
	return (GfxTexture *)t;
}

GfxTexture * GfxDeviceD3D11::Create_Volume_Texture(unsigned width, unsigned height,
	unsigned depth, unsigned levels, WW3DFormat format, unsigned usage)
{
	TRACE("Create_Volume_Texture");
	const DXGI_FORMAT dxgi = WW3D_To_DXGI(format);
	if (dxgi == DXGI_FORMAT_UNKNOWN || width == 0 || height == 0 || depth == 0) return nullptr;
	if (levels == 0) levels = Full_Mip_Chain(width, height);

	D3D11_TEXTURE3D_DESC desc;
	memset(&desc, 0, sizeof(desc));
	desc.Width = width;
	desc.Height = height;
	desc.Depth = depth;
	desc.MipLevels = levels;
	desc.Format = dxgi;

	const bool staging = (usage & GFX_USAGE_STAGING) != 0;
	if (staging) {
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
	} else {
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		if (usage & GFX_USAGE_UAV) desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
	}

	ID3D11Texture3D * texture = nullptr;
	if (FAILED(m_impl->device->CreateTexture3D(&desc, nullptr, &texture))) return nullptr;

	D3D11Texture * t = New_Texture(levels, 1);
	t->resource = texture;
	t->width = width;
	t->height = height;
	t->depth = depth;
	t->dxgi = dxgi;
	t->ww = format;
	t->usage = usage;
	t->volume = true;
	t->mappable = staging;

	if (!staging) {
		D3D11_SHADER_RESOURCE_VIEW_DESC srv;
		memset(&srv, 0, sizeof(srv));
		srv.Format = dxgi;
		srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
		srv.Texture3D.MipLevels = levels;
		if (FAILED(m_impl->device->CreateShaderResourceView(texture, &srv, &t->srv))) {
			t->srv = nullptr;
		}
		if (usage & GFX_USAGE_UAV) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav;
			memset(&uav, 0, sizeof(uav));
			uav.Format = dxgi;
			uav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
			uav.Texture3D.MipSlice = 0;
			uav.Texture3D.FirstWSlice = 0;
			uav.Texture3D.WSize = depth;
			if (FAILED(m_impl->device->CreateUnorderedAccessView(texture, &uav, &t->uav))) {
				t->uav = nullptr;
			}
		}
	}
	return (GfxTexture *)t;
}

GfxTexture * GfxDeviceD3D11::Create_Depth_Texture(unsigned width, unsigned height,
	unsigned levels, WW3DZFormat format, unsigned usage)
{
	TRACE("Create_Depth_Texture");
	// The shadow map: written by the depth test and sampled afterwards. D3D11 refuses a
	// resource that is both a typed depth format and a shader resource, so it is created
	// typeless and viewed twice -- typed to write it, colour to read it. Getting this
	// wrong does not fail loudly; it fails as an empty shadow map.
	const DepthFormats zf = WW3DZ_To_DXGI(format);
	if (width == 0 || height == 0) return nullptr;
	if (levels == 0) levels = 1;

	D3D11_TEXTURE2D_DESC desc;
	memset(&desc, 0, sizeof(desc));
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = levels;
	desc.ArraySize = 1;
	desc.Format = zf.typeless;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

	ID3D11Texture2D * texture = nullptr;
	if (FAILED(m_impl->device->CreateTexture2D(&desc, nullptr, &texture))) return nullptr;

	D3D11Texture * t = New_Texture(levels, 1);
	t->resource = texture;
	t->width = width;
	t->height = height;
	t->depth = 1;
	t->dxgi = zf.typeless;
	t->wwz = format;
	t->usage = usage;
	t->is_depth = true;

	D3D11_SHADER_RESOURCE_VIEW_DESC srv;
	memset(&srv, 0, sizeof(srv));
	srv.Format = zf.shader;
	srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srv.Texture2D.MipLevels = levels;
	if (FAILED(m_impl->device->CreateShaderResourceView(texture, &srv, &t->srv))) {
		t->srv = nullptr;
	}
	return (GfxTexture *)t;
}

void GfxDeviceD3D11::Release_Texture(GfxTexture * texture)
{
	TRACE("Release_Texture");
	D3D11Texture * t = (D3D11Texture *)texture;
	if (t == nullptr) return;
	Unbind_Texture_Everywhere(m_impl, t);
	if (--t->refs <= 0) Free_Texture(t);
}

void GfxDeviceD3D11::Reference_Texture(GfxTexture * texture)
{
	TRACE("Reference_Texture");
	D3D11Texture * t = (D3D11Texture *)texture;
	if (t != nullptr) ++t->refs;
}

unsigned GfxDeviceD3D11::Get_Texture_Level_Count(GfxTexture * texture)
{
	TRACE("Get_Texture_Level_Count");
	D3D11Texture * t = (D3D11Texture *)texture;
	return (t != nullptr) ? t->levels : 0;
}

GfxSurface * GfxDeviceD3D11::Get_Texture_Surface_Level(GfxTexture * texture, unsigned level)
{
	TRACE("Get_Texture_Surface_Level");
	D3D11Texture * t = (D3D11Texture *)texture;
	if (t == nullptr || t->volume || level >= t->levels) return nullptr;
	ID3D11Texture2D * tex2d = (ID3D11Texture2D *)t->resource;

	const DXGI_FORMAT view = t->is_depth
		? WW3DZ_To_DXGI(t->wwz).depth : t->dxgi;
#ifdef RTS_DEBUG
	s_surfaceSite = "Get_Texture_Surface_Level";
#endif
	D3D11Surface * s = Wrap_Surface(tex2d, level, view, t->is_depth);
	if (s != nullptr) {
		if (!t->is_depth) s->ww = t->ww;
		s->parent_texture = t;
		++t->refs;
	}
	return (GfxSurface *)s;
}

void GfxDeviceD3D11::Set_Texture_Detail_Level(GfxTexture * texture, unsigned skip_levels)
{
	TRACE("Set_Texture_Detail_Level");
	D3D11Texture * t = (D3D11Texture *)texture;
	if (t == nullptr || t->resource == nullptr) return;
	// D3D9's SetLOD is a property of the texture; D3D11 spells the same thing as a
	// per-resource minimum level of detail on the context, which is exactly what the
	// texture-reduction setting wants.
	t->lod = skip_levels;
	m_impl->context->SetResourceMinLOD(t->resource, (float)skip_levels);
}

// ---------------------------------------------------------------------------
// Surfaces
// ---------------------------------------------------------------------------

GfxSurface * GfxDeviceD3D11::Create_Render_Target_Surface(unsigned width, unsigned height,
	WW3DFormat format, WW3DMultiSampleType multisample)
{
	TRACE("Create_Render_Target_Surface");
	const DXGI_FORMAT dxgi = WW3D_To_DXGI(format);
	if (dxgi == DXGI_FORMAT_UNKNOWN) return nullptr;

	D3D11_TEXTURE2D_DESC desc;
	memset(&desc, 0, sizeof(desc));
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = dxgi;
	desc.SampleDesc.Count = (multisample != WW3D_MULTISAMPLE_NONE) ? (UINT)multisample : 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	ID3D11Texture2D * texture = nullptr;
	if (FAILED(m_impl->device->CreateTexture2D(&desc, nullptr, &texture))) return nullptr;
#ifdef RTS_DEBUG
	s_surfaceSite = "Create_Render_Target_Surface";
#endif
	D3D11Surface * s = Wrap_Surface(texture, 0, dxgi, false);
	texture->Release();
	if (s != nullptr) s->ww = format;
	return (GfxSurface *)s;
}

GfxSurface * GfxDeviceD3D11::Create_Depth_Stencil_Surface(unsigned width, unsigned height,
	WW3DZFormat format, WW3DMultiSampleType multisample)
{
	TRACE("Create_Depth_Stencil_Surface");
	const DepthFormats zf = WW3DZ_To_DXGI(format);

	D3D11_TEXTURE2D_DESC desc;
	memset(&desc, 0, sizeof(desc));
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = zf.typeless;
	desc.SampleDesc.Count = (multisample != WW3D_MULTISAMPLE_NONE) ? (UINT)multisample : 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

	ID3D11Texture2D * texture = nullptr;
	if (FAILED(m_impl->device->CreateTexture2D(&desc, nullptr, &texture))) return nullptr;
#ifdef RTS_DEBUG
	s_surfaceSite = "Create_Depth_Stencil_Surface";
#endif
	D3D11Surface * s = Wrap_Surface(texture, 0, zf.depth, true);
	texture->Release();
	return (GfxSurface *)s;
}

GfxSurface * GfxDeviceD3D11::Create_Offscreen_Surface(unsigned width, unsigned height,
	WW3DFormat format)
{
	TRACE("Create_Offscreen_Surface");
	const DXGI_FORMAT dxgi = WW3D_To_DXGI(format);
	if (dxgi == DXGI_FORMAT_UNKNOWN) return nullptr;

	D3D11_TEXTURE2D_DESC desc;
	memset(&desc, 0, sizeof(desc));
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = dxgi;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;

	ID3D11Texture2D * texture = nullptr;
	if (FAILED(m_impl->device->CreateTexture2D(&desc, nullptr, &texture))) return nullptr;

	// A new surface is blank, and the engine has always been written against that.
	// D3D9's system-memory offscreen surface came back zeroed in practice; D3D11's
	// staging texture comes back holding whatever the allocation held. It matters because
	// the engine *reads* surfaces it has only partly written: FontCharsClass::Blit_Char
	// ORs each glyph's first PixelOverlap columns with what is already in the font atlas,
	// so under D3D11 the first glyph of every atlas row ORed itself with somebody else's
	// freed pixels. That is the stray vertical stroke down the left of the digits in the
	// top-left readout, and it is not the half-pixel rule.
	D3D11_MAPPED_SUBRESOURCE m;
	if (SUCCEEDED(m_impl->context->Map(texture, 0, D3D11_MAP_WRITE, 0, &m))) {
		memset(m.pData, 0, (size_t)m.RowPitch * height);
		m_impl->context->Unmap(texture, 0);
	}

#ifdef RTS_DEBUG
	s_surfaceSite = "Create_Offscreen_Surface";
#endif
	D3D11Surface * s = Wrap_Surface(texture, 0, dxgi, false);
	texture->Release();
	if (s != nullptr) s->ww = format;
	return (GfxSurface *)s;
}

void GfxDeviceD3D11::Release_Surface(GfxSurface * surface)
{
	TRACE("Release_Surface");
	D3D11Surface * s = (D3D11Surface *)surface;
	if (s == nullptr) return;
	// The back buffer and the depth buffer are owned by the swap chain and outlive every
	// reference the engine takes out on them.
	if (s == m_impl->back_buffer || s == m_impl->depth_buffer) {
		if (s->refs > 1) --s->refs;
		return;
	}
	if (--s->refs <= 0) {
		if (m_impl->current_rt == s) m_impl->current_rt = m_impl->back_buffer;
		if (m_impl->current_ds == s) m_impl->current_ds = m_impl->depth_buffer;
		Free_Surface(s);
	}
}

void GfxDeviceD3D11::Reference_Surface(GfxSurface * surface)
{
	TRACE("Reference_Surface");
	D3D11Surface * s = (D3D11Surface *)surface;
	if (s != nullptr) ++s->refs;
}

// ---------------------------------------------------------------------------
// Mapping
// ---------------------------------------------------------------------------

namespace
{
	unsigned Subresource_Of(const D3D11Texture * t, unsigned face, unsigned level)
	{
		return level + face * t->levels;
	}

	// Copy one subresource of a GPU-side texture into CPU memory, through a staging
	// texture made and thrown away for the purpose. Only ever needed for a resource
	// something other than the CPU can write -- see the comment at the read-back below.
	bool Read_Back_Subresource(GfxD3D11Impl * impl, ID3D11Texture2D * tex2d,
		unsigned subresource, unsigned char * dst, unsigned dst_pitch, unsigned rows)
	{
#ifdef RTS_DEBUG
		PROFILE_D3D11_SCOPE(t_readback_subresource);
		++s_d3d11_prof.n_readback_subresource;
#endif
		if (tex2d == nullptr || dst == nullptr) return false;
		D3D11_TEXTURE2D_DESC desc;
		tex2d->GetDesc(&desc);
		const unsigned levels = (desc.MipLevels != 0) ? desc.MipLevels : 1;
		const unsigned level = subresource % levels;
		D3D11_TEXTURE2D_DESC sd = desc;
		sd.Width = desc.Width >> level; if (sd.Width == 0) sd.Width = 1;
		sd.Height = desc.Height >> level; if (sd.Height == 0) sd.Height = 1;
		sd.MipLevels = 1;
		sd.ArraySize = 1;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.Usage = D3D11_USAGE_STAGING;
		sd.BindFlags = 0;
		sd.MiscFlags = 0;
		sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ID3D11Texture2D * staging = nullptr;
		if (FAILED(impl->device->CreateTexture2D(&sd, nullptr, &staging))) return false;
		impl->context->CopySubresourceRegion(staging, 0, 0, 0, 0, tex2d, subresource, nullptr);
		D3D11_MAPPED_SUBRESOURCE m;
		bool ok = false;
		if (SUCCEEDED(impl->context->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
			const unsigned copy = (dst_pitch < m.RowPitch) ? dst_pitch : m.RowPitch;
			for (unsigned r = 0; r < rows; ++r) {
				memcpy(dst + r * dst_pitch, (const unsigned char *)m.pData + r * m.RowPitch, copy);
			}
			impl->context->Unmap(staging, 0);
			ok = true;
		}
		staging->Release();
		return ok;
	}

	bool Map_Texture_Subresource(GfxD3D11Impl * impl, D3D11Texture * t, unsigned subresource,
		const GfxRect * rect, GfxMapMode mode, GfxMappedRect & mapped)
	{
#ifdef RTS_DEBUG
		PROFILE_D3D11_SCOPE(t_map_texture);
		++s_d3d11_prof.n_map_texture;
#endif
		mapped.Data = nullptr;
		mapped.Pitch = 0;
		if (t == nullptr || t->resource == nullptr) return false;
		if (subresource >= t->levels * t->faces) return false;

		if (t->mappable) {
			// A staging resource maps for real, which is what makes it the source of an
			// Update_Texture and the destination of a readback.
			D3D11_MAP map = D3D11_MAP_WRITE;
			if (mode == GFX_MAP_READ) map = D3D11_MAP_READ;
			else if (mode == GFX_MAP_READ_WRITE) map = D3D11_MAP_READ_WRITE;
			D3D11_MAPPED_SUBRESOURCE m;
			if (FAILED(impl->context->Map(t->resource, subresource, map, 0, &m))) return false;
			mapped.Data = m.pData;
			mapped.Pitch = (int)m.RowPitch;
			if (rect != nullptr) {
				const unsigned bpp = Bytes_Per_Pixel(t->dxgi);
				if (bpp != 0) {
					mapped.Data = (unsigned char *)m.pData + rect->top * m.RowPitch
						+ rect->left * bpp;
				}
			}
			return true;
		}

		// Everything else is a D3D11_USAGE_DEFAULT resource, which D3D11 will not map at
		// all. The caller gets system memory for the level and Unmap sends it up with
		// UpdateSubresource -- which is exactly what the engine's mip upload does with
		// the pointer anyway: it writes a whole level and never reads one back.
		D3D11Texture::Scratch & scratch = t->scratch[subresource];
		const unsigned level = subresource % t->levels;
		unsigned width = t->width >> level;
		unsigned height = t->height >> level;
		if (width == 0) width = 1;
		if (height == 0) height = 1;

		unsigned pitch;
		unsigned rows;
		const unsigned bpp = Bytes_Per_Pixel(t->dxgi);
		if (bpp != 0) {
			pitch = width * bpp;
			rows = height;
		} else {
			// Block compressed: four rows of pixels per row of blocks, and eight or
			// sixteen bytes per block depending on the format.
			const unsigned block_bytes = (t->dxgi == DXGI_FORMAT_BC1_UNORM) ? 8 : 16;
			const unsigned blocks_across = (width + 3) / 4;
			pitch = blocks_across * block_bytes;
			rows = (height + 3) / 4;
		}

		const unsigned bytes = pitch * rows;
		if (scratch.data == nullptr) {
			scratch.data = new unsigned char[bytes];
			memset(scratch.data, 0, bytes);
		}
		scratch.row_pitch = pitch;
		scratch.slice_pitch = bytes;

		// GFX_MAP_WRITE means whatever the caller does not write stays. scratch is this
		// subresource's CPU-side copy -- zeroed when first allocated and holding every CPU
		// write since -- so for a texture only the CPU writes, it already *is* the current
		// contents and preserving costs nothing. A read-back is needed exactly when
		// something else can have written the resource, which here means a render target.
		//
		// That distinction is what keeps this off the mip-upload path: Lock_Surfaces maps
		// every level of every one of the ~1900 textures the game loads, and a staging
		// copy per level there would be paid on every load for nothing.
		if (mode != GFX_MAP_WRITE_DISCARD && !t->volume) {
			ID3D11Texture2D * tex2d = (ID3D11Texture2D *)t->resource;
			D3D11_TEXTURE2D_DESC rd;
			tex2d->GetDesc(&rd);
			if ((rd.BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL)) != 0) {
				Read_Back_Subresource(impl, tex2d, subresource, scratch.data, pitch, rows);
			}
		}

		// Honour the sub-rectangle. The mappable branch above does; this one used to hand
		// back the base of the whole level whatever was asked for, so a caller that mapped
		// a rectangle and wrote at the returned pointer wrote to the wrong place.
		scratch.has_box = false;
		mapped.Data = scratch.data;
		mapped.Pitch = (int)pitch;
		if (rect != nullptr && bpp != 0) {
			mapped.Data = scratch.data + rect->top * pitch + rect->left * bpp;
			scratch.has_box = true;
			scratch.box_left = (unsigned)rect->left;
			scratch.box_top = (unsigned)rect->top;
			scratch.box_right = (unsigned)rect->right;
			scratch.box_bottom = (unsigned)rect->bottom;
		}
		return true;
	}

	void Unmap_Texture_Subresource(GfxD3D11Impl * impl, D3D11Texture * t, unsigned subresource)
	{
#ifdef RTS_DEBUG
		PROFILE_D3D11_SCOPE(t_unmap_texture);
		++s_d3d11_prof.n_unmap_texture;
#endif
		if (t == nullptr || t->resource == nullptr) return;
		if (subresource >= t->levels * t->faces) return;
		if (t->mappable) {
			impl->context->Unmap(t->resource, subresource);
			return;
		}
		const D3D11Texture::Scratch & scratch = t->scratch[subresource];
		if (scratch.data == nullptr) return;
		if (scratch.has_box) {
			// UpdateSubresource with a destination box wants the pointer to the box's own
			// first pixel, not to the start of the level.
			D3D11_BOX box;
			box.left = scratch.box_left;
			box.top = scratch.box_top;
			box.front = 0;
			box.right = scratch.box_right;
			box.bottom = scratch.box_bottom;
			box.back = 1;
			const unsigned bpp = Bytes_Per_Pixel(t->dxgi);
			const unsigned char * src = scratch.data
				+ scratch.box_top * scratch.row_pitch + scratch.box_left * bpp;
			impl->context->UpdateSubresource(t->resource, subresource, &box,
				src, scratch.row_pitch, scratch.slice_pitch);
			return;
		}
		impl->context->UpdateSubresource(t->resource, subresource, nullptr,
			scratch.data, scratch.row_pitch, scratch.slice_pitch);
	}
}

bool GfxDeviceD3D11::Map_Texture(GfxTexture * texture, unsigned level, const GfxRect * rect,
	GfxMapMode mode, GfxMappedRect & mapped)
{
	TRACE("Map_Texture");
	D3D11Texture * t = (D3D11Texture *)texture;
	if (t == nullptr) return false;
	return Map_Texture_Subresource(m_impl, t, Subresource_Of(t, 0, level), rect, mode, mapped);
}

void GfxDeviceD3D11::Unmap_Texture(GfxTexture * texture, unsigned level)
{
	TRACE("Unmap_Texture");
	D3D11Texture * t = (D3D11Texture *)texture;
	if (t == nullptr) return;
	Unmap_Texture_Subresource(m_impl, t, Subresource_Of(t, 0, level));
}

bool GfxDeviceD3D11::Map_Cube_Texture(GfxTexture * texture, unsigned face, unsigned level,
	const GfxRect * rect, GfxMapMode mode, GfxMappedRect & mapped)
{
	TRACE("Map_Cube_Texture");
	D3D11Texture * t = (D3D11Texture *)texture;
	if (t == nullptr || face >= t->faces) return false;
	return Map_Texture_Subresource(m_impl, t, Subresource_Of(t, face, level), rect, mode, mapped);
}

void GfxDeviceD3D11::Unmap_Cube_Texture(GfxTexture * texture, unsigned face, unsigned level)
{
	TRACE("Unmap_Cube_Texture");
	D3D11Texture * t = (D3D11Texture *)texture;
	if (t == nullptr || face >= t->faces) return;
	Unmap_Texture_Subresource(m_impl, t, Subresource_Of(t, face, level));
}

bool GfxDeviceD3D11::Map_Volume_Texture(GfxTexture * texture, unsigned level,
	GfxMapMode mode, GfxMappedBox & mapped)
{
	TRACE("Map_Volume_Texture");
	mapped.Data = nullptr;
	mapped.RowPitch = 0;
	mapped.SlicePitch = 0;

	D3D11Texture * t = (D3D11Texture *)texture;
	if (t == nullptr || !t->volume || level >= t->levels) return false;

	if (t->mappable) {
		D3D11_MAP map = (mode == GFX_MAP_READ) ? D3D11_MAP_READ : D3D11_MAP_WRITE;
		D3D11_MAPPED_SUBRESOURCE m;
		if (FAILED(m_impl->context->Map(t->resource, level, map, 0, &m))) return false;
		mapped.Data = m.pData;
		mapped.RowPitch = (int)m.RowPitch;
		mapped.SlicePitch = (int)m.DepthPitch;
		return true;
	}

	D3D11Texture::Scratch & scratch = t->scratch[level];
	unsigned width = t->width >> level;
	unsigned height = t->height >> level;
	unsigned depth = t->depth >> level;
	if (width == 0) width = 1;
	if (height == 0) height = 1;
	if (depth == 0) depth = 1;
	const unsigned bpp = Bytes_Per_Pixel(t->dxgi);
	if (bpp == 0) return false;

	const unsigned row = width * bpp;
	const unsigned slice = row * height;
	if (scratch.data == nullptr) {
		scratch.data = new unsigned char[slice * depth];
		memset(scratch.data, 0, slice * depth);
	}
	scratch.row_pitch = row;
	scratch.slice_pitch = slice;
	mapped.Data = scratch.data;
	mapped.RowPitch = (int)row;
	mapped.SlicePitch = (int)slice;
	return true;
}

void GfxDeviceD3D11::Unmap_Volume_Texture(GfxTexture * texture, unsigned level)
{
	TRACE("Unmap_Volume_Texture");
	D3D11Texture * t = (D3D11Texture *)texture;
	if (t == nullptr || !t->volume) return;
	Unmap_Texture_Subresource(m_impl, t, level);
}

bool GfxDeviceD3D11::Map_Surface(GfxSurface * surface, const GfxRect * rect,
	GfxMapMode mode, GfxMappedRect & mapped)
{
	TRACE("Map_Surface");
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_map_surface);
	++s_d3d11_prof.n_map_surface;
#endif
	mapped.Data = nullptr;
	mapped.Pitch = 0;

	D3D11Surface * s = (D3D11Surface *)surface;
	if (s == nullptr || s->texture == nullptr) return false;

	if (s->parent_texture != nullptr) {
		return Map_Texture_Subresource(m_impl, s->parent_texture, s->subresource, rect, mode, mapped);
	}

	D3D11_TEXTURE2D_DESC desc;
	s->texture->GetDesc(&desc);
	if (desc.Usage == D3D11_USAGE_STAGING) {
		D3D11_MAP map = D3D11_MAP_WRITE;
		if (mode == GFX_MAP_READ) map = D3D11_MAP_READ;
		else if (mode == GFX_MAP_READ_WRITE) map = D3D11_MAP_READ_WRITE;
		D3D11_MAPPED_SUBRESOURCE m;
		if (FAILED(m_impl->context->Map(s->texture, s->subresource, map, 0, &m))) return false;
		mapped.Data = m.pData;
		mapped.Pitch = (int)m.RowPitch;
		if (rect != nullptr) {
			const unsigned bpp = Bytes_Per_Pixel(s->dxgi);
			if (bpp != 0) {
				mapped.Data = (unsigned char *)m.pData + rect->top * m.RowPitch
					+ rect->left * bpp;
			}
		}
		return true;
	}

	// A GPU-side surface. Reading one means copying it into a staging texture first --
	// which is what the smudge and the frame dump do -- and writing one means the same
	// copy in reverse on Unmap.
	if (s->readback == nullptr) {
		D3D11_TEXTURE2D_DESC rd = desc;
		rd.Width = s->width;
		rd.Height = s->height;
		rd.MipLevels = 1;
		rd.ArraySize = 1;
		// Single-sampled, because a staging resource cannot be anything else. The copy
		// into it below resolves when the source is multisampled -- the frame dump maps
		// the back buffer, and with multisampling on that is exactly this case.
		rd.SampleDesc.Count = 1;
		rd.SampleDesc.Quality = 0;
		rd.Usage = D3D11_USAGE_STAGING;
		rd.BindFlags = 0;
		rd.MiscFlags = 0;
		rd.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
		if (FAILED(m_impl->device->CreateTexture2D(&rd, nullptr, &s->readback))) {
			s->readback = nullptr;
			return false;
		}
	}
	// Every mode but WRITE_DISCARD has to start from the surface's current pixels.
	//
	// GFX_MAP_WRITE says so -- "whatever is not written stays" (gfxdevice.h) -- and this
	// is the engine's whole read-modify-write path: SurfaceClass::Lock passes
	// GFX_MAP_WRITE, W3DRadar draws individual pixels through it into the minimap's
	// terrain, overlay and shroud textures, and the shroud pass updates only the cells
	// that changed. Skipping the copy did not merely fail to preserve: the staging
	// texture is made fresh for each wrapper, so the caller was handed *uninitialised*
	// memory and Unmap then copied all of it back over the surface.
	//
	// The wrapper is per Get_Texture_Surface_Level call and the radar takes a new one
	// every frame, so there is no earlier copy to carry forward and this cannot be
	// skipped by remembering one.
	if (mode != GFX_MAP_WRITE_DISCARD) {
		if (desc.SampleDesc.Count > 1) {
			// CopySubresourceRegion refuses to cross a sample count, and refuses it
			// quietly: the staging texture keeps its zeros and the caller reads a black
			// image. That is what the frame dump produced the first time the back buffer
			// was multisampled. Resolve into a plain intermediate and copy off that.
			D3D11_TEXTURE2D_DESC id = desc;
			id.Width = s->width;
			id.Height = s->height;
			id.MipLevels = 1;
			id.ArraySize = 1;
			id.SampleDesc.Count = 1;
			id.SampleDesc.Quality = 0;
			id.Usage = D3D11_USAGE_DEFAULT;
			id.BindFlags = D3D11_BIND_RENDER_TARGET;
			id.MiscFlags = 0;
			id.CPUAccessFlags = 0;
			ID3D11Texture2D * resolved = nullptr;
			if (SUCCEEDED(m_impl->device->CreateTexture2D(&id, nullptr, &resolved))) {
				m_impl->context->ResolveSubresource(resolved, 0, s->texture,
					s->subresource, s->view_format);
				m_impl->context->CopySubresourceRegion(s->readback, 0, 0, 0, 0,
					resolved, 0, nullptr);
				resolved->Release();
			}
		} else {
			m_impl->context->CopySubresourceRegion(s->readback, 0, 0, 0, 0,
				s->texture, s->subresource, nullptr);
		}
	}
	D3D11_MAPPED_SUBRESOURCE m;
	if (FAILED(m_impl->context->Map(s->readback, 0, D3D11_MAP_READ_WRITE, 0, &m)))
		return false;
	mapped.Data = m.pData;
	mapped.Pitch = (int)m.RowPitch;
	if (rect != nullptr) {
		const unsigned bpp = Bytes_Per_Pixel(s->dxgi);
		if (bpp != 0) {
			mapped.Data = (unsigned char *)m.pData + rect->top * m.RowPitch
				+ rect->left * bpp;
		}
	}
	return true;
}

void GfxDeviceD3D11::Unmap_Surface(GfxSurface * surface)
{
	TRACE("Unmap_Surface");
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_unmap_surface);
	++s_d3d11_prof.n_unmap_surface;
#endif
	D3D11Surface * s = (D3D11Surface *)surface;
	if (s == nullptr || s->texture == nullptr) return;

	if (s->parent_texture != nullptr) {
		Unmap_Texture_Subresource(m_impl, s->parent_texture, s->subresource);
		return;
	}

	if (s->readback != nullptr) {
		m_impl->context->Unmap(s->readback, 0);
		// Written back only where the destination can take it. A depth surface and a
		// multisampled one cannot, and nothing writes to either through this path.
		D3D11_TEXTURE2D_DESC desc;
		s->texture->GetDesc(&desc);
		if (!s->is_depth && desc.SampleDesc.Count == 1) {
			m_impl->context->CopySubresourceRegion(s->texture, s->subresource, 0, 0, 0,
				s->readback, 0, nullptr);
		}
		return;
	}
	m_impl->context->Unmap(s->texture, s->subresource);
}

// ---------------------------------------------------------------------------
// Describing a resource
// ---------------------------------------------------------------------------

bool GfxDeviceD3D11::Describe_Surface(GfxSurface * surface, WW3DSurfaceDescription & desc)
{
	TRACE("Describe_Surface");
	D3D11Surface * s = (D3D11Surface *)surface;
	if (s == nullptr) return false;
	desc.Width = s->width;
	desc.Height = s->height;
	desc.Format = s->ww;
	desc.MultiSample = s->multisample;
	return true;
}

bool GfxDeviceD3D11::Describe_Texture_Level(GfxTexture * texture, unsigned level,
	WW3DSurfaceDescription & desc)
{
	TRACE("Describe_Texture_Level");
	D3D11Texture * t = (D3D11Texture *)texture;
	if (t == nullptr || level >= t->levels) return false;
	unsigned width = t->width >> level;
	unsigned height = t->height >> level;
	desc.Width = width != 0 ? width : 1;
	desc.Height = height != 0 ? height : 1;
	desc.Format = t->ww;
	desc.MultiSample = WW3D_MULTISAMPLE_NONE;
	return true;
}

bool GfxDeviceD3D11::Describe_Volume_Level(GfxTexture * texture, unsigned level,
	WW3DSurfaceDescription & desc, unsigned & depth)
{
	TRACE("Describe_Volume_Level");
	D3D11Texture * t = (D3D11Texture *)texture;
	if (t == nullptr || !t->volume || level >= t->levels) return false;
	if (!Describe_Texture_Level(texture, level, desc)) return false;
	unsigned d = t->depth >> level;
	depth = d != 0 ? d : 1;
	return true;
}

bool GfxDeviceD3D11::Describe_Depth_Texture_Level(GfxTexture * texture, unsigned level,
	WW3DZFormat & format)
{
	TRACE("Describe_Depth_Texture_Level");
	D3D11Texture * t = (D3D11Texture *)texture;
	if (t == nullptr || !t->is_depth || level >= t->levels) return false;
	format = t->wwz;
	return true;
}

// ---------------------------------------------------------------------------
// Transfers
//
// Under D3D9 three of these were D3DX services -- LoadSurfaceFromSurface and
// FilterTexture -- and there is no D3DX11. What replaces them here is a GPU copy where
// the two ends agree and a CPU walk where they do not, which is the honest first-backend
// answer: every caller of the CPU path is working on a system-memory surface anyway.
// ---------------------------------------------------------------------------

namespace
{
	struct SurfaceView
	{
		unsigned char *		data;
		unsigned			pitch;
		unsigned			width, height;
		DXGI_FORMAT			format;
		ID3D11Texture2D *	mapped_texture;		// what to Unmap
		unsigned			mapped_subresource;
		ID3D11Texture2D *	temporary;			// a staging copy to release
	};

	/// Get at a surface's pixels on the CPU, copying it into a staging texture if that is
	/// what it takes. Every exit path is matched by Close_Surface_View.
	bool Open_Surface_View(GfxD3D11Impl * impl, D3D11Surface * s, bool for_write,
		SurfaceView & view)
	{
		memset(&view, 0, sizeof(view));
		if (s == nullptr || s->texture == nullptr) return false;

		D3D11_TEXTURE2D_DESC desc;
		s->texture->GetDesc(&desc);

		ID3D11Texture2D * target = s->texture;
		unsigned subresource = s->subresource;

		if (desc.Usage != D3D11_USAGE_STAGING) {
			D3D11_TEXTURE2D_DESC sd = desc;
			sd.Width = s->width;
			sd.Height = s->height;
			sd.MipLevels = 1;
			sd.ArraySize = 1;
			// A staging resource is single-sampled by definition, so a multisampled
			// source has to be resolved on the way down. Without this the create fails
			// and every CPU read of the back buffer -- the screenshot, the frame dump --
			// returns nothing the moment multisampling is on.
			sd.SampleDesc.Count = 1;
			sd.SampleDesc.Quality = 0;
			sd.Usage = D3D11_USAGE_STAGING;
			sd.BindFlags = 0;
			sd.MiscFlags = 0;
			sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
			if (FAILED(impl->device->CreateTexture2D(&sd, nullptr, &view.temporary)))
				return false;
			if (desc.SampleDesc.Count > 1) {
				// Resolve into a plain intermediate, then take the staging copy off that.
				D3D11_TEXTURE2D_DESC rd = sd;
				rd.Usage = D3D11_USAGE_DEFAULT;
				rd.CPUAccessFlags = 0;
				rd.BindFlags = D3D11_BIND_RENDER_TARGET;
				ID3D11Texture2D * resolved = nullptr;
				if (FAILED(impl->device->CreateTexture2D(&rd, nullptr, &resolved))) {
					view.temporary->Release();
					view.temporary = nullptr;
					return false;
				}
				impl->context->ResolveSubresource(resolved, 0, s->texture, s->subresource,
					s->view_format);
				impl->context->CopySubresourceRegion(view.temporary, 0, 0, 0, 0,
					resolved, 0, nullptr);
				resolved->Release();
			} else {
				impl->context->CopySubresourceRegion(view.temporary, 0, 0, 0, 0,
					s->texture, s->subresource, nullptr);
			}
			target = view.temporary;
			subresource = 0;
		}

		D3D11_MAPPED_SUBRESOURCE m;
		const D3D11_MAP map = for_write ? D3D11_MAP_READ_WRITE : D3D11_MAP_READ;
		if (FAILED(impl->context->Map(target, subresource, map, 0, &m))) {
			if (view.temporary != nullptr) { view.temporary->Release(); view.temporary = nullptr; }
			return false;
		}
		view.data = (unsigned char *)m.pData;
		view.pitch = m.RowPitch;
		view.width = s->width;
		view.height = s->height;
		view.format = s->dxgi;
		view.mapped_texture = target;
		view.mapped_subresource = subresource;
		return true;
	}

	void Close_Surface_View(GfxD3D11Impl * impl, D3D11Surface * s, SurfaceView & view,
		bool write_back)
	{
		if (view.mapped_texture == nullptr) return;
		impl->context->Unmap(view.mapped_texture, view.mapped_subresource);
		if (write_back && view.temporary != nullptr && s != nullptr) {
			impl->context->CopySubresourceRegion(s->texture, s->subresource, 0, 0, 0,
				view.temporary, 0, nullptr);
		}
		if (view.temporary != nullptr) view.temporary->Release();
		memset(&view, 0, sizeof(view));
	}

	/// A rectangle copy on the CPU, in whichever of the two supported widths the surfaces
	/// are. Point sampling unless the caller asked to resample, which is the one thing
	/// SurfaceClass::Copy and SurfaceClass::Stretch_Copy genuinely differ on.
	bool CPU_Blit(const SurfaceView & src, const GfxRect * src_rect,
		SurfaceView & dst, const GfxRect * dst_rect, bool resample)
	{
		const unsigned bpp = Bytes_Per_Pixel(src.format);
		if (bpp == 0 || bpp != Bytes_Per_Pixel(dst.format)) return false;
		if (src.format != dst.format) return false;

		const long sx0 = (src_rect != nullptr) ? src_rect->left : 0;
		const long sy0 = (src_rect != nullptr) ? src_rect->top : 0;
		const long sx1 = (src_rect != nullptr) ? src_rect->right : (long)src.width;
		const long sy1 = (src_rect != nullptr) ? src_rect->bottom : (long)src.height;
		const long dx0 = (dst_rect != nullptr) ? dst_rect->left : 0;
		const long dy0 = (dst_rect != nullptr) ? dst_rect->top : 0;
		const long dx1 = (dst_rect != nullptr) ? dst_rect->right : (long)dst.width;
		const long dy1 = (dst_rect != nullptr) ? dst_rect->bottom : (long)dst.height;

		const long sw = sx1 - sx0, sh = sy1 - sy0;
		const long dw = dx1 - dx0, dh = dy1 - dy0;
		if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return false;

		for (long y = 0; y < dh; ++y) {
			const long dy = dy0 + y;
			if (dy < 0 || dy >= (long)dst.height) continue;
			// Nearest source row. A box filter would be the faithful answer to
			// GFX_COPY_RESAMPLE, and nothing in this tree can tell the difference:
			// GFX_COPY_RESAMPLE has exactly one producer, SurfaceClass::Stretch_Copy,
			// and Stretch_Copy has no callers at all -- not in either game, not in the
			// tools. GFX_COPY_HALVE has two, and neither scales anything that varies:
			// _Create_DX8_Texture copies between two surfaces of the same size (its own
			// comment says so), and MissingTexture::_Init halves a level every texel of
			// which is the same constant 0x7FFF00FF. The claim this comment used to
			// make -- that the difference shows on the font atlas -- was wrong; the font
			// atlas is copied at 1:1 through Copy_Surface, which does not filter at all.
			long sy = sy0 + (sh == dh ? y : (y * sh) / dh);
			if (resample && sh != dh) sy = sy0 + (long)(((double)y + 0.5) * sh / dh);
			if (sy < sy0) sy = sy0;
			if (sy >= sy1) sy = sy1 - 1;

			unsigned char * dst_row = dst.data + dy * dst.pitch;
			const unsigned char * src_row = src.data + sy * src.pitch;
			for (long x = 0; x < dw; ++x) {
				const long dx = dx0 + x;
				if (dx < 0 || dx >= (long)dst.width) continue;
				long sxp = sx0 + (sw == dw ? x : (x * sw) / dw);
				if (sxp < sx0) sxp = sx0;
				if (sxp >= sx1) sxp = sx1 - 1;
				memcpy(dst_row + dx * bpp, src_row + sxp * bpp, bpp);
			}
		}
		return true;
	}

	bool GPU_Blit(GfxD3D11Impl * impl, D3D11Surface * src, const GfxRect * source_rect,
		D3D11Surface * dst, const GfxRect * dest_rect, bool linear_filter)
	{
		if (impl == nullptr || src == nullptr || dst == nullptr) return false;
		if (src->texture == nullptr || dst->texture == nullptr) return false;
		if (!Ensure_Blit_Shaders(impl)) return false;
		if (impl->blit_vs == nullptr || impl->blit_ps == nullptr || impl->blit_cb == nullptr) return false;

		// Only default GPU textures can participate in GPU blit. Staging/CPU textures go to CPU_Blit.
		D3D11_TEXTURE2D_DESC src_td, dst_td;
		src->texture->GetDesc(&src_td);
		dst->texture->GetDesc(&dst_td);
		if (src_td.Usage != D3D11_USAGE_DEFAULT || dst_td.Usage != D3D11_USAGE_DEFAULT)
			return false;
		if ((dst_td.BindFlags & D3D11_BIND_RENDER_TARGET) == 0)
			return false;

		ID3D11RenderTargetView * dst_rtv = Get_RTV(impl->device, dst);
		if (dst_rtv == nullptr) return false;

		ID3D11ShaderResourceView * src_srv = nullptr;
		bool need_intermediate = false;

		if (src->multisample != WW3D_MULTISAMPLE_NONE) {
			need_intermediate = true;
		} else if (src->texture == dst->texture) {
			need_intermediate = true;
		} else {
			src_srv = Get_SRV(impl->device, src);
			if (src_srv == nullptr) {
				need_intermediate = true;
			}
		}

		if (need_intermediate) {
			if (impl->blit_cache_tex == nullptr ||
			    impl->blit_cache_width != src->width ||
			    impl->blit_cache_height != src->height ||
			    impl->blit_cache_format != src->view_format) {
				if (impl->blit_cache_srv != nullptr) { impl->blit_cache_srv->Release(); impl->blit_cache_srv = nullptr; }
				if (impl->blit_cache_tex != nullptr) { impl->blit_cache_tex->Release(); impl->blit_cache_tex = nullptr; }

				D3D11_TEXTURE2D_DESC td;
				memset(&td, 0, sizeof(td));
				td.Width = src->width;
				td.Height = src->height;
				td.MipLevels = 1;
				td.ArraySize = 1;
				td.Format = src->view_format;
				td.SampleDesc.Count = 1;
				td.Usage = D3D11_USAGE_DEFAULT;
				td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

				if (FAILED(impl->device->CreateTexture2D(&td, nullptr, &impl->blit_cache_tex))) {
					td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
					if (FAILED(impl->device->CreateTexture2D(&td, nullptr, &impl->blit_cache_tex)))
						return false;
				}

				D3D11_SHADER_RESOURCE_VIEW_DESC srvd;
				memset(&srvd, 0, sizeof(srvd));
				srvd.Format = src->view_format;
				srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				srvd.Texture2D.MipLevels = 1;
				if (FAILED(impl->device->CreateShaderResourceView(impl->blit_cache_tex, &srvd, &impl->blit_cache_srv))) {
					impl->blit_cache_tex->Release();
					impl->blit_cache_tex = nullptr;
					return false;
				}
				impl->blit_cache_width = src->width;
				impl->blit_cache_height = src->height;
				impl->blit_cache_format = src->view_format;
			}

			if (src->multisample != WW3D_MULTISAMPLE_NONE) {
				impl->context->ResolveSubresource(impl->blit_cache_tex, 0,
					src->texture, src->subresource, src->view_format);
			} else {
				impl->context->CopySubresourceRegion(impl->blit_cache_tex, 0, 0, 0, 0,
					src->texture, src->subresource, nullptr);
			}
			src_srv = impl->blit_cache_srv;
		}

		if (src_srv == nullptr) return false;

		// Calculate UV coordinates and viewport
		float u0 = 0.0f, v0 = 0.0f, u_w = 1.0f, v_h = 1.0f;
		if (source_rect != nullptr && src->width > 0 && src->height > 0) {
			u0 = (float)source_rect->left / (float)src->width;
			v0 = (float)source_rect->top / (float)src->height;
			u_w = (float)(source_rect->right - source_rect->left) / (float)src->width;
			v_h = (float)(source_rect->bottom - source_rect->top) / (float)src->height;
		}
		const float uv_rect[4] = { u0, v0, u_w, v_h };

		D3D11_VIEWPORT vp;
		if (dest_rect != nullptr) {
			vp.TopLeftX = (float)dest_rect->left;
			vp.TopLeftY = (float)dest_rect->top;
			vp.Width = (float)(dest_rect->right - dest_rect->left);
			vp.Height = (float)(dest_rect->bottom - dest_rect->top);
		} else {
			vp.TopLeftX = 0.0f;
			vp.TopLeftY = 0.0f;
			vp.Width = (float)dst->width;
			vp.Height = (float)dst->height;
		}
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;

		if (vp.Width <= 0.0f || vp.Height <= 0.0f) return false;

		// Upload constant buffer
		D3D11_MAPPED_SUBRESOURCE mapped;
		if (FAILED(impl->context->Map(impl->blit_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
			return false;
		}
		memcpy(mapped.pData, uv_rect, sizeof(uv_rect));
		impl->context->Unmap(impl->blit_cb, 0);

		// Save pipeline state
		ID3D11RenderTargetView * saved_rtv = nullptr;
		ID3D11DepthStencilView * saved_dsv = nullptr;
		impl->context->OMGetRenderTargets(1, &saved_rtv, &saved_dsv);

		UINT saved_num_vps = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		D3D11_VIEWPORT saved_vps[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
		impl->context->RSGetViewports(&saved_num_vps, saved_vps);

		ID3D11VertexShader * saved_vs = nullptr;
		ID3D11PixelShader * saved_ps = nullptr;
		impl->context->VSGetShader(&saved_vs, nullptr, nullptr);
		impl->context->PSGetShader(&saved_ps, nullptr, nullptr);

		ID3D11ShaderResourceView * saved_ps_srv0 = nullptr;
		ID3D11SamplerState * saved_ps_samp0 = nullptr;
		impl->context->PSGetShaderResources(0, 1, &saved_ps_srv0);
		impl->context->PSGetSamplers(0, 1, &saved_ps_samp0);

		ID3D11Buffer * saved_vs_cb0 = nullptr;
		impl->context->VSGetConstantBuffers(0, 1, &saved_vs_cb0);

		ID3D11BlendState * saved_blend = nullptr;
		FLOAT saved_blend_factor[4];
		UINT saved_sample_mask = 0xFFFFFFFF;
		impl->context->OMGetBlendState(&saved_blend, saved_blend_factor, &saved_sample_mask);

		ID3D11DepthStencilState * saved_depth = nullptr;
		UINT saved_stencil_ref = 0;
		impl->context->OMGetDepthStencilState(&saved_depth, &saved_stencil_ref);

		ID3D11RasterizerState * saved_raster = nullptr;
		impl->context->RSGetState(&saved_raster);

		D3D11_PRIMITIVE_TOPOLOGY saved_topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		impl->context->IAGetPrimitiveTopology(&saved_topology);

		ID3D11InputLayout * saved_layout = nullptr;
		impl->context->IAGetInputLayout(&saved_layout);

		// Set render target and viewport
		impl->context->OMSetRenderTargets(1, &dst_rtv, nullptr);
		impl->context->RSSetViewports(1, &vp);

		// Set shaders and buffers
		impl->context->VSSetShader(impl->blit_vs, nullptr, 0);
		impl->context->PSSetShader(impl->blit_ps, nullptr, 0);
		impl->context->VSSetConstantBuffers(0, 1, &impl->blit_cb);

		// Set SRV and sampler
		impl->context->PSSetShaderResources(0, 1, &src_srv);
		ID3D11SamplerState * sampler = linear_filter ? impl->blit_sampler_linear : impl->blit_sampler_point;
		impl->context->PSSetSamplers(0, 1, &sampler);

		// Set pipeline states
		impl->context->OMSetBlendState(impl->blit_blend_state, nullptr, 0xFFFFFFFF);
		impl->context->OMSetDepthStencilState(impl->blit_depth_state, 0);
		impl->context->RSSetState(impl->blit_raster_state);

		impl->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		impl->context->IASetInputLayout(nullptr);

		// Render fullscreen triangle
		impl->context->Draw(3, 0);

		// Unbind SRV 0 to prevent hazard
		ID3D11ShaderResourceView * null_srv = nullptr;
		impl->context->PSSetShaderResources(0, 1, &null_srv);

		// Restore pipeline state
		impl->context->OMSetRenderTargets(1, &saved_rtv, saved_dsv);
		if (saved_rtv != nullptr) saved_rtv->Release();
		if (saved_dsv != nullptr) saved_dsv->Release();

		if (saved_num_vps > 0) impl->context->RSSetViewports(saved_num_vps, saved_vps);

		impl->context->VSSetShader(saved_vs, nullptr, 0);
		if (saved_vs != nullptr) saved_vs->Release();
		impl->context->PSSetShader(saved_ps, nullptr, 0);
		if (saved_ps != nullptr) saved_ps->Release();

		impl->context->VSSetConstantBuffers(0, 1, &saved_vs_cb0);
		if (saved_vs_cb0 != nullptr) saved_vs_cb0->Release();

		impl->context->PSSetShaderResources(0, 1, &saved_ps_srv0);
		if (saved_ps_srv0 != nullptr) saved_ps_srv0->Release();
		impl->context->PSSetSamplers(0, 1, &saved_ps_samp0);
		if (saved_ps_samp0 != nullptr) saved_ps_samp0->Release();

		impl->context->OMSetBlendState(saved_blend, saved_blend_factor, saved_sample_mask);
		if (saved_blend != nullptr) saved_blend->Release();

		impl->context->OMSetDepthStencilState(saved_depth, saved_stencil_ref);
		if (saved_depth != nullptr) saved_depth->Release();

		impl->context->RSSetState(saved_raster);
		if (saved_raster != nullptr) saved_raster->Release();

		impl->context->IASetPrimitiveTopology(saved_topology);
		impl->context->IASetInputLayout(saved_layout);
		if (saved_layout != nullptr) saved_layout->Release();

		impl->Invalidate_Bound_States();
		impl->vs_constants_dirty = true;
		impl->ps_constants_dirty = true;

		return true;
	}
}

bool GfxDeviceD3D11::Copy_Surface(GfxSurface * source, const GfxRect * source_rect,
	GfxSurface * dest, const GfxRect * dest_rect)
{
	TRACE("Copy_Surface");
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_copy_surface);
	++s_d3d11_prof.n_copy_surface;
#endif
	D3D11Surface * src = (D3D11Surface *)source;
	D3D11Surface * dst = (D3D11Surface *)dest;
	if (src == nullptr || dst == nullptr) return false;

	const bool same_size =
		(source_rect == nullptr && dest_rect == nullptr) ||
		(source_rect != nullptr && dest_rect != nullptr &&
			(source_rect->right - source_rect->left) == (dest_rect->right - dest_rect->left) &&
			(source_rect->bottom - source_rect->top) == (dest_rect->bottom - dest_rect->top));

	// The multisample resolve. This is the call the whole multisampled arrangement turns
	// on: the engine draws the scene into a multisampled colour surface and then copies it
	// into a plain texture for the post-process chain to sample, and CopySubresourceRegion
	// cannot cross a sample count -- D3D11 spells that ResolveSubresource and nothing else.
	// Whole surfaces only, which is all a resolve can do and all any caller asks for.
	const bool crosses_sample_count =
		src->multisample != WW3D_MULTISAMPLE_NONE &&
		dst->multisample == WW3D_MULTISAMPLE_NONE;

	if (crosses_sample_count &&
		source_rect == nullptr && dest_rect == nullptr &&
		src->width == dst->width && src->height == dst->height) {
		// The destination has to be a GPU-side resource: ResolveSubresource refuses a
		// staging one, and the screenshot's destination is exactly that -- it copies the
		// back buffer into an offscreen surface and locks it. Those go the long way, via
		// Copy_Surface_Rect, which resolves on its way to system memory.
		D3D11_TEXTURE2D_DESC dd;
		dst->texture->GetDesc(&dd);
		if (dd.Usage == D3D11_USAGE_DEFAULT) {
			// A typeless resource has no resolve format of its own; the view's typed
			// format is the one both sides agree on.
			m_impl->context->ResolveSubresource(dst->texture, dst->subresource,
				src->texture, src->subresource, dst->view_format);
			return true;
		}
	}

	// The fast route: same format, no scaling. CopySubresourceRegion takes any pair of
	// usages, which is the one place D3D11 is simpler than the three-way ladder D3D9
	// needed here -- but it will not cross a sample count, and it says so only to the
	// debug layer. Anything that has to resolve goes the long way instead, or the caller
	// silently gets whatever the destination already held: a black screenshot.
	if (src->dxgi == dst->dxgi && same_size && !crosses_sample_count) {
		D3D11_BOX box;
		const D3D11_BOX * box_ptr = nullptr;
		if (source_rect != nullptr) {
			box.left = source_rect->left;
			box.top = source_rect->top;
			box.right = source_rect->right;
			box.bottom = source_rect->bottom;
			box.front = 0;
			box.back = 1;
			box_ptr = &box;
		}
		m_impl->context->CopySubresourceRegion(dst->texture, dst->subresource,
			dest_rect != nullptr ? dest_rect->left : 0,
			dest_rect != nullptr ? dest_rect->top : 0, 0,
			src->texture, src->subresource, box_ptr);
		return true;
	}

	// GPU blit for format conversion or scaling
	if (GPU_Blit(m_impl, src, source_rect, dst, dest_rect, false)) {
		return true;
	}

#ifdef RTS_DEBUG
	static unsigned s_fallback_log_count = 0;
	if (++s_fallback_log_count <= 10) {
		WWDEBUG_SAY(("Copy_Surface fallback to CPU: src=%ux%u dxgi=%d ms=%d, dst=%ux%u dxgi=%d ms=%d, srect=(%d,%d,%d,%d) drect=(%d,%d,%d,%d)",
			src->width, src->height, (int)src->dxgi, (int)src->multisample,
			dst->width, dst->height, (int)dst->dxgi, (int)dst->multisample,
			source_rect ? source_rect->left : -1, source_rect ? source_rect->top : -1,
			source_rect ? source_rect->right : -1, source_rect ? source_rect->bottom : -1,
			dest_rect ? dest_rect->left : -1, dest_rect ? dest_rect->top : -1,
			dest_rect ? dest_rect->right : -1, dest_rect ? dest_rect->bottom : -1));
	}
#endif
	return Copy_Surface_Rect(source, source_rect, dest, dest_rect, GFX_COPY_NO_FILTER);
}

bool GfxDeviceD3D11::Copy_Surface_Rect(GfxSurface * source, const GfxRect * source_rect,
	GfxSurface * dest, const GfxRect * dest_rect, GfxCopyFilter filter)
{
	TRACE("Copy_Surface_Rect");
#ifdef RTS_DEBUG
	PROFILE_D3D11_SCOPE(t_copy_surface_rect);
	++s_d3d11_prof.n_copy_surface_rect;
#endif
	D3D11Surface * src = (D3D11Surface *)source;
	D3D11Surface * dst = (D3D11Surface *)dest;
	if (src == nullptr || dst == nullptr) return false;

	if (GPU_Blit(m_impl, src, source_rect, dst, dest_rect, filter != GFX_COPY_NO_FILTER)) {
		return true;
	}

	SurfaceView sv, dv;
	if (!Open_Surface_View(m_impl, src, false, sv)) return false;
	if (!Open_Surface_View(m_impl, dst, true, dv)) {
		Close_Surface_View(m_impl, src, sv, false);
		return false;
	}
	const bool ok = CPU_Blit(sv, source_rect, dv, dest_rect, filter != GFX_COPY_NO_FILTER);
	Close_Surface_View(m_impl, dst, dv, ok);
	Close_Surface_View(m_impl, src, sv, false);
	return ok;
}

bool GfxDeviceD3D11::Update_Texture(GfxTexture * source, GfxTexture * dest)
{
	TRACE("Update_Texture");
	D3D11Texture * src = (D3D11Texture *)source;
	D3D11Texture * dst = (D3D11Texture *)dest;
	if (src == nullptr || dst == nullptr) return false;
	if (src->resource == nullptr || dst->resource == nullptr) return false;
	// The system-memory-to-device-memory upload D3D9 spelled UpdateTexture. CopyResource
	// wants the two to agree on everything but usage, which is exactly the pairing the
	// engine already makes: it creates the pair with the same size, format and mip count
	// and differs only in where it asked for them to live.
	m_impl->context->CopyResource(dst->resource, src->resource);
	return true;
}

bool GfxDeviceD3D11::Generate_Mips(GfxTexture * texture, unsigned base_level)
{
	TRACE("Generate_Mips");
	D3D11Texture * t = (D3D11Texture *)texture;
	if (t == nullptr || t->srv == nullptr) return false;
	if (!t->can_generate_mips) {
		// D3D11 needs the flag at creation and it cannot be added afterwards. Asking
		// anyway is not harmless: the call is refused and the mip chain below the base
		// level keeps whatever it was created with, which for an atlas is nothing.
		// Name the format too. Which two textures these are is not answerable from the
		// size alone, and the format is also the reason: Can_Generate_Mips only admits
		// the three formats D3D11 will let be a render target here.
		WWDEBUG_SAY(("D3D11: Generate_Mips on a %ux%u %u-level texture in WW3D format %d "
			"(DXGI %d), created without D3D11_RESOURCE_MISC_GENERATE_MIPS -- its lower "
			"mips stay empty.",
			t->width, t->height, t->levels, (int)t->ww, (int)t->dxgi));
		return false;
	}
	(void)base_level;
	// D3D11's GenerateMips always fills the whole chain from level 0; D3D9's
	// D3DXFilterTexture took a base level, and every caller here passed 0.
	m_impl->context->GenerateMips(t->srv);
	return true;
}

bool GfxDeviceD3D11::Capture_Front_Buffer(GfxSurface * dest)
{
	TRACE("Capture_Front_Buffer");
	// There is no front buffer under DXGI and nothing to read one from. The back buffer
	// after a Present holds the same image, and it is what every caller of this actually
	// wanted -- the screenshot and the frame dump both take it immediately after a frame.
	D3D11Surface * dst = (D3D11Surface *)dest;
	if (dst == nullptr || m_impl->back_buffer == nullptr) return false;
	return Copy_Surface((GfxSurface *)m_impl->back_buffer, nullptr, dest, nullptr) ||
		Copy_Surface_Rect((GfxSurface *)m_impl->back_buffer, nullptr, dest, nullptr,
			GFX_COPY_NO_FILTER);
}

// ---------------------------------------------------------------------------
// Writing a surface out
//
// Every measurement in this port has come through here, so it is the one method in this
// file that had to work before anything else could be judged. D3D9 did it with
// D3DXSaveSurfaceToFileA and there is no D3DX11; this maps the surface, converts to
// R8G8B8 and writes a PNG through the stb_image_write the tree already links -- which is
// what W3DScreenshot has always done with a locked surface.
// ---------------------------------------------------------------------------

bool GfxDeviceD3D11::Save_Surface_To_File(const char * path, GfxSurface * surface)
{
	TRACE("Save_Surface_To_File");
	D3D11Surface * s = (D3D11Surface *)surface;
	if (path == nullptr || s == nullptr) return false;

	SurfaceView view;
	if (!Open_Surface_View(m_impl, s, false, view)) return false;

	bool ok = false;
	unsigned char * rgb = new unsigned char[view.width * view.height * 3];

	if (view.format == DXGI_FORMAT_B8G8R8A8_UNORM ||
		view.format == DXGI_FORMAT_B8G8R8X8_UNORM) {
		for (unsigned y = 0; y < view.height; ++y) {
			const unsigned char * row = view.data + y * view.pitch;
			for (unsigned x = 0; x < view.width; ++x) {
				unsigned char * out = rgb + 3 * (x + y * view.width);
				out[0] = row[x * 4 + 2];		// BGRA in memory
				out[1] = row[x * 4 + 1];
				out[2] = row[x * 4 + 0];
			}
		}
		ok = true;
	} else if (view.format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
		// The HDR scene buffer. Written out with the same clamp the D3D9 path's save
		// applied, which makes the two dumps comparable as images even though what is in
		// the buffer is not the same numbers.
		for (unsigned y = 0; y < view.height; ++y) {
			const unsigned short * row = (const unsigned short *)(view.data + y * view.pitch);
			for (unsigned x = 0; x < view.width; ++x) {
				unsigned char * out = rgb + 3 * (x + y * view.width);
				out[0] = Float_To_Byte(Half_To_Float(row[x * 4 + 0]));
				out[1] = Float_To_Byte(Half_To_Float(row[x * 4 + 1]));
				out[2] = Float_To_Byte(Half_To_Float(row[x * 4 + 2]));
			}
		}
		ok = true;
	} else if (view.format == DXGI_FORMAT_R32_FLOAT) {
		// The directional shadow map. One 32-bit float per texel holding sun-clip z/w in
		// [0,1], written straight out as grey so the dump is legible on its own -- a
		// near-black image is a map full of near geometry, a white one is the clear
		// value nothing wrote over. Without this case the shadow-map dump wrote nothing
		// at all and said only that it could not convert format 41.
		for (unsigned y = 0; y < view.height; ++y) {
			const float * row = (const float *)(view.data + y * view.pitch);
			for (unsigned x = 0; x < view.width; ++x) {
				unsigned char * out = rgb + 3 * (x + y * view.width);
				const unsigned char g = Float_To_Byte(row[x]);
				out[0] = g;
				out[1] = g;
				out[2] = g;
			}
		}
		ok = true;
	} else if (view.format == DXGI_FORMAT_B5G6R5_UNORM) {
		for (unsigned y = 0; y < view.height; ++y) {
			const unsigned short * row = (const unsigned short *)(view.data + y * view.pitch);
			for (unsigned x = 0; x < view.width; ++x) {
				unsigned char * out = rgb + 3 * (x + y * view.width);
				out[0] = (unsigned char)((row[x] & 0xF800) >> 8);
				out[1] = (unsigned char)((row[x] & 0x07E0) >> 3);
				out[2] = (unsigned char)((row[x] & 0x001F) << 3);
			}
		}
		ok = true;
	} else {
		WWDEBUG_SAY(("D3D11: Save_Surface_To_File cannot convert DXGI format %d.",
			(int)view.format));
	}

	// Size taken before the view is closed: Close_Surface_View memsets the whole
	// SurfaceView, so reading view.width/view.height after it hands the encoder a
	// 0x0 image, which it refuses -- this function then returned false having written
	// nothing, for every caller, with no message to say so. That is what made
	// -dumpShadowMap report "no shadow map to write" on every capture.
	const unsigned out_width = view.width;
	const unsigned out_height = view.height;
	Close_Surface_View(m_impl, s, view, false);
	if (ok) ok = Gfx_Write_Png_RGB(path, out_width, out_height, rgb);
	delete [] rgb;
	return ok;
}

// ---------------------------------------------------------------------------
// The rest
// ---------------------------------------------------------------------------

bool GfxDeviceD3D11::Get_Display_Mode(unsigned & width, unsigned & height, WW3DFormat & format)
{
	TRACE("Get_Display_Mode");
	if (m_impl->back_buffer == nullptr) return false;
	width = m_impl->back_buffer->width;
	height = m_impl->back_buffer->height;
	format = m_impl->back_buffer->ww;
	return true;
}

unsigned GfxDeviceD3D11::Get_Available_Texture_Memory()
{
	TRACE("Get_Available_Texture_Memory");
	// DXGI reports what the adapter has, not what is free -- there is no equivalent of
	// D3D9's GetAvailableTextureMem, which was itself a driver's guess. The engine uses
	// this to decide how hard to work at freeing textures; reporting the dedicated pool
	// makes it behave as it would on a card with that much and nothing else running.
	if (m_impl->adapter == nullptr) return 0;
	DXGI_ADAPTER_DESC desc;
	if (FAILED(m_impl->adapter->GetDesc(&desc))) return 0;
	const SIZE_T bytes = desc.DedicatedVideoMemory;
	// Clamped to what an unsigned can carry, which is what the caller reads it as.
	return (bytes > 0xffffffffu) ? 0xffffffffu : (unsigned)bytes;
}

void GfxDeviceD3D11::Trim_Resource_Memory()
{
	TRACE("Trim_Resource_Memory");
	// D3D9 evicts its managed pool here. There is no managed pool, and nothing this
	// backend is holding is a cache it may drop.
}

void GfxDeviceD3D11::Set_Gamma_Ramp(const void *, bool)
{
	TRACE("Set_Gamma_Ramp");
	// DXGI sets gamma through IDXGIOutput, and only on a swap chain that owns the display
	// exclusively. A windowed game -- which is what the harness runs and what this phase
	// measures -- cannot set it at all, and Query_Capabilities reports FullScreenGamma
	// false so the engine already knows not to expect it.
}

bool GfxDeviceD3D11::Set_Hardware_Cursor(GfxSurface *, unsigned, unsigned)
{
	TRACE("Set_Hardware_Cursor");
	// D3D11 has no cursor concept. Answering false is not a missing feature: W3DMouse
	// reads it and takes RM_POLYGON, which draws the cursor as geometry.
	return false;
}

void GfxDeviceD3D11::Show_Hardware_Cursor(bool) { }
void GfxDeviceD3D11::Set_Hardware_Cursor_Position(unsigned, unsigned) { }

GfxQuery * GfxDeviceD3D11::Create_Query(GfxQueryType)
{
	TRACE("Show_Hardware_Cursor");
	TRACE("Set_Hardware_Cursor_Position");
	TRACE("Create_Query");
	// D3D11 has timestamp queries, and this backend deliberately does not offer them yet.
	// The GPU timer is a measurement instrument, and a half-built one is worse than none:
	// this tree has already published one confidently wrong GPU measurement, and D3D9's
	// timestamps read *inverted* on this machine. gfxdevice.h names null as the answer a
	// backend without them gives, and the caller then goes quiet rather than reporting
	// zeroes.
	return nullptr;
}

void GfxDeviceD3D11::Release_Query(GfxQuery *) { }
void GfxDeviceD3D11::Begin_Query(GfxQuery *) { }
void GfxDeviceD3D11::End_Query(GfxQuery *) { }
bool GfxDeviceD3D11::Get_Query_Data(GfxQuery *, void *, unsigned) { return false; }

bool GfxDeviceD3D11::Query_Capabilities(GfxDeviceCaps & caps)
{
	TRACE("Release_Query");
	TRACE("Begin_Query");
	TRACE("End_Query");
	TRACE("Get_Query_Data");
	TRACE("Query_Capabilities");
	Fill_D3D11_Caps(0, caps);
	return true;
}

bool GfxDeviceD3D11::Validate_Draw_State(unsigned & passes)
{
	TRACE("Validate_Draw_State");
	// D3D11 has no ValidateDevice: a state combination it cannot draw in one pass is a
	// state combination it refuses at creation, which is a different and earlier failure.
	passes = 1;
	return false;
}

// The constant buffer, off the device and back through a staging copy.
//
// Deliberately not impl->vs_constants: that array is this backend's copy of what the
// wrapper handed it, and the wrapper handed the same floats to D3D9, so reading it
// here would compare the wrapper with itself. What is worth measuring is the buffer
// the vertex shader actually reads, after the map, the memcpy and whatever packing
// register(cN) turned into -- so the copy comes out of the resource.
//
// Debug only, and it stalls: CopyResource then a blocking map is a full pipeline
// flush. Nothing calls it per frame.
bool GfxDeviceD3D11::Debug_Read_Vertex_Constants(unsigned first_register, unsigned count,
	float * out)
{
	TRACE("Debug_Read_Vertex_Constants");
	if (out == nullptr || count == 0) return false;
	if (m_impl->vs_constant_buffer == nullptr) return false;
	if (first_register + count > GFX_VS_CONSTANTS) return false;

	// One staging buffer, made on first use and kept: this is asked for a few hundred
	// times in the one frame a dump names, and creating a buffer per call would make
	// the instrument's cost the thing being measured.
	if (m_impl->debug_constant_staging == nullptr) {
		D3D11_BUFFER_DESC desc;
		memset(&desc, 0, sizeof(desc));
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		desc.ByteWidth = GFX_VS_CONSTANTS * 4 * sizeof(float);
		if (FAILED(m_impl->device->CreateBuffer(&desc, nullptr,
				&m_impl->debug_constant_staging)))
			return false;
	}

	// Whatever is pending has to be in the buffer before it is copied out of, or the
	// readback reports the previous draw's constants and every comparison made with it
	// is off by one draw.
	Upload_Constants(m_impl);

	m_impl->context->CopyResource(m_impl->debug_constant_staging,
		m_impl->vs_constant_buffer);
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(m_impl->context->Map(m_impl->debug_constant_staging, 0, D3D11_MAP_READ,
			0, &mapped)))
		return false;
	memcpy(out, (const float *)mapped.pData + first_register * 4,
		count * 4 * sizeof(float));
	m_impl->context->Unmap(m_impl->debug_constant_staging, 0);
	return true;
}

#ifdef RTS_DEBUG
namespace
{
	const char * Blend_Name(D3D11_BLEND b)
	{
		switch (b) {
		case D3D11_BLEND_ZERO:				return "ZERO";
		case D3D11_BLEND_ONE:				return "ONE";
		case D3D11_BLEND_SRC_COLOR:			return "SRC_COLOR";
		case D3D11_BLEND_INV_SRC_COLOR:		return "INV_SRC_COLOR";
		case D3D11_BLEND_SRC_ALPHA:			return "SRC_ALPHA";
		case D3D11_BLEND_INV_SRC_ALPHA:		return "INV_SRC_ALPHA";
		case D3D11_BLEND_DEST_ALPHA:		return "DEST_ALPHA";
		case D3D11_BLEND_INV_DEST_ALPHA:	return "INV_DEST_ALPHA";
		case D3D11_BLEND_DEST_COLOR:		return "DEST_COLOR";
		case D3D11_BLEND_INV_DEST_COLOR:	return "INV_DEST_COLOR";
		case D3D11_BLEND_SRC_ALPHA_SAT:		return "SRC_ALPHA_SAT";
		case D3D11_BLEND_BLEND_FACTOR:		return "BLEND_FACTOR";
		case D3D11_BLEND_INV_BLEND_FACTOR:	return "INV_BLEND_FACTOR";
		default:							return "?";
		}
	}
}
#endif

// What the next draw would be rasterised with, as this backend has it. See the note on
// the declaration in gfxdevice.h.
//
// The blend object is *re-created* here rather than read back off the context, and that is
// the point of the instrument rather than a shortcut: the state is materialised at the
// draw, so at the moment a caller wants to look nothing has been sent yet, and the failure
// this was written to catch is Apply_Blend_State returning silently when CreateBlendState
// refuses the description. Asking the device to make the same description again reports
// the HRESULT that the draw path throws away.
bool GfxDeviceD3D11::Debug_Describe_Draw_State(char * out, unsigned cap)
{
#ifdef RTS_DEBUG
	TRACE("Debug_Describe_Draw_State");
	if (out == nullptr || cap == 0) return false;
	GfxD3D11Impl * const impl = m_impl;
	if (impl == nullptr || impl->device == nullptr) return false;

	D3D11_BLEND_DESC desc;
	memset(&desc, 0, sizeof(desc));
	desc.AlphaToCoverageEnable = FALSE;
	desc.IndependentBlendEnable = FALSE;
	D3D11_RENDER_TARGET_BLEND_DESC & rt = desc.RenderTarget[0];
	rt.BlendEnable = impl->rs[RS_ALPHABLENDENABLE] ? TRUE : FALSE;
	rt.SrcBlend = To_Blend(impl->rs[RS_SRCBLEND]);
	rt.DestBlend = To_Blend(impl->rs[RS_DESTBLEND]);
	rt.BlendOp = To_Blend_Op(impl->rs[RS_BLENDOP]);
	if (impl->rs[RS_SEPARATEALPHABLENDENABLE]) {
		rt.SrcBlendAlpha = To_Alpha_Blend(impl->rs[RS_SRCBLENDALPHA]);
		rt.DestBlendAlpha = To_Alpha_Blend(impl->rs[RS_DESTBLENDALPHA]);
		rt.BlendOpAlpha = To_Blend_Op(impl->rs[RS_BLENDOPALPHA]);
	} else {
		rt.SrcBlendAlpha = To_Alpha_Blend(impl->rs[RS_SRCBLEND]);
		rt.DestBlendAlpha = To_Alpha_Blend(impl->rs[RS_DESTBLEND]);
		rt.BlendOpAlpha = To_Blend_Op(impl->rs[RS_BLENDOP]);
	}
	rt.RenderTargetWriteMask = (UINT8)(impl->rs[RS_COLORWRITEENABLE] & 0xf);

	ID3D11BlendState * probe = nullptr;
	const HRESULT hr = impl->device->CreateBlendState(&desc, &probe);
	if (probe != nullptr) probe->Release();

	// And what is actually standing on the context right now, which is a different
	// question from what the words above would build: the state objects are materialised
	// at the draw, so before one this reports the *previous* draw's and after one it
	// reports this draw's. Asked of the context rather than of the tracked words because
	// the failure being hunted is precisely a description that never reached the device.
	char livebuf[128];
	{
		ID3D11BlendState * live = nullptr;
		float lf[4] = { 0, 0, 0, 0 };
		UINT lm = 0;
		impl->context->OMGetBlendState(&live, lf, &lm);
		if (live == nullptr) {
			strcpy(livebuf, "context blend = DEFAULT (no object bound)");
		} else {
			D3D11_BLEND_DESC ld;
			live->GetDesc(&ld);
			sprintf(livebuf, "context blend en=%u rgb %s/%s alpha %s/%s mask=0x%x",
				(unsigned)ld.RenderTarget[0].BlendEnable,
				Blend_Name(ld.RenderTarget[0].SrcBlend),
				Blend_Name(ld.RenderTarget[0].DestBlend),
				Blend_Name(ld.RenderTarget[0].SrcBlendAlpha),
				Blend_Name(ld.RenderTarget[0].DestBlendAlpha),
				(unsigned)ld.RenderTarget[0].RenderTargetWriteMask);
			live->Release();
		}
	}

	// And what the pixel shader stage is actually holding in slot 0, which is a different
	// question again from impl->textures[0]: that array is what the wrapper asked for, and
	// the whole family of bugs this instrument exists for is a binding that was asked for
	// and did not arrive.
	char slotbuf[320];
	{
		ID3D11ShaderResourceView * srv[GFX_MAX_STAGES] = { nullptr };
		ID3D11SamplerState * samp[GFX_MAX_STAGES] = { nullptr };
		impl->context->PSGetShaderResources(0, GFX_MAX_STAGES, srv);
		impl->context->PSGetSamplers(0, GFX_MAX_STAGES, samp);
		unsigned srvMask = 0, sampMask = 0;
		char dims[GFX_MAX_STAGES][32];
		for (unsigned st = 0; st < GFX_MAX_STAGES; ++st) {
			strcpy(dims[st], "-");
			if (samp[st] != nullptr) { sampMask |= (1u << st); samp[st]->Release(); }
			if (srv[st] == nullptr) continue;
			srvMask |= (1u << st);
			ID3D11Resource * res = nullptr;
			srv[st]->GetResource(&res);
			if (res != nullptr) {
				ID3D11Texture2D * t2 = nullptr;
				if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t2))
						&& t2 != nullptr) {
					D3D11_TEXTURE2D_DESC td;
					t2->GetDesc(&td);
					sprintf(dims[st], "%ux%u/%u", td.Width, td.Height, (unsigned)td.Format);
					t2->Release();
				}
				res->Release();
			}
			srv[st]->Release();
		}
		sprintf(slotbuf, "PS slots srv=0x%02x samp=0x%02x t0=%s t1=%s t2=%s",
			srvMask, sampMask, dims[0], dims[1], dims[2]);
	}

	const D3D11Texture * const t0 = impl->textures[0];
	char texbuf[160];
	if (t0 == nullptr) {
		strcpy(texbuf, "tex0 = NONE");
	} else {
		sprintf(texbuf, "tex0 = %ux%u ww=%d dxgi=%d levels=%u srv=%s lod=%u",
			t0->width, t0->height, (int)t0->ww, (int)t0->dxgi, t0->levels,
			t0->srv != nullptr ? "yes" : "NULL", t0->lod);
	}

	_snprintf(out, cap,
		"blend en=%u src=%u dst=%u op=%u sepa=%u -> rgb %s/%s alpha %s/%s mask=0x%x "
		"CreateBlendState=0x%08x | vs=%s ps=%s psTexMask=0x%02x | %s | "
		"srvUnbound=0x%x sampler0 applied=%u addrUV=%u/%u filt %u/%u/%u | "
		"ps c0 = (%.3f, %.3f, %.3f, %.3f) | ztest=%u zwrite=%u zfunc=%u alphatest=%u | %s | %s",
		impl->rs[RS_ALPHABLENDENABLE], impl->rs[RS_SRCBLEND], impl->rs[RS_DESTBLEND],
		impl->rs[RS_BLENDOP], impl->rs[RS_SEPARATEALPHABLENDENABLE],
		Blend_Name(rt.SrcBlend), Blend_Name(rt.DestBlend),
		Blend_Name(rt.SrcBlendAlpha), Blend_Name(rt.DestBlendAlpha),
		(unsigned)rt.RenderTargetWriteMask, (unsigned)hr,
		impl->vertex_shader != nullptr ? "yes" : "NULL",
		impl->pixel_shader != nullptr ? "yes" : "NULL",
		impl->pixel_shader != nullptr ? impl->pixel_shader->texture_mask : 0u,
		texbuf,
		impl->srv_unbound_mask, (unsigned)impl->sampler_applied[0],
		impl->tss[0][TSS_ADDRESSU], impl->tss[0][TSS_ADDRESSV],
		impl->tss[0][TSS_MAGFILTER], impl->tss[0][TSS_MINFILTER], impl->tss[0][TSS_MIPFILTER],
		impl->ps_constants[0], impl->ps_constants[1], impl->ps_constants[2],
		impl->ps_constants[3],
		impl->rs[RS_ZENABLE], impl->rs[RS_ZWRITEENABLE], impl->rs[RS_ZFUNC],
		impl->rs[RS_ALPHATESTENABLE], livebuf, slotbuf);
	out[cap - 1] = '\0';
	return true;
#else
	(void)out; (void)cap;
	return false;
#endif
}

// The texels themselves, off the resource through a staging copy. Debug only and it
// stalls; nothing calls it per frame.
bool GfxDeviceD3D11::Debug_Read_Texture_Texels(unsigned stage, unsigned level,
	unsigned * out, unsigned count)
{
#ifdef RTS_DEBUG
	TRACE("Debug_Read_Texture_Texels");
	if (out == nullptr || count == 0) return false;
	if (stage >= GFX_MAX_STAGES) return false;
	D3D11Texture * const t = m_impl->textures[stage];
	if (t == nullptr || t->resource == nullptr) return false;
	if (level >= t->levels) return false;
	// The two straight 32-bit orders, plus the three block-compressed ones decoded below.
	// Everything else is refused: a plausible-looking wrong number is worse than nothing.
	const bool bc1 = (t->dxgi == DXGI_FORMAT_BC1_UNORM || t->dxgi == DXGI_FORMAT_BC1_UNORM_SRGB);
	const bool bc2 = (t->dxgi == DXGI_FORMAT_BC2_UNORM || t->dxgi == DXGI_FORMAT_BC2_UNORM_SRGB);
	const bool bc3 = (t->dxgi == DXGI_FORMAT_BC3_UNORM || t->dxgi == DXGI_FORMAT_BC3_UNORM_SRGB);
	const bool block = bc1 || bc2 || bc3;
	if (!block && t->dxgi != DXGI_FORMAT_B8G8R8A8_UNORM && t->dxgi != DXGI_FORMAT_R8G8B8A8_UNORM)
		return false;

	const unsigned w = t->width >> level ? t->width >> level : 1;
	const unsigned h = t->height >> level ? t->height >> level : 1;

	D3D11_TEXTURE2D_DESC sd;
	memset(&sd, 0, sizeof(sd));
	sd.Width = w;
	sd.Height = h;
	sd.MipLevels = 1;
	sd.ArraySize = 1;
	sd.Format = t->dxgi;
	sd.SampleDesc.Count = 1;
	sd.Usage = D3D11_USAGE_STAGING;
	sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ID3D11Texture2D * staging = nullptr;
	if (FAILED(m_impl->device->CreateTexture2D(&sd, nullptr, &staging))) return false;

	m_impl->context->CopySubresourceRegion(staging, 0, 0, 0, 0, t->resource, level, nullptr);
	D3D11_MAPPED_SUBRESOURCE mapped;
	bool ok = false;
	if (SUCCEEDED(m_impl->context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
		for (unsigned i = 0; i < count; ++i) {
			// Along the diagonal, so a few samples cross the whole image rather than
			// sitting in one corner of it.
			const unsigned x = (count > 1) ? (i * (w - 1)) / (count - 1) : w / 2;
			const unsigned y = (count > 1) ? (i * (h - 1)) / (count - 1) : h / 2;
			if (!block) {
				const unsigned char * row = (const unsigned char *)mapped.pData + y * mapped.RowPitch;
				const unsigned char b0 = row[x * 4 + 0], b1 = row[x * 4 + 1];
				const unsigned char b2 = row[x * 4 + 2], b3 = row[x * 4 + 3];
				// Reported as 0xAARRGGBB whichever way round the resource stores it.
				out[i] = (t->dxgi == DXGI_FORMAT_B8G8R8A8_UNORM)
					? ((unsigned)b3 << 24) | ((unsigned)b2 << 16) | ((unsigned)b1 << 8) | b0
					: ((unsigned)b3 << 24) | ((unsigned)b0 << 16) | ((unsigned)b1 << 8) | b2;
				continue;
			}
			// Block compressed. One 4x4 block per 8 (BC1) or 16 (BC2/BC3) bytes, and the
			// decode is written out rather than approximated -- the whole question this
			// instrument exists to answer is what value the sampler returns, and an
			// endpoint average would not answer it.
			const unsigned bw = (w + 3) / 4;
			(void)bw;
			const unsigned bx = x / 4, by = y / 4;
			const unsigned bytesPerBlock = bc1 ? 8u : 16u;
			const unsigned char * blk = (const unsigned char *)mapped.pData
				+ by * mapped.RowPitch + bx * bytesPerBlock;
			const unsigned char * colour = bc1 ? blk : blk + 8;
			const unsigned c0 = colour[0] | ((unsigned)colour[1] << 8);
			const unsigned c1 = colour[2] | ((unsigned)colour[3] << 8);
			const unsigned lx = x & 3, ly = y & 3;
			const unsigned idx = (colour[4 + ly] >> (2 * lx)) & 3;
			unsigned r[2], g[2], b[2];
			const unsigned cs[2] = { c0, c1 };
			for (int k = 0; k < 2; ++k) {
				r[k] = ((cs[k] >> 11) & 0x1f) * 255 / 31;
				g[k] = ((cs[k] >> 5)  & 0x3f) * 255 / 63;
				b[k] = ( cs[k]        & 0x1f) * 255 / 31;
			}
			unsigned rr, gg, bb;
			if (!bc1 || c0 > c1) {
				switch (idx) {
				case 0: rr = r[0]; gg = g[0]; bb = b[0]; break;
				case 1: rr = r[1]; gg = g[1]; bb = b[1]; break;
				case 2: rr = (2*r[0]+r[1])/3; gg = (2*g[0]+g[1])/3; bb = (2*b[0]+b[1])/3; break;
				default: rr = (r[0]+2*r[1])/3; gg = (g[0]+2*g[1])/3; bb = (b[0]+2*b[1])/3; break;
				}
			} else {
				switch (idx) {
				case 0: rr = r[0]; gg = g[0]; bb = b[0]; break;
				case 1: rr = r[1]; gg = g[1]; bb = b[1]; break;
				case 2: rr = (r[0]+r[1])/2; gg = (g[0]+g[1])/2; bb = (b[0]+b[1])/2; break;
				default: rr = 0; gg = 0; bb = 0; break;
				}
			}
			unsigned aa = 255;
			if (bc2) {
				const unsigned nib = (blk[ly * 2 + (lx >> 1)] >> ((lx & 1) * 4)) & 0xf;
				aa = nib * 255 / 15;
			} else if (bc3) {
				const unsigned a0 = blk[0], a1 = blk[1];
				// Six bytes of 3-bit indices, little-endian across the whole 48 bits.
				unsigned __int64 bits = 0;
				for (int k = 0; k < 6; ++k) bits |= (unsigned __int64)blk[2 + k] << (8 * k);
				const unsigned ai = (unsigned)((bits >> (3 * (ly * 4 + lx))) & 7);
				if (a0 > a1) {
					aa = (ai == 0) ? a0 : (ai == 1) ? a1
					   : (((8 - ai) * a0 + (ai - 1) * a1) / 7);
				} else {
					aa = (ai == 0) ? a0 : (ai == 1) ? a1 : (ai == 6) ? 0 : (ai == 7) ? 255
					   : (((6 - ai) * a0 + (ai - 1) * a1) / 5);
				}
			}
			out[i] = (aa << 24) | (rr << 16) | (gg << 8) | bb;
		}
		m_impl->context->Unmap(staging, 0);
		ok = true;
	}
	staging->Release();
	return ok;
#else
	(void)stage; (void)level; (void)out; (void)count;
	return false;
#endif
}

bool GfxDeviceD3D11::Reset_Swap_Chain(GfxSwapChainDesc & desc)
{
	TRACE("Reset_Swap_Chain");
	if (m_impl->swap_chain == nullptr) return false;

	m_impl->desc = desc;

	// Everything that views a back buffer has to go before ResizeBuffers, and DXGI will
	// say so rather than guess: it fails with DXGI_ERROR_INVALID_CALL while any view or
	// reference is outstanding. Unbinding the targets is what releases the last of the
	// runtime's own.
	m_impl->context->OMSetRenderTargets(0, nullptr, nullptr);
	m_impl->current_rt = nullptr;
	m_impl->current_ds = nullptr;
	m_impl->Invalidate_Bound_States();
	Release_Swap_Chain_Surfaces(m_impl);

	HRESULT hr = m_impl->swap_chain->ResizeBuffers(
		desc.BackBufferCount != 0 ? desc.BackBufferCount : 1,
		desc.Width, desc.Height, DXGI_FORMAT_B8G8R8A8_UNORM,
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
	if (FAILED(hr)) {
		WWDEBUG_SAY(("D3D11: ResizeBuffers failed (0x%08x).", (unsigned)hr));
		// The wrappers are left without textures, which every path here checks for. The
		// caller retries next frame, which is what the interface asks of it.
		return false;
	}

	if (!Attach_Swap_Chain_Surfaces(m_impl)) {
		WWDEBUG_SAY(("D3D11: could not re-make the back buffer after a resize."));
		return false;
	}

	GfxViewport vp;
	vp.X = 0;
	vp.Y = 0;
	vp.Width = m_impl->back_buffer->width;
	vp.Height = m_impl->back_buffer->height;
	vp.MinZ = 0.0f;
	vp.MaxZ = 1.0f;
	Set_Viewport(vp);

	WWDEBUG_SAY(("D3D11: swap chain reset to %ux%u.", desc.Width, desc.Height));
	return true;
}

void GfxDeviceD3D11::Report_Absorbed_State()
{
#ifdef RTS_DEBUG
	// The guard that made this silent under D3D9 went with the second backend, and so did
	// the ABSORBED half of the report: Phase 9 measured those writes dead at 0 differing
	// pixels on four scenes, and after that a count of them is a number with nothing to
	// compare it to. What is left is about this backend alone and every figure in it reads
	// its own control -- DROPPED, SIGNATURE MISMATCH, RENDER TARGET HAZARD, SAMPLER CENSUS.
	static unsigned frames = 0;
	const unsigned REPORT_INTERVAL = 100;
	if (++frames < REPORT_INTERVAL) return;
	frames = 0;

	WWDEBUG_SAY(("D3D11 RENDER TARGET HAZARD over 600 frames: %u shader-resource slots were "
		"force-unbound because the texture in them became the render target and %u were put "
		"back once it stopped being one. %u draws were submitted while a slot's texture "
		"really was the target -- legal, unavoidable and true of D3D9 too, so that figure "
		"does not go to zero. %u draws sampled through a slot this backend put back on its "
		"own initiative, and that is the bug: the wrapper's redundancy check never re-sends "
		"a texture it believes is still bound, so without the fix every one of those draws "
		"read a NULL slot as zero. A zero beside a zero first figure is the instrument not "
		"running.",
		s_srv_forced_unbound, s_srv_rebound, s_draws_target_conflict, s_draws_rescued));

	WWDEBUG_SAY(("D3D11 RENDER TARGET HAZARD, the narrowing's own control: %u of %u pixel "
		"shaders created declare at least one texture register, and the union of the "
		"registers they declare is 0x%02x. A zero here would make the narrowed draw count "
		"above read zero whatever the truth was -- it would be the RDEF parse failing, "
		"reported as a result.",
		s_ps_with_textures, s_ps_created, s_ps_texture_union));


	s_sampler_textured_stages = 0;
	s_sampler_stage_mip_none = 0;
	s_sampler_stage_mip_point = 0;
	s_sampler_stage_mip_linear = 0;
	s_sampler_stage_aniso = 0;
	s_sampler_stage_no_sampler = 0;

	LARGE_INTEGER freq;
	if (QueryPerformanceFrequency(&freq) && freq.QuadPart > 0) {
		const double to_ms = 1000.0 / (double)freq.QuadPart;
		const double inv_frames = 1.0 / (double)REPORT_INTERVAL;
		WWDEBUG_SAY(("D3D11 PROFILE (ms/frame over %u frames):", REPORT_INTERVAL));
		WWDEBUG_SAY(("  DrawIndexed:  %6.2f ms (cnt=%u, raw=%6.2f ms, prepare=%6.2f ms)",
			(double)s_d3d11_prof.t_draw_indexed * to_ms * inv_frames,
			s_d3d11_prof.n_draw_indexed,
			(double)s_d3d11_prof.t_raw_draw_call * to_ms * inv_frames,
			(double)s_d3d11_prof.t_prepare_draw * to_ms * inv_frames));
		WWDEBUG_SAY(("    Prepare:    layout=%6.2f ms, states=%6.2f ms, const_upload=%6.2f ms (vs_up=%u, ps_up=%u, frame_up=%u)",
			(double)s_d3d11_prof.t_input_layout * to_ms * inv_frames,
			(double)s_d3d11_prof.t_apply_states * to_ms * inv_frames,
			(double)s_d3d11_prof.t_upload_constants * to_ms * inv_frames,
			s_d3d11_prof.n_upload_vs, s_d3d11_prof.n_upload_ps, s_d3d11_prof.n_upload_frame));
		WWDEBUG_SAY(("  Draw / DrawUp: %6.2f ms / %6.2f ms (cnt=%u / %u)",
			(double)s_d3d11_prof.t_draw * to_ms * inv_frames,
			(double)s_d3d11_prof.t_draw_up * to_ms * inv_frames,
			s_d3d11_prof.n_draw, s_d3d11_prof.n_draw_up));
		WWDEBUG_SAY(("  Present:      %6.2f ms (drain=%6.2f ms, swap=%6.2f ms)",
			(double)s_d3d11_prof.t_present * to_ms * inv_frames,
			(double)s_d3d11_prof.t_drain_debug * to_ms * inv_frames,
			(double)s_d3d11_prof.t_swap_present * to_ms * inv_frames));
		WWDEBUG_SAY(("  SetRT / Clear: %6.2f ms / %6.2f ms (cnt=%u / %u)",
			(double)s_d3d11_prof.t_set_rt * to_ms * inv_frames,
			(double)s_d3d11_prof.t_clear * to_ms * inv_frames,
			s_d3d11_prof.n_set_rt, s_d3d11_prof.n_clear));
		WWDEBUG_SAY(("  SetTexture:   %6.2f ms (cnt=%u)",
			(double)s_d3d11_prof.t_set_texture * to_ms * inv_frames,
			s_d3d11_prof.n_set_texture));
		WWDEBUG_SAY(("  SetConstants: %6.2f ms (vs_cnt=%u, ps_cnt=%u, frame_cnt=%u)",
			((double)s_d3d11_prof.t_set_vs_const + (double)s_d3d11_prof.t_set_ps_const +
				(double)s_d3d11_prof.t_set_frame_const) * to_ms * inv_frames,
			s_d3d11_prof.n_set_vs_const, s_d3d11_prof.n_set_ps_const,
			s_d3d11_prof.n_set_frame_const));
		WWDEBUG_SAY(("  SetShaders:   vs=%6.2f ms, ps=%6.2f ms (cnt=%u / %u)",
			(double)s_d3d11_prof.t_set_vs * to_ms * inv_frames,
			(double)s_d3d11_prof.t_set_ps * to_ms * inv_frames,
			s_d3d11_prof.n_set_vs, s_d3d11_prof.n_set_ps));
		WWDEBUG_SAY(("  SetStream/IB: stream=%6.2f ms, ib=%6.2f ms (cnt=%u / %u)",
			(double)s_d3d11_prof.t_set_stream * to_ms * inv_frames,
			(double)s_d3d11_prof.t_set_ib * to_ms * inv_frames,
			s_d3d11_prof.n_set_stream, s_d3d11_prof.n_set_ib));
		WWDEBUG_SAY(("  SetState/VP:  rs=%6.2f ms, tss=%6.2f ms, vp=%6.2f ms (cnt=%u / %u / %u)",
			(double)s_d3d11_prof.t_set_rs * to_ms * inv_frames,
			(double)s_d3d11_prof.t_set_tss * to_ms * inv_frames,
			(double)s_d3d11_prof.t_set_vp * to_ms * inv_frames,
			s_d3d11_prof.n_set_rs, s_d3d11_prof.n_set_tss, s_d3d11_prof.n_set_vp));
		WWDEBUG_SAY(("  Map/UnmapBuf: %6.2f ms / %6.2f ms (cnt=%u / %u)",
			(double)s_d3d11_prof.t_map_buffer * to_ms * inv_frames,
			(double)s_d3d11_prof.t_unmap_buffer * to_ms * inv_frames,
			s_d3d11_prof.n_map_buffer, s_d3d11_prof.n_unmap_buffer));
		WWDEBUG_SAY(("  Dispatch:     %6.2f ms (cnt=%u); buffer read-back %6.2f ms (cnt=%u)",
			(double)s_d3d11_prof.t_dispatch * to_ms * inv_frames, s_d3d11_prof.n_dispatch,
			(double)s_d3d11_prof.t_map_buffer_read * to_ms * inv_frames,
			s_d3d11_prof.n_map_buffer_read));
		WWDEBUG_SAY(("  MapTexture:   map=%6.2f ms, unmap=%6.2f ms, readback=%6.2f ms (cnt=%u / %u / %u)",
			(double)s_d3d11_prof.t_map_texture * to_ms * inv_frames,
			(double)s_d3d11_prof.t_unmap_texture * to_ms * inv_frames,
			(double)s_d3d11_prof.t_readback_subresource * to_ms * inv_frames,
			s_d3d11_prof.n_map_texture, s_d3d11_prof.n_unmap_texture, s_d3d11_prof.n_readback_subresource));
		WWDEBUG_SAY(("  MapSurface:   map=%6.2f ms, unmap=%6.2f ms (cnt=%u / %u)",
			(double)s_d3d11_prof.t_map_surface * to_ms * inv_frames,
			(double)s_d3d11_prof.t_unmap_surface * to_ms * inv_frames,
			s_d3d11_prof.n_map_surface, s_d3d11_prof.n_unmap_surface));
		WWDEBUG_SAY(("  CopySurface:  fast=%6.2f ms (cnt=%u), rect/cpu=%6.2f ms (cnt=%u)",
			(double)s_d3d11_prof.t_copy_surface * to_ms * inv_frames,
			s_d3d11_prof.n_copy_surface,
			(double)s_d3d11_prof.t_copy_surface_rect * to_ms * inv_frames,
			s_d3d11_prof.n_copy_surface_rect));
		WWDEBUG_SAY(("  PASS TIMINGS: BackBuffer=%6.2f ms (draws=%u), Shadow=%6.2f ms (draws=%u), DepthPrepass=%6.2f ms (draws=%u), Other=%6.2f ms (draws=%u)",
			(double)s_d3d11_prof.t_pass_backbuffer * to_ms * inv_frames, s_d3d11_prof.n_draws_backbuffer,
			(double)s_d3d11_prof.t_pass_shadow * to_ms * inv_frames, s_d3d11_prof.n_draws_shadow,
			(double)s_d3d11_prof.t_pass_depthprepass * to_ms * inv_frames, s_d3d11_prof.n_draws_depthprepass,
			(double)s_d3d11_prof.t_pass_other * to_ms * inv_frames, s_d3d11_prof.n_draws_other));
		WWDEBUG_SAY(("  FRAME SPANS:  Render(Begin->End)=%6.2f ms (begin_cnt=%u, end_cnt=%u), Post=%6.2f ms, Logic=%6.2f ms",
			(double)s_d3d11_prof.t_render_span * to_ms * inv_frames,
			s_d3d11_prof.n_begin_scene, s_d3d11_prof.n_end_scene,
			(double)s_d3d11_prof.t_post_span * to_ms * inv_frames,
			(double)s_d3d11_prof.t_logic_span * to_ms * inv_frames));
	}
	s_d3d11_prof.Reset();
	s_dropped_no_vertex_shader = 0;
	s_dropped_trianglefan = 0;
	s_dropped_no_input_layout = 0;
	s_dropped_signature_mismatch = 0;
	s_draws = 0;
	s_srv_forced_unbound = 0;
	s_srv_rebound = 0;
	s_draws_target_conflict = 0;
	s_draws_target_conflict_read = 0;
	s_draws_target_conflict_noshader = 0;
	s_draws_rescued = 0;
	s_buffer_srv_forced_unbound = 0;
	s_buffer_uav_forced_unbound = 0;
#endif
}

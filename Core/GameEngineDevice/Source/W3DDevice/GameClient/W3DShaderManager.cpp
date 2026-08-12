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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: W3DShaderManager.cpp ////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//
//                       Westwood Studios Pacific.
//
//                       Confidential Information
//                Copyright (C) 2001 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: W3DShaderManager.cpp
//
// Created:   Mark Wilczynski, August 2001
//
// Desc:      Perform tests on currently selected WW3D/D3D device to determine
//			  which of our rendering features are supported.  The system allows
//			  setting up a few custom shaders that are selected based on video
//			  card features.
//
//			  To add a new shader to the system:
//			  0) Add your shader to the ShaderTypes enum
//			  1) Create shader using W3DShaderInterface
//			  2) Repeat step 1 for any alternate shaders
//			  3) Create list of alternate shaders sorted by order of preference.
//				 The first shader which passes hardware validation will be selected.
//			  4) Add list from step 3) to MasterShaderList[].
//
//-----------------------------------------------------------------------------

#include "WW3D2/dx8wrapper.h"
#include "WW3D2/assetmgr.h"
#include "WW3D2/ddsfile.h"
#include <unordered_map>
#include <math.h>
#include <string.h>
#include "Lib/BaseType.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DCustomScene.h"
#include "W3DDevice/GameClient/W3DSmudge.h"
#include "GameClient/View.h"
#include "GameClient/CommandXlat.h"
#include "GameClient/Display.h"
#include "GameClient/Water.h"
#include "GameLogic/GameLogic.h"
#include "Common/GlobalData.h"
#include "Common/OptionPreferences.h"
#include "Common/GameLOD.h"
#include "d3dx8tex.h"
#include "WW3D2/dx8caps.h"


// Turn this on to turn off pixel shaders. jba[4/3/2003]
#define do_not_DISABLE_PIXEL_SHADERS 1

/** Interface definition for custom shaders we define in our app.  These shaders can perform more complex
	operations than those allowed in the WW3D2 shader system.
*/
class W3DShaderInterface
{
public:
	Int getNumPasses() {return m_numPasses;};	///<return number of passes needed for this shader
	virtual Int set(Int pass) {return TRUE;};		///<setup shader for the specified rendering pass.
	 ///do any custom resetting necessary to bring W3D in sync.
	virtual void reset() {
		ShaderClass::Invalidate();
		DX8Wrapper::Set_DX8_Texture(0, nullptr);
		DX8Wrapper::Set_DX8_Texture(1, nullptr);};
	virtual Int init() = 0;			///<perform any one time initialization and validation
	virtual Int shutdown() { return TRUE;};			///<release resources used by shader
protected:
	Int m_numPasses;						///<number of passes to complete shader
};

//this table will contain custom versions of each shader tuned for specific video card and user options.
static W3DFilterInterface *W3DFilters[FT_MAX];
static W3DShaderInterface *W3DShaders[W3DShaderManager::ST_MAX];
static Int W3DShadersPassCount[W3DShaderManager::ST_MAX];	//number of passes for each of the above shaders
TextureClass *W3DShaderManager::m_Textures[8];
W3DShaderManager::ShaderTypes W3DShaderManager::m_currentShader;
FilterTypes W3DShaderManager::m_currentFilter=FT_NULL_FILTER; ///< Last filter that was set.
Int W3DShaderManager::m_currentShaderPass;
ChipsetType W3DShaderManager::m_currentChipset;
GraphicsVenderID W3DShaderManager::m_currentVendor;
__int64 W3DShaderManager::m_driverVersion;

Bool W3DShaderManager::m_renderingToTexture = false;
Bool W3DShaderManager::m_sceneHistoryCaptured = false;
IDirect3DSurface8 *W3DShaderManager::m_oldRenderSurface=nullptr;	///<previous render target
DWORD W3DShaderManager::m_debugDepthPS = 0;
DWORD W3DShaderManager::m_debugShadowPS = 0;
DWORD W3DShaderManager::m_debugBloomPS = 0;
DWORD W3DShaderManager::m_debugShroudPS = 0;
IDirect3DTexture8 *W3DShaderManager::m_debugBrightTexture = nullptr;
IDirect3DSurface8 *W3DShaderManager::m_debugBrightSurface = nullptr;
IDirect3DTexture8 *W3DShaderManager::m_renderTexture=nullptr;		///<texture into which rendering will be redirected.
IDirect3DSurface8 *W3DShaderManager::m_newRenderSurface=nullptr;	///<new render target inside m_renderTexture
IDirect3DSurface8 *W3DShaderManager::m_resolveSurface=nullptr;	///<MSAA resolve destination (m_renderTexture surface) when MSAA is on
IDirect3DSurface8 *W3DShaderManager::m_oldDepthSurface=nullptr;	///<previous depth buffer surface
IDirect3DTexture8 *W3DShaderManager::m_pShadowMapTexture=nullptr;
IDirect3DSurface8 *W3DShaderManager::m_pShadowMapSurface=nullptr;
IDirect3DSurface8 *W3DShaderManager::m_pShadowMapDepthSurface=nullptr;
IDirect3DSurface8 *W3DShaderManager::m_shadowSavedRT=nullptr;
IDirect3DSurface8 *W3DShaderManager::m_shadowSavedDepth=nullptr;
DWORD W3DShaderManager::m_shadowSavedStates[W3DShaderManager::NUM_SHADOW_SAVED_STATES]={0};
/*===========================================================================================*/
/*=========      Screen Shaders	=============================================================*/
/*===========================================================================================*/

class ScreenDefaultFilter : public W3DFilterInterface
{
public:
	virtual Int init() override;			///<perform any one time initialization and validation
	virtual Bool preRender(Bool &skipRender, CustomScenePassModes &scenePassMode) override; ///< Set up at start of render.  Only applies to screen filter shaders.
	virtual Bool postRender(FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender) override; ///< Called after render.  Only applies to screen filter shaders.
	virtual Bool setup(FilterModes mode) override {return true;} ///< Called when the filter is started, one time before the first prerender.
protected:
	virtual Int set(FilterModes mode) override;		///<setup shader for the specified rendering pass.
	virtual void reset() override;		///<do any custom resetting necessary to bring W3D in sync.
};

ScreenDefaultFilter screenDefaultFilter;

///Default filter that just renders screen to off-screen texture and then copies it the the screen.
///Useful because we added some full-time unit effects (microwave tank smudge) to Generals MD that need access
///to the background as a texture.  This filter makes that texture always available for these effects.
W3DFilterInterface *ScreenDefaultFilterList[]=
{
	&screenDefaultFilter,
	nullptr
};

Int ScreenDefaultFilter::init()
{
	if (!W3DShaderManager::canRenderToTexture()) {
		// Have to be able to render to texture.
		return FALSE;
	}

	//Can render to texture, but we don't know if it can read and write to the same texture.
	//Since there is no D3D caps bit to tell you this, we will just hard-code some specific
	//cards that we know should work.

	Int res;

	if ((res=W3DShaderManager::getChipset()) != DC_UNKNOWN)
	{
		if ( res >=	DC_GEFORCE2)
		{
			//Check if their driver is newer than what we tested for this vendor
/*			if (TheGameLODManager)
			{
				if (TheGameLODManager->getTestedDriverVersion(W3DShaderManager::getCurrentVendor()) < W3DShaderManager::getCurrentDriverVersion())
					return FALSE;
			}*/
		}
	}

	W3DFilters[FT_VIEW_DEFAULT]=&screenDefaultFilter;

	return TRUE;
}

Bool ScreenDefaultFilter::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	// TheSuperHackers @bugfix Disable Render To Texture redirection for the default filter
	// When MSAA is forced by Nvidia driver profile depth buffer is multisampled internally.
	// Rendering to non-MSAA texture with this depth buffer corrupts depth testing producing black screen
	// The smudge system has its own Copy path that works without Render To Texture.
	return FALSE;
}

Bool ScreenDefaultFilter::postRender(FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender)
{
	IDirect3DTexture8 * tex =	W3DShaderManager::endRenderToTexture();
	DEBUG_ASSERTCRASH(tex, ("Require rendered texture."));
	if (!tex) return false;
	if (!set(mode)) return false;

	LPDIRECT3DDEVICE8 pDev=DX8Wrapper::_Get_D3D_Device8();

	struct _TRANS_LIT_TEX_VERTEX {
		D3DXVECTOR4 p;
		DWORD color;   // diffuse color
		float	u;
		float	v;
	} v[4];

	Int xpos, ypos, width, height;

	DX8Wrapper::Set_DX8_Texture(0,tex);	//previously rendered frame inside this texture
	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

	//bottom right
	v[0].p = D3DXVECTOR4( xpos+width-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[0].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[0].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top right
	v[1].p = D3DXVECTOR4( xpos+width-0.5f, ypos-0.5f, 0.0f, 1.0f );
	v[1].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[1].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	//bottom left
	v[2].p = D3DXVECTOR4(  xpos-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[2].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[2].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top left
	v[3].p = D3DXVECTOR4(  xpos-0.5f,  ypos-0.5f, 0.0f, 1.0f );
	v[3].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[3].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	v[0].color = 0xffffffff;
	v[1].color = 0xffffffff;
	v[2].color = 0xffffffff;
	v[3].color = 0xffffffff;

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	DX8Wrapper::Set_Vertex_Shader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

	pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));

	reset();
	return true;
}

Int ScreenDefaultFilter::set(FilterModes mode)
{
	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
	DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaqueShader);
	DX8Wrapper::Set_Texture(0,nullptr);
	DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_ALWAYS);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE,FALSE);
	DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

	return true;
}

void ScreenDefaultFilter::reset()
{
	DX8Wrapper::Set_DX8_Texture(0,nullptr);	//previously rendered frame inside this texture
	DX8Wrapper::Invalidate_Cached_Render_States();
}

/*=========  ScreenBloomFilter  =========================================================*/
///Screen-space bloom: extract bright pixels, blur them, and add the glow back over the
///scene. Installed as the view's default filter when render-to-texture is available;
///drawn entirely with fullscreen XYZRHW quads + ps_2_0 pixel shaders. The bloom look
///(threshold / knee / intensity) is tuned by editing the bloom_*_ps.hlsl shaders and
///recompiling them -- no engine rebuild needed. The only per-frame shader constant set
///from here is the blur step, which depends on the runtime target size.

// Fullscreen-quad vertex: pre-transformed position + diffuse + two texcoord sets
// (TEXCOORD0 = scene / pass source, TEXCOORD1 = bloom target).
struct BloomVtx
{
	D3DXVECTOR4 p;
	DWORD       color;
	float       u0, v0;
	float       u1, v1;
};

///Set stage to linear filtering with clamped addressing -- what every screen-space
///quad wants, and what none of them should be re-deriving for itself.
void W3DShaderManager::setLinearClampSampler(DWORD stage)
{
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MIPFILTER, D3DTEXF_NONE);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
}

// Draw a screen-space quad over [dx,dy]..[dx+dw,dy+dh] sampling source UVs
// (sU0..sU1, sV0..sV1) on TEXCOORD0 and a second set on TEXCOORD1.
//
// Written for the bloom chain and now shared with the debug visualizations, which is
// why it is a member rather than a file static: two subsystems putting a rectangle on
// screen should not each carry their own copy of the half-texel offset and the vertex
// layout that goes with it.
HRESULT W3DShaderManager::drawScreenQuad(LPDIRECT3DDEVICE8 dev,
	float dx, float dy, float dw, float dh,
	float sU0, float sV0, float sU1, float sV1,
	float bU0, float bV0, float bU1, float bV1)
{
	const float ox = dx - 0.5f, oy = dy - 0.5f;  // -0.5 texel: align pixels to texels
	BloomVtx v[4];
	v[0].p = D3DXVECTOR4(ox + dw, oy + dh, 0.0f, 1.0f); v[0].u0 = sU1; v[0].v0 = sV1; v[0].u1 = bU1; v[0].v1 = bV1;
	v[1].p = D3DXVECTOR4(ox + dw, oy,      0.0f, 1.0f); v[1].u0 = sU1; v[1].v0 = sV0; v[1].u1 = bU1; v[1].v1 = bV0;
	v[2].p = D3DXVECTOR4(ox,      oy + dh, 0.0f, 1.0f); v[2].u0 = sU0; v[2].v0 = sV1; v[2].u1 = bU0; v[2].v1 = bV1;
	v[3].p = D3DXVECTOR4(ox,      oy,      0.0f, 1.0f); v[3].u0 = sU0; v[3].v0 = sV0; v[3].u1 = bU0; v[3].v1 = bV0;
	v[0].color = v[1].color = v[2].color = v[3].color = 0xffffffff;
	// Solid, always. D3DRS_FILLMODE is sticky device state that nothing else here resets,
	// so DEBUG_VIS_WIREFRAME -- which sets it per scene draw -- otherwise leaks into the
	// whole post-process chain, and a wireframe composite quad draws two diagonal lines
	// instead of the frame. There is no case where a screen-space quad wants to be
	// anything but solid, so this belongs here rather than in every caller.
	DX8Wrapper::Set_DX8_Render_State(D3DRS_FILLMODE, D3DFILL_SOLID);
	DX8Wrapper::Set_Vertex_Shader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX2);
	return dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(BloomVtx));
}

class ScreenBloomFilter : public W3DFilterInterface
{
public:
	virtual Int init() override;
	virtual Int shutdown() override;
	virtual Bool preRender(Bool &skipRender, CustomScenePassModes &scenePassMode) override;
	virtual Bool postRender(FilterModes mode, Coord2D &scrollDelta, Bool &doExtraRender) override;
	virtual Bool setup(FilterModes mode) override { return true; }
protected:
	virtual Int set(FilterModes mode) override { return true; }
	virtual void reset() override;

	static DWORD m_brightPS;
	static DWORD m_blurPS;
	static DWORD m_compositePS;
	static IDirect3DTexture8 *m_texA;   // ping
	static IDirect3DTexture8 *m_texB;   // pong
	static IDirect3DSurface8 *m_surfA;
	static IDirect3DSurface8 *m_surfB;
	static Int m_w;
	static Int m_h;
};

DWORD ScreenBloomFilter::m_brightPS = 0;
DWORD ScreenBloomFilter::m_blurPS = 0;
DWORD ScreenBloomFilter::m_compositePS = 0;
IDirect3DTexture8 *ScreenBloomFilter::m_texA = nullptr;
IDirect3DTexture8 *ScreenBloomFilter::m_texB = nullptr;
IDirect3DSurface8 *ScreenBloomFilter::m_surfA = nullptr;
IDirect3DSurface8 *ScreenBloomFilter::m_surfB = nullptr;
Int ScreenBloomFilter::m_w = 0;
Int ScreenBloomFilter::m_h = 0;

ScreenBloomFilter screenBloomFilter;

W3DFilterInterface *ScreenBloomFilterList[] =
{
	&screenBloomFilter,
	nullptr
};

Int ScreenBloomFilter::init()
{
	if (!W3DShaderManager::canRenderToTexture())
		return FALSE;   // bloom needs the scene rendered into a texture

	LPDIRECT3DDEVICE8 dev = DX8Wrapper::_Get_D3D_Device8();
	IDirect3DTexture8 *sceneTex = W3DShaderManager::getRenderTexture();
	if (!dev || !sceneTex)
		return FALSE;

	// Need at least SM1.1-class hardware for the fullscreen pixel shaders.
	if (W3DShaderManager::getChipset() < DC_GENERIC_PIXEL_SHADER_1_1)
		return FALSE;

	if (FAILED(W3DShaderManager::LoadAndCreateD3DShader("shaders\\bloom_bright_ps.pso",    nullptr, 0, false, &m_brightPS)) ||
	    FAILED(W3DShaderManager::LoadAndCreateD3DShader("shaders\\bloom_blur_ps.pso",      nullptr, 0, false, &m_blurPS)) ||
	    FAILED(W3DShaderManager::LoadAndCreateD3DShader("shaders\\bloom_composite_ps.pso", nullptr, 0, false, &m_compositePS)))
	{
		shutdown();
		return FALSE;
	}

	// Quarter-resolution ping/pong bloom targets, in the scene texture's format.
	D3DSURFACE_DESC sd;
	if (FAILED(sceneTex->GetLevelDesc(0, &sd)))
	{
		shutdown();
		return FALSE;
	}
	m_w = (Int)sd.Width  / 4;  if (m_w < 1) m_w = 1;
	m_h = (Int)sd.Height / 4;  if (m_h < 1) m_h = 1;

	if (FAILED(dev->CreateTexture(m_w, m_h, 1, D3DUSAGE_RENDERTARGET, sd.Format, D3DPOOL_DEFAULT, &m_texA)) ||
	    FAILED(dev->CreateTexture(m_w, m_h, 1, D3DUSAGE_RENDERTARGET, sd.Format, D3DPOOL_DEFAULT, &m_texB)) ||
	    FAILED(m_texA->GetSurfaceLevel(0, &m_surfA)) ||
	    FAILED(m_texB->GetSurfaceLevel(0, &m_surfB)))
	{
		shutdown();
		return FALSE;
	}

	W3DFilters[FT_VIEW_BLOOM] = &screenBloomFilter;
	return TRUE;
}

Int ScreenBloomFilter::shutdown()
{
	W3DFilters[FT_VIEW_BLOOM] = nullptr;   // don't leave a stale pointer if re-init fails
	SAFE_RELEASE(m_surfA);
	SAFE_RELEASE(m_surfB);
	SAFE_RELEASE(m_texA);
	SAFE_RELEASE(m_texB);
	if (m_brightPS)    { reinterpret_cast<IDirect3DPixelShader9*>(m_brightPS)->Release();    m_brightPS = 0; }
	if (m_blurPS)      { reinterpret_cast<IDirect3DPixelShader9*>(m_blurPS)->Release();      m_blurPS = 0; }
	if (m_compositePS) { reinterpret_cast<IDirect3DPixelShader9*>(m_compositePS)->Release(); m_compositePS = 0; }
	return TRUE;
}

Bool ScreenBloomFilter::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	skipRender = false;
	W3DShaderManager::startRenderToTexture();   // redirect the scene into the render texture
	return true;
}

Bool ScreenBloomFilter::postRender(FilterModes mode, Coord2D &scrollDelta, Bool &doExtraRender)
{
	IDirect3DTexture8 *sceneTex = W3DShaderManager::endRenderToTexture();   // restores back buffer as target
	if (!sceneTex)
		return false;

	LPDIRECT3DDEVICE8 dev = DX8Wrapper::_Get_D3D_Device8();
	if (!dev || !m_surfA || !m_surfB || !m_brightPS || !m_blurPS || !m_compositePS)
		return true;   // bloom unavailable this frame; scene already in the back buffer target

	// Capture the restored back buffer + depth so the composite can return to them
	// after bouncing through the reduced-resolution bloom targets.
	IDirect3DSurface8 *backBuf = nullptr, *backDepth = nullptr;
	dev->GetRenderTarget(0, &backBuf);
	dev->GetDepthStencilSurface(&backDepth);

	// Scene occupies the tactical viewport sub-rect of its (back-buffer-sized) texture.
	Int xpos, ypos, width, height;
	TheTacticalView->getOrigin(&xpos, &ypos);
	width  = TheTacticalView->getWidth();
	height = TheTacticalView->getHeight();
	const float dispW = (float)TheDisplay->getWidth();
	const float dispH = (float)TheDisplay->getHeight();
	const float su0 = (float)xpos / dispW,          sv0 = (float)ypos / dispH;
	const float su1 = (float)(xpos + width) / dispW, sv1 = (float)(ypos + height) / dispH;

	// Common state for all fullscreen passes: opaque, no depth test/write, no blend.
	VertexMaterialClass *vmat = VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);
	DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaqueShader);
	DX8Wrapper::Set_Texture(0, nullptr);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC, D3DCMP_ALWAYS);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE, FALSE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, FALSE);
	DX8Wrapper::Apply_Render_State_Changes();

	// Pass 1: bright-pass. scene(sub-rect) -> texA (quarter-res). Threshold/knee are
	// baked into bloom_bright_ps.hlsl.
	DX8Wrapper::Set_DX8_Render_Target(m_surfA, nullptr);
	DX8Wrapper::Set_Pixel_Shader(m_brightPS);
	DX8Wrapper::Set_DX8_Texture(0, sceneTex);
	W3DShaderManager::setLinearClampSampler(0);
	W3DShaderManager::drawScreenQuad(dev, 0.0f, 0.0f, (float)m_w, (float)m_h, su0, sv0, su1, sv1, 0, 0, 1, 1);
	// Copy it before the blurs run, for DEBUG_VIS_BLOOM. Does nothing unless that mode
	// is on. It has to happen here: passes 2 and 3 ping-pong through this very pair of
	// targets, so by the end of the frame neither holds the unblurred result.
	W3DShaderManager::captureBloomBrightPass(m_surfA, m_w, m_h);

	// Pass 2: horizontal blur. texA -> texB.
	DX8Wrapper::Set_DX8_Render_Target(m_surfB, nullptr);
	DX8Wrapper::Set_Pixel_Shader(m_blurPS);
	DX8Wrapper::Set_Pixel_Shader_Constant(0, D3DXVECTOR4(1.0f / (float)m_w, 0.0f, 0.0f, 0.0f), 1);
	DX8Wrapper::Set_DX8_Texture(0, m_texA);
	W3DShaderManager::setLinearClampSampler(0);
	W3DShaderManager::drawScreenQuad(dev, 0.0f, 0.0f, (float)m_w, (float)m_h, 0, 0, 1, 1, 0, 0, 1, 1);

	// Pass 3: vertical blur. texB -> texA.
	DX8Wrapper::Set_DX8_Render_Target(m_surfA, nullptr);
	DX8Wrapper::Set_Pixel_Shader_Constant(0, D3DXVECTOR4(0.0f, 1.0f / (float)m_h, 0.0f, 0.0f), 1);
	DX8Wrapper::Set_DX8_Texture(0, m_texB);
	W3DShaderManager::setLinearClampSampler(0);
	W3DShaderManager::drawScreenQuad(dev, 0.0f, 0.0f, (float)m_w, (float)m_h, 0, 0, 1, 1, 0, 0, 1, 1);

	// Pass 4: composite scene + bloom -> back buffer (over the tactical rect).
	// Bloom intensity is baked into bloom_composite_ps.hlsl.
	DX8Wrapper::Set_DX8_Render_Target(backBuf, backDepth);
	DX8Wrapper::Set_Pixel_Shader(m_compositePS);
	DX8Wrapper::Set_DX8_Texture(0, sceneTex);
	DX8Wrapper::Set_DX8_Texture(1, m_texA);
	W3DShaderManager::setLinearClampSampler(0);
	W3DShaderManager::setLinearClampSampler(1);
	W3DShaderManager::drawScreenQuad(dev, (float)xpos, (float)ypos, (float)width, (float)height, su0, sv0, su1, sv1, 0, 0, 1, 1);

	SAFE_RELEASE(backBuf);
	SAFE_RELEASE(backDepth);
	reset();
	return true;
}

void ScreenBloomFilter::reset()
{
	LPDIRECT3DDEVICE8 dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev)
	{
		DX8Wrapper::Set_Pixel_Shader(0);   // unbind the bloom pixel shader
		DX8Wrapper::Set_DX8_Texture(0, nullptr);
		DX8Wrapper::Set_DX8_Texture(1, nullptr);
	}
	DX8Wrapper::Invalidate_Cached_Render_States();
}

/*=========  ScreenBWFilter	=============================================================*/
///converts viewport to black & white.

Int ScreenBWFilter::m_fadeFrames;
Int ScreenBWFilter::m_curFadeFrame;
Real ScreenBWFilter::m_curFadeValue;
Int ScreenBWFilter::m_fadeDirection;

ScreenBWFilter screenBWFilter;
ScreenBWFilterDOT3 screenBWFilterDOT3;	//slower version for older cards without pixel shaders.

///List of different BW shader implementations in order of preference
W3DFilterInterface *ScreenBWFilterList[]=
{
	&screenBWFilter,
	&screenBWFilterDOT3,	//slower version for older cards without pixel shaders.
	nullptr
};

Int ScreenBWFilter::init()
{
	Int res;
	HRESULT hr;

	m_dwBWPixelShader = 0;
	m_curFadeFrame = 0;

	if (!W3DShaderManager::canRenderToTexture()) {
		// Have to be able to render to texture.
		return false;
	}

	if ((res=W3DShaderManager::getChipset()) != 0)
	{
		if (res >= DC_GENERIC_PIXEL_SHADER_1_1)
		{
			//this shader needs some assets that need to be loaded
			//shader decleration
			DWORD Declaration[]=
			{
				(D3DVSD_STREAM(0)),
				(D3DVSD_REG(0, D3DVSDT_FLOAT3)), // Position
				(D3DVSD_REG(1, D3DVSDT_D3DCOLOR)), // Diffuse
				(D3DVSD_REG(2, D3DVSDT_FLOAT2)), //  Texture Coordinates
				(D3DVSD_END())
			};

			//Monochrome pixel shader.
			hr = W3DShaderManager::LoadAndCreateD3DShader("shaders\\monochrome.pso", &Declaration[0], 0, false, &m_dwBWPixelShader);
			if (FAILED(hr))
				return FALSE;

			W3DFilters[FT_VIEW_BW_FILTER]=&screenBWFilter;

			return TRUE;
		}
	}
	return FALSE;
}

Bool ScreenBWFilter::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	skipRender = false;
	W3DShaderManager::startRenderToTexture();
	return true;
}

Bool ScreenBWFilter::postRender(FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender)
{
	IDirect3DTexture8 * tex =	W3DShaderManager::endRenderToTexture();
	DEBUG_ASSERTCRASH(tex, ("Require rendered texture."));
	if (!tex) return false;
	if (!set(mode)) return false;

	LPDIRECT3DDEVICE8 pDev=DX8Wrapper::_Get_D3D_Device8();

	struct _TRANS_LIT_TEX_VERTEX {
		D3DXVECTOR4 p;
		DWORD color;   // diffuse color
		float	u;
		float	v;
	} v[4];

	Int xpos, ypos, width, height;

	DX8Wrapper::Set_DX8_Texture(0,tex);	//previously rendered frame inside this texture
	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

	//bottom right
	v[0].p = D3DXVECTOR4( xpos+width-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[0].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[0].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top right
	v[1].p = D3DXVECTOR4( xpos+width-0.5f, ypos-0.5f, 0.0f, 1.0f );
	v[1].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[1].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	//bottom left
	v[2].p = D3DXVECTOR4(  xpos-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[2].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[2].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top left
	v[3].p = D3DXVECTOR4(  xpos-0.5f,  ypos-0.5f, 0.0f, 1.0f );
	v[3].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[3].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	v[0].color = 0xffffffff;
	v[1].color = 0xffffffff;
	v[2].color = 0xffffffff;
	v[3].color = 0xffffffff;

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	DX8Wrapper::Set_Vertex_Shader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

	pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));

	reset();
	return true;
}

Int ScreenBWFilter::set(FilterModes mode)
{
	if (mode > FM_NULL_MODE)
	{	//rendering a quad with redirected rendering surface tinted by pixel shader

		if (m_fadeDirection > 0)
		{	//turning effect on
			m_curFadeFrame++;
			Int fade = m_curFadeFrame;

			if (fade<m_fadeFrames)
			{
				m_curFadeValue = (Real)fade/(Real)m_fadeFrames;
			}
			else
			{
				m_curFadeFrame = 0;
				m_curFadeValue = 1.0f;
				m_fadeDirection = 0;
			}
		}
		else
		if (m_fadeDirection < 0)
		{	//turning effect off
			m_curFadeFrame++;
			Int fade = m_curFadeFrame;
			if (fade<m_fadeFrames)
			{
				m_curFadeValue = 1.0f - (Real)fade/(Real)m_fadeFrames;
			}
			else
			{	m_curFadeValue = 0.0f;
				TheTacticalView->setViewFilterMode(FM_NULL_MODE);
				TheTacticalView->setViewFilter(FT_NULL_FILTER);
				m_curFadeFrame = 0;
				m_fadeDirection = 0;
			}
		}

		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		DX8Wrapper::Set_Material(vmat);
		REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
		DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaqueShader);
		DX8Wrapper::Set_Texture(0,nullptr);
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_ALWAYS);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE,FALSE);
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

		DX8Wrapper::Set_Pixel_Shader(m_dwBWPixelShader);
		DX8Wrapper::Set_Pixel_Shader_Constant(0,   D3DXVECTOR4(0.3f, 0.59f, 0.11f, 1.0f), 1);

		D3DXVECTOR4	color(1.0f,1.0f,1.0f,1.0f);	//multiply color

		if (mode == FM_VIEW_BW_BLACK_AND_WHITE)
		{	//back & white mode
			color.x=1.0f;
			color.y=1.0f;
			color.z=1.0f;
		}
		if (mode == FM_VIEW_BW_RED_AND_WHITE)
		{	//red is on
			color.x = 1.0f;
			color.y = 0.0f;
			color.z = 0.0f;
			//inverse red is on
			//red is on
//			color.x = 0.0f;
//			color.y = 1.0f;
//			color.z = 1.0f;
		}
		if (mode == FM_VIEW_BW_GREEN_AND_WHITE)
		{
			color.x = 0.0f;
			color.y = 1.0f;
			color.z = 0.0f;
		}

		DX8Wrapper::Set_Pixel_Shader_Constant(1,   color, 1);
		DX8Wrapper::Set_Pixel_Shader_Constant(2,	D3DXVECTOR4(m_curFadeValue, m_curFadeValue, m_curFadeValue, 1.0f), 1);
/*		DX8Wrapper::Set_Pixel_Shader_Constant(2,   D3DXVECTOR4(150.0f/255.0f, 150.0f/255.0f, 150.0f/255.0f, 0.0f), 1);
		DX8Wrapper::Set_Pixel_Shader_Constant(3,   D3DXVECTOR4((765.0f/450.0f)/3, (765.0f/450.0f)/3, (765.0f/450.0f)/3, 1.0f), 1);
		DX8Wrapper::Set_Pixel_Shader_Constant(4,   D3DXVECTOR4(0.5f, 0.5f, 0.5f, 0), 1);
		DX8Wrapper::Set_Pixel_Shader_Constant(5,   D3DXVECTOR4((60.0f)/255.0f, (60.0f)/255.0f, (60.0f)/255.0f, 0), 1);
		DX8Wrapper::Set_Pixel_Shader_Constant(6,   D3DXVECTOR4((157.0f)/255.0f, (157.0f)/255.0f, (157.0f)/255.0f, 0), 1);
		DX8Wrapper::Set_Pixel_Shader_Constant(7,   D3DXVECTOR4((30.0f)/255.0f, (30.0f)/255.0f, (30.0f)/255.0f, 0), 1);
*/
		return true;
	}
	return false;
}

void ScreenBWFilter::reset()
{
	DX8Wrapper::Set_DX8_Texture(0,nullptr);	//previously rendered frame inside this texture
	DX8Wrapper::Set_Pixel_Shader(0);	//turn off pixel shader
	DX8Wrapper::Invalidate_Cached_Render_States();
}

Int ScreenBWFilter::shutdown()
{
	if (m_dwBWPixelShader)
		DX8Wrapper::_Get_D3D_Device8()->DeletePixelShader(m_dwBWPixelShader);

	m_dwBWPixelShader=0;

	return TRUE;
}

/**Alternate version of the above filter which does not require pixel shaders - good for older cards*/
Int ScreenBWFilterDOT3::init()
{
	Int res;

	m_curFadeFrame = 0;

	if (!W3DShaderManager::canRenderToTexture()) {
		// Have to be able to render to texture.
		return false;
	}

	if ((res=W3DShaderManager::getChipset()) != 0)
	{
			W3DFilters[FT_VIEW_BW_FILTER]=&screenBWFilterDOT3;
			return TRUE;
	}
	return FALSE;
}

Bool ScreenBWFilterDOT3::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	skipRender = false;
	W3DShaderManager::startRenderToTexture();
	return true;
}

Bool ScreenBWFilterDOT3::postRender(FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender)
{
	IDirect3DTexture8 * tex =	W3DShaderManager::endRenderToTexture();
	DEBUG_ASSERTCRASH(tex, ("Require rendered texture."));
	if (!tex) return false;
	if (!set(mode)) return false;

	LPDIRECT3DDEVICE8 pDev=DX8Wrapper::_Get_D3D_Device8();

	struct _TRANS_LIT_TEX_VERTEX {
		D3DXVECTOR4 p;
		DWORD color;   // diffuse color
		float	u;
		float	v;
	} v[4];

	Int xpos, ypos, width, height;

	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

	//bottom right
	v[0].p = D3DXVECTOR4( xpos+width-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[0].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[0].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top right
	v[1].p = D3DXVECTOR4( xpos+width-0.5f, ypos-0.5f, 0.0f, 1.0f );
	v[1].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[1].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	//bottom left
	v[2].p = D3DXVECTOR4(  xpos-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[2].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[2].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top left
	v[3].p = D3DXVECTOR4(  xpos-0.5f,  ypos-0.5f, 0.0f, 1.0f );
	v[3].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[3].v = (Real)(ypos)/(Real)TheDisplay->getHeight();

	DWORD currentFade=(((Int)((1.0f-m_curFadeValue) * 255.0f))<<24) | 0x00ffffff;	//store alpha value

	v[0].color = currentFade;
	v[1].color = currentFade;
	v[2].color = currentFade;
	v[3].color = currentFade;

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	DX8Wrapper::Set_Vertex_Shader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

	//Draw B&W version first
	if (DX8Wrapper::Get_Current_Caps()->Support_Dot3())
	{	//Override W3D states with customizations for grayscale
		DX8Wrapper::Set_DX8_Render_State(D3DRS_TEXTUREFACTOR, 0x80A5CA8E);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG0, D3DTA_TFACTOR | D3DTA_ALPHAREPLICATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_TFACTOR | D3DTA_ALPHAREPLICATE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP, D3DTOP_MULTIPLYADD);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_CURRENT);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_TFACTOR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP, D3DTOP_DOTPRODUCT3);
	}
	else
	{	//doesn't have DOT3 blend mode so fake it another way.
		DX8Wrapper::Set_DX8_Render_State(D3DRS_TEXTUREFACTOR, 0x60606060);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	}

	DX8Wrapper::Set_DX8_Texture(0,tex);	//previously rendered frame inside this texture

	pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));

	//Draw normal view blended by current fade level
	ShaderClass::Invalidate();	//reset DOT3 blend from above.
	ShaderClass shader=ShaderClass::_PresetAlphaShader;
	shader.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);
	DX8Wrapper::Set_Shader(shader);
	DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices
	//replace texture alpha with vertex alpha
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);

	pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));

	reset();
	return true;
}

Int ScreenBWFilterDOT3::set(FilterModes mode)
{
	if (mode > FM_NULL_MODE)
	{	//rendering a quad with redirected rendering surface tinted by pixel shader

		if (m_fadeDirection > 0)
		{	//turning effect on
			m_curFadeFrame++;
			Int fade = m_curFadeFrame;

			if (fade<m_fadeFrames)
			{
				m_curFadeValue = (Real)fade/(Real)m_fadeFrames;
			}
			else
			{
				m_curFadeFrame = 0;
				m_curFadeValue = 1.0f;
				m_fadeDirection = 0;
			}
		}
		else
		if (m_fadeDirection < 0)
		{	//turning effect off
			m_curFadeFrame++;
			Int fade = m_curFadeFrame;
			if (fade<m_fadeFrames)
			{
				m_curFadeValue = 1.0f - (Real)fade/(Real)m_fadeFrames;
			}
			else
			{	m_curFadeValue = 0.0f;
				TheTacticalView->setViewFilterMode(FM_NULL_MODE);
				TheTacticalView->setViewFilter(FT_NULL_FILTER);
				m_curFadeFrame = 0;
				m_fadeDirection = 0;
			}
		}

		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		DX8Wrapper::Set_Material(vmat);
		REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
		DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaqueShader);
		DX8Wrapper::Set_Texture(0,nullptr);
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_ALWAYS);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE,FALSE);
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

		return true;
	}
	return false;
}

void ScreenBWFilterDOT3::reset()
{
	DX8Wrapper::Set_DX8_Texture(0,nullptr);	//previously rendered frame inside this texture
	DX8Wrapper::Invalidate_Cached_Render_States();
}

Int ScreenBWFilterDOT3::shutdown()
{
	return TRUE;
}

/*=========  ScreenCrossFadeFilter	=============================================================*/
///Fades screen between 2 different views of the scene with both being visible at once.

Int ScreenCrossFadeFilter::m_fadeFrames;
Int ScreenCrossFadeFilter::m_curFadeFrame;
Real ScreenCrossFadeFilter::m_curFadeValue;
Int ScreenCrossFadeFilter::m_fadeDirection;
TextureClass *ScreenCrossFadeFilter::m_fadePatternTexture=nullptr;
Bool ScreenCrossFadeFilter::m_skipRender = FALSE;

ScreenCrossFadeFilter screenCrossFadeFilter;

///List of different BW shader implementations in order of preference
///@todo: Add a version that doesn't require pixel shader
W3DFilterInterface *ScreenCrossFadeFilterList[]=
{
	&screenCrossFadeFilter,
	nullptr
};

Int ScreenCrossFadeFilter::init()
{
	if (!TheDisplay)
		return FALSE;	//effect is useless without a view so no point initializing for the WB, etc.

	m_curFadeFrame = 0;

	if (!W3DShaderManager::canRenderToTexture())
		// Have to be able to render to texture.
		return FALSE;

	//Load an alpha mask texture that will mix foreground/background views.
	m_fadePatternTexture=WW3DAssetManager::Get_Instance()->Get_Texture("exmask_g.tga");
	if (!m_fadePatternTexture)
		return FALSE;
	m_fadePatternTexture->Get_Filter().Set_U_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
	m_fadePatternTexture->Get_Filter().Set_V_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_CLAMP);
	m_fadePatternTexture->Get_Filter().Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_NONE);

	W3DFilters[FT_VIEW_CROSSFADE]=&screenCrossFadeFilter;

	return TRUE;
}

Bool ScreenCrossFadeFilter::updateFadeLevel()
{
	if (m_fadeDirection > 0)
	{	//turning effect on
		m_curFadeFrame++;
		Int fade = m_curFadeFrame;

		if (fade<m_fadeFrames)
		{
			m_curFadeValue = (Real)fade/(Real)m_fadeFrames;
		}
		else
		{
			m_curFadeFrame = 0;
			m_curFadeValue = 1.0f;
			m_fadeDirection = 0;
			return false;
		}
	}
	else
	if (m_fadeDirection < 0)
	{	//turning effect off
		Int fade = m_curFadeFrame;
		if (fade<m_fadeFrames)
		{
			m_curFadeValue = 1.0f - (Real)fade/(Real)m_fadeFrames;
			m_curFadeFrame++;
		}
		else
		{	m_curFadeValue = 0.0f;
			TheTacticalView->setViewFilterMode(FM_NULL_MODE);
			TheTacticalView->setViewFilter(FT_NULL_FILTER);
			m_curFadeFrame = 0;
			m_fadeDirection = 0;
			return false;
		}
	}
	return true;
}

Bool ScreenCrossFadeFilter::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	if (updateFadeLevel())
	{	//if fade has not completed
		W3DShaderManager::startRenderToTexture();
		scenePassMode=SCENE_PASS_ALPHA_MASK;
		skipRender = false;
		m_skipRender=true;	//tell the postRender function not to draw into framebuffer yet.
		return true;
	}
	//fade must have completed
	return true;
}

Bool ScreenCrossFadeFilter::postRender(FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender)
{
	IDirect3DTexture8 * tex;

	if (m_skipRender)
	{
		//don't render anything to frame buffer because we still need to draw the new scene
		//that we're fading into.  Okay to render on the next call.
		m_skipRender = false;
		doExtraRender = TRUE;
		tex =	W3DShaderManager::endRenderToTexture();
		return true;
	}

	tex=W3DShaderManager::getRenderTexture();

	DEBUG_ASSERTCRASH(tex, ("Require last rendered texture."));
	if (!tex) return false;
	if (!set(mode)) return false;

	LPDIRECT3DDEVICE8 pDev=DX8Wrapper::_Get_D3D_Device8();

	struct _TRANS_LIT_TEX_VERTEX {
		D3DXVECTOR4 p;
		DWORD color;   // diffuse color
		float	u;
		float	v;
		float	u1;
		float	v1;
	} v[4];

	Int xpos, ypos, width, height;
	Real radius = 0.0f;

	DX8Wrapper::Set_DX8_Texture(0,tex);	//previously rendered frame inside this texture
	if (mode == FM_VIEW_CROSSFADE_CIRCLE)
	{	DX8Wrapper::Set_DX8_Texture(1,m_fadePatternTexture->Peek_D3D_Texture());
		//Use the current fade level to scale the mask texture, for other modes the texture
		//comes pre-scaled so doesn't require uv scaling.
		radius = (1.0f-m_curFadeValue)*2.0f;
		if (radius <= 0)
			radius = 0.01f;
		radius = 0.5f/radius;
	}

	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

/*	Real radius = (1.0f-m_curFadeValue);
	if (radius <= 0)
		radius = 0.01f;
	radius = 25.0f-radius*24.75f;
*/
	//bottom right
	v[0].p = D3DXVECTOR4( xpos+width-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[0].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[0].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	v[0].u1 = 0.5f+radius;	v[0].v1 = 0.5f+radius;
	//top right
	v[1].p = D3DXVECTOR4( xpos+width-0.5f, ypos-0.5f, 0.0f, 1.0f );
	v[1].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[1].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	v[1].u1 = 0.5f+radius;	v[1].v1 = 0.5f-radius;
	//bottom left
	v[2].p = D3DXVECTOR4(  xpos-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[2].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[2].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	v[2].u1 = 0.5f-radius;	v[2].v1 = 0.5f+radius;
	//top left
	v[3].p = D3DXVECTOR4(  xpos-0.5f,  ypos-0.5f, 0.0f, 1.0f );
	v[3].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[3].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	v[3].u1 = 0.5f-radius;	v[3].v1 = 0.5f-radius;

	DWORD diffuse = 0xffffffff;//((Int)((m_curFadeValue) * 255.0f) << 24) | 0x00ffffff;	//store alpha value in vertex diffuse

	v[0].color = diffuse;
	v[1].color = diffuse;
	v[2].color = diffuse;
	v[3].color = diffuse;

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	DX8Wrapper::Set_Vertex_Shader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX2);

//		m_pDev->SetTextureStageState(0,D3DTSS_MAGFILTER,D3DTEXF_POINT);
//		m_pDev->SetTextureStageState(0,D3DTSS_MINFILTER,D3DTEXF_POINT);

	pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));

	reset();
	return true;
}

Int ScreenCrossFadeFilter::set(FilterModes mode)
{
	if (mode > FM_NULL_MODE)
	{	//rendering a quad with redirected rendering surface
		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		DX8Wrapper::Set_Material(vmat);
		REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
		DX8Wrapper::Set_Shader(ShaderClass::_PresetAlphaShader);
		DX8Wrapper::Set_Texture(0,nullptr);
		DX8Wrapper::Set_Texture(1,nullptr);
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

		if (mode == FM_VIEW_CROSSFADE_CIRCLE)
		{	//cross-fading using circle mask stored in stage 1
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_CURRENT );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_MODULATE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG2, D3DTA_CURRENT );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_MODULATE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, 1 );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_MIPFILTER, D3DTEXF_NONE);
		}

		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_ALWAYS);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE,FALSE);

		return true;
	}
	return false;
}

void ScreenCrossFadeFilter::reset()
{
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_DISABLE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
	DX8Wrapper::Set_DX8_Texture(0,nullptr);	//previously rendered frame inside this texture
	DX8Wrapper::Invalidate_Cached_Render_States();
}

Int ScreenCrossFadeFilter::shutdown()
{
	REF_PTR_RELEASE(m_fadePatternTexture);

	return TRUE;
}

/*=========  ScreenMotionBlurFilter	=============================================================*/
///applies motion blur to viewport.

ScreenMotionBlurFilter screenMotionBlurFilter;

Coord3D ScreenMotionBlurFilter::m_zoomToPos;
Bool ScreenMotionBlurFilter::m_zoomToValid = false;

ScreenMotionBlurFilter::ScreenMotionBlurFilter():
m_decrement(false),
m_maxCount(0),
m_lastFrame(0),
m_skipRender(false)
{
}
///List of different motion blur implementations in order of preference
W3DFilterInterface *ScreenMotionBlurFilterList[]=
{
	&screenMotionBlurFilter,
	nullptr
};

Int ScreenMotionBlurFilter::init()
{
	if (!W3DShaderManager::canRenderToTexture()) {
		// Have to be able to render to texture.
		return false;
	}
	W3DFilters[FT_VIEW_MOTION_BLUR_FILTER]=this;
	return true;
}

Bool ScreenMotionBlurFilter::preRender(Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	skipRender = m_skipRender;
	W3DShaderManager::startRenderToTexture();
	return true;
}

Bool ScreenMotionBlurFilter::postRender(FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender)
{
	IDirect3DTexture8 * tex =	W3DShaderManager::endRenderToTexture();
	DEBUG_ASSERTCRASH(tex, ("Require rendered texture."));
	if (!tex) return false;
	if (!set(mode)) return false;

	LPDIRECT3DDEVICE8 pDev=DX8Wrapper::_Get_D3D_Device8();

	Bool continueEffect = true;
	struct _TRANS_LIT_TEX_VERTEX {
		D3DXVECTOR4 p;
		DWORD color;   // diffuse color
		float	u;
		float	v;
	} v[4];

	Int xpos, ypos, width, height;

	DX8Wrapper::Set_DX8_Texture(0,tex);	//previously rendered frame inside this texture
	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

	//bottom right
	v[0].p = D3DXVECTOR4( xpos+width-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[0].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[0].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top right
	v[1].p = D3DXVECTOR4( xpos+width-0.5f, ypos-0.5f, 0.0f, 1.0f );
	v[1].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[1].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	//bottom left
	v[2].p = D3DXVECTOR4(  xpos-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[2].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[2].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top left
	v[3].p = D3DXVECTOR4(  xpos-0.5f,  ypos-0.5f, 0.0f, 1.0f );
	v[3].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[3].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	v[0].color = 0xffffffff;
	v[1].color = 0xffffffff;
	v[2].color = 0xffffffff;
	v[3].color = 0xffffffff;


	if (m_additive) {
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND,D3DBLEND_ONE);
	} else {
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
	}
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,false);
	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	DX8Wrapper::Apply_Render_State_Changes();
	DX8Wrapper::Set_Vertex_Shader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

	Coord2D center;
	center.x = 0.5f;
	center.y = 0.5f;
	Bool pan = false;
	if (mode>=FM_VIEW_MB_PAN_ALPHA) {
		Real len = sqrt(scrollDelta.x*scrollDelta.x + scrollDelta.y*scrollDelta.y);
		//center.x += 0.5f * (scrollDelta.x/len);
		center.y -= 0.5f; // * (scrollDelta.y/len);
		m_decrement = false;
		m_maxCount = (len*200*m_panFactor/(Real)DEFAULT_PAN_FACTOR);
		if (m_maxCount<m_panFactor/2)
			m_maxCount = m_panFactor/2;
		if (m_maxCount>m_panFactor)
			m_maxCount=m_panFactor;
		pan = true;
		m_priorDelta = scrollDelta;
	} else if (mode == FM_VIEW_MB_END_PAN_ALPHA) {
		Real len = sqrt(m_priorDelta.x*m_priorDelta.x + m_priorDelta.y*m_priorDelta.y);
		center.x += 0.5f * (m_priorDelta.x/len);
		center.y -= 0.5f * (m_priorDelta.y/len);
		m_decrement = false;
		m_maxCount--;
		if (m_maxCount<2) {
			continueEffect = false;
		}
		pan = true;
	}


	m_skipRender = false;
	if (!pan && m_lastFrame != TheGameLogic->getFrame()) {
		if (m_decrement) {
			m_maxCount-=COUNT_STEP;
			if (m_maxCount<1) {
				m_decrement = false;
				continueEffect = false;
			}	else {
				m_skipRender = true;
			}
		} else {
			m_maxCount+=COUNT_STEP;
			if (m_maxCount>=MAX_COUNT) {
				m_decrement = true;
				if (m_doZoomTo && m_zoomToValid) {
					TheTacticalView->lookAt(&m_zoomToPos);
				} else {
					continueEffect = false;
				}
			}	else {
				m_skipRender = true;
			}
		}
	}
	Int	 i, j;
	if (!pan) {
		for (i=0; i<4; i++) {
			Real factor = 1.0f - (m_maxCount/(Real)MAX_COUNT)*0.90f;
			factor = sqrt(factor);
			v[i].u = ((v[i].u-center.x)*factor) + center.x;
			v[i].v = ((v[i].v-center.y)*factor) + center.y;
		}
	}
	pDev->SetTextureStageState(0,D3DTSS_ALPHAARG1, D3DTA_CURRENT);
	pDev->SetTextureStageState(0,D3DTSS_ALPHAARG2, D3DTA_TEXTURE);
	pDev->SetTextureStageState(0,D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,true);

	DX8Wrapper::Apply_Render_State_Changes();
	{
		Int limit = m_maxCount;
		if (m_maxCount>30) limit = 30;
		for (j=0; j<limit; j++) {
			for (i=0; i<4; i++) {
				Real factor = 0.99f;
				if (m_additive) factor = 0.98f;
				Int alpha = 0x15;
				if (m_additive) {
					alpha = 0x09;
					if (m_maxCount>limit) {
						alpha += (m_maxCount-limit)/5;
					}
					if (m_maxCount==MAX_COUNT) alpha += 60;
				}
				v[i].color = (alpha<<24)|0x00ffffff; //
				if (pan) {
					v[i].u = ((v[i].u-center.x)*(factor+.006)) + center.x;
					v[i].v = ((v[i].v-center.y)*factor) + center.y;
				} else {
					v[i].u = ((v[i].u-center.x)*factor) + center.x;
					v[i].v = ((v[i].v-center.y)*factor) + center.y;
				}
			}
			pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));

		}
	}
	m_lastFrame = TheGameLogic->getFrame();
	if (pan){
		m_skipRender = false;
	}
	reset();
	if (!continueEffect) {
		m_zoomToValid = false;
	}
	return continueEffect;
}

Bool ScreenMotionBlurFilter::setup(FilterModes mode)
{

	m_additive = false;

	if (mode == FM_VIEW_MB_IN_AND_OUT_SATURATE ||
			mode == FM_VIEW_MB_IN_SATURATE ||
			mode == FM_VIEW_MB_OUT_SATURATE) {
		m_additive = true;
	}

	m_doZoomTo = false;
	if (mode == FM_VIEW_MB_IN_AND_OUT_SATURATE ||
			mode == FM_VIEW_MB_IN_AND_OUT_ALPHA ) {
		m_doZoomTo = true;
	}
	if (mode >= FM_VIEW_MB_PAN_ALPHA)	{
		m_panFactor = (int)mode - FM_VIEW_MB_PAN_ALPHA;
		if (m_panFactor<1) m_panFactor = DEFAULT_PAN_FACTOR;
	}
	m_skipRender = false;
	if (mode != FM_VIEW_MB_END_PAN_ALPHA)
		m_maxCount = 0;
	m_decrement = false;
	m_skipRender = false;
	switch (mode) {
		case FM_VIEW_MB_OUT_SATURATE:
		case FM_VIEW_MB_OUT_ALPHA:
			m_maxCount = MAX_COUNT;
			m_decrement = TRUE;
			break;
	}
	return true;
}

Int ScreenMotionBlurFilter::set(FilterModes mode)
{
	if (mode > FM_NULL_MODE)
	{	//rendering a quad with redirected rendering surface motion blurred

		VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		DX8Wrapper::Set_Material(vmat);
		REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
		DX8Wrapper::Set_Shader(ShaderClass::_PresetOpaqueShader);
		DX8Wrapper::Set_Texture(0,nullptr);
		DX8Wrapper::Set_Texture(1,nullptr);
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices

		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_ALWAYS);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE,FALSE);
		DX8Wrapper::Apply_Render_State_Changes();	//force update of view and projection matrices
	}
	return TRUE;
}

void ScreenMotionBlurFilter::reset()
{
	DX8Wrapper::Set_DX8_Texture(0,nullptr);	//previously rendered frame inside this texture
	DX8Wrapper::Invalidate_Cached_Render_States();
}

Int ScreenMotionBlurFilter::shutdown()
{
	return TRUE;
}

/*===========================================================================================*/
/*=========      Shroud Shaders	=============================================================*/
/*===========================================================================================*/

///Shroud layer rendering shader
class ShroudTextureShader : public W3DShaderInterface
{
	virtual Int set(Int pass) override;		///<setup shader for the specified rendering pass.
	virtual Int init() override;			///<perform any one time initialization and validation
	virtual void reset() override;		///<do any custom resetting necessary to bring W3D in sync.
	Int m_stageOfSet;
} shroudTextureShader;

///List of different shroud shader implementations in order of preference
W3DShaderInterface *ShroudShaderList[]=
{
	&shroudTextureShader,
	nullptr
};

//#define SHROUD_STRETCH_FACTOR	(1.0f/MAP_XY_FACTOR)	//1 texel per heightmap cell width

Int ShroudTextureShader::init()
{
	W3DShaders[W3DShaderManager::ST_SHROUD_TEXTURE]=&shroudTextureShader;
	W3DShadersPassCount[W3DShaderManager::ST_SHROUD_TEXTURE]=1;

	return TRUE;
}

//Setup a texture projection in the given stage that applies our shroud.
Int ShroudTextureShader::set(Int stage)
{
	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.
	DX8Wrapper::Set_Texture(stage, W3DShaderManager::getShaderTexture(0));	//shroud always stored in texture 0

	if (stage == 0)
	{
#if defined(RTS_DEBUG)
	if (TheGlobalData && TheGlobalData->m_fogOfWarOn)
		DX8Wrapper::Set_Shader(ShaderClass::_PresetAlphaSpriteShader);
	else
		DX8Wrapper::Set_Shader(ShaderClass::_PresetMultiplicativeSpriteShader);
#else
	DX8Wrapper::Set_Shader(ShaderClass::_PresetMultiplicativeSpriteShader);
#endif
	}
	DX8Wrapper::Apply_Render_State_Changes();

	// Everything below is written straight into the tracked state, after that Apply and not
	// before it -- deliberately, per the note at the top of this function: the material's
	// own Apply would otherwise overwrite the coordinate source again.
	//
	// That ordering means the routing block has already chosen this pass's shader constants
	// by the time the texgen exists. It used to keep them: a caller like W3DBridgeBuffer,
	// which applies its state once and then issues several draws without touching
	// render_state, left render_state_changed clear, so every draw returned early from
	// Apply_Render_State_Changes and the routing block never ran again. The projection
	// below therefore never reached the vertex shader -- TexGenCtl went up as zero and
	// unit_vs sampled the shroud with the *mesh's own UVs*, which for a bridge is the
	// deck-and-stonework layout of cbwbridgekh.tga. On a map whose shroud is uniform that
	// is invisible; on one with any structure in it, it painted the deck and the arch
	// soffits with whatever those UVs happened to land on, black included.
	//
	// Set_DX8_Texture_Stage_State and _Set_DX8_Transform now raise TEXGEN_STATE_CHANGED for
	// exactly these writes, so the decision is retaken before the draw. Nothing else re-runs
	// on that bit, so the ordering this function depends on still holds.
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
	DX8Wrapper::Set_DX8_Texture_Stage_State(stage,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
	// The base terrain is now transformed by the programmable terrain shader, whose
	// depth differs from this fixed-function shroud pass by a few ULPs. A ZFUNC of
	// EQUAL then fails in a camera-dependent pattern, punching gaps in the shroud, so
	// use LESSEQUAL (as the flat-map shroud path already does) to tolerate the tiny
	// depth mismatch while still rejecting geometry in front of the terrain.
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
	// LESSEQUAL is still an exact comparison, and exactness is the problem: this pass
	// re-draws geometry that has already been drawn, and it no longer computes its depth
	// the same way. A bridge's base pass runs through the programmable unit shader while
	// this one is fixed-function, and the two disagree by a few ULPs -- enough for the
	// comparison to fail wherever the fixed-function result lands fractionally behind.
	// The failures are not scattered pixels but whole triangular regions, because the
	// sign of the difference varies smoothly across a face and flips along a contour;
	// they slide as the camera turns, which is what made a shrouded bridge look like it
	// had holes cut in it. Measured directly: forcing ZFUNC to ALWAYS removes the pattern
	// completely, while every other input to this pass (view, world, texgen source and
	// matrix, sampler state) measured correct.
	//
	// So give the pass the tolerance the comparison cannot express: bias it a hair toward
	// the camera. Slope-scaled because a co-planar pair diverges fastest where the surface
	// is steep to the viewer, plus a small constant for the flat case. This is the usual
	// remedy for a co-planar decal pass and it costs nothing when the two agree. Note
	// D3DRS_ZBIAS cannot do this -- the D3D9 compatibility header maps it to a dummy
	// render-state slot, so those calls have been doing nothing since the port.
	{
		const float slopeBias = -1.0f;
		const float constBias = -1.0e-5f;
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SLOPESCALEDEPTHBIAS,
			*reinterpret_cast<const DWORD*>(&slopeBias));
		DX8Wrapper::Set_DX8_Render_State(D3DRS_DEPTHBIAS,
			*reinterpret_cast<const DWORD*>(&constBias));
	}

	//We need to scale so shroud texel stretches over one full terrain cell.  Each texel
	//is 1/128 the size of full texture. (assuming 128x128 vid-mem texture).
	W3DShroud *shroud;
	if ((shroud=TheTerrainRenderObject->getShroud()) != nullptr)
	{	///@todo: All this code really only need to be done once per camera/view.  Find a way to optimize it out.
		D3DXMATRIX curView;
		DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

		D3DXMATRIX inv;
		float det;
		D3DXMatrixInverse(&inv, &det, &curView);

		D3DXMATRIX scale,offset;

		//We need to make all world coordinates be relative to the heightmap data origin since that
		//is where the shroud begins.

		float xoffset = 0;
		float yoffset = 0;
		Real width=shroud->getCellWidth();
		Real height=shroud->getCellHeight();

		if (TheTerrainRenderObject->getMap())
		{	//subtract origin position from all coordinates.  Origin is shifted by 1 cell width/height to allow for unused border texels.
			xoffset = -(float)shroud->getDrawOriginX() + width;
			yoffset = -(float)shroud->getDrawOriginY() + height;
		}

		D3DXMatrixTranslation(&offset, xoffset, yoffset,0);

		width = 1.0f/(width*shroud->getTextureWidth());
		height = 1.0f/(height*shroud->getTextureHeight());
		D3DXMatrixScaling(&scale, width, height, 1);
		curView = (inv * offset) * scale;
		DX8Wrapper::_Set_DX8_Transform((D3DTRANSFORMSTATETYPE )(D3DTS_TEXTURE0+stage), curView);
	}
	m_stageOfSet=stage;
	return TRUE;
}

void ShroudTextureShader::reset()
{
	DX8Wrapper::Set_Texture(m_stageOfSet,nullptr);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
	// Take the co-planar bias back off; nothing after this pass wants it.
	DX8Wrapper::Set_DX8_Render_State(D3DRS_SLOPESCALEDEPTHBIAS, 0);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_DEPTHBIAS, 0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(m_stageOfSet,  D3DTSS_TEXCOORDINDEX, m_stageOfSet);
	DX8Wrapper::Set_DX8_Texture_Stage_State(m_stageOfSet,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
}


//#define SHROUD_STRETCH_FACTOR	(1.0f/MAP_XY_FACTOR)	//1 texel per heightmap cell width


///Mask layer rendering shader
class MaskTextureShader : public W3DShaderInterface
{
	virtual Int set(Int pass) override;		///<setup shader for the specified rendering pass.
	virtual Int init() override;			///<perform any one time initialization and validation
	virtual void reset() override;		///<do any custom resetting necessary to bring W3D in sync.
} maskTextureShader;

///List of different shroud shader implementations in order of preference
W3DShaderInterface *MaskShaderList[]=
{
	&maskTextureShader,
	nullptr
};

Int MaskTextureShader::init()
{
	W3DShaders[W3DShaderManager::ST_MASK_TEXTURE]=&maskTextureShader;
	W3DShadersPassCount[W3DShaderManager::ST_MASK_TEXTURE]=1;

	return TRUE;
}

Int MaskTextureShader::set(Int pass)
{
	Real fadeLevel=ScreenCrossFadeFilter::getCurrentFadeValue();

	//Use the current fade level to scale the mask texture
	Real radius = (1.0f-fadeLevel)*2.0f;
	if (radius <= 0)
		radius = 0.01f;
	radius = 0.5f/radius;

	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.

	//For now we're always going to project the texture coming from the crossfade effect
	DX8Wrapper::Set_Texture(0, ScreenCrossFadeFilter::getCurrentMaskTexture());
	ShaderClass shader=ShaderClass::_PresetOpaqueShader;
	shader.Set_Primary_Gradient(ShaderClass::GRADIENT_DISABLE);
	DX8Wrapper::Set_Shader(shader);
	DX8Wrapper::Apply_Render_State_Changes();

	D3DXMATRIX curView;
	DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

	DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);

	D3DXMATRIX inv;
	float det;

	//Get inverse view matrix so we can transform camera space points back to world space
	D3DXMatrixInverse(&inv, &det, &curView);

	D3DXMATRIX scale,offset,offsetTextureCenter;
	Coord3D centerPos;
	centerPos.zero();

	//Find center of projection (this should be returned from some other filter, etc. but
	//for now assume terrain location at center of screen.
	if (TheTacticalView)
	{	Int xpos,ypos;

		TheTacticalView->getOrigin(&xpos,&ypos);

		ICoord2D screenPos;
		screenPos.x=(Real)TheTacticalView->getWidth()*0.5f;
		screenPos.y=(Real)TheTacticalView->getHeight()*0.5f;
		TheTacticalView->screenToTerrain(&screenPos,&centerPos);
	}

	D3DXMatrixTranslation(&offset, -centerPos.x, -centerPos.y,0);

	D3DXMatrixTranslation(&offsetTextureCenter, 0.5f, 0.5f, 0);	//shift coordinates so center of projection falls at uv 0.5,0.5

	Real worldTexelWidth=(1.0f-fadeLevel)*25.0f;	//9 worked well for circle but weird shape requires more stretch to cover.
	Real worldTexelHeight=(1.0f-fadeLevel)*25.0f;

	///@todo: Fix this to work with non 128x128 textures.
	if (worldTexelWidth != 0 && worldTexelHeight != 0)
	{
		Real widthScale = 1.0f/(worldTexelWidth*128.0f);
		Real heightScale = 1.0f/(worldTexelHeight*128.0f);
		D3DXMatrixScaling(&scale, widthScale, heightScale, 1);
		curView = ((inv * offset) * scale)*offsetTextureCenter;
	}
	else
	{
		D3DXMatrixScaling(&scale, 0, 0, 1);	//scaling by 0 will set uv coordinates to 0,0
		curView = ((inv * offset) * scale);
	}

	DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE0, curView);

	return TRUE;
}

void MaskTextureShader::reset()
{
	DX8Wrapper::Set_Texture(0,nullptr);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXCOORDINDEX, 0);
	DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
}

/*===========================================================================================*/
/*=========      Terrain Shaders	=========================================================*/
/*===========================================================================================*/


// Wind for the programmable cloud path, in world units per second, and the periods the
// two layers are projected at. Layer B is slower and pulls in a different direction:
// two decks drifting together at slightly different rates is what stops the field
// reading as one texture being slid across the map.
static const float CLOUD_PERIOD_A = 1800.0f;
static const float CLOUD_PERIOD_B = 2900.0f;
static const float CLOUD_WIND_AX  = -8.9f;
static const float CLOUD_WIND_AY  = -13.3f;
static const float CLOUD_WIND_BX  = -7.5f;
static const float CLOUD_WIND_BY  = -8.0f;

///regular terrain shader that should work on all multi-texture video cards (slowest version)
class TerrainShader2Stage : public W3DShaderInterface
{
public:
	float m_xSlidePerSecond ;	 ///< How far the clouds move per second.
	float m_ySlidePerSecond ;	 ///< How far the clouds move per second.
	float m_xOffset;
	float m_yOffset;
	// The programmable path drifts the two cloud layers in *world* units instead, so one
	// wind speed reads the same however the layers are scaled.
	float m_cloudWorldAX, m_cloudWorldAY;
	float m_cloudWorldBX, m_cloudWorldBY;

	virtual Int set(Int pass) override;		///<setup shader for the specified rendering pass.
	virtual Int init() override;			///<perform any one time initialization and validation
	virtual void reset() override;		///<do any custom resetting necessary to bring W3D in sync.

	void updateCloud();
	void updateNoise1 (D3DXMATRIX *destMatrix,D3DXMATRIX *curViewInverse, Bool doUpdate=true);	///<generate the uv coordinates for Noise1 (i.e clouds)
	void updateNoise2 (D3DXMATRIX *destMatrix,D3DXMATRIX *curViewInverse, Bool doUpdate=true);	///<generate the uv coordinates for Noise2 (i.e lightmap)
} terrainShader2Stage;




///List of different terrain shader implementations in order of preference
W3DShaderInterface *TerrainShaderList[]=
{
	&terrainShader2Stage,
	nullptr
};


Int TerrainShader2Stage::init()
{
	//initialize settings for uv animated clouds
	m_xSlidePerSecond = -0.02f;
	m_ySlidePerSecond =  1.50f * m_xSlidePerSecond;
	m_cloudWorldAX = m_cloudWorldAY = 0.0f;
	m_cloudWorldBX = m_cloudWorldBY = 0.0f;
	m_xOffset = 0;
	m_yOffset = 0;

	//no special device validation needed - anything in our min spec should handle this.

	W3DShaders[W3DShaderManager::ST_TERRAIN_BASE]=&terrainShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE]=2;
	W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE1]=&terrainShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE1]=3;
	W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE2]=&terrainShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE2]=3;
	W3DShaders[W3DShaderManager::ST_TERRAIN_BASE_NOISE12]=&terrainShader2Stage;
	W3DShadersPassCount[W3DShaderManager::ST_TERRAIN_BASE_NOISE12]=3;

	return TRUE;
}

void TerrainShader2Stage::reset()
{
	ShaderClass::Invalidate();

	//Free references to textures
	DX8Wrapper::Set_DX8_Texture(0, nullptr);
	DX8Wrapper::Set_DX8_Texture(1, nullptr);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|0);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU|1);
}

void TerrainShader2Stage::updateCloud()
{
	const float frame_time = WW3D::Get_Logic_Frame_Time_Seconds();
	m_xOffset += m_xSlidePerSecond * frame_time;
	m_yOffset += m_ySlidePerSecond * frame_time;

	// This moves offsets towards zero when smaller -1.0 or larger 1.0
	m_xOffset -= (Int)m_xOffset;
	m_yOffset -= (Int)m_yOffset;

	// World-space drift for the programmable path. The old field moved about 11 world
	// units a second, which is only ~3.6 m/s. It looked fast because its features were
	// 20 units wide and crossed their own width in a few seconds; now that the shapes are
	// hundreds of units across the same wind reads as calm.
	m_cloudWorldAX += CLOUD_WIND_AX * frame_time;
	m_cloudWorldAY += CLOUD_WIND_AY * frame_time;
	m_cloudWorldBX += CLOUD_WIND_BX * frame_time;
	m_cloudWorldBY += CLOUD_WIND_BY * frame_time;

	// Wrap on each layer's period, so a long game cannot drift the accumulator into the
	// range where a float stops resolving world-unit steps.
	m_cloudWorldAX -= CLOUD_PERIOD_A * (Int)(m_cloudWorldAX / CLOUD_PERIOD_A);
	m_cloudWorldAY -= CLOUD_PERIOD_A * (Int)(m_cloudWorldAY / CLOUD_PERIOD_A);
	m_cloudWorldBX -= CLOUD_PERIOD_B * (Int)(m_cloudWorldBX / CLOUD_PERIOD_B);
	m_cloudWorldBY -= CLOUD_PERIOD_B * (Int)(m_cloudWorldBY / CLOUD_PERIOD_B);
}

void TerrainShader2Stage::updateNoise1(D3DXMATRIX *destMatrix,D3DXMATRIX *curViewInverse, Bool doUpdate)
{
	#define STRETCH_FACTOR ((float)(1/(63.0*MAP_XY_FACTOR/2))) /* covers 63/2 tiles */

	D3DXMATRIX scale;

	D3DXMatrixScaling(&scale, STRETCH_FACTOR, STRETCH_FACTOR,1);
	*destMatrix = *curViewInverse * scale;

	D3DXMATRIX offset;
	D3DXMatrixTranslation(&offset, m_xOffset, m_yOffset,0);
	*destMatrix *= offset;
}

void TerrainShader2Stage::updateNoise2(D3DXMATRIX *destMatrix,D3DXMATRIX *curViewInverse, Bool doUpdate)
{

	D3DXMATRIX scale;

	D3DXMatrixScaling(&scale, STRETCH_FACTOR, STRETCH_FACTOR,1);
	*destMatrix = *curViewInverse * scale;
}

Int TerrainShader2Stage::set(Int pass)
{
	//force WW3D2 system to set it's states so it won't later overwrite our custom settings.
	DX8Wrapper::Apply_Render_State_Changes();

	if (TheGlobalData && (TheGlobalData->m_bilinearTerrainTex || TheGlobalData->m_trilinearTerrainTex)) {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
	} else {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_POINT);
	}
	if (TheGlobalData && TheGlobalData->m_trilinearTerrainTex) {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
	} else {
		DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MIPFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
	}

	switch (pass)
	{
		case 0:
			DX8Wrapper::Set_DX8_Texture(0, W3DShaderManager::getShaderTexture(0)->Peek_D3D_Texture());
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

			// Modulate the diffuse color with the texture as lighting comes from diffuse.
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_MODULATE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_DISABLE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 0 );
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,false);
			break;
		case 1:
			DX8Wrapper::Set_DX8_Texture(0, W3DShaderManager::getShaderTexture(1)->Peek_D3D_Texture());
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

			// Modulate the diffuse color with the texture as lighting comes from diffuse.
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_MODULATE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 1 );
			// Blend the result using the alpha. (came from diffuse mod texture)
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,true);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
			// Disable stage 2.
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_DISABLE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
			break;
		case 2:
			// Noise/cloud pass
			D3DXMATRIX curView;
			DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

			//these states apply to all noise/cloud combination passes
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1 );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );

			DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
			// Two output coordinates are used.
			DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0,  D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);

			//blend into frame buffer
			DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,true);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_DESTCOLOR);
			DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND,D3DBLEND_ZERO);

			D3DXMATRIX inv;
			float det;
			D3DXMatrixInverse(&inv, &det, &curView);

			if (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_TERRAIN_BASE_NOISE12)
			{
				//setup cloud pass
				DX8Wrapper::Set_DX8_Texture(0, W3DShaderManager::getShaderTexture(2)->Peek_D3D_Texture());

				updateNoise1(&curView,&inv);	//update curView with texture matrix
				DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE0, curView);
				//clouds always need bilinear filtering
				DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
				DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

				//setup noise pass
				DX8Wrapper::Set_DX8_Texture(1, W3DShaderManager::getShaderTexture(3)->Peek_D3D_Texture());

				updateNoise2(&curView,&inv);
				DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE1, curView);
				//noise always needs point/linear filtering.  Why point!?
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_POINT);
				DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_CURRENT );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_MODULATE );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
				// Two output coordinates are used.
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);

				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
			}
			else
			{	//only 1 noise or cloud texture
				// Now setup the texture pipeline.
				if (W3DShaderManager::getCurrentShader() == W3DShaderManager::ST_TERRAIN_BASE_NOISE1)
				{	//setup cloud pass
					DX8Wrapper::Set_DX8_Texture(0, W3DShaderManager::getShaderTexture(2)->Peek_D3D_Texture());
					updateNoise1(&curView,&inv);	//update curView with texture matrix
					DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
					DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
				}
				else
				{
					//setup noise pass
					DX8Wrapper::Set_DX8_Texture(0, W3DShaderManager::getShaderTexture(3)->Peek_D3D_Texture());
					updateNoise2(&curView,&inv);	//update curView with texture matrix
					DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MINFILTER, D3DTEXF_POINT);
					DX8Wrapper::Set_DX8_Texture_Stage_State(1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
				}

				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_DISABLE );
				DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
				DX8Wrapper::_Set_DX8_Transform(D3DTS_TEXTURE0, curView);
			}
			break;
	}

	return TRUE;
}




/** List of all custom shader lists - each list in this list contains variations of the same
	shader to allow it to work on different hardware configurations.
*/
W3DShaderInterface **MasterShaderList[]=
{
	TerrainShaderList,
	ShroudShaderList,
	MaskShaderList,
	nullptr
};

/** List of all custom filter lists - each list in this list contains variations of the same
	filter to allow it to work on different hardware configurations.
*/
W3DFilterInterface **MasterFilterList[]=
{
	ScreenDefaultFilterList,
	ScreenBWFilterList,
	ScreenMotionBlurFilterList,
	ScreenCrossFadeFilterList,
	ScreenBloomFilterList,
	nullptr
};

// W3DShaderManager::W3DShaderManager =========================================
/** Constructor - just clears some variables */
//=============================================================================
W3DShaderManager::W3DShaderManager()
{
	m_currentShader = ST_INVALID;
	m_currentFilter = FT_NULL_FILTER;
	m_oldRenderSurface = nullptr;
	m_renderTexture = nullptr;
	m_newRenderSurface = nullptr;
	m_oldDepthSurface = nullptr;
	m_renderingToTexture = false;
	Int i;
	for (i=0; i<W3DShaderManager::ST_MAX; i++)
	{	W3DShaders[i]=nullptr;
		W3DShadersPassCount[i]=0;
	}
	for (i=0; i<FT_MAX; i++)
	{	W3DFilters[i]=nullptr;
	}
	for (i=0; i<8; i++)
	{
		m_Textures[i]=nullptr;
	}
	m_currentShader=(W3DShaderManager::ShaderTypes)-1;
}

// W3DShaderManager::init =======================================================
/** Walk through all shaders and find versions suitable for current hardware */
//=============================================================================
void W3DShaderManager::init()
{
	int i,j;

	D3DSURFACE_DESC desc;
	// For now, check & see if we are gf3 or higher on the food chain.

	ChipsetType res=DC_UNKNOWN;
	if ((res=W3DShaderManager::getChipset()) != 0)
	{
		m_currentChipset = res;	//cache the current chipset.

		// Load the programmable unit shaders (safe no-op if the compiled
		// shaders are missing or the hardware is too old).
		W3DShaderManager::initUnitShaders();

		//Some of our effects require an offscreen render target, so try creating it here.
		HRESULT hr=DX8Wrapper::_Get_D3D_Device8()->GetRenderTarget(0,&m_oldRenderSurface);

		if (hr != S_OK || !m_oldRenderSurface)
			return;

		m_oldRenderSurface->GetDesc(&desc);

		// The post-process reads a plain (non-multisampled) texture, so always create
		// that. Redirecting the scene straight into a non-MSAA texture while the depth
		// buffer is multisampled is an API violation, so when MSAA is active the scene
		// is drawn into a matching multisampled colour surface (m_newRenderSurface) and
		// resolved into the plain texture (m_resolveSurface) by endRenderToTexture.
		LPDIRECT3DDEVICE8 dev = DX8Wrapper::_Get_D3D_Device8();
		hr = dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT, &m_renderTexture);

		if (hr == S_OK)
		{
			if (desc.MultiSampleType == D3DMULTISAMPLE_NONE)
			{
				// No MSAA: render straight into the plain texture, no resolve needed.
				hr = m_renderTexture->GetSurfaceLevel(0, &m_newRenderSurface);
				m_resolveSurface = nullptr;
			}
			else
			{
				// MSAA: multisampled colour surface to render into + the plain texture
				// surface as the resolve destination. The 6-arg form goes through the
				// d3d9_compat shim (multisample quality 0, matching the standard MSAA the
				// back buffer / depth use), so the surface pairs with the MSAA depth.
				hr = dev->CreateRenderTarget(desc.Width, desc.Height, desc.Format,
					desc.MultiSampleType, FALSE, &m_newRenderSurface);
				if (hr == S_OK)
					hr = m_renderTexture->GetSurfaceLevel(0, &m_resolveSurface);
			}
		}

		if (hr != S_OK)
		{
			SAFE_RELEASE(m_resolveSurface);
			SAFE_RELEASE(m_newRenderSurface);
			SAFE_RELEASE(m_renderTexture);
			SAFE_RELEASE(m_oldRenderSurface);
		} else {
			hr = dev->GetDepthStencilSurface(&m_oldDepthSurface);
			if (hr != S_OK)
			{
				SAFE_RELEASE(m_resolveSurface);
				SAFE_RELEASE(m_newRenderSurface);
				SAFE_RELEASE(m_renderTexture);
				SAFE_RELEASE(m_oldRenderSurface);
				m_oldDepthSurface = nullptr;
			}
		}
	}

	W3DShaderInterface **shaders;

	for (i=0; MasterShaderList[i] != nullptr; i++)
	{
		shaders=MasterShaderList[i];
		for (j=0; shaders[j] != nullptr; j++)
		{
			if (shaders[j]->init())
				break;	//found a working shader
		}
	}
	W3DFilterInterface **filters;

	for (i=0; MasterFilterList[i] != nullptr; i++)
	{
		filters=MasterFilterList[i];
		for (j=0; filters[j] != nullptr; j++)
		{
			if (filters[j]->init())
				break;	//found a working shader
		}
	}

	DEBUG_LOG(("ShaderManager ChipsetID %d", res));
}

//=============================================================================
/** Create the vertex declaration and load the compiled unit vertex/pixel
    shaders used by the programmable object-mesh render path. */
//=============================================================================
void W3DShaderManager::initUnitShaders()
{
	if (DX8Wrapper::_Get_D3D_Device8() == nullptr)
		return;

	// options.ini "ShaderRouting" selects which draw categories the programmable path
	// claims (see DX8Wrapper::ShaderRoutingFlags). Read here rather than compiled in so
	// the routing can be changed and compared in game without a rebuild. Unset, it is
	// the detail and texgen categories: those are what keep every pass of a mesh on one
	// pipeline, so that coincident passes cannot end up with differing depth and z-fight.
	{
		OptionPreferences shaderRoutingPrefs;
		const Int configured = shaderRoutingPrefs.getShaderRouting();
		DX8Wrapper::m_shaderRoutingMask = (configured < 0)
			? (DWORD)(DX8Wrapper::SHADER_ROUTE_DETAIL | DX8Wrapper::SHADER_ROUTE_TEXGEN)
			: (DWORD)configured;
	}

	// No explicit vertex declaration: the mesh FVF (set on the device before the
	// draw) is used as the declaration, so a single shader serves every format.
	if (DX8Wrapper::m_dwUnitVS == 0) {
		LoadAndCreateD3DShader("shaders\\unit_vs.vso", nullptr, 0, true, &DX8Wrapper::m_dwUnitVS);
	}
	// Variant for geometry that carries no normal (roads, tank tracks). Separate rather
	// than a branch in unit_vs because the FVF doubles as the vertex declaration, so the
	// declared inputs have to match what the buffer actually supplies.
	if (DX8Wrapper::m_dwUnitPrelitVS == 0) {
		LoadAndCreateD3DShader("shaders\\unit_prelit_vs.vso", nullptr, 0, true, &DX8Wrapper::m_dwUnitPrelitVS);
	}
	if (DX8Wrapper::m_dwUnitPS == 0) {
		LoadAndCreateD3DShader("shaders\\unit_ps.pso", nullptr, 0, false, &DX8Wrapper::m_dwUnitPS);
	}
	if (DX8Wrapper::m_dwUnitDetailPS == 0) {
		LoadAndCreateD3DShader("shaders\\unit_detail_ps.pso", nullptr, 0, false, &DX8Wrapper::m_dwUnitDetailPS);
	}
	if (DX8Wrapper::m_dwTerrainVS == 0) {
		LoadAndCreateD3DShader("shaders\\terrain_vs.vso", nullptr, 0, true, &DX8Wrapper::m_dwTerrainVS);
	}
	if (DX8Wrapper::m_dwTerrainPS == 0) {
		LoadAndCreateD3DShader("shaders\\terrain_ps.pso", nullptr, 0, false, &DX8Wrapper::m_dwTerrainPS);
	}
	// Roads: the terrain's shading (cloud, noise, cast shadows) over a single UV set,
	// keeping the road's alpha for the blend into the ground.
	if (DX8Wrapper::m_dwRoadVS == 0) {
		LoadAndCreateD3DShader("shaders\\road_vs.vso", nullptr, 0, true, &DX8Wrapper::m_dwRoadVS);
	}
	if (DX8Wrapper::m_dwRoadPS == 0) {
		LoadAndCreateD3DShader("shaders\\road_ps.pso", nullptr, 0, false, &DX8Wrapper::m_dwRoadPS);
	}
	// Water: the two ps.1.1 combines the surface used, plus the depth, sky and sun terms
	// that only became expressible once it was on the programmable path. Optional in the
	// same sense the rest are -- if these fail to load, Has_Water_Shader() is false and the
	// water object keeps its assembled shaders.
	if (DX8Wrapper::m_dwWaterVS == 0) {
		LoadAndCreateD3DShader("shaders\\water_vs.vso", nullptr, 0, true, &DX8Wrapper::m_dwWaterVS);
	}
	if (DX8Wrapper::m_dwWaterPS == 0) {
		LoadAndCreateD3DShader("shaders\\water_ps.pso", nullptr, 0, false, &DX8Wrapper::m_dwWaterPS);
	}
	// PBR (Shader Model 3) variant of the unit shader, used for meshes that ship a
	// <name>_orm map. Optional: if these fail to load, the plain unit shader is used.
	if (DX8Wrapper::m_dwUnitPbrVS == 0) {
		LoadAndCreateD3DShader("shaders\\unit_pbr_vs.vso", nullptr, 0, true, &DX8Wrapper::m_dwUnitPbrVS);
	}
	if (DX8Wrapper::m_dwUnitPbrPS == 0) {
		LoadAndCreateD3DShader("shaders\\unit_pbr_ps.pso", nullptr, 0, false, &DX8Wrapper::m_dwUnitPbrPS);
	}
	// Install the base-texture -> ORM resolver the render path calls per draw.
	DX8Wrapper::Set_Orm_Resolver(W3DShaderManager::resolveOrmTexture);
	// Neutral ORM for every mesh that ships none, so PBR is not limited to the HD set.
	initDefaultOrmMap();

	// Say what the PBR path is actually able to do, once, at init. Every one of these
	// silently routes every mesh away from PBR when it is wrong, and the symptom is
	// identical in all four cases: the frame renders correctly, on the old shader. That
	// is not something a screenshot can show, and a routing census that reads zero
	// cannot say which of them it was.
	DEBUG_LOG(("PBR: routing mask = %u (PBR %s, team-colour maps %s, authored-only %s); "
		"shaders %s; default ORM map %s\n",
		(unsigned)DX8Wrapper::m_shaderRoutingMask,
		(DX8Wrapper::m_shaderRoutingMask & DX8Wrapper::SHADER_ROUTE_PBR) ? "on" : "OFF",
		(DX8Wrapper::m_shaderRoutingMask & DX8Wrapper::SHADER_ROUTE_PBR_TEAMCOLOR) ? "on" : "off",
		(DX8Wrapper::m_shaderRoutingMask & DX8Wrapper::SHADER_ROUTE_PBR_AUTHORED_ONLY) ? "ON" : "off",
		(DX8Wrapper::m_dwUnitPbrVS != 0 && DX8Wrapper::m_dwUnitPbrPS != 0) ? "loaded" : "MISSING",
		(DX8Wrapper::m_defaultOrmMap != nullptr) ? "created" : "MISSING"));
	// Build the shared environment cubemap the PBR shader reflects.
	initEnvMap();
	// Directional shadow map (sun-view depth) for cast shadows.
	initShadowMap();
	// Screen-space reflections. After the shadow map, which owns the depth shaders
	// this reuses -- initSsr checks they loaded and stands down if they did not.
	initSsr();
	// The water's refraction grab. Independent of SSR: it needs no depth shaders, only a
	// render target the size of the screen.
	initRefraction();

	// The in-game debug visualizations.
	initDebugVis();
}

//=============================================================================
/** Release the unit shaders and vertex declaration. */
//=============================================================================
void W3DShaderManager::shutdownUnitShaders()
{
	if (DX8Wrapper::m_dwUnitVS != 0) {
		reinterpret_cast<IDirect3DVertexShader9*>(DX8Wrapper::m_dwUnitVS)->Release();
		DX8Wrapper::m_dwUnitVS = 0;
	}
	if (DX8Wrapper::m_dwUnitPS != 0) {
		reinterpret_cast<IDirect3DPixelShader9*>(DX8Wrapper::m_dwUnitPS)->Release();
		DX8Wrapper::m_dwUnitPS = 0;
	}
	if (DX8Wrapper::m_dwUnitDetailPS != 0) {
		reinterpret_cast<IDirect3DPixelShader9*>(DX8Wrapper::m_dwUnitDetailPS)->Release();
		DX8Wrapper::m_dwUnitDetailPS = 0;
	}
	if (DX8Wrapper::m_dwTerrainVS != 0) {
		reinterpret_cast<IDirect3DVertexShader9*>(DX8Wrapper::m_dwTerrainVS)->Release();
		DX8Wrapper::m_dwTerrainVS = 0;
	}
	if (DX8Wrapper::m_dwTerrainPS != 0) {
		reinterpret_cast<IDirect3DPixelShader9*>(DX8Wrapper::m_dwTerrainPS)->Release();
		DX8Wrapper::m_dwTerrainPS = 0;
	}
	if (DX8Wrapper::m_dwRoadVS != 0) {
		reinterpret_cast<IDirect3DVertexShader9*>(DX8Wrapper::m_dwRoadVS)->Release();
		DX8Wrapper::m_dwRoadVS = 0;
	}
	if (DX8Wrapper::m_dwRoadPS != 0) {
		reinterpret_cast<IDirect3DPixelShader9*>(DX8Wrapper::m_dwRoadPS)->Release();
		DX8Wrapper::m_dwRoadPS = 0;
	}
	if (DX8Wrapper::m_dwWaterVS != 0) {
		reinterpret_cast<IDirect3DVertexShader9*>(DX8Wrapper::m_dwWaterVS)->Release();
		DX8Wrapper::m_dwWaterVS = 0;
	}
	if (DX8Wrapper::m_dwWaterPS != 0) {
		reinterpret_cast<IDirect3DPixelShader9*>(DX8Wrapper::m_dwWaterPS)->Release();
		DX8Wrapper::m_dwWaterPS = 0;
	}
	// The water publishes its shroud pointer per frame; drop it so a device reset cannot
	// leave a released texture bound on stage 6.
	DX8Wrapper::Set_Water_Shroud(nullptr, 0.0f, 0.0f, 0.0f, 0.0f);
	if (DX8Wrapper::m_dwUnitPbrVS != 0) {
		reinterpret_cast<IDirect3DVertexShader9*>(DX8Wrapper::m_dwUnitPbrVS)->Release();
		DX8Wrapper::m_dwUnitPbrVS = 0;
	}
	if (DX8Wrapper::m_dwUnitPbrPS != 0) {
		reinterpret_cast<IDirect3DPixelShader9*>(DX8Wrapper::m_dwUnitPbrPS)->Release();
		DX8Wrapper::m_dwUnitPbrPS = 0;
	}
	DX8Wrapper::Set_Orm_Resolver(nullptr);
	clearOrmCache();
	if (DX8Wrapper::m_defaultOrmMap != nullptr) {
		DX8Wrapper::m_defaultOrmMap->Release();
		DX8Wrapper::m_defaultOrmMap = nullptr;
	}
	if (DX8Wrapper::m_envCubeMap != nullptr) {
		DX8Wrapper::m_envCubeMap->Release();
		DX8Wrapper::m_envCubeMap = nullptr;
	}
	// initEnvMap re-bakes and resets the bake baseline when it recreates the cube,
	// so there is nothing to reset here.
	shutdownShadowMap();
	shutdownDebugVis();
	shutdownSsr();
	shutdownRefraction();
	DX8Wrapper::m_bUnitShaderBound = false;
}

// ---------------------------------------------------------------------------
// In-game debug visualizations. The modes themselves live in DX8Wrapper, which is
// where the per-draw override has to run; what belongs here is loading the shaders
// they substitute, because this is where every other shader is loaded.
// ---------------------------------------------------------------------------

void W3DShaderManager::initDebugVis()
{
#ifdef RTS_DEBUG
	if (m_debugDepthPS == 0)
		LoadAndCreateD3DShader("shaders\\debugdepth_ps.pso", nullptr, 0, false, &m_debugDepthPS);
	if (m_debugShadowPS == 0)
		LoadAndCreateD3DShader("shaders\\debugshadow_ps.pso", nullptr, 0, false, &m_debugShadowPS);
	if (m_debugBloomPS == 0)
		LoadAndCreateD3DShader("shaders\\debugbloom_ps.pso", nullptr, 0, false, &m_debugBloomPS);
	if (m_debugShroudPS == 0)
		LoadAndCreateD3DShader("shaders\\debugshroud_ps.pso", nullptr, 0, false, &m_debugShroudPS);
	if (DX8Wrapper::m_dwDebugTintPS == 0)
		LoadAndCreateD3DShader("shaders\\debugtint_ps.pso", nullptr, 0, false, &DX8Wrapper::m_dwDebugTintPS);
	if (DX8Wrapper::m_dwDebugNormalVS == 0)
		LoadAndCreateD3DShader("shaders\\debugnormal_vs.vso", nullptr, 0, true, &DX8Wrapper::m_dwDebugNormalVS);
	if (DX8Wrapper::m_dwDebugNormalPS == 0)
		LoadAndCreateD3DShader("shaders\\debugnormal_ps.pso", nullptr, 0, false, &DX8Wrapper::m_dwDebugNormalPS);
	// Said either way, not only on failure. A missing shader makes its mode draw nothing,
	// which is indistinguishable from the mode working and finding nothing -- and a line
	// that appears only when something is wrong cannot be used to confirm that the setup
	// ran at all. This one states the outcome, so a silent log means initDebugVis was
	// never reached rather than "everything is fine".
	DEBUG_LOG(("Debug vis: depth %s, shadow %s, bloom %s, shroud %s, tint %s, normals %s",
		(m_debugDepthPS != 0) ? "loaded" : "MISSING",
		(m_debugShadowPS != 0) ? "loaded" : "MISSING",
		(m_debugBloomPS != 0) ? "loaded" : "MISSING",
		(m_debugShroudPS != 0) ? "loaded" : "MISSING",
		(DX8Wrapper::m_dwDebugTintPS != 0) ? "loaded" : "MISSING",
		(DX8Wrapper::m_dwDebugNormalVS != 0 && DX8Wrapper::m_dwDebugNormalPS != 0)
			? "loaded" : "MISSING"));
#endif
}

void W3DShaderManager::shutdownDebugVis()
{
	if (m_debugDepthPS) {
		reinterpret_cast<IDirect3DPixelShader9*>(m_debugDepthPS)->Release();
		m_debugDepthPS = 0;
	}
	if (m_debugShadowPS) {
		reinterpret_cast<IDirect3DPixelShader9*>(m_debugShadowPS)->Release();
		m_debugShadowPS = 0;
	}
	if (m_debugBloomPS) {
		reinterpret_cast<IDirect3DPixelShader9*>(m_debugBloomPS)->Release();
		m_debugBloomPS = 0;
	}
	if (m_debugShroudPS) {
		reinterpret_cast<IDirect3DPixelShader9*>(m_debugShroudPS)->Release();
		m_debugShroudPS = 0;
	}
	// Released here as well as at device reset: this is a D3DPOOL_DEFAULT render target,
	// and one of those outliving a Reset() is exactly the leak that pinned the device
	// shut on alt-tab once already.
	SAFE_RELEASE(m_debugBrightSurface);
	SAFE_RELEASE(m_debugBrightTexture);
	if (DX8Wrapper::m_dwDebugNormalVS) {
		reinterpret_cast<IDirect3DVertexShader9*>(DX8Wrapper::m_dwDebugNormalVS)->Release();
		DX8Wrapper::m_dwDebugNormalVS = 0;
	}
	if (DX8Wrapper::m_dwDebugNormalPS) {
		reinterpret_cast<IDirect3DPixelShader9*>(DX8Wrapper::m_dwDebugNormalPS)->Release();
		DX8Wrapper::m_dwDebugNormalPS = 0;
	}
	if (DX8Wrapper::m_dwDebugTintPS) {
		reinterpret_cast<IDirect3DPixelShader9*>(DX8Wrapper::m_dwDebugTintPS)->Release();
		DX8Wrapper::m_dwDebugTintPS = 0;
	}
}

void W3DShaderManager::captureBloomBrightPass(IDirect3DSurface8 *brightSurface, Int width, Int height)
{
#ifdef RTS_DEBUG
	if (DX8Wrapper::Get_Debug_Vis_Mode() != DEBUG_VIS_BLOOM || brightSurface == nullptr)
		return;

	LPDIRECT3DDEVICE8 dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev == nullptr)
		return;

	// Create on first use, in the bright target's own format, and recreate if that target
	// has been resized under us. The format is taken from the source rather than assumed,
	// because StretchRect between differing formats is where this would quietly start
	// failing. The copy target is created on first use and released with the rest, so a
	// session that never enters the mode pays nothing for it.
	D3DSURFACE_DESC sd;
	if (FAILED(brightSurface->GetDesc(&sd)))
		return;
	if (m_debugBrightTexture != nullptr)
	{
		D3DSURFACE_DESC have;
		if (FAILED(m_debugBrightSurface->GetDesc(&have)) ||
			have.Width != sd.Width || have.Height != sd.Height || have.Format != sd.Format)
		{
			SAFE_RELEASE(m_debugBrightSurface);
			SAFE_RELEASE(m_debugBrightTexture);
		}
	}
	if (m_debugBrightTexture == nullptr)
	{
		if (FAILED(dev->CreateTexture(sd.Width, sd.Height, 1, D3DUSAGE_RENDERTARGET,
				sd.Format, D3DPOOL_DEFAULT, &m_debugBrightTexture)) || m_debugBrightTexture == nullptr)
		{
			m_debugBrightTexture = nullptr;
			return;
		}
		if (FAILED(m_debugBrightTexture->GetSurfaceLevel(0, &m_debugBrightSurface)))
		{
			SAFE_RELEASE(m_debugBrightTexture);
			return;
		}
	}

	// Checked, and loudly. A failed copy leaves the texture holding whatever the driver
	// allocated -- which in practice is not black -- and the inspector then paints the
	// whole viewport as "everything blooms", the most confidently wrong answer it could
	// give.
	HRESULT hr = dev->StretchRect(brightSurface, nullptr, m_debugBrightSurface, nullptr, D3DTEXF_NONE);
	if (FAILED(hr))
	{
		static Bool s_reported = FALSE;
		if (!s_reported)
		{
			DEBUG_LOG(("Debug vis: bright-pass copy failed (0x%08X) -- bloom inspector disabled",
				(unsigned)hr));
			s_reported = TRUE;
		}
		// Drop the target rather than show it. The mode reports having nothing to draw,
		// which is true and is a different statement from a screen full of false positives.
		SAFE_RELEASE(m_debugBrightSurface);
		SAFE_RELEASE(m_debugBrightTexture);
	}
#else
	(void)brightSurface; (void)width; (void)height;
#endif
}

#ifdef RTS_DEBUG
// One tile of the shadow-map inspector. `showAlpha` picks the coverage channel rather
// than the depth.
static HRESULT drawShadowMapTile(LPDIRECT3DDEVICE8 dev, IDirect3DTexture8 *shadowTex,
							  DWORD ps, float x, float y, float side, Bool showAlpha)
{
	DX8Wrapper::Set_Pixel_Shader(ps);
	const D3DXVECTOR4 ctl(0.0f, 1.0f, showAlpha ? 1.0f : 0.0f, 0.0f);
	DX8Wrapper::Set_Pixel_Shader_Constant(0, ctl, 1);
	DX8Wrapper::Set_DX8_Texture(0, shadowTex);
	// Linear, so a 4096-wide map shrunk into a small tile shows the average of what is
	// there rather than one texel in twelve. Point sampling here reads as a map full of
	// holes on exactly the thin geometry -- wires, railings, rotor blades -- whose
	// presence in the map is the thing most often in question.
	W3DShaderManager::setLinearClampSampler(0);
	return W3DShaderManager::drawScreenQuad(dev, x, y, side, side,
		0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
}
#endif

void W3DShaderManager::drawDebugVisOverlay(Int screenWidth, Int screenHeight)
{
#ifdef RTS_DEBUG
	const DebugVisMode mode = DX8Wrapper::Get_Debug_Vis_Mode();
	if (mode != DEBUG_VIS_BLOOM && mode != DEBUG_VIS_SHROUD &&
		mode != DEBUG_VIS_SHADOW_MAP && mode != DEBUG_VIS_DEPTH)
		return;   // the remaining modes are per-draw and have already happened

	LPDIRECT3DDEVICE8 dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev == nullptr)
		return;

	// What each mode wants to look at, resolved before any state is touched so that a
	// mode with nothing to show costs nothing and leaves the pipeline alone.
	IDirect3DTexture8 *shroudTex = nullptr;
	if (mode == DEBUG_VIS_SHROUD)
	{
		W3DShroud *shroud = (TheTerrainRenderObject != nullptr)
			? TheTerrainRenderObject->getShroud() : nullptr;
		TextureClass *tex = (shroud != nullptr) ? shroud->getShroudTexture() : nullptr;
		if (tex != nullptr)
			shroudTex = tex->Peek_D3D_Texture();
	}

	switch (mode)
	{
		case DEBUG_VIS_SHADOW_MAP:
			if (m_debugShadowPS == 0 || m_pShadowMapTexture == nullptr) return;
			break;
		case DEBUG_VIS_DEPTH:
			// The camera depth target only exists when screen-space reflections are on --
			// it is their prepass, not a buffer the renderer keeps regardless. Said once,
			// because an absent tile otherwise reads as the mode being broken when it is
			// the feature that supplies it being switched off.
			if (m_debugDepthPS == 0 || m_ssrDepthTexture == nullptr)
			{
				static Bool s_reportedNoDepth = FALSE;
				if (!s_reportedNoDepth)
				{
					DEBUG_LOG(("Debug vis: no camera depth to draw -- is SSR enabled? "
						"(shader=%u depth=%p)", (unsigned)m_debugDepthPS, (void*)m_ssrDepthTexture));
					s_reportedNoDepth = TRUE;
				}
				return;
			}
			break;
		case DEBUG_VIS_BLOOM:
			// No copy means the bloom filter never ran a bright pass this frame -- bloom
			// is off, or render-to-texture is unavailable. Said once, because an empty
			// screen here is otherwise indistinguishable from "nothing in this scene
			// blooms", which is a completely different answer.
			if (m_debugBloomPS == 0 || m_debugBrightTexture == nullptr)
			{
				static Bool s_reportedNoBloom = FALSE;
				if (!s_reportedNoBloom)
				{
					DEBUG_LOG(("Debug vis: bloom inspector has nothing to draw -- "
						"is the bloom filter active? (shader=%u copy=%p)",
						(unsigned)m_debugBloomPS, (void*)m_debugBrightTexture));
					s_reportedNoBloom = TRUE;
				}
				return;
			}
			break;
		case DEBUG_VIS_SHROUD:
			if (m_debugShroudPS == 0 || shroudTex == nullptr) return;
			break;
		default:
			return;
	}

	// Shared setup.
	//
	// The depth and blend rules come from a ShaderClass preset rather than from
	// Set_DX8_Render_State calls here, and that is not a style preference: everything
	// this function sets is applied by Apply_Render_State_Changes below, which runs
	// ShaderClass::Apply, which writes D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND and
	// D3DRS_DESTBLEND itself from the bound preset. Blend states set *before* that call
	// are simply overwritten. The bloom overlay was written that way first and drew the
	// whole viewport opaque black -- its "leave this pixel alone" output composited with
	// blending switched back off.
	//
	// Both presets are the 2D ones, which read and write no depth: an inspector is not
	// part of the scene and must not be occluded by it.
	VertexMaterialClass *vmat = VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);
	// The tile overwrites what is under it; the bloom overlay blends, because it is an
	// annotation *of* the scene and hiding the scene would defeat it.
	DX8Wrapper::Set_Shader((mode == DEBUG_VIS_BLOOM)
		? ShaderClass::_PresetAlpha2DShader
		: ShaderClass::_PresetOpaque2DShader);
	DX8Wrapper::Set_Texture(0, nullptr);
	// Colour only. Destination alpha in the back buffer is live data -- the cross-fade's
	// framebuffer-mask mode and the soft water edge both read it back -- so a debug
	// overlay that scribbled alpha over the frame would change how the *next* effect
	// composites, which is a visualization altering what it visualizes.
	DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE,
		D3DCOLORWRITEENABLE_RED|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_BLUE);
	DX8Wrapper::Apply_Render_State_Changes();

	// Tile geometry, in the corner, so later inspectors can land in the same place and be
	// compared by flicking between them.
	const float side   = (float)screenHeight * 0.25f;
	const float margin = (float)screenHeight * 0.02f;
	const float x      = (float)screenWidth - side - margin;

	HRESULT hrA = S_OK, hrB = S_OK;
	const char *what = "";

	switch (mode)
	{
		case DEBUG_VIS_SHADOW_MAP:
		{
			// Two square tiles down the right edge: the packed depth, and above it the
			// coverage the depth pass let through. Both, not one, because they fail
			// differently and the pair localises which -- a caster absent from the depth
			// tile was culled before it reached the pass, while a caster present in depth
			// but solid-white in coverage is casting its bounding quad, not its
			// silhouette.
			what = "shadow";
			hrA = drawShadowMapTile(dev, m_pShadowMapTexture, m_debugShadowPS, x, margin, side, TRUE);
			hrB = drawShadowMapTile(dev, m_pShadowMapTexture, m_debugShadowPS,
									x, margin * 2.0f + side, side, FALSE);
			break;
		}

		case DEBUG_VIS_DEPTH:
		{
			// Same corner and size as the other tiles. Point sampling: the target is
			// screen sized, so the tile is a heavy reduction, and averaging depth across
			// a silhouette edge invents distances that are in neither surface.
			what = "depth";
			DX8Wrapper::Set_Pixel_Shader(m_debugDepthPS);
			// The projection's own depth coefficients, taken from where the depth prepass
			// published them for the PBR shader rather than recomputed here -- two copies
			// of this algebra would be free to disagree, and a tile that disagreed with
			// the reflections would be worse than no tile.
			const float *ssr = DX8Wrapper::m_ssrParams;   // {strength, maxRay, _33, _43}
			// Saturate the ramp at the far plane this projection actually implies
			// (ndcZ = 1), so the tile is scaled to the view rather than to a constant.
			// Same inversion the shader and unit_pbr_ps use, abs() included -- the
			// projection here is right-handed and the left-handed form comes out negative.
			const float denom = ssr[2] + 1.0f;
			const float farZ = (fabsf(denom) > 1.0e-6f) ? fabsf(ssr[3] / denom) : 1000.0f;
			const D3DXVECTOR4 ctl(ssr[2], ssr[3], (farZ > 1.0f) ? farZ : 1000.0f, 0.0f);
			DX8Wrapper::Set_Pixel_Shader_Constant(0, ctl, 1);
			DX8Wrapper::Set_DX8_Texture(0, m_ssrDepthTexture);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
			hrA = drawScreenQuad(dev, x, margin, side, side,
				0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
			break;
		}

		case DEBUG_VIS_BLOOM:
		{
			// Over the tactical viewport rather than the whole screen. The bright-pass
			// copy only ever covered that sub-rect -- the bloom chain samples the scene
			// through it -- so stretching it over the control bar would be inventing
			// coverage the data does not have.
			what = "bloom";
			Int xpos, ypos;
			TheTacticalView->getOrigin(&xpos, &ypos);
			const float vw = (float)TheTacticalView->getWidth();
			const float vh = (float)TheTacticalView->getHeight();
			DX8Wrapper::Set_Pixel_Shader(m_debugBloomPS);
			DX8Wrapper::Set_DX8_Texture(0, m_debugBrightTexture);
			// Point sampling on purpose. The copy is quarter resolution, and a bilinear
			// tap would spread each blooming texel over its neighbours -- which is
			// precisely the confusion between "this pixel bloomed" and "a pixel near it
			// did" that this mode exists to remove.
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
			hrA = drawScreenQuad(dev, (float)xpos, (float)ypos, vw, vh,
				0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
			break;
		}

		case DEBUG_VIS_SHROUD:
		{
			// One tile in the corner. The field is small (a texel per terrain cell), so it
			// is magnified rather than reduced here -- point sampling, so the cell grid
			// stays visible and a field that varies per cell cannot be mistaken for a
			// smooth one.
			what = "shroud";
			DX8Wrapper::Set_Pixel_Shader(m_debugShroudPS);
			DX8Wrapper::Set_DX8_Texture(0, shroudTex);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
			DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
			hrA = drawScreenQuad(dev, x, margin, side, side,
				0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
			break;
		}

		default:
			break;
	}

	// Said once if the overlay ever stops reaching the screen. A failed draw here leaves
	// the frame looking exactly like the mode being off, which is the one thing that must
	// not be silent -- every one of these modes is an argument from what is and is not on
	// screen.
	if (FAILED(hrA) || FAILED(hrB))
	{
		static Bool s_reported = FALSE;
		if (!s_reported)
		{
			DEBUG_LOG(("Debug vis: %s overlay draw failed -- 0x%08X / 0x%08X",
				what, (unsigned)hrA, (unsigned)hrB));
			s_reported = TRUE;
		}
	}

	DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE, 0x0000000F);
	DX8Wrapper::Set_Pixel_Shader(0);
#else
	(void)screenWidth; (void)screenHeight;
#endif
}

//=============================================================================
// PBR ORM (Occlusion/Roughness/Metallic + Height) texture resolution.
//
// For a mesh's base texture <name>.<ext>, the PBR maps live in a sibling
// <name>_orm.dds (installed via !TexturesHD.big). This looks it up once per base
// texture and caches the result (including "no map", stored as nullptr) so the
// per-draw cost after the first hit is a single hash lookup. A nullptr no longer
// means "not a PBR mesh" -- the render path binds the neutral default map for those
// and shades them by PBR all the same (see initDefaultOrmMap).
//=============================================================================
static std::unordered_map<TextureBaseClass*, TextureBaseClass*> s_ormCache;

TextureBaseClass* W3DShaderManager::resolveOrmTexture(TextureBaseClass* base)
{
	if (base == nullptr)
		return nullptr;

	std::unordered_map<TextureBaseClass*, TextureBaseClass*>::iterator it = s_ormCache.find(base);
	if (it != s_ormCache.end())
		return it->second;

	TextureBaseClass* orm = nullptr;
	const char* name = base->Get_Texture_Name();
	// Units are recoloured at runtime, which prefixes the texture name with a
	// "#<color>#" munge (e.g. "#-26881#avtank.tga"). The ORM map is keyed on the
	// original texture name, so strip the munge before deriving it.
	if (name != nullptr && name[0] == '#') {
		const char* second = strchr(name + 1, '#');
		if (second != nullptr)
			name = second + 1;
	}
	if (name != nullptr && name[0] != '\0' && strstr(name, "_orm") == nullptr) {
		char ormName[256];
		strncpy(ormName, name, sizeof(ormName) - 1);
		ormName[sizeof(ormName) - 1] = '\0';
		char* dot = strrchr(ormName, '.');
		if (dot != nullptr)
			*dot = '\0';
		strncat(ormName, "_orm.dds", sizeof(ormName) - strlen(ormName) - 1);

		// Only load when the map is actually present in a mounted archive; otherwise
		// Get_Texture would substitute a missing-texture placeholder.
		DDSFileClass dds(ormName, 0);
		if (dds.Is_Available()) {
			orm = WW3DAssetManager::Get_Instance()->Get_Texture(ormName);
		}
	}

	s_ormCache[base] = orm; // holds the Get_Texture ref; released in clearOrmCache
	return orm;
}

void W3DShaderManager::clearOrmCache()
{
	for (std::unordered_map<TextureBaseClass*, TextureBaseClass*>::iterator it = s_ormCache.begin();
	     it != s_ormCache.end(); ++it) {
		if (it->second != nullptr)
			it->second->Release_Ref();
	}
	s_ormCache.clear();
}

//=============================================================================
// Neutral ORM map for meshes that ship none.
//
// Only the HD texture set has authored _orm siblings, so gating PBR on their
// presence meant almost every faction unit and structure stayed on the M3 lit
// shader -- and a frame containing both read as two different renderers: one lot
// of buildings with GGX specular, cast shadows sampled per pixel and an
// environment reflection, the lot beside them flat. This is what lets the rest
// onto the same shader: one texel of stand-in ORM data, bound to stage 1 in place
// of a real map, so the shader needs no variant and no branch.
//
// The channels are the values that put the metallic-roughness BRDF closest to what
// the fixed-function pipeline drew for these meshes:
//
//   R  AO = 1.0        No occlusion. Fixed function applied the scene ambient at
//                      full strength everywhere; there is no baked cavity data to
//                      darken it with, and inventing some would be a guess.
//
//   B  metallic = 0.0  Everything is a dielectric. This is the important one. A
//                      metal's diffuse goes to zero and its F0 becomes its albedo,
//                      so guessing metal wrong turns a surface into dark tinted
//                      chrome -- which is exactly the failure the house-colour
//                      exclusion was written around, when a generated map read a
//                      white team-colour texture as near-metal. Zero cannot make
//                      that mistake: F0 stays 0.04, the albedo keeps its full
//                      diffuse, and the texture reads as the texture.
//
//   G  roughness       The one genuine judgement call. Fixed function had no
//                      specular term at all for these meshes, so 1.0 is the
//                      literal match -- but it also switches off everything PBR
//                      was added for, and a unit rendered at roughness 1.0 beside
//                      an HD one is still visibly a different material.
//
//                      0.8 is the measured mean of the authored HD ORM set (its
//                      maps run 0.51-0.53 minimum, 0.78-0.83 mean). Taking their
//                      average is what makes a mapped unit and an unmapped one sit
//                      in the same material family: a broad, dull sheen at grazing
//                      angles and no highlight anywhere near a mirror.
//
//                      It is not free. The shader's SSR weight is
//                      saturate(1 - roughness), so 0.8 opens a 32-step screen-space
//                      march at 20% weight on every mesh in the scene, where before
//                      only HD units marched. If that costs too much, raising this
//                      towards 1.0 shuts the march off before it dims anything else
//                      much -- the roughness fade on the cubemap term is only
//                      (1 - 0.6*roughness), so the reflection survives either way.
//
//   A  height = 0      Flat. The height channel drives derivative-based bump
//                      mapping, and a constant has zero gradient, so the shading
//                      normal is left exactly as the geometry gave it. (Moot while
//                      BUMP_STRENGTH is 0, but correct if it is ever raised.)
//
// One texel, D3DPOOL_MANAGED, no mip chain: it is sampled at every UV of every
// mesh and returns the same value each time.
//=============================================================================
static const float DEFAULT_ORM_AO        = 1.0f;
static const float DEFAULT_ORM_ROUGHNESS = 0.8f;
static const float DEFAULT_ORM_METALLIC  = 0.0f;
static const float DEFAULT_ORM_HEIGHT    = 0.0f;

void W3DShaderManager::initDefaultOrmMap()
{
	if (DX8Wrapper::m_defaultOrmMap != nullptr)
		return;
	IDirect3DDevice8* dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev == nullptr)
		return;

	IDirect3DTexture8* tex = nullptr;
	if (FAILED(dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex)) || tex == nullptr)
		return;

	D3DLOCKED_RECT lr;
	if (FAILED(tex->LockRect(0, &lr, nullptr, 0))) {
		tex->Release();
		return;
	}
	// A8R8G8B8 is ARGB in memory order, and the shader reads the sampled texel as
	// R=AO, G=roughness, B=metallic, A=height -- so the pack order here is
	// (height, AO, roughness, metallic).
	const unsigned char a = (unsigned char)(DEFAULT_ORM_HEIGHT    * 255.0f + 0.5f);
	const unsigned char r = (unsigned char)(DEFAULT_ORM_AO        * 255.0f + 0.5f);
	const unsigned char g = (unsigned char)(DEFAULT_ORM_ROUGHNESS * 255.0f + 0.5f);
	const unsigned char b = (unsigned char)(DEFAULT_ORM_METALLIC  * 255.0f + 0.5f);
	*(unsigned*)lr.pBits = ((unsigned)a << 24) | ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
	tex->UnlockRect(0);

	DX8Wrapper::m_defaultOrmMap = tex;
}

//=============================================================================
// Shared environment cubemap for PBR reflections.
//
// A single cubemap sampled by every PBR unit's reflection vector, filled
// procedurally with a sky/ground gradient plus a sun disc (world up = +Z).
// It is re-baked (updateEnvMap) whenever the scene's dominant light / ambient
// drifts, so the reflections track time-of-day. Colours are stored linear --
// the PBR shader adds them before its own gamma pass.
//=============================================================================

// Env cubemap edge length. 64 was chosen when the bake ran constantly (it was fed by a
// per-draw light snapshot that never settled). Now that it reads the map's global
// lighting it bakes about twice per session, so the budget goes into resolution
// instead: 256 is what makes cloud shapes survive into the reflection rather than
// smearing into the blue gradient underneath.
//
// The GPU does not care -- a texCUBE is one tap at any size, and six 256x256 faces plus
// mips is ~2MB. The cost is the CPU bake, which is O(size^2) and now does noise per
// texel, so keep an eye on this if it goes higher.
static const int ENV_MAP_SIZE = 256;

// Base sky / ground tint at full daylight; modulated per-bake by scene brightness.
static const float ENV_SKY_BASE[3]    = { 0.35f, 0.52f, 0.85f };
static const float ENV_GROUND_BASE[3] = { 0.20f, 0.18f, 0.15f };

// Parameters the last bake used, so updateEnvMap can skip re-baking when nothing
// meaningful changed (time-of-day drifts slowly; most frames are a no-op).
static bool  s_envBaked = false;
static float s_envBakedSunDir[3];
static float s_envBakedSunColor[3];
static float s_envBakedSky[3];
static float s_envBakedGround[3];

// Cloud structure for the sky.
//
// Without this the sky is a pure vertical gradient, so its reflection carries no
// information except "which way is up": a surface turning under it changes brightness
// but never shape, which reads as a washed-out blue smear rather than as a sky.
//
// The noise is sampled on the 3D direction vector rather than per-face in 2D, which
// makes it seamless across cube edges for free. A 2D field would need matching edges on
// all twelve joins.
// Tuning note: three octaves of value noise land in a narrow band -- mean 0.5, sigma
// about 0.19 -- so the threshold has to sit near or below the mean and rise steeply, or
// the mask never saturates and the result is faint wisps instead of cloud. The first
// attempt used cover 0.52 / sharpness 2.2, which peaked around 0.4 at +1 sigma and was
// invisible once reflected onto a unit.
// Frequency note: a flat surface only sweeps 5-8 degrees of reflection vector across its
// whole width (that is just its angular size from the camera -- N is constant on a flat
// face, so R varies only as V does). Cloud features therefore have to be a few degrees
// across to show up on a roof at all. At 2.6 the base masses were ~22 degrees wide and
// every flat face sampled one uniform patch of them.
static const float ENV_CLOUD_FREQ      = 6.0f;  // lower = larger cloud masses
static const float ENV_CLOUD_COVER     = 0.46f; // noise level above which cloud appears
static const float ENV_CLOUD_SHARPNESS = 5.0f;  // how hard the cloud edge is
static const float ENV_CLOUD_STRENGTH  = 0.90f; // how far towards cloud colour to go

static float envHash3(int x, int y, int z)
{
	unsigned n = (unsigned)(x * 374761393) + (unsigned)(y * 668265263) + (unsigned)(z * 1442695041);
	n = (n ^ (n >> 13)) * 1274126177u;
	n =  n ^ (n >> 16);
	return (float)(n & 0xFFFFu) * (1.0f / 65535.0f);
}

static float envValueNoise(float x, float y, float z)
{
	const float fx = floorf(x), fy = floorf(y), fz = floorf(z);
	const int   ix = (int)fx,   iy = (int)fy,   iz = (int)fz;
	float tx = x - fx, ty = y - fy, tz = z - fz;
	// Smoothstep the interpolants; linear ones leave visible lattice creases.
	tx = tx*tx*(3.0f - 2.0f*tx);
	ty = ty*ty*(3.0f - 2.0f*ty);
	tz = tz*tz*(3.0f - 2.0f*tz);

	const float c000 = envHash3(ix,   iy,   iz  ), c100 = envHash3(ix+1, iy,   iz  );
	const float c010 = envHash3(ix,   iy+1, iz  ), c110 = envHash3(ix+1, iy+1, iz  );
	const float c001 = envHash3(ix,   iy,   iz+1), c101 = envHash3(ix+1, iy,   iz+1);
	const float c011 = envHash3(ix,   iy+1, iz+1), c111 = envHash3(ix+1, iy+1, iz+1);

	const float x00 = c000 + (c100 - c000) * tx;
	const float x10 = c010 + (c110 - c010) * tx;
	const float x01 = c001 + (c101 - c001) * tx;
	const float x11 = c011 + (c111 - c011) * tx;
	const float y0  = x00 + (x10 - x00) * ty;
	const float y1  = x01 + (x11 - x01) * ty;
	return y0 + (y1 - y0) * tz;
}

// Four octaves: the fourth is what puts wispy edge detail at roughly a degree, which is
// the scale a flat face can actually resolve. The mip chain handles the aliasing this
// would otherwise cause.
static float envCloudFbm(float x, float y, float z)
{
	float sum = 0.0f, amp = 0.5f, freq = 1.0f;
	for (int o = 0; o < 4; ++o) {
		sum  += envValueNoise(x*freq, y*freq, z*freq) * amp;
		freq *= 2.17f;   // non-integral, so the octaves do not line up on the lattice
		amp  *= 0.5f;
	}
	return sum * (1.0f / 0.9375f);   // 0.5 + 0.25 + 0.125 + 0.0625
}

// Fill all six faces of a locked cubemap with the sky/ground gradient + sun disc.
static void bakeEnvMapFaces(IDirect3DCubeTexture8* cube,
                            const float sunDir[3], const float sunColor[3],
                            const float sky[3], const float ground[3])
{
	// Clouds are lit by the sun and sit against the sky, so their colour is the sky
	// lifted towards white in proportion to how bright the sun is. Tying them to the
	// sun rather than fixing them white keeps them from staying a bright band at night.
	float cloudCol[3];
	{
		float sunLuma = 0.30f*sunColor[0] + 0.59f*sunColor[1] + 0.11f*sunColor[2];
		if (sunLuma < 0.0f) sunLuma = 0.0f; else if (sunLuma > 1.0f) sunLuma = 1.0f;
		const float lift = (0.35f + 0.65f * sunLuma) * 0.72f;
		for (int i = 0; i < 3; ++i)
			cloudCol[i] = sky[i] + (1.0f - sky[i]) * lift;
	}

	// Mean colour over all six faces. The shader divides its irradiance tap by this, so
	// the directional ambient it derives averages to exactly 1.0 and can scale the
	// engine's own ambient without changing the overall exposure -- only its direction.
	double envSumR = 0.0, envSumG = 0.0, envSumB = 0.0;

	for (int face = 0; face < 6; ++face) {
		D3DLOCKED_RECT lr;
		if (FAILED(cube->LockRect((D3DCUBEMAP_FACES)face, 0, &lr, nullptr, 0)))
			continue;
		for (int y = 0; y < ENV_MAP_SIZE; ++y) {
			unsigned* row = (unsigned*)((unsigned char*)lr.pBits + y * lr.Pitch);
			float t = ((float)y + 0.5f) / ENV_MAP_SIZE * 2.0f - 1.0f;
			for (int x = 0; x < ENV_MAP_SIZE; ++x) {
				float s = ((float)x + 0.5f) / ENV_MAP_SIZE * 2.0f - 1.0f;
				float dx, dy, dz;
				switch (face) {
					case 0: dx = 1;  dy = -t; dz = -s; break; // +X
					case 1: dx = -1; dy = -t; dz = s;  break; // -X
					case 2: dx = s;  dy = 1;  dz = t;  break; // +Y
					case 3: dx = s;  dy = -1; dz = -t; break; // -Y
					case 4: dx = s;  dy = -t; dz = 1;  break; // +Z
					default: dx = -s; dy = -t; dz = -1; break; // -Z
				}
				float il = 1.0f / sqrtf(dx*dx + dy*dy + dz*dz);
				dx *= il; dy *= il; dz *= il;
				float up = dz * 0.5f + 0.5f; // world up = +Z
				if (up < 0.0f) up = 0.0f; else if (up > 1.0f) up = 1.0f;
				float r = ground[0] + (sky[0] - ground[0]) * up;
				float g = ground[1] + (sky[1] - ground[1]) * up;
				float b = ground[2] + (sky[2] - ground[2]) * up;
				// Cloud masses, upper hemisphere only and faded out towards the horizon
				// so none of them appear below it.
				if (dz > 0.0f) {
					const float n = envCloudFbm(dx*ENV_CLOUD_FREQ, dy*ENV_CLOUD_FREQ, dz*ENV_CLOUD_FREQ);
					float mask = (n - ENV_CLOUD_COVER) * ENV_CLOUD_SHARPNESS;
					if (mask < 0.0f) mask = 0.0f; else if (mask > 1.0f) mask = 1.0f;
					const float horizonFade = dz * (2.0f - dz);   // 0 at the horizon, 1 overhead
					const float cf = mask * horizonFade * ENV_CLOUD_STRENGTH;
					r += (cloudCol[0] - r) * cf;
					g += (cloudCol[1] - g) * cf;
					b += (cloudCol[2] - b) * cf;
				}

				float sd = dx*sunDir[0] + dy*sunDir[1] + dz*sunDir[2];
				if (sd > 0.0f) {
					// sd^256 and sd^8 by repeated squaring. This used to be two powf
					// calls, which dominated the bake once it grew to 16x the texels
					// (256 rather than 250 for the disc is visually identical).
					const float sd2  = sd*sd,     sd4   = sd2*sd2,   sd8   = sd4*sd4;
					const float sd16 = sd8*sd8,   sd32  = sd16*sd16, sd64  = sd32*sd32;
					const float sd128= sd64*sd64, sd256 = sd128*sd128;
					const float sun = sd256 + sd8 * 0.2f;
					r += sunColor[0] * sun; g += sunColor[1] * sun; b += sunColor[2] * sun;
				}
				int ri = (int)((r > 1.0f ? 1.0f : r) * 255.0f);
				int gi = (int)((g > 1.0f ? 1.0f : g) * 255.0f);
				int bi = (int)((b > 1.0f ? 1.0f : b) * 255.0f);
				row[x] = 0xFF000000u | (ri << 16) | (gi << 8) | bi;

				envSumR += ri; envSumG += gi; envSumB += bi;
			}
		}
		cube->UnlockRect((D3DCUBEMAP_FACES)face, 0);
	}

	{
		const double texels = 6.0 * ENV_MAP_SIZE * ENV_MAP_SIZE * 255.0;
		DX8Wrapper::m_envAverage[0] = (float)(envSumR / texels);
		DX8Wrapper::m_envAverage[1] = (float)(envSumG / texels);
		DX8Wrapper::m_envAverage[2] = (float)(envSumB / texels);
		DX8Wrapper::m_envAverage[3] = 1.0f;
	}

	// Rebuild the mip chain from the level 0 we just wrote. Required now that the faces
	// carry cloud detail: the reflection vector can sweep most of a face across a single
	// pixel on a curved surface, and without mips that undersampling sparkles.
	D3DXFilterTexture(cube, nullptr, 0, D3DX_DEFAULT);
}

// The scene's dominant light, for the env bake.
//
// This is deliberately the map's own global lighting rather than anything sampled out
// of the draw path. A per-draw snapshot is whichever mesh happened to be rendered last,
// and W3D gives every object its own LightEnvironment (the global directionals plus any
// nearby point lights), so light 0 is only sometimes the sun. Baking from it made the
// cubemap's sun colour lurch between draws and re-baked all six faces on the CPU each
// time. W3DView's shadow frustum reads the same globals, so the reflected sun disc and
// the cast shadows now agree on where the sun is.
static void getSceneEnvLight(float sunDir[3], float sunColor[3], float ambient[3])
{
	// Fallbacks for a bake that runs before the map's lighting is loaded. updateEnvMap
	// re-bakes once the real values arrive, so an early bake is self-correcting.
	sunDir[0] = 0.40f; sunDir[1] = 0.30f; sunDir[2] = 0.85f;
	sunColor[0] = 1.00f; sunColor[1] = 0.92f; sunColor[2] = 0.72f;
	ambient[0] = 0.20f; ambient[1] = 0.20f; ambient[2] = 0.22f;

	if (TheGlobalData == nullptr)
		return;

	// m_terrainLightPos points along the light's travel, so negate for "toward the sun".
	const Coord3D &lightPos = TheGlobalData->m_terrainLightPos[0];
	const float len = sqrtf(lightPos.x*lightPos.x + lightPos.y*lightPos.y + lightPos.z*lightPos.z);
	if (len > 1e-3f) {
		sunDir[0] = -lightPos.x / len;
		sunDir[1] = -lightPos.y / len;
		sunDir[2] = -lightPos.z / len;
	}

	sunColor[0] = TheGlobalData->m_terrainDiffuse[0].red;
	sunColor[1] = TheGlobalData->m_terrainDiffuse[0].green;
	sunColor[2] = TheGlobalData->m_terrainDiffuse[0].blue;

	ambient[0] = TheGlobalData->m_terrainAmbient[0].red;
	ambient[1] = TheGlobalData->m_terrainAmbient[0].green;
	ambient[2] = TheGlobalData->m_terrainAmbient[0].blue;
}

// Derive sky/ground reflection colours from the captured scene light. The sun
// disc uses the light's own colour (so it warms/dims with time-of-day); the sky
// and ground are the daylight tints scaled by overall brightness and lifted by
// the scene ambient, so night scenes reflect dark and dusk reflects warm.
static void deriveEnvColors(const float sunColor[3], const float ambient[3],
                            float sky[3], float ground[3])
{
	float sunLuma = 0.30f*sunColor[0] + 0.59f*sunColor[1] + 0.11f*sunColor[2];
	if (sunLuma < 0.0f) sunLuma = 0.0f; else if (sunLuma > 1.0f) sunLuma = 1.0f;
	float lvl = 0.25f + 0.75f * sunLuma;
	// The ambient is only meant to tint the reflection. Adding it at full strength to the
	// ground (and half to the sky) swamps the gradient: this game's D3DRS_AMBIENT runs
	// around 0.7, which lifted ground to ~(0.91,0.86,0.70) against a sky of
	// ~(0.69,0.84,1.00) -- brighter than the sky, and near enough to it that the cubemap
	// became a flat colour. A reflection then returns the same value in every direction
	// and stops responding to surface orientation or camera movement at all, which reads
	// as "PBR is doing nothing". Keep the lift small and the ground clearly darker than
	// the sky: that contrast is what makes a reflection legible as things move.
	const float AMBIENT_TINT_SKY    = 0.15f;
	const float AMBIENT_TINT_GROUND = 0.10f;
	for (int i = 0; i < 3; ++i) {
		sky[i]    = ENV_SKY_BASE[i]    * lvl + ambient[i] * AMBIENT_TINT_SKY;
		ground[i] = ENV_GROUND_BASE[i] * lvl + ambient[i] * AMBIENT_TINT_GROUND;
	}
}

//=============================================================================
// Directional shadow map.
//
// A square colour render target (packed depth) plus its own depth buffer. Each
// frame the scene is rendered once from the sun's point of view into it (via the
// shadowdepth shaders, routed by DX8Wrapper's shadow-depth-pass flag); the unit
// and terrain shaders then reproject and sample it for cast shadows.
//=============================================================================
static const int SHADOW_MAP_SIZE = DX8Wrapper::SHADOW_MAP_SIZE;

// Render states the depth pass overrides, saved across it so none of them escape.
static const DWORD s_shadowSavedStateIds[W3DShaderManager::NUM_SHADOW_SAVED_STATES] =
{
	D3DRS_COLORWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_ZENABLE,
	D3DRS_ZWRITEENABLE,     D3DRS_ZFUNC,            D3DRS_SRCBLEND,
	D3DRS_CULLMODE,         D3DRS_FILLMODE,         D3DRS_STENCILENABLE,
	D3DRS_ZBIAS,
};

void W3DShaderManager::initShadowMap()
{
	if (m_pShadowMapTexture != nullptr)
		return;
	LPDIRECT3DDEVICE8 dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev == nullptr)
		return;

	if (DX8Wrapper::m_dwShadowDepthVS == 0)
		LoadAndCreateD3DShader("shaders\\shadowdepth_vs.vso", nullptr, 0, true, &DX8Wrapper::m_dwShadowDepthVS);
	if (DX8Wrapper::m_dwShadowDepthPS == 0)
		LoadAndCreateD3DShader("shaders\\shadowdepth_ps.pso", nullptr, 0, false, &DX8Wrapper::m_dwShadowDepthPS);
	if (DX8Wrapper::m_dwShadowDepthVS == 0 || DX8Wrapper::m_dwShadowDepthPS == 0)
	{
		return;   // shaders missing -> shadow mapping stays off (Has_Shadow_Map() false)
	}

	// The particle-sprite variant of the same pair. Not fatal if it is missing: the
	// routing checks both handles and falls back to leaving particles out of the map, so
	// an install with the older shader set loses smoke shadows rather than all shadows.
	if (DX8Wrapper::m_dwShadowDepthParticleVS == 0)
		LoadAndCreateD3DShader("shaders\\shadowdepthparticle_vs.vso", nullptr, 0, true, &DX8Wrapper::m_dwShadowDepthParticleVS);
	if (DX8Wrapper::m_dwShadowDepthParticlePS == 0)
		LoadAndCreateD3DShader("shaders\\shadowdepthparticle_ps.pso", nullptr, 0, false, &DX8Wrapper::m_dwShadowDepthParticlePS);
	if (DX8Wrapper::m_dwShadowDepthParticleVS == 0 || DX8Wrapper::m_dwShadowDepthParticlePS == 0)
	{
		DEBUG_LOG(("Shadow map: particle depth shaders did not load -- particles will not cast\n"));
	}

	HRESULT texHr = dev->CreateTexture(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1, D3DUSAGE_RENDERTARGET,
			D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_pShadowMapTexture);
	if (FAILED(texHr) || m_pShadowMapTexture == nullptr)
	{
		m_pShadowMapTexture = nullptr;
		return;
	}
	if (FAILED(m_pShadowMapTexture->GetSurfaceLevel(0, &m_pShadowMapSurface)))
	{
		SAFE_RELEASE(m_pShadowMapTexture);
		return;
	}
	HRESULT dsHr = dev->CreateDepthStencilSurface(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, D3DFMT_D16,
			D3DMULTISAMPLE_NONE, &m_pShadowMapDepthSurface);
	if (FAILED(dsHr))
	{
		SAFE_RELEASE(m_pShadowMapSurface);
		SAFE_RELEASE(m_pShadowMapTexture);
		m_pShadowMapDepthSurface = nullptr;
		return;
	}

	DX8Wrapper::m_pShadowMap = m_pShadowMapTexture;   // now Has_Shadow_Map() is true
}

void W3DShaderManager::shutdownShadowMap()
{
	DX8Wrapper::m_pShadowMap = nullptr;
	SAFE_RELEASE(m_pShadowMapDepthSurface);
	SAFE_RELEASE(m_pShadowMapSurface);
	SAFE_RELEASE(m_pShadowMapTexture);
	if (DX8Wrapper::m_dwShadowDepthVS) {
		reinterpret_cast<IDirect3DVertexShader9*>(DX8Wrapper::m_dwShadowDepthVS)->Release();
		DX8Wrapper::m_dwShadowDepthVS = 0;
	}
	if (DX8Wrapper::m_dwShadowDepthPS) {
		reinterpret_cast<IDirect3DPixelShader9*>(DX8Wrapper::m_dwShadowDepthPS)->Release();
		DX8Wrapper::m_dwShadowDepthPS = 0;
	}
	if (DX8Wrapper::m_dwShadowDepthParticleVS) {
		reinterpret_cast<IDirect3DVertexShader9*>(DX8Wrapper::m_dwShadowDepthParticleVS)->Release();
		DX8Wrapper::m_dwShadowDepthParticleVS = 0;
	}
	if (DX8Wrapper::m_dwShadowDepthParticlePS) {
		reinterpret_cast<IDirect3DPixelShader9*>(DX8Wrapper::m_dwShadowDepthParticlePS)->Release();
		DX8Wrapper::m_dwShadowDepthParticlePS = 0;
	}
}

#ifdef RTS_DEBUG
// Save the shadow map to PNG part-way through the depth pass.
//
// The end-of-frame dump shows the finished map, in which one contributor cannot be told
// from another -- a smoke silhouette and the terrain it stands on are both just depth.
// Dumping either side of a single submission makes the difference between the two files
// exactly that submission's contribution, and nothing else, within one frame. No second
// run, and so no assumption that two runs put the particles in the same place.
void W3DShaderManager::debugDumpShadowMap(const char *tag)
{
	if (m_pShadowMapSurface == nullptr)
		return;
	char path[MAX_PATH];
	sprintf(path, "dump_shadowmap_%s.png", tag);
	D3DXSaveSurfaceToFileA(path, D3DXIFF_PNG, m_pShadowMapSurface, nullptr, nullptr);
}
#endif

// Single answer to "are cast shadows coming from the shadow map this frame?", so the
// depth pass and the legacy volume/decal shadows can never both decide they are on.
Bool W3DShaderManager::isShadowMappingActive()
{
	return TheGlobalData->m_useShadowMapping && DX8Wrapper::Has_Shadow_Map();
}

// ---------------------------------------------------------------------------
// The sun frustum, published for culling.
//
// The depth pass runs over the scene with the camera, so everything it draws is chosen
// by the camera's frustum -- and that is the wrong volume. A caster only has to be
// inside the *sun's* frustum for its shadow to land on screen, and the two disagree
// most exactly where it shows: anything above the ground is displaced up-sun from where
// its shadow falls, so an aircraft whose shadow is in the middle of the view sits well
// outside a zoomed-in camera frustum, and a tree just off the edge of the screen throws
// a long shadow well inside it. Both stopped casting the moment the caster itself left
// the screen.
//
// Kept as an explicit light basis rather than reusing SunVP: a CameraClass would be the
// obvious carrier, but FrustumClass::Init always builds a perspective pyramid from the
// view plane, so an ORTHO camera culls against the wrong shape.
// The box itself is stored by DX8Wrapper, next to the SunVP it mirrors. The last place
// that still culled the depth pass by the camera is MeshClass::Render, which sits below
// this layer and cannot see W3DShaderManager -- so there is one owner, and the two can
// never disagree about what the sun can see.
void W3DShaderManager::setShadowFrustum(const Vector3 &eye, const Vector3 &lookDir,
										Real halfWidth, Real upMin, Real upMax,
										Real nearDist, Real farDist)
{
	Vector3 fwd = lookDir;
	if (fwd.Length2() < 1e-12f || halfWidth <= 0.0f || upMax <= upMin)
	{
		DX8Wrapper::Clear_Sun_Cull_Box();
		return;
	}
	fwd.Normalize();

	// The same basis D3DXMatrixLookAtLH builds from the same hint, so that "up" here is
	// the axis the projection's asymmetric up range is quoted in. Pick the world axis the
	// light is least aligned with so the cross product never degenerates.
	const Vector3 hint = (fabsf(fwd.Z) > 0.9f) ? Vector3(0.0f, 1.0f, 0.0f)
											   : Vector3(0.0f, 0.0f, 1.0f);
	Vector3 right, up;
	Vector3::Cross_Product(hint, fwd, &right);
	right.Normalize();
	Vector3::Cross_Product(fwd, right, &up);
	up.Normalize();

	DX8Wrapper::Set_Sun_Cull_Box(eye, right, up, fwd, halfWidth, upMin, upMax,
								 nearDist, farDist);
}

Bool W3DShaderManager::hasShadowFrustum()
{
	return DX8Wrapper::Has_Sun_Cull_Box() ? TRUE : FALSE;
}

Bool W3DShaderManager::cullSphereFromShadowFrustum(const Vector3 &center, Real radius)
{
	return DX8Wrapper::Cull_Sphere_By_Sun(center, radius) ? TRUE : FALSE;
}

// ---------------------------------------------------------------------------
// Screen-space reflections.
//
// Two resources: a camera-view depth target, rendered by the shadow map's own depth
// shaders from the camera rather than the sun, and a copy of the previous frame's
// scene. The rays march the first and read the second. It has to be the previous
// frame's copy -- while units are drawing, this frame's scene is the live render
// target, and D3D9 leaves a read from the bound render target undefined.
// ---------------------------------------------------------------------------

IDirect3DTexture8 *W3DShaderManager::m_ssrDepthTexture = nullptr;
IDirect3DSurface8 *W3DShaderManager::m_ssrDepthSurface = nullptr;
IDirect3DSurface8 *W3DShaderManager::m_ssrDepthStencil = nullptr;
IDirect3DTexture8 *W3DShaderManager::m_sceneHistoryTexture = nullptr;
IDirect3DSurface8 *W3DShaderManager::m_sceneHistorySurface = nullptr;

void W3DShaderManager::initSsr()
{
	if (m_ssrDepthTexture != nullptr)
		return;
	LPDIRECT3DDEVICE8 dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev == nullptr)
		return;
	// The depth pass is the shadow map's, pointed elsewhere. Without those shaders
	// there is nothing to render depth with, and SSR simply stays off.
	if (DX8Wrapper::m_dwShadowDepthVS == 0 || DX8Wrapper::m_dwShadowDepthPS == 0)
	{
		DEBUG_LOG(("SSR: disabled -- the shadow depth shaders did not load\n"));
		return;
	}

	IDirect3DSurface8 *rt = nullptr;
	if (FAILED(dev->GetRenderTarget(0, &rt)) || rt == nullptr)
	{
		DEBUG_LOG(("SSR: disabled -- no render target to take the screen size from\n"));
		return;
	}
	D3DSURFACE_DESC desc;
	rt->GetDesc(&desc);
	rt->Release();

	// Both targets are screen-sized: the depth one because the shader reprojects
	// straight into screen UV and any other size would need a scale factor nothing
	// else knows about, the history one because it is a copy of the frame. The depth
	// buffer is deliberately non-multisampled -- this target is never resolved and
	// nothing samples its edges, so MSAA would only cost fill rate.
	if (FAILED(dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET,
				D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_ssrDepthTexture)) ||
		FAILED(m_ssrDepthTexture->GetSurfaceLevel(0, &m_ssrDepthSurface)) ||
		FAILED(dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET,
				D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_sceneHistoryTexture)) ||
		FAILED(m_sceneHistoryTexture->GetSurfaceLevel(0, &m_sceneHistorySurface)) ||
		FAILED(dev->CreateDepthStencilSurface(desc.Width, desc.Height, D3DFMT_D16,
				D3DMULTISAMPLE_NONE, &m_ssrDepthStencil)))
	{
		DEBUG_LOG(("SSR: disabled -- could not create the %dx%d targets\n",
			desc.Width, desc.Height));
		shutdownSsr();
		return;
	}
	DEBUG_LOG(("SSR: active, %dx%d depth + scene history\n", desc.Width, desc.Height));

	// Clear both before anything can sample them. A render target's contents are
	// undefined until something writes them, and undefined does not mean black -- it
	// means whatever was last in that memory. The shader reflects it perfectly happily,
	// which shows up as reflections in colours that appear nowhere in the scene, and
	// the "is the history still black" check never fires to say so.
	IDirect3DSurface8 *savedRT = nullptr;
	IDirect3DSurface8 *savedDS = nullptr;
	dev->GetRenderTarget(0, &savedRT);
	dev->GetDepthStencilSurface(&savedDS);
	if (SUCCEEDED(DX8Wrapper::Set_DX8_Render_Target(m_ssrDepthSurface, m_ssrDepthStencil)))
		DX8Wrapper::Clear(true, true, Vector3(1.0f, 0.0f, 0.0f), 1.0f, 1.0f);   // red = far
	if (SUCCEEDED(DX8Wrapper::Set_DX8_Render_Target(m_sceneHistorySurface, nullptr)))
		DX8Wrapper::Clear(true, false, Vector3(0.0f, 0.0f, 0.0f), 1.0f, 1.0f);
	DX8Wrapper::Set_DX8_Render_Target(savedRT, savedDS);
	SAFE_RELEASE(savedRT);
	SAFE_RELEASE(savedDS);

	DX8Wrapper::m_pSceneDepth = m_ssrDepthTexture;   // now Has_Ssr() is true
	DX8Wrapper::m_pSceneColor = m_sceneHistoryTexture;
}

// ---------------------------------------------------------------------------
// Refraction grab.
//
// A copy of the scene as it stood immediately before the water drew, so the water can
// read what is behind it at a *displaced* pixel. It cannot use the SSR history for this:
// that is the previous frame, which lags the camera and already contains last frame's
// water, so the surface would refract itself.
//
// Separate from the SSR pair on purpose. This one is captured mid-frame, at the moment
// the water flushes, and it is the only one of the three that is guaranteed to match what
// the hardware blend is about to read out of the frame buffer -- which is what makes the
// difference trick in water_ps legitimate.
// ---------------------------------------------------------------------------

IDirect3DTexture8 *W3DShaderManager::m_refractionTexture = nullptr;
IDirect3DSurface8 *W3DShaderManager::m_refractionSurface = nullptr;

void W3DShaderManager::initRefraction()
{
	if (m_refractionTexture != nullptr)
		return;
	LPDIRECT3DDEVICE8 dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev == nullptr)
		return;

	IDirect3DSurface8 *rt = nullptr;
	if (FAILED(dev->GetRenderTarget(0, &rt)) || rt == nullptr)
		return;
	D3DSURFACE_DESC desc;
	rt->GetDesc(&desc);
	rt->Release();

	// Screen-sized: the shader reprojects world positions straight into screen UV, the
	// same as the depth lookup, and a different size would need a scale factor nothing
	// else knows about. Non-multisampled -- StretchRect resolves on the way in.
	if (FAILED(dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET,
				D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &m_refractionTexture)) ||
		FAILED(m_refractionTexture->GetSurfaceLevel(0, &m_refractionSurface)))
	{
		DEBUG_LOG(("Water refraction: disabled -- could not create the %dx%d target\n",
			desc.Width, desc.Height));
		shutdownRefraction();
		return;
	}

	// Clear before anything can sample it. A render target's contents are undefined until
	// written, and undefined is not black -- the water would refract whatever was last in
	// that memory, which is the failure mode the SSR history hit.
	IDirect3DSurface8 *savedRT = nullptr;
	IDirect3DSurface8 *savedDS = nullptr;
	dev->GetRenderTarget(0, &savedRT);
	dev->GetDepthStencilSurface(&savedDS);
	if (SUCCEEDED(DX8Wrapper::Set_DX8_Render_Target(m_refractionSurface, nullptr)))
		DX8Wrapper::Clear(true, false, Vector3(0.0f, 0.0f, 0.0f), 1.0f, 1.0f);
	DX8Wrapper::Set_DX8_Render_Target(savedRT, savedDS);
	SAFE_RELEASE(savedRT);
	SAFE_RELEASE(savedDS);

	DEBUG_LOG(("Water refraction: active, %dx%d grab target\n", desc.Width, desc.Height));
	DX8Wrapper::m_pRefraction = m_refractionTexture;
}

void W3DShaderManager::shutdownRefraction()
{
	DX8Wrapper::m_pRefraction = nullptr;
	SAFE_RELEASE(m_refractionSurface);
	SAFE_RELEASE(m_refractionTexture);
}

void W3DShaderManager::captureRefraction()
{
	if (m_refractionSurface == nullptr)
		return;
	LPDIRECT3DDEVICE8 dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev == nullptr)
		return;

	// Whatever is the render target right now -- the back buffer normally, the filter
	// chain's texture when bloom is running. Both are correct: it is the surface the
	// water's own blend is about to read.
	IDirect3DSurface8 *src = nullptr;
	if (SUCCEEDED(dev->GetRenderTarget(0, &src)) && src != nullptr)
	{
		dev->StretchRect(src, nullptr, m_refractionSurface, nullptr, D3DTEXF_NONE);
		src->Release();
	}
}

void W3DShaderManager::shutdownSsr()
{
	DX8Wrapper::m_pSceneDepth = nullptr;
	DX8Wrapper::m_pSceneColor = nullptr;
	SAFE_RELEASE(m_ssrDepthStencil);
	SAFE_RELEASE(m_ssrDepthSurface);
	SAFE_RELEASE(m_ssrDepthTexture);
	SAFE_RELEASE(m_sceneHistorySurface);
	SAFE_RELEASE(m_sceneHistoryTexture);
}

Bool W3DShaderManager::isSsrActive()
{
	return DX8Wrapper::Has_Ssr();
}

void W3DShaderManager::startCameraDepthRendering()
{
	if (m_ssrDepthSurface == nullptr || m_ssrDepthStencil == nullptr)
		return;
	LPDIRECT3DDEVICE8 dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev == nullptr)
		return;

	// The saved render target and state slots are the shadow pass's. Sharing them is
	// safe only because the two passes are strictly sequential -- one finishes and
	// restores before the other starts -- and it keeps one restore path rather than
	// two that could drift apart.
	m_shadowSavedRT = nullptr;
	m_shadowSavedDepth = nullptr;
	dev->GetRenderTarget(0, &m_shadowSavedRT);
	dev->GetDepthStencilSurface(&m_shadowSavedDepth);

	if (FAILED(DX8Wrapper::Set_DX8_Render_Target(m_ssrDepthSurface, m_ssrDepthStencil)))
	{
		static Bool s_loggedRtFail = FALSE;
		if (!s_loggedRtFail) {
			s_loggedRtFail = TRUE;
			DEBUG_LOG(("SSR: depth prepass SKIPPED -- Set_DX8_Render_Target failed. The "
				"pass never runs, so nothing is written and SsrParams stays zero.\n"));
		}
		SAFE_RELEASE(m_shadowSavedRT);
		SAFE_RELEASE(m_shadowSavedDepth);
		return;
	}
	for (Int i = 0; i < NUM_SHADOW_SAVED_STATES; ++i)
		dev->GetRenderState((D3DRENDERSTATETYPE)s_shadowSavedStateIds[i], &m_shadowSavedStates[i]);

	DX8Wrapper::Set_Shadow_Depth_Pass(true);
	DX8Wrapper::Set_Depth_Prepass(true);
	// Red is depth 1.0 exactly -- the pack weights the channels 1, 1/255, 1/255^2, so R
	// alone is the far plane. Anywhere the pass rasterises nothing then reads as empty
	// sky, and a ray crossing it finds no hit rather than one at the near plane.
	DX8Wrapper::Clear(true, true, Vector3(1.0f, 0.0f, 0.0f), 1.0f, 1.0f);
}

void W3DShaderManager::endCameraDepthRendering()
{
	DX8Wrapper::Set_Depth_Prepass(false);
	endShadowMapRendering();   // same restore: flag off, target back, states put back

	// New frame's scene colour has not been captured yet. This runs once per frame,
	// before the scene is drawn, which is exactly where the flag needs clearing.
	resetSceneHistoryCaptured();
}

void W3DShaderManager::captureSceneHistory()
{
	if (m_sceneHistorySurface == nullptr || m_renderTexture == nullptr)
		return;
	LPDIRECT3DDEVICE8 dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev == nullptr)
		return;
	// m_renderTexture is the scene already resolved out of MSAA by endRenderToTexture,
	// so this is a straight copy. It exists because that texture is the render target
	// again next frame, and a texture cannot be read while it is being written.
	IDirect3DSurface8 *src = nullptr;
	if (SUCCEEDED(m_renderTexture->GetSurfaceLevel(0, &src)) && src != nullptr)
	{
		dev->StretchRect(src, nullptr, m_sceneHistorySurface, nullptr, D3DTEXF_NONE);
		src->Release();
		m_sceneHistoryCaptured = true;
	}
}

// Capture the history straight off the back buffer.
//
// captureSceneHistory above hangs off endRenderToTexture, which is only ever reached from
// a screen filter's postRender. With no filter active -- the normal case when bloom is
// off -- the scene never goes through a render target at all, nothing wrote the history,
// and every SSR ray resolved against a black texture. That is why SSR appeared to do
// nothing while its depth prepass was demonstrably correct: it has two inputs and only
// the depth one was ever populated.
//
// StretchRect resolves multisampling on the way, so this is also correct with MSAA on.
void W3DShaderManager::captureSceneHistoryFromBackBuffer()
{
	if (m_sceneHistorySurface == nullptr)
		return;
	LPDIRECT3DDEVICE8 dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev == nullptr)
		return;

	IDirect3DSurface8 *back = nullptr;
	if (SUCCEEDED(dev->GetRenderTarget(0, &back)) && back != nullptr)
	{
		dev->StretchRect(back, nullptr, m_sceneHistorySurface, nullptr, D3DTEXF_NONE);
		back->Release();
		m_sceneHistoryCaptured = true;
	}
}

void W3DShaderManager::startShadowMapRendering()
{
	if (m_pShadowMapSurface == nullptr || m_pShadowMapDepthSurface == nullptr)
		return;
	LPDIRECT3DDEVICE8 dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev == nullptr)
		return;

	m_shadowSavedRT = nullptr;
	m_shadowSavedDepth = nullptr;
	dev->GetRenderTarget(0, &m_shadowSavedRT);
	dev->GetDepthStencilSurface(&m_shadowSavedDepth);

	if (FAILED(DX8Wrapper::Set_DX8_Render_Target(m_pShadowMapSurface, m_pShadowMapDepthSurface)))
	{
		SAFE_RELEASE(m_shadowSavedRT);
		SAFE_RELEASE(m_shadowSavedDepth);
		return;
	}
	// The depth pass overrides the render states it needs (see the shadow-depth branch in
	// DX8Wrapper::Apply_Render_State_Changes) and leaves them on the device. Invalidating
	// the state cache afterwards is not enough on its own: it makes the next Set_ write
	// through, but only for states something actually sets again, and the normal scene
	// never touches COLORWRITEENABLE. An unroutable draw masks colour and z to keep
	// itself out of the map, and that mask would then follow the pass out and quietly
	// blank whatever drew next. Put back exactly what was here.
	LPDIRECT3DDEVICE8 stateDev = dev;
	for (Int i = 0; i < NUM_SHADOW_SAVED_STATES; ++i)
		stateDev->GetRenderState((D3DRENDERSTATETYPE)s_shadowSavedStateIds[i], &m_shadowSavedStates[i]);

	DX8Wrapper::Set_Shadow_Depth_Pass(true);
	// Clear colour so untouched texels read as far (unpack -> depth 1.0 -> lit). R is the
	// most significant channel, so red, not blue: the pack weights the channels 1, 1/255,
	// 1/255^2 coarse-to-fine. Clearing to blue here would unpack to ~0 -- the near plane --
	// and every receiver outside the rasterised area would come out fully shadowed.
	DX8Wrapper::Clear(true, true, Vector3(1.0f, 0.0f, 0.0f), 1.0f, 1.0f);
}

void W3DShaderManager::endShadowMapRendering()
{
	DX8Wrapper::Set_Shadow_Depth_Pass(false);
	if (m_shadowSavedRT != nullptr)
	{
		DX8Wrapper::Set_DX8_Render_Target(m_shadowSavedRT, m_shadowSavedDepth);
		SAFE_RELEASE(m_shadowSavedRT);
		SAFE_RELEASE(m_shadowSavedDepth);
	}
	// Put the render states back before the cache is invalidated, so both the device and
	// the cache end up holding what the scene had. Nothing the depth pass forced --
	// least of all the colour/z mask that keeps unroutable draws out of the map -- may
	// outlive it.
	if (LPDIRECT3DDEVICE8 dev = DX8Wrapper::_Get_D3D_Device8())
	{
		for (Int i = 0; i < NUM_SHADOW_SAVED_STATES; ++i)
			dev->SetRenderState((D3DRENDERSTATETYPE)s_shadowSavedStateIds[i], m_shadowSavedStates[i]);
	}

	DX8Wrapper::Invalidate_Cached_Render_States();
}

void W3DShaderManager::initEnvMap()
{
	if (DX8Wrapper::m_envCubeMap != nullptr)
		return;
	IDirect3DDevice8* dev = DX8Wrapper::_Get_D3D_Device8();
	if (dev == nullptr)
		return;

	IDirect3DCubeTexture8* cube = nullptr;
	// Levels = 0 asks for a full mip chain; bakeEnvMapFaces fills level 0 and filters
	// the rest down. See the note there on why the chain is needed at this size.
	if (FAILED(dev->CreateCubeTexture(ENV_MAP_SIZE, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &cube)) || cube == nullptr)
		return;

	// Initial bake from whatever the map's lighting says now; re-baked when it changes.
	float sunDir[3], sunColor[3], ambient[3];
	getSceneEnvLight(sunDir, sunColor, ambient);
	float sky[3], ground[3];
	deriveEnvColors(sunColor, ambient, sky, ground);
	bakeEnvMapFaces(cube, sunDir, sunColor, sky, ground);

	DX8Wrapper::m_envCubeMap = cube;
	memcpy(s_envBakedSunDir, sunDir, sizeof(sunDir));
	memcpy(s_envBakedSunColor, sunColor, sizeof(s_envBakedSunColor));
	memcpy(s_envBakedSky, sky, sizeof(sky));
	memcpy(s_envBakedGround, ground, sizeof(ground));
	s_envBaked = true;
}

//=============================================================================
// Re-bake the shared env cubemap when the scene's dominant light drifts.
// Called once per render frame (from the terrain render). Cheap in the common
// case: it recomputes target colours and returns unless they moved enough to
// matter, so an actual face re-bake only happens on time-of-day changes.
//=============================================================================
void W3DShaderManager::updateEnvMap()
{
	IDirect3DCubeTexture8* cube = (IDirect3DCubeTexture8*)DX8Wrapper::m_envCubeMap;
	if (cube == nullptr)
		return;

	float sunDir[3], sunColor[3], ambient[3];
	getSceneEnvLight(sunDir, sunColor, ambient);
	float sky[3], ground[3];
	deriveEnvColors(sunColor, ambient, sky, ground);

	// Sum of absolute deltas across everything that shapes the bake. A small
	// threshold keeps gradual time-of-day drift from re-baking every frame while
	// still catching real changes within a frame or two.
	float drift = 0.0f;
	for (int i = 0; i < 3; ++i) {
		drift += fabsf(sunDir[i]   - s_envBakedSunDir[i]);
		drift += fabsf(sunColor[i] - s_envBakedSunColor[i]);
		drift += fabsf(sky[i]      - s_envBakedSky[i]);
		drift += fabsf(ground[i]   - s_envBakedGround[i]);
	}
	if (s_envBaked && drift < 0.03f)
		return;

	bakeEnvMapFaces(cube, sunDir, sunColor, sky, ground);
	memcpy(s_envBakedSunDir, sunDir, sizeof(sunDir));
	memcpy(s_envBakedSunColor, sunColor, sizeof(s_envBakedSunColor));
	memcpy(s_envBakedSky, sky, sizeof(sky));
	memcpy(s_envBakedGround, ground, sizeof(ground));
	s_envBaked = true;
}

//=============================================================================
/** World-space drift of the two programmable-path cloud layers. World units, not UV,
	so the shader can divide by whatever period each layer is projected at. */
//=============================================================================
void W3DShaderManager::getCloudScroll(float& ax, float& ay, float& bx, float& by)
{
	ax = terrainShader2Stage.m_cloudWorldAX;
	ay = terrainShader2Stage.m_cloudWorldAY;
	bx = terrainShader2Stage.m_cloudWorldBX;
	by = terrainShader2Stage.m_cloudWorldBY;
}

// W3DShaderManager::shutdown =======================================================
/** Any shaders which allocate resources will be allowed to free them */
//=============================================================================
void W3DShaderManager::shutdown()
{
	shutdownUnitShaders();
	SAFE_RELEASE(m_resolveSurface);
	SAFE_RELEASE(m_newRenderSurface);
	SAFE_RELEASE(m_renderTexture);
	SAFE_RELEASE(m_oldRenderSurface);
	SAFE_RELEASE(m_oldDepthSurface);
	m_currentShader = ST_INVALID;
	m_currentFilter = FT_NULL_FILTER;
	//release any assets associated with a shader (vertex/pixel shaders, textures, etc.)
	Int i=0;
	for (; i<W3DShaderManager::ST_MAX; i++) {
		if (W3DShaders[i]) {
			W3DShaders[i]->shutdown();
		}
	}

	for (i=0; i < FT_MAX; i++)
	{
		if (W3DFilters[i])
		{
			W3DFilters[i]->shutdown();
		}
	}
}

//=============================================================================
void W3DShaderManager::updateCloud()
{
	terrainShader2Stage.updateCloud();
}

// W3DShaderManager::getShaderPasses =======================================================
/** Return number of renderig passes required in perform the desired shader on current
	hardware.  App will need to re-render the polygons this many times to complete the
	effect.
 */
//=============================================================================
Int W3DShaderManager::getShaderPasses(ShaderTypes shader)
{
	return W3DShadersPassCount[shader];
}

// W3DShaderManager::setShader =======================================================
/** Must call this method before each rendering pass in order to perform proper D3D
	setup for each shader.
 */
//=============================================================================
Int W3DShaderManager::setShader(ShaderTypes shader, Int pass)
{
	if (shader == m_currentShader && pass == m_currentShaderPass)
		return TRUE;	//shader is already set
	m_currentShader=shader;
	m_currentShaderPass = pass;
	if (W3DShaders[shader])
		return W3DShaders[shader]->set(pass);
	return FALSE;
}

// W3DShaderManager::resetShader =======================================================
/** Must call this method after all polygons and rendering passes have been submitted.
	This method allows D3D to reset itself to a default state that doesn't conflict
	with the WW3D2 Shader system.
 */
//=============================================================================
void W3DShaderManager::resetShader(ShaderTypes shader)
{
	if (m_currentShader == ST_INVALID)
		return;	//last shader is already reset.
	if (W3DShaders[shader])
		W3DShaders[shader]->reset();
	m_currentShader = ST_INVALID;
}
// W3DShaderManager::filterPreRender =======================================================
/** Call to view filter shaders before rendering starts.
 */
//=============================================================================
Bool W3DShaderManager::filterPreRender(FilterTypes filter, Bool &skipRender, CustomScenePassModes &scenePassMode)
{
	if (W3DFilters[filter])
	{	Bool result=W3DFilters[filter]->preRender(skipRender,scenePassMode);
		if (result)
			m_currentFilter = filter;
		return result;
	}
	return FALSE;
}

// W3DShaderManager::filterPostRender =======================================================
/** Call to view filter shaders after rendering is complete.
 */
//=============================================================================
Bool W3DShaderManager::filterPostRender(FilterTypes filter, FilterModes mode, Coord2D &scrollDelta, Bool &doExtraRender)
{
	if (W3DFilters[filter])
		return W3DFilters[filter]->postRender(mode, scrollDelta,doExtraRender);

	m_currentFilter = FT_NULL_FILTER;
	return FALSE;
}

// W3DShaderManager::filterPostRender =======================================================
/** Call to view filter shaders after rendering is complete.
 */
//=============================================================================
	static Bool filterSetup(FilterTypes filter, FilterModes mode);
Bool W3DShaderManager::filterSetup(FilterTypes filter, FilterModes mode)
{
	if (W3DFilters[filter])
		return W3DFilters[filter]->setup(mode);
	return FALSE;
}

/*Draws 2 triangles covering the viewport given the current render states*/
void W3DShaderManager::drawViewport(Int color)
{
	LPDIRECT3DDEVICE8 pDev=DX8Wrapper::_Get_D3D_Device8();

	struct _TRANS_LIT_TEX_VERTEX {
		D3DXVECTOR4 p;
		DWORD color;   // diffuse color
		float	u;
		float	v;
	} v[4];

	Int xpos, ypos, width, height;

	TheTacticalView->getOrigin(&xpos,&ypos);
	width=TheTacticalView->getWidth();
	height=TheTacticalView->getHeight();

	//bottom right
	v[0].p = D3DXVECTOR4( xpos+width-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[0].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[0].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top right
	v[1].p = D3DXVECTOR4( xpos+width-0.5f, ypos-0.5f, 0.0f, 1.0f );
	v[1].u = (Real)(xpos+width)/(Real)TheDisplay->getWidth();	v[1].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	//bottom left
	v[2].p = D3DXVECTOR4(  xpos-0.5f, ypos+height-0.5f, 0.0f, 1.0f );
	v[2].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[2].v = (Real)(ypos+height)/(Real)TheDisplay->getHeight();
	//top left
	v[3].p = D3DXVECTOR4(  xpos-0.5f,  ypos-0.5f, 0.0f, 1.0f );
	v[3].u = (Real)(xpos)/(Real)TheDisplay->getWidth();	v[3].v = (Real)(ypos)/(Real)TheDisplay->getHeight();
	v[0].color = color;
	v[1].color = color;
	v[2].color = color;
	v[3].color = color;

	//draw polygons like this is very inefficient but for only 2 triangles, it's
	//not worth bothering with index/vertex buffers.
	DX8Wrapper::Set_Vertex_Shader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

	pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(_TRANS_LIT_TEX_VERTEX));
}

// W3DShaderManager::startRenderToTexture =======================================================
/** Starts rendering to a texture.
 */
//=============================================================================
void W3DShaderManager::startRenderToTexture()
{
	DEBUG_ASSERTCRASH(!m_renderingToTexture, ("Already rendering to texture - cannot nest calls."));

	if (m_renderingToTexture || m_newRenderSurface==nullptr || m_oldDepthSurface==nullptr) return;
	HRESULT hr = DX8Wrapper::Set_DX8_Render_Target(m_newRenderSurface, m_oldDepthSurface);

	// TheSuperHackers @bugfix If SetRenderTarget fails (e.g. due to MSAA forced by driver
	// profile causing a depth buffer mismatch that D3DSURFACE_DESC doesn't report), permanently
	// disable RTT to prevent repeated failures and accidental backbuffer clears.
	if (hr != S_OK)
	{
		// Permanently disable RTT
		SAFE_RELEASE(m_resolveSurface);
		SAFE_RELEASE(m_newRenderSurface);
		SAFE_RELEASE(m_renderTexture);
		SAFE_RELEASE(m_oldRenderSurface);
		SAFE_RELEASE(m_oldDepthSurface);
		return;
	}

	m_renderingToTexture = true;
	if (TheGlobalData->m_showSoftWaterEdge)
	{	//Soft water edges use frame buffer destination alpha so we must clear it to a known value.
		if (m_currentFilter == FT_VIEW_MOTION_BLUR_FILTER || m_currentFilter == FT_VIEW_CROSSFADE)
		{	//these filters rely on the previous frame being visible so we must be careful about clearing
			//frame buffer.  Only clear the alpha channel
			DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_ALPHA);	//only clear alpha
			ShaderClass shader=ShaderClass::_PresetOpaqueSolidShader;
			shader.Set_Depth_Compare(ShaderClass::PASS_ALWAYS);
			shader.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);
			DX8Wrapper::Set_Shader(shader);

			VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
			DX8Wrapper::Set_Material(vmat);
			REF_PTR_RELEASE(vmat);	//no need to keep a reference since it's a preset.

			drawViewport(0x00ffffff | (((Int)(TheWaterTransparency->m_minWaterOpacity*255.0f)) <<24));
			DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_RED|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_BLUE);	//disable writes to alpha
		}
		else	//normal clear that overwrites everything.
			DX8Wrapper::Clear(true, false, Vector3( 0.0f, 0.0f, 0.0f ), TheWaterTransparency->m_minWaterOpacity);
	}
}

// W3DShaderManager::startRenderToTexture =======================================================
/** Ends rendering to a texture.
 */
//=============================================================================
IDirect3DTexture8 *W3DShaderManager::endRenderToTexture()
{
	DEBUG_ASSERTCRASH(m_renderingToTexture, ("Not rendering to texture."));
	if (!m_renderingToTexture) return nullptr;
	HRESULT hr = DX8Wrapper::Set_DX8_Render_Target(m_oldRenderSurface, m_oldDepthSurface);	//restore original render target
	DEBUG_ASSERTCRASH(hr==S_OK, ("Set target failed unexpectedly."));
	if (hr == S_OK)
	{
		// When MSAA is active the scene was drawn into a multisampled colour surface;
		// resolve it down into the plain texture (StretchRect from a multisampled to a
		// non-multisampled surface of the same size performs the resolve) so the
		// post-process can sample it. Done after the back buffer is restored so the
		// multisampled surface is no longer the active render target.
		if (m_resolveSurface != nullptr)
			DX8Wrapper::_Get_D3D_Device8()->StretchRect(m_newRenderSurface, nullptr, m_resolveSurface, nullptr, D3DTEXF_NONE);

		//assume render target texture will be in stage 0.  Most hardware has "conditional" support for
		//non-power-of-2 textures so we must force some required states:
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSW, D3DTADDRESS_CLAMP);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_MIPFILTER, D3DTEXF_NONE);

		m_renderingToTexture = false;

		// Keep a copy for next frame's screen-space reflections. Every filter path ends
		// up here, so this is the one place that sees the finished, resolved scene --
		// and the copy has to exist because m_renderTexture becomes the render target
		// again next frame, and nothing may sample a texture it is drawing into.
		captureSceneHistory();
	}
	return m_renderTexture;
}

/**Returns texture containing the image that was last rendered using any of the effects requiring render target
textures.  Used mostly for cross-fading effects that need an unmodified version of the view before the effect
was applied.  NOTE: This texture does not survive device reset.. so quit effect on reset!*/
IDirect3DTexture8 *W3DShaderManager::getRenderTexture()
{
	return m_renderTexture;
}

/** True once the bloom filter has successfully initialised (shaders loaded and its
render targets created). The view uses this to select FT_VIEW_BLOOM as its default
filter; when bloom is unavailable (no render-to-texture, e.g. forced MSAA) it is
false and the plain default filter is used instead. */
Bool W3DShaderManager::isBloomFilterActive()
{
	return W3DFilters[FT_VIEW_BLOOM] != nullptr;
}

enum GraphicsVenderID CPP_11(: Int)
{
	DC_NVIDIA_VENDOR_ID	= 0x10DE,
	DC_3DFX_VENDOR_ID	= 0x121A,
	DC_ATI_VENDOR_ID	= 0x1002
};

// W3DShaderManager::ChipsetType =======================================================
/** Returns the chipset used by the currently active rendering device.  Can be useful
	for coding around specific driver bugs.
 */
//=============================================================================
ChipsetType W3DShaderManager::getChipset()
{
	//check if globaldata has an override for current chipset
	if (TheGlobalData && TheGlobalData->m_chipSetType != DC_UNKNOWN)
		return (ChipsetType)TheGlobalData->m_chipSetType;

	ChipsetType chip=DC_UNKNOWN;
	IDirect3D8* d3d8Interface=DX8Wrapper::_Get_D3D8();

	if (d3d8Interface && DX8Wrapper::_Get_D3D_Device8())
	{

		D3DADAPTER_IDENTIFIER8 did;
		::ZeroMemory(&did, sizeof(D3DADAPTER_IDENTIFIER8));
	/*	HRESULT res = */ d3d8Interface->GetAdapterIdentifier(0,D3DENUM_NO_WHQL_LEVEL,&did);
		*((LARGE_INTEGER*)&m_driverVersion) = did.DriverVersion;

		if(did.VendorId == DC_NVIDIA_VENDOR_ID)
		{
			m_currentVendor = DC_NVIDIA_VENDOR_ID;

			if (did.DeviceId == 0x20)
				return DC_TNT;

			if (did.DeviceId >= 0x28 && did.DeviceId < 0x100)
				return DC_TNT2;

			if ( (did.DeviceId >= 0x100 && did.DeviceId <= 0x103) ||	//GeForce
				 (did.DeviceId >= 0x110 && did.DeviceId <= 0x113) ||	//GeForce2 MX
						 (did.DeviceId >= 0x150 && did.DeviceId <= 0x153) )	//GeForce2
           		return DC_GEFORCE2;

			if (did.DeviceId >= 0x200 && did.DeviceId < 0x250)
				return DC_GEFORCE3;

			if (did.DeviceId >= 0x250)
				return DC_GEFORCE4;
		}
		else
		if(did.VendorId == DC_3DFX_VENDOR_ID)
		{
			m_currentVendor = DC_3DFX_VENDOR_ID;

			if (did.DeviceId == 0x0002)
				return DC_VOODOO2;
			if (did.DeviceId == 0x0005)
				return DC_VOODOO3;
			if (did.DeviceId == 0x0008)	///@todo: Just guessing on this one - find actual Voodoo4 deviceID.
				return DC_VOODOO4;
			if (did.DeviceId == 0x0009)
				return DC_VOODOO5;
		}
		else
		if(did.VendorId == DC_ATI_VENDOR_ID)
		{
			m_currentVendor = DC_ATI_VENDOR_ID;

			if (did.DeviceId == 0x5144)
				return DC_RADEON;
			if (did.DeviceId == 0x514C)
				return DC_RADEON_8500;
			if (did.DeviceId == 0x4e44)
				return DC_RADEON_9700;
		}

		//None of the vendor specific ID's matched so use generic means to classify the card
		Int maxTextures=DX8Wrapper::Get_Current_Caps()->Get_Max_Simultaneous_Textures();
		Real pixelShaderVersion;

		char buf[256];

		//Convert version to Real
		sprintf(buf,"%d.%d",DX8Wrapper::Get_Current_Caps()->Get_Pixel_Shader_Major_Version(),DX8Wrapper::Get_Current_Caps()->Get_Pixel_Shader_Minor_Version());
		sscanf(buf,"%f",&pixelShaderVersion);

		if (maxTextures >= 4)
		{	if (pixelShaderVersion >= 1.1f)
				chip=DC_GENERIC_PIXEL_SHADER_1_1;
			if (pixelShaderVersion >= 1.4f)
				chip=DC_GENERIC_PIXEL_SHADER_1_4;
			if (maxTextures >= 8 && pixelShaderVersion >= 2.0f)
				chip=DC_GENERIC_PIXEL_SHADER_2_0;
		}
	}

	return chip;
}

//=============================================================================
// WaterRenderObjClass::LoadAndCreateShader
//=============================================================================
/** Loads and creates a D3D pixel or vertex shader.*/
//=============================================================================
HRESULT W3DShaderManager::LoadAndCreateD3DShader(const char* strFilePath, const DWORD* pDeclaration, DWORD Usage, Bool ShaderType, DWORD* pHandle)
{
	if (getChipset() < DC_GENERIC_PIXEL_SHADER_1_1)
		return E_FAIL;	//don't allow loading any shaders if hardware can't handle it.

	try
	{
		File *file = nullptr;
		HRESULT hr;

		file = TheFileSystem->openFile(strFilePath, File::READ | File::BINARY);
		if (file == nullptr)
		{
			OutputDebugString("Could not find file \n" );
			return E_FAIL;
		}

		FileInfo fileInfo;
		TheFileSystem->getFileInfo(AsciiString(strFilePath), &fileInfo);
		DWORD dwFileSize = fileInfo.sizeLow;

		const DWORD* pShader = (DWORD*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwFileSize);
		if (!pShader)
		{
			OutputDebugString( "Failed to allocate memory to load shader\n " );
			return E_FAIL;
		}

		file->read((void *)pShader, dwFileSize);

		file->close();
		file = nullptr;

		if (ShaderType) // SHADERTYPE_VERTEX
		{
			hr = DX8Wrapper::_Get_D3D_Device8()->CreateVertexShader(pDeclaration, pShader, pHandle, Usage);
		}
		else // SHADERTYPE_PIXEL
		{
			hr = DX8Wrapper::_Get_D3D_Device8()->CreatePixelShader(pShader, pHandle);
		}

		HeapFree(GetProcessHeap(), 0, (void*)pShader);

		if (FAILED(hr))
		{
			OutputDebugString( "Failed to create shader\n ");
			return E_FAIL;
		}
	}
	catch(...)
	{
		OutputDebugString( "Error opening file \n" );
		return E_FAIL;
	}

	return S_OK;
}

//For the MP test, we're enforcing high min-spec requirements that need to be verified.
#define MIN_INTEL_CPU_FREQ	1300
#define MIN_AMD_CPU_FREQ	1100
#define MIN_ACCEPTED_FREQUENCY	1300
#define MIN_ACCEPTED_MEMORY	(1024*1024*256)	//256 MB
#define MIN_ACCEPTED_TEXTURE_MEMORY	(1024*1024*30)	//30 MB

/**Hack to give gameengine access to this function*/
Bool testMinimumRequirements(ChipsetType *videoChipType, CpuType *cpuType, Int *cpuFreq, MemValueType *numRAM, Real *intBenchIndex, Real *floatBenchIndex, Real *memBenchIndex)
{
	return W3DShaderManager::testMinimumRequirements(videoChipType,cpuType,cpuFreq,numRAM,intBenchIndex,floatBenchIndex,memBenchIndex);
}

Bool W3DShaderManager::testMinimumRequirements(ChipsetType *videoChipType, CpuType *cpuType, Int *cpuFreq, MemValueType *numRAM, Real *intBenchIndex, Real *floatBenchIndex, Real *memBenchIndex)
{
	if (videoChipType)
		*videoChipType = getChipset();

	if (cpuType)
	{
		*cpuType = XX;	//unknown

		//Check if it's an Athlon
		if (CPUDetectClass::Get_Processor_Manufacturer() == CPUDetectClass::MANUFACTURER_AMD &&
				CPUDetectClass::Get_AMD_Processor() >= CPUDetectClass::AMD_PROCESSOR_ATHLON_025)
				*cpuType = K7;

		//Check if it's a P3
		if (CPUDetectClass::Get_Processor_Manufacturer() == CPUDetectClass::MANUFACTURER_INTEL &&
				CPUDetectClass::Get_Intel_Processor() >= CPUDetectClass::INTEL_PROCESSOR_PENTIUM_III_MODEL_7)
				*cpuType = P3;
		//Check if it's a P4
		if (CPUDetectClass::Get_Processor_Manufacturer() == CPUDetectClass::MANUFACTURER_INTEL &&
				CPUDetectClass::Get_Intel_Processor() >= CPUDetectClass::INTEL_PROCESSOR_PENTIUM4)
				*cpuType = P4;
	}

	if (cpuFreq)
		*cpuFreq=CPUDetectClass::Get_Processor_Speed();

	if (numRAM)
		*numRAM=CPUDetectClass::Get_Total_Physical_Memory();

	if (intBenchIndex && floatBenchIndex && memBenchIndex)
	{
		// TheSuperHackers @tweak Aliendroid1 19/06/2025 Legacy benchmarking code was removed.
		// Since modern hardware always meets the minimum requirements, we preset the benchmark "results" to a high value.
		*intBenchIndex = 10.0f;
		*floatBenchIndex = 10.0f;
		*memBenchIndex = 10.0f;
	}

	return TRUE;
}

/**Try to guess how well the video card will handle the game assuming very fast CPU*/
StaticGameLODLevel W3DShaderManager::getGPUPerformanceIndex()
{
	ChipsetType	chipType;
	StaticGameLODLevel detailSetting=STATIC_GAME_LOD_LOW;	//assume lowest settings for now.

	if ((chipType=getChipset()) != DC_UNKNOWN)
	{	//a known video card so we can make some assumptions
		if (chipType >=	DC_GEFORCE2)
			detailSetting=STATIC_GAME_LOD_LOW;	//these cards need multiple terrain passes.
		if (chipType >= DC_GENERIC_PIXEL_SHADER_1_1)	//these cards can do terrain in single pass.
			detailSetting=STATIC_GAME_LOD_VERY_HIGH;
	}

	return detailSetting;
}

/**We need a hardware independent method to compare different CPU's.  For lack of anything better, we'll
use time to calculate PIE using a slow random number algorithm.*/

/**Used to test function call overhead*/
void add(float *sum,float *addend)
{
	*sum = *sum + *addend;
}

/**Returns seconds needed to run the test*/
Real W3DShaderManager::GetCPUBenchTime()
{
	float ztot, yran, ymult, ymod, x, y, z, pi, prod;
    long int low, ixran, itot, j, iprod;

  	__int64 endTime64,freq64,startTime64;
	QueryPerformanceFrequency((LARGE_INTEGER *)&freq64);
	QueryPerformanceCounter((LARGE_INTEGER *)&startTime64);

    ztot = 0.0;
    low = 1;
    ixran = 1907;
    yran = 5813.0;
    ymult = 1307.0;
    ymod = 5471.0;
    itot = 560000;	//total iterations. This value ends up running at ~30 fps on our P4-2.2Ghz.

    for(j=1; j<=itot; j++)
    {
		iprod = 27611 * ixran;
		ixran = iprod - 74383*(long int)(iprod/74383);
		x = (float)ixran / 74383.0;
		prod = ymult * yran;
		yran = (prod - ymod*(long int)(prod/ymod));
		y = yran / ymod;
		z = x*x + y*y;
		add(&ztot,&z);
		if ( z <= 1.0 )
		{
		  low = low + 1;
		}
	}
	pi = 4.0 * (float)low/(float)itot;

	QueryPerformanceCounter((LARGE_INTEGER *)&endTime64);
	return ((double)(endTime64-startTime64)/(double)(freq64));
}


// W3DShaderManager::setShroudTex =======================================================
/** Puts the shroud texture into a texture stage.
 */
//=============================================================================
Int W3DShaderManager::setShroudTex(Int stage)
{
	//We need to scale so shroud texel stretches over one full terrain cell.  Each texel
	//is 1/128 the size of full texture. (assuming 128x128 vid-mem texture).
	W3DShroud *shroud;
	if ((shroud=TheTerrainRenderObject->getShroud()) != nullptr)
	{
		DX8Wrapper::Set_Texture(stage, shroud->getShroudTexture());

		DX8Wrapper::Set_DX8_Texture_Stage_State(stage,  D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
		DX8Wrapper::Set_DX8_Texture_Stage_State(stage,  D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_COLORARG1, D3DTA_TEXTURE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_COLORARG2, D3DTA_CURRENT );
		DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_ALPHAARG2, D3DTA_CURRENT );
		DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_COLOROP,   D3DTOP_MODULATE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( stage, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG2 );

		D3DXMATRIX curView;
		DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, curView);

		D3DXMATRIX inv;
		float det;
		D3DXMatrixInverse(&inv, &det, &curView);

		D3DXMATRIX scale,offset;

		//We need to make all world coordinates be relative to the heightmap data origin since that
		//is where the shroud begins.

		float xoffset = 0;
		float yoffset = 0;
		Real width=shroud->getCellWidth();
		Real height=shroud->getCellHeight();

		if (TheTerrainRenderObject->getMap())
		{	//subtract origin position from all coordinates.  Origin is shifted by 1 cell width/height to allow for unused border texels.
			xoffset = -(float)shroud->getDrawOriginX() + width;
			yoffset = -(float)shroud->getDrawOriginY() + height;
		}

		D3DXMatrixTranslation(&offset, xoffset, yoffset,0);

		width = 1.0f/(width*shroud->getTextureWidth());
		height = 1.0f/(height*shroud->getTextureHeight());
		D3DXMatrixScaling(&scale, width, height, 1);
		curView = (inv * offset) * scale;
		DX8Wrapper::_Set_DX8_Transform((D3DTRANSFORMSTATETYPE )(D3DTS_TEXTURE0+stage), curView);
		return TRUE;
	}
	return FALSE;
}





















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

// FILE: W3DShaderManager.h /////////////////////////////////////////////////////////
//
// Custom shader system that allows more options and easier device validation than
// possible with W3D2.
//
// Author: Mark Wilczynski, August 2001
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "WW3D2/texture.h"
#include "WWMath/sphere.h"
enum FilterTypes CPP_11(: Int);
enum FilterModes CPP_11(: Int);
enum CustomScenePassModes CPP_11(: Int);
enum StaticGameLODLevel CPP_11(: Int);
enum ChipsetType CPP_11(: Int);
enum CpuType CPP_11(: Int);
enum GraphicsVenderID CPP_11(: Int);

class TextureClass;	///forward reference
/** System for managing complex rendering settings which are either not handled by
	WW3D2 or need custom paths depending on the video card.  This system will determine
	the proper shader given video card limitations and also allow the app to query the
	hardware for specific features.
*/
class W3DShaderManager
{
public:

	//put any custom shaders (not going through W3D) in here.
	enum ShaderTypes
	{	ST_INVALID,			//invalid shader type.
		ST_TERRAIN_BASE,	//shader to apply base terrain texture only
		ST_TERRAIN_BASE_NOISE1,	//shader to apply base texture and cloud/noise 1.
		ST_TERRAIN_BASE_NOISE2,	//shader to apply base texture and cloud/noise 2.
		ST_TERRAIN_BASE_NOISE12,//shader to apply base texture and both cloud/noise
		ST_SHROUD_TEXTURE,		//shader to apply shroud texture projection.
		ST_MASK_TEXTURE,		//shader to apply alpha mask texture projection.
		ST_ROAD_BASE,	//shader to apply base terrain texture only
		ST_ROAD_BASE_NOISE1,	//shader to apply base texture and cloud/noise 1.
		ST_ROAD_BASE_NOISE2,	//shader to apply base texture and cloud/noise 2.
		ST_ROAD_BASE_NOISE12,//shader to apply base texture and both cloud/noise
		ST_CLOUD_TEXTURE,			//shader to project clouds.
		ST_MAX
	};


	W3DShaderManager();	///<constructor
	static void init();	///<determine optimal shaders for current device.
	static void shutdown();	///<release resources used by shaders
	static void updateCloud();	///<update the cloud position once every render frame.

	// Programmable (D3D9) unit render path.
	static void initUnitShaders();	///<create the vertex declaration and load the unit vertex/pixel shaders.
	///Set stage to linear filtering with clamped addressing -- what every screen-space
	///quad wants, and what none of them should be re-deriving for itself.
	static void setLinearClampSampler(DWORD stage);
	///Draw a screen-space quad over [dx,dy]..[dx+dw,dy+dh], sampling source UVs on
	///TEXCOORD0 and a second set on TEXCOORD1. The caller binds its own pixel shader.
	static HRESULT drawScreenQuad(LPDIRECT3DDEVICE8 dev,
		float dx, float dy, float dw, float dh,
		float sU0, float sV0, float sU1, float sV1,
		float bU0, float bV0, float bU1, float bV1);
	///Keep a copy of the bloom bright-pass result for DEBUG_VIS_BLOOM to draw.
	///
	///Called by the bloom filter immediately after its bright pass, and does nothing
	///unless that mode is active. It has to be a copy: the two blur passes ping-pong
	///through the same pair of targets straight afterwards, so by the end of the frame
	///neither holds the unblurred result -- and a blurred glow is spread over its
	///neighbours by construction, which is the one thing that mode must not show.
	static void captureBloomBrightPass(IDirect3DSurface8 *brightSurface, Int width, Int height);
	///Draw whatever the current debug visualization mode puts on top of the frame.
	///Called once per frame after the scene and its post-process, before the game UI.
	///Does nothing unless a mode that draws an overlay is active.
	static void drawDebugVisOverlay(Int screenWidth, Int screenHeight);
	///Load the debug-visualization shaders. Failure is not fatal: each mode checks its
	///own shader and does nothing if it is missing.
	static void initDebugVis();
	static void shutdownDebugVis();
	static DWORD m_debugBloomPS;					///<debugbloom_ps: false-colours the bright-pass copy
	static DWORD m_debugShroudPS;					///<debugshroud_ps: draws the shroud field as a tile
	static IDirect3DTexture8 *m_debugBrightTexture;	///<copy of the bloom bright pass, made only while DEBUG_VIS_BLOOM is on
	static IDirect3DSurface8 *m_debugBrightSurface;	///<its surface, the StretchRect destination
	static void shutdownUnitShaders();	///<release the unit shaders and vertex declaration.
	static void getCloudOffset(float& x, float& y); ///<current scrolling cloud-overlay offset.
	static TextureBaseClass* resolveOrmTexture(TextureBaseClass* baseTexture); ///<PBR ORM map for a base texture, or null (cached).
	static void clearOrmCache();	///<release cached ORM lookups.
	static void initDefaultOrmMap();	///<build the neutral 1x1 ORM map used by meshes that ship none.
	static void initEnvMap();	///<build the shared environment cubemap for PBR reflections.
	static void updateEnvMap();	///<re-bake the env cubemap when scene lighting drifts (once/frame).
	static void initShadowMap();	///<create the directional shadow-map render target + depth-pass shaders.
	static void shutdownShadowMap();	///<release the shadow-map resources.
	static void startShadowMapRendering();	///<redirect rendering into the shadow map (sun-view depth pass).
	static void endShadowMapRendering();	///<restore the back buffer after the shadow depth pass.
	static Bool isShadowMappingActive();	///<true when the shadow map is enabled and usable; the legacy volume/decal shadows stand down.
	// The orthographic box the shadow map is currently fitted to, in world space.
	// Published by the view when it builds SunVP so that the depth pass can cull casters
	// against the light instead of against the camera -- they are different volumes, and
	// culling by the camera is what makes a caster's shadow vanish the moment the caster
	// itself leaves the screen.
	// The box is not square -- see the note where it is built in W3DView -- so the up
	// axis is given as an explicit range rather than a half-extent, and that range is
	// asymmetric because height only ever displaces a caster toward the sun.
	static void setShadowFrustum(const Vector3 &eye, const Vector3 &lookDir,
								 Real halfWidth, Real upMin, Real upMax,
								 Real nearDist, Real farDist);
	// Stored by DX8Wrapper, not here: MeshClass::Render culls by the camera too, and it
	// sits below this layer, so the box has to live where both can reach it.
	static Bool hasShadowFrustum();
	///<true when the sphere lies wholly outside the sun frustum, i.e. cannot cast into the map.
	static Bool cullSphereFromShadowFrustum(const Vector3 &center, Real radius);
	static Bool cullSphereFromShadowFrustum(const SphereClass &sphere)
	{
		return cullSphereFromShadowFrustum(sphere.Center, sphere.Radius);
	}
	enum { NUM_SHADOW_SAVED_STATES = 10 };	///<render states saved across the shadow depth pass

	static ChipsetType getChipset();	///<return current device chipset.
	static GraphicsVenderID getCurrentVendor() {return m_currentVendor;}	///<return current card vendor.
	static __int64 getCurrentDriverVersion() {return m_driverVersion; }	///<return current driver version.
	static Int getShaderPasses(ShaderTypes shader);	///<rendering passes required for shader
	static Int setShader(ShaderTypes shader, Int pass);	///<enable specific shader pass.
	static Int setShroudTex(Int stage);	///<Set shroud in a texture stage.
	static void resetShader(ShaderTypes shader);	///<make sure W3D2 gets restored to normal
	///Specify all textures (up to 8) which can be accessed by the shaders.
	static void setTexture(Int stage,TextureClass* texture) {m_Textures[stage]=texture;}
	///Return current texture available to shaders.
	static TextureClass *getShaderTexture(Int stage) { return m_Textures[stage];}	///<returns currently selected texture for given stage
	///Return last activated shader.
	static ShaderTypes getCurrentShader() {return m_currentShader;}
	/// Loads a .vso file and creates a vertex shader for it
	static HRESULT LoadAndCreateD3DShader(const char* strFilePath, const DWORD* pDeclaration, DWORD Usage, Bool ShaderType, DWORD* pHandle);

	static Bool testMinimumRequirements(ChipsetType *videoChipType, CpuType *cpuType, Int *cpuFreq, MemValueType *numRAM, Real *intBenchIndex, Real *floatBenchIndex, Real *memBenchIndex);
	static StaticGameLODLevel getGPUPerformanceIndex();
	static Real GetCPUBenchTime();

	// Filter methods
	static Bool filterPreRender(FilterTypes filter, Bool &skipRender, CustomScenePassModes &scenePassMode); ///< Set up at start of render.  Only applies to screen filter shaders.
	static Bool filterPostRender(FilterTypes filter, FilterModes mode, Coord2D &scrollDelta, Bool &doExtraRender); ///< Called after render.  Only applies to screen filter shaders.
	static Bool filterSetup(FilterTypes filter, FilterModes mode);

	// Support routines for filter methods.
	static Bool canRenderToTexture() { return (m_oldRenderSurface && m_newRenderSurface);}
	static void startRenderToTexture(); ///< Sets render target to texture.
	static IDirect3DTexture8 * endRenderToTexture(); ///< Ends render to texture, & returns texture.
	static IDirect3DTexture8 * getRenderTexture();	///< returns last used render target texture
	static Bool isBloomFilterActive();	///< true when the bloom filter initialised (render-to-texture available)
	static Bool isRenderingToTexture() {return m_renderingToTexture; }
	static void drawViewport(Int color);	///<draws 2 triangles covering the current tactical viewport


protected:
	static TextureClass *m_Textures[8];	///textures assigned to each of the possible stages
	static ChipsetType m_currentChipset;	///<last video card chipset that was detected.
	static GraphicsVenderID m_currentVendor;	///<last video card vendor
	static __int64 m_driverVersion;			///<driver version of last chipset.
	static ShaderTypes m_currentShader;	///<last shader that was set.
	static Int m_currentShaderPass;		///<pass of last shader that was set.

	static FilterTypes m_currentFilter; ///< Last filter that was set.
	// Info for a render to texture surface for special effects.
	static Bool m_renderingToTexture;
	static IDirect3DSurface8 *m_oldRenderSurface;	///<previous render target
	static IDirect3DTexture8 *m_renderTexture;		///<plain (non-MSAA) texture the redirected scene ends up in (post-process reads this)
	static IDirect3DSurface8 *m_newRenderSurface;	///<render target the scene is drawn into: m_renderTexture's surface, or an MSAA surface when MSAA is on
	static IDirect3DSurface8 *m_resolveSurface;		///<when MSAA: m_renderTexture's surface, the StretchRect resolve destination; null otherwise
	static IDirect3DSurface8 *m_oldDepthSurface;	///<previous depth buffer surface
	// Directional shadow map (sun-view depth) render target + its own depth buffer.
	static IDirect3DTexture8 *m_pShadowMapTexture;	///<depth-packed shadow map (A8R8G8B8)
	static IDirect3DSurface8 *m_pShadowMapSurface;	///<colour surface of the shadow map
	static IDirect3DSurface8 *m_pShadowMapDepthSurface;	///<the shadow map's own depth buffer
	static IDirect3DSurface8 *m_shadowSavedRT;		///<render target saved across the shadow depth pass
	static IDirect3DSurface8 *m_shadowSavedDepth;	///<depth surface saved across the shadow depth pass
	static DWORD m_shadowSavedStates[NUM_SHADOW_SAVED_STATES];	///<render states saved across the shadow depth pass


};

class W3DFilterInterface
{
public:
	virtual Int init() = 0;			///<perform any one time initialization and validation
	virtual Int shutdown() { return TRUE;};			///<release resources used by shader
	virtual Bool preRender(Bool &skipRender, CustomScenePassModes &scenePassMode) {skipRender=false; return false;} ///< Set up at start of render.  Only applies to screen filter shaders.
	virtual Bool postRender(FilterModes mode, Coord2D &scrollDelta, Bool &doExtraRender){return false;} ///< Called after render.  Only applies to screen filter shaders.
	virtual Bool setup(FilterModes mode){return false;} ///< Called when the filter is started, one time before the first prerender.
protected:
	virtual Int set(FilterModes mode) = 0;		///<setup shader for the specified rendering pass.
	 ///do any custom resetting necessary to bring W3D in sync.
	virtual void reset() = 0;
};


/*=========  ScreenMotionBlurFilter	=============================================================*/
///applies motion blur to viewport.
class ScreenMotionBlurFilter : public W3DFilterInterface
{
public:
	virtual Int set(FilterModes mode) override;		///<setup shader for the specified rendering pass.
	virtual Int init() override;			///<perform any one time initialization and validation
	virtual void reset() override;		///<do any custom resetting necessary to bring W3D in sync.
	virtual Int shutdown() override;		///<release resources used by shader
	virtual Bool preRender(Bool &skipRender, CustomScenePassModes &scenePassMode) override; ///< Set up at start of render.  Only applies to screen filter shaders.
	virtual Bool postRender(FilterModes mode, Coord2D &scrollDelta, Bool &doExtraRender) override; ///< Called after render.  Only applies to screen filter shaders.
	virtual Bool setup(FilterModes mode) override; ///< Called when the filter is started, one time before the first prerender.
	ScreenMotionBlurFilter();

	static void setZoomToPos(const Coord3D *pos) {m_zoomToPos = *pos; m_zoomToValid = true;}

protected:
	enum {MAX_COUNT = 60,
				MAX_LIMIT = 30,
				COUNT_STEP = 5,
				DEFAULT_PAN_FACTOR = 30};
	Int m_maxCount;
	Int m_lastFrame;
	Bool m_decrement;
	Bool m_skipRender;
	Bool m_additive;
	Bool m_doZoomTo;
	Coord2D m_priorDelta;
	Int m_panFactor;


	static Coord3D m_zoomToPos;
	static Bool m_zoomToValid;
} ;

/*=========  ScreenBWFilter	=============================================================*/
///converts viewport to black & white.
class ScreenBWFilter : public W3DFilterInterface
{
	DWORD	m_dwBWPixelShader;		///<D3D handle to pixel shader which tints texture to black & white.
public:
	virtual Int init() override;			///<perform any one time initialization and validation
	virtual Int shutdown() override;		///<release resources used by shader
	virtual Bool preRender(Bool &skipRender, CustomScenePassModes &scenePassMode) override; ///< Set up at start of render.  Only applies to screen filter shaders.
	virtual Bool postRender(FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender) override; ///< Called after render.  Only applies to screen filter shaders.
	virtual Bool setup(FilterModes mode) override {return true;} ///< Called when the filter is started, one time before the first prerender.
	static void setFadeParameters(Int fadeFrames, Int direction)
	{
		m_curFadeFrame = 0;
		m_fadeFrames = fadeFrames;
		m_fadeDirection = direction;
	}
protected:
	virtual Int set(FilterModes mode) override;		///<setup shader for the specified rendering pass.
	virtual void reset() override;		///<do any custom resetting necessary to bring W3D in sync.
	static Int m_fadeFrames;
	static Int m_fadeDirection;
	static Int m_curFadeFrame;
	static Real m_curFadeValue;
};

class ScreenBWFilterDOT3 : public ScreenBWFilter
{
public:
	virtual Int init() override;			///<perform any one time initialization and validation
	virtual Int shutdown() override;		///<release resources used by shader
	virtual Bool preRender(Bool &skipRender, CustomScenePassModes &scenePassMode) override; ///< Set up at start of render.  Only applies to screen filter shaders.
	virtual Bool postRender(FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender) override; ///< Called after render.  Only applies to screen filter shaders.
	virtual Bool setup(FilterModes mode) override {return true;} ///< Called when the filter is started, one time before the first prerender.
protected:
	virtual Int set(FilterModes mode) override;		///<setup shader for the specified rendering pass.
	virtual void reset() override;		///<do any custom resetting necessary to bring W3D in sync.
};

/*=========  ScreenCrossFadeFilter	=============================================================*/
///Fades between 2 different rendered frames.
class ScreenCrossFadeFilter : public W3DFilterInterface
{
public:
	virtual Int init() override;			///<perform any one time initialization and validation
	virtual Int shutdown() override;		///<release resources used by shader
	virtual Bool preRender(Bool &skipRender, CustomScenePassModes &scenePassMode) override; ///< Set up at start of render.  Only applies to screen filter shaders.
	virtual Bool postRender(FilterModes mode, Coord2D &scrollDelta,Bool &doExtraRender) override; ///< Called after render.  Only applies to screen filter shaders.
	virtual Bool setup(FilterModes mode) override {return true;} ///< Called when the filter is started, one time before the first prerender.
	static void setFadeParameters(Int fadeFrames, Int direction)
	{
		m_curFadeFrame = 0;
		m_fadeFrames = fadeFrames;
		m_fadeDirection = direction;
	}
	static Real getCurrentFadeValue()	{ return m_curFadeValue;}
	static TextureClass *getCurrentMaskTexture() { return m_fadePatternTexture;}
protected:
	virtual Int set(FilterModes mode) override;		///<setup shader for the specified rendering pass.
	virtual void reset() override;		///<do any custom resetting necessary to bring W3D in sync.
	Bool updateFadeLevel();		///<updated current state of fade and return true if not finished.
	static Int m_fadeFrames;
	static Int m_fadeDirection;
	static Int m_curFadeFrame;
	static Real m_curFadeValue;
	static Bool m_skipRender;
	static TextureClass *m_fadePatternTexture;	///<shape/pattern of the fade
};

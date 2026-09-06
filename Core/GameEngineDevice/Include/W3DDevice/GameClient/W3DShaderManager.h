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
	///What the quad's pixel side should be. Most callers bring their own pixel shader;
	///the ones that were relying on the fixed-function texture stages instead do not, and
	///for them the interface pixel shader expresses the same combine.
	enum ScreenQuadPixel CPP_11(: Int)
	{
		SCREEN_QUAD_PIXEL_CALLER = 0,	///<the caller bound a pixel shader; leave it alone
		SCREEN_QUAD_PIXEL_TEXTURE,		///<texture * vertex diffuse, in colour and alpha
		SCREEN_QUAD_PIXEL_TEXTURE_RGB,	///<texture * diffuse in colour, diffuse alpha alone
		SCREEN_QUAD_PIXEL_GREY,			///<the luma of the texture; diffuse alpha alone
		SCREEN_QUAD_PIXEL_DIFFUSE,		///<flat vertex diffuse; the texture is not sampled
	};
	///Draw a screen-space quad over [dx,dy]..[dx+dw,dy+dh], sampling source UVs on
	///TEXCOORD0 and a second set on TEXCOORD1.
	///
	///The one screen-space quad builder. Every subsystem that puts a rectangle over the
	///frame comes through here: the bloom chain, the tone map, the screen filters, the
	///smudge, the scene overlay, the debug visualizations. It owns the vertex layout, the
	///half-pixel offset, the fill mode and the choice of shader, none of which a caller
	///should be re-deriving -- twelve of them were, out of four vertex structs, and nobody
	///could say which had been converted off D3DFVF_XYZRHW and which had not.
	static HRESULT drawScreenQuad(
		float dx, float dy, float dw, float dh,
		float sU0, float sV0, float sU1, float sV1,
		float bU0, float bV0, float bU1, float bV1,
		DWORD diffuse = 0xffffffff,
		ScreenQuadPixel pixel = SCREEN_QUAD_PIXEL_CALLER,
		///The name this draw reports itself under. One builder must not mean one label:
		///nine sites in this file shared "screenFilter" and the census could say that some
		///of the nine had run and nothing about which. The default names the chain this
		///was written for -- the bloom passes, the tone map, the debug visualizations --
		///and every caller outside it passes its own.
		const char * site = "screenQuad",
		///Whether to apply the half-pixel offset. On for anything that samples a texture,
		///which is what the offset is for; off for a quad whose edges are geometry rather
		///than a sampling grid, because there it moves coverage instead of aligning it.
		///Measured: leaving it on for the player-colour overlay moved 2 pixels.
		bool alignToTexels = true);
	///Keep a copy of the bloom bright-pass result for DEBUG_VIS_BLOOM to draw.
	///
	///Called by the bloom filter immediately after its bright pass, and does nothing
	///unless that mode is active. It has to be a copy: the two blur passes ping-pong
	///through the same pair of targets straight afterwards, so by the end of the frame
	///neither holds the unblurred result -- and a blurred glow is spread over its
	///neighbours by construction, which is the one thing that mode must not show.
	static void captureBloomBrightPass(GfxSurface *brightSurface, Int width, Int height);
	///Draw whatever the current debug visualization mode puts on top of the frame.
	///Called once per frame after the scene and its post-process, before the game UI.
	///Does nothing unless a mode that draws an overlay is active.
	static void drawDebugVisOverlay(Int screenWidth, Int screenHeight);
	///Load the debug-visualization shaders. Failure is not fatal: each mode checks its
	///own shader and does nothing if it is missing.
	static void initDebugVis();
	static void shutdownDebugVis();
	static DWORD m_debugDepthPS;					///<debugdepth_ps: linearises the camera depth prepass
	static DWORD m_debugShadowPS;					///<debugshadow_ps: unpacks the shadow map for the inspector tile
	static DWORD m_debugBloomPS;					///<debugbloom_ps: false-colours the bright-pass copy
	static DWORD m_debugShroudPS;					///<debugshroud_ps: draws the shroud field as a tile
	static GfxTexture *m_debugBrightTexture;	///<copy of the bloom bright pass, made only while DEBUG_VIS_BLOOM is on
	static GfxSurface *m_debugBrightSurface;	///<its surface, the StretchRect destination
	static void shutdownUnitShaders();	///<release the unit shaders and vertex declaration.
	static void getCloudScroll(float& ax, float& ay, float& bx, float& by); ///<world-space drift of the two cloud layers.
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
#ifdef RTS_DEBUG
	///<save the shadow map to PNG mid-pass, so one draw's contribution to it can be isolated.
	static void debugDumpShadowMap(const char *tag);
#endif
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
	static void initSsr();	///<create the camera-depth target and the scene-colour history texture.
	static void shutdownSsr();	///<release the screen-space reflection resources.
	static void initRefraction();	///<create the mid-frame scene grab the water refracts.
	static void shutdownRefraction();	///<release it.
	///Copy the scene as it stands right now into the grab. Called immediately before the
	///water draws, which is the only moment the copy matches what the water's own alpha
	///blend is about to read out of the frame buffer.
	static void captureRefraction();
	static Bool isSsrActive();	///<true when SSR is enabled and its resources exist.
	static void startCameraDepthRendering();	///<redirect rendering into the camera-view depth target.
	static void endCameraDepthRendering();	///<restore the back buffer after the camera depth pass.
	static void captureSceneHistory();	///<copy the resolved scene into the history texture for next frame.
	static void captureSceneHistoryFromBackBuffer();	///<same, straight off the back buffer, for when the filter chain did not capture.
	static Bool sceneHistoryCaptured() { return m_sceneHistoryCaptured; }	///<true once this frame's scene colour has been copied to the history.
	static void resetSceneHistoryCaptured() { m_sceneHistoryCaptured = false; }	///<call once per frame before the scene is drawn.
	enum { NUM_SHADOW_SAVED_STATES = 9 };	///<render states saved across the shadow depth pass

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
	/// Which pipeline stage LoadAndCreateD3DShader is being asked to make a shader for.
	///
	/// It took a Bool -- true for vertex, false for pixel -- and 42 call sites spell that
	/// literal out. Widening the parameter to an Int keeps every one of them compiling and
	/// meaning what it always meant, because false and true convert to 0 and 1 and those
	/// are still the same two stages. A real enum would be the tidier spelling and would
	/// have required editing all 42 to say the same thing, so it is offered rather than
	/// imposed: new callers pass one of these, old callers keep their literal.
	enum ShaderStage
	{
		SHADER_STAGE_PIXEL = 0,
		SHADER_STAGE_VERTEX = 1,
		SHADER_STAGE_COMPUTE = 2
	};
	/// Loads a compiled shader blob and creates a shader of the named stage for it
	static HRESULT LoadAndCreateD3DShader(const char* strFilePath, const DWORD* pDeclaration, DWORD Usage, Int ShaderType, DWORD* pHandle);
#ifdef RTS_DEBUG
	/// The compute stage's positive control. See the definition; deleted with C4.
	static void runComputeSelfTest();
#endif

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
	static GfxTexture * endRenderToTexture(); ///< Ends render to texture, & returns texture.
	static GfxTexture * getRenderTexture();	///< returns last used render target texture
	///Create the floating-point scene target and the tone map that brings it back to 8 bits.
	///Safe to call when the option is off or the device cannot do it; HDR simply stays off.
	static void initHdr();
	static void shutdownHdr();	///<release the HDR resources.
	///True when the scene is being drawn into the floating-point target this frame. Anything
	///taking a mid-scene copy of the scene has to match its format -- see initRefraction.
	static Bool isHdrActive() { return m_hdrActive; }
	///The floating-point scene, valid between startRenderToTexture and the tone map at the
	///end of endRenderToTexture. This is what a post-process wanting the range reads.
	static GfxTexture * getHdrTexture() { return m_hdrTexture; }
	///Format any mid-scene grab of the scene colour must be created in, so its copy is a
	///same-format one. Follows the scene target: floating point under HDR, else the back
	///buffer's own format.
	static WW3DFormat getSceneColorFormat();
	///Draw the floating-point scene into m_renderTexture through the tone map. Called at the
	///end of the render-to-texture bracket, so nothing downstream ever meets the wide range.
	static void toneMapSceneToRenderTexture();
	///Apply the display gamma/brightness/contrast curve to the finished frame.
	///
	///This is what SetDeviceGammaRamp used to do at scanout and what D3D11 has no windowed
	///equivalent for. Called last in W3DDisplay::draw, after the interface, because the
	///hardware ramp applied to the interface too. Does nothing at all when the curve is the
	///identity, which is the default slider position -- so a default run costs neither the
	///copy nor the pass and cannot move a pixel.
	static void applyDisplayGamma();
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
	static Bool m_sceneHistoryCaptured;	///<set once this frame's scene colour reached the SSR history texture.
	static GfxSurface *m_oldRenderSurface;	///<previous render target
	static GfxTexture *m_renderTexture;		///<plain (non-MSAA) texture the redirected scene ends up in (post-process reads this)
	static GfxSurface *m_newRenderSurface;	///<render target the scene is drawn into: m_renderTexture's surface, or an MSAA surface when MSAA is on
	static GfxSurface *m_resolveSurface;		///<when MSAA: m_renderTexture's surface, the StretchRect resolve destination; null otherwise
	static GfxSurface *m_oldDepthSurface;	///<previous depth buffer surface
	// Directional shadow map (sun-view depth) render target + its own depth buffer.
	static GfxTexture *m_pShadowMapTexture;	///<depth-packed shadow map (A8R8G8B8)
	static GfxSurface *m_pShadowMapSurface;	///<colour surface of the shadow map
	static GfxSurface *m_pShadowMapDepthSurface;	///<the shadow map's own depth buffer
	static GfxSurface *m_shadowSavedRT;		///<render target saved across the shadow depth pass
	static GfxSurface *m_shadowSavedDepth;	///<depth surface saved across the shadow depth pass
	static unsigned m_shadowSavedStates[NUM_SHADOW_SAVED_STATES];	///<render states saved across the shadow depth pass
	// Screen-space reflections. The depth target is the shadow map's arrangement at
	// screen size and from the camera; the history texture is last frame's scene, which
	// is what the rays actually read (this frame's is the live render target).
	static GfxTexture *m_ssrDepthTexture;	///<camera-view depth-packed target (A8R8G8B8)
	static GfxSurface *m_ssrDepthSurface;	///<colour surface of the depth target
	static GfxSurface *m_ssrDepthStencil;	///<the depth pass's own depth buffer
	static GfxTexture *m_sceneHistoryTexture;	///<previous frame's resolved scene colour
	static GfxSurface *m_sceneHistorySurface;	///<its surface, the StretchRect destination
	static GfxTexture *m_refractionTexture;	///<scene as it stood just before the water drew
	static GfxSurface *m_refractionSurface;	///<its surface, the StretchRect destination
	// High dynamic range scene target. The scene is drawn here instead of straight into
	// m_renderTexture, and tone mapped down into it at the end of render-to-texture, so
	// everything downstream still finds the 8-bit scene texture it has always read.
	static Bool m_hdrActive;						///<HDR wanted, supported, and its resources exist
	static GfxTexture *m_hdrTexture;			///<floating-point scene colour (A16B16G16R16F)
	static GfxSurface *m_hdrRenderSurface;	///<what the scene draws into: the texture's surface, or an MSAA surface
	static GfxSurface *m_hdrResolveSurface;	///<when MSAA: the texture's surface, the resolve destination; null otherwise
	static DWORD m_toneMapPS;						///<tonemap_ps: HDR scene -> the 8-bit scene texture
	// The display gamma pass. A copy of the finished back buffer, because a pass cannot
	// sample the surface it is drawing into, and the shader that evaluates the ramp.
	static DWORD m_gammaPS;							///<gamma_ps: the display ramp, per pixel
	static GfxTexture *m_gammaCopyTexture;	///<back-buffer-sized copy the pass samples
	static unsigned m_gammaCopyWidth;
	static unsigned m_gammaCopyHeight;
	static WW3DFormat m_gammaCopyFormat;


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

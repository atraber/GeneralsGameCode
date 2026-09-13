/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
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

#pragma once

#include "Common/GameCommon.h"
#include "WWMath/vector3.h"
#include "WWMath/vector4.h"

class CameraClass;
struct GfxTexture;
struct GfxBuffer;

// Volumetric fog manager: froxel-based Eulerian fog and light scattering pipeline.
//
// 120 x 68 x 32 froxels in camera frustum.
// The froxels carry the low-frequency terms: ambient haze and sun shafts. Punctual lights
// (headlights, spotlights, point lights) from the clustered grid are integrated per pixel in
// the composite instead, clipped to each light's sphere, cone and the scene depth -- a 26 unit
// headlight cone is far thinner than one ~70 unit froxel slice. W3D_FOG_LIGHTS=froxel puts
// them back in the volume for A/B; W3D_FOG_DEBUG=1|2 are the probes (volumetric_composite_ps).
class VolumetricFogClass
{
public:
	enum
	{
		FROXEL_GRID_X = 120,
		FROXEL_GRID_Y = 68,
		FROXEL_GRID_Z = 32
	};

	VolumetricFogClass();
	~VolumetricFogClass();

	void Init();
	void Shutdown();

	// Updates volumetric fog constant registers (slots 9..20 of FrameConstants at b1)
	void Update_Constants(CameraClass & camera);

	// Evaluates in-scattering, integrates along rays, and composites onto active scene render target
	void Render(CameraClass & camera, GfxBuffer * lightBuffer, GfxBuffer * clusterGrid,
		GfxBuffer * lightIndexList, GfxTexture * depthTexture);

	static Bool Is_Active();
	static void Set_Active(Bool active);

	// Is_Active says the feature is switched on. This says the frame would actually SEE
	// anything if it ran, and it is a different question with a different consumer: the
	// camera depth prepass is a second full pass over the scene geometry and it exists only
	// to feed this. Running it to composite a volume that integrates to nothing is the most
	// expensive no-op in the frame. localLightCount is this frame's clustered light list
	// size -- the punctual lights are the only term that varies per frame.
	static Bool Would_Contribute(unsigned localLightCount);

	static float Get_Density() { return s_density; }
	static void  Set_Density(float d) { s_density = d; }

	static float Get_Height_Falloff() { return s_heightFalloff; }
	static void  Set_Height_Falloff(float k) { s_heightFalloff = k; }

	static float Get_Ground_Height() { return s_groundHeight; }
	static void  Set_Ground_Height(float h) { s_groundHeight = h; }

	static float Get_Anisotropy() { return s_anisotropy; }
	static void  Set_Anisotropy(float g) { s_anisotropy = g; }

	static float Get_Sun_Shaft_Intensity() { return s_sunShaftIntensity; }
	static void  Set_Sun_Shaft_Intensity(float i) { s_sunShaftIntensity = i; }

	static float Get_Ambient_Intensity() { return s_ambientIntensity; }
	static void  Set_Ambient_Intensity(float i) { s_ambientIntensity = i; }

	static float Get_Light_Boost() { return s_lightBoost; }
	static void  Set_Light_Boost(float b) { s_lightBoost = b; }

	// Scattering coefficient for punctual lights on the per-pixel path, per world unit. Not the
	// height fog's density: a beam has to read at night without the haze greying the map.
	static float Get_Light_Scatter() { return s_lightScatter; }
	static void  Set_Light_Scatter(float s) { s_lightScatter = s; }

private:
	void Ensure_Textures();
	void Ensure_Shaders();
	void Release_Resources();

	GfxTexture * m_volumeScatter;
	GfxTexture * m_volumeIntegrated;

	DWORD m_scatterCS;
	DWORD m_integrateCS;
	DWORD m_compositePS;

	Bool m_shadersLoaded;
	Bool m_loadFailed;

	static Bool  s_active;
	static float s_density;
	static float s_heightFalloff;
	static float s_groundHeight;
	static float s_anisotropy;
	static float s_sunShaftIntensity;
	static float s_ambientIntensity;
	static float s_lightBoost;
	static float s_lightScatter;
	static float s_pointLightWeight;
	static Bool  s_lightsPerPixel;
	static Int   s_debugMode;
};

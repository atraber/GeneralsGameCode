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


// FILE: W3DShadow.cpp ///////////////////////////////////////////////////////////
//
// Real time shadow representations
//
// Author: Mark Wilczynski, February 2002
//
//

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "always.h"
#include "GameClient/View.h"
#include "WW3D2/camera.h"
#include "WW3D2/light.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/hlod.h"
#include "WW3D2/mesh.h"
#include "WW3D2/meshmdl.h"
#include "Lib/BaseType.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "Common/GlobalData.h"
#include "W3DDevice/GameClient/W3DProjectedShadow.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "WW3D2/statistics.h"
#include "Common/Debug.h"
#include "Common/PerfTimer.h"
#include "Common/FrameTiming.h"
#include "GameClient/DayNightCycle.h"
#include "WWMath/wwmath.h"

#define SUN_DISTANCE_FROM_GROUND	10000.0f	//distance of sun (our only light source).

// Global Variables and Functions /////////////////////////////////////////////
W3DShadowManager *TheW3DShadowManager=nullptr;
const FrustumClass *shadowCameraFrustum;

Vector3 LightPosWorld[ MAX_SHADOW_LIGHTS ] =
{

	Vector3( 94.0161f, 50.499f, 200.0f)
};

void PrepareShadows()
{
	if (TheW3DProjectedShadowManager)
		TheW3DProjectedShadowManager->prepareShadows();
}

//DECLARE_PERF_TIMER(shadowsRender)
void DoShadows(RenderInfoClass & rinfo, Bool stencilPass)
{
	//USE_PERF_TIMER(shadowsRender)
	shadowCameraFrustum=&rinfo.Camera.Get_Frustum();

	// The directional shadow map already casts every shadow in the scene, so the decal
	// shadows stand down while it is active (options.ini UseShadowMapping) -- running
	// both would double-darken every caster.
	//
	// Not by skipping this function, though: the projected-shadow manager also draws
	// m_decalList, which is not shadows at all. Radius cursors, special-power targeting
	// reticles and delivery markers live there, and they are still wanted. It runs in
	// decals-only mode instead.
	const Bool shadowMapping = W3DShaderManager::isShadowMappingActive();

	if (stencilPass == FALSE  && TheW3DProjectedShadowManager)
	{
			if (TheW3DShadowManager->isShadowScene())
				TheW3DProjectedShadowManager->renderShadows(rinfo, shadowMapping);
	}

	if (TheW3DShadowManager && stencilPass)	//reset so no more shadow processing this frame.
		TheW3DShadowManager->queueShadows(FALSE);

}

W3DShadowManager::W3DShadowManager()
{
	DEBUG_ASSERTCRASH(TheW3DProjectedShadowManager == nullptr,
		("Creating a new shadow manager without deleting the old one"));

	m_shadowColor = 0x7fa0a0a0;
	m_isShadowScene = FALSE;
	m_stencilShadowMask = 0;	//all bits can be used for storing shadows.

	Vector3 lightRay(-TheGlobalData->m_terrainLightPos[0].x,
		-TheGlobalData->m_terrainLightPos[0].y, -TheGlobalData->m_terrainLightPos[0].z);
	lightRay.Normalize();

	LightPosWorld[0]=lightRay*SUN_DISTANCE_FROM_GROUND;

	TheProjectedShadowManager = TheW3DProjectedShadowManager = NEW W3DProjectedShadowManager;
}

W3DShadowManager::~W3DShadowManager()
{
	delete TheW3DProjectedShadowManager;
	TheProjectedShadowManager = TheW3DProjectedShadowManager = nullptr;
}

/** Do one-time initilalization of shadow systems that need to be
active for full duration of game*/
Bool W3DShadowManager::init()
{
	Bool result=TRUE;

	if ( TheW3DProjectedShadowManager && TheW3DProjectedShadowManager->init())
	{
		if (TheW3DProjectedShadowManager->ReAcquireResources())
			result = TRUE;
	}

	return result;
}

/** Do per-map reset.  This frees up shadows from all objects since
they may not exist on the next map*/
void W3DShadowManager::Reset()
{

	if (TheW3DProjectedShadowManager)
		TheW3DProjectedShadowManager->reset();
}

Bool W3DShadowManager::ReAcquireResources()
{
	Bool result = TRUE;

	if (TheW3DProjectedShadowManager && !TheW3DProjectedShadowManager->ReAcquireResources())
		result = FALSE;

	return result;
}

void W3DShadowManager::ReleaseResources()
{
	if (TheW3DProjectedShadowManager)
		TheW3DProjectedShadowManager->ReleaseResources();
}

Shadow *W3DShadowManager::addShadow( RenderObjClass *robj, Shadow::ShadowTypeInfo *shadowInfo, Drawable *draw)
{
	ShadowType type = SHADOW_VOLUME;

	if (shadowInfo)
		type = shadowInfo->m_type;

	switch(type)
	{
		case	SHADOW_VOLUME:
			// Stencil shadow volumes are gone. The token still parses -- 1244 shipped object
			// definitions declare it -- but it no longer builds a shadow object; the
			// directional shadow map already casts these casters' shadows.
			return nullptr;
		case	SHADOW_PROJECTION:
		case	SHADOW_DECAL:
			if (TheW3DProjectedShadowManager)
				return (Shadow *)TheW3DProjectedShadowManager->addShadow(robj, shadowInfo, draw);
			break;
		default:
			return nullptr;
	}

	return nullptr;
}

void W3DShadowManager::removeShadow(Shadow *shadow)
{
	shadow->release();
}

void W3DShadowManager::removeAllShadows()
{
	if (TheW3DProjectedShadowManager)
		TheW3DProjectedShadowManager->removeAllShadows();
}

/**Force update of all shadows even when light source and object have not moved*/
void W3DShadowManager::invalidateCachedLightPositions()
{
	if (TheW3DProjectedShadowManager)
		TheW3DProjectedShadowManager->invalidateCachedLightPositions();
}

Vector3 &W3DShadowManager::getLightPosWorld(Int lightIndex)
{
	return LightPosWorld[lightIndex];
}

void W3DShadowManager::setLightPosition(Int lightIndex, Real x, Real y, Real z)
{
	if (lightIndex != 0)
		return;	///@todo: Add support for multiple lights

	LightPosWorld[lightIndex]=Vector3(x,y,z);
}

// TheSuperHackers @perf andytraber 12/09/2026 QUANTISED. The day/night cycle rewrites the sun
// direction every client frame, and this is the only thing standing between that and a full
// re-render of every projected shadow texture on every one of them -- W3DProjectedShadow::update
// compares light positions with an exact Vector3 inequality and has no threshold of its own.
//
// THE THRESHOLD. One degree of change in the direction TOWARDS the light, not one degree of
// elevation and not a distance. Three things argue for that number:
//
//  - it is what the shadow texture can actually resolve. The texture is the caster's silhouette
//    as seen from the light, rendered into DEFAULT_RENDER_TARGET_WIDTH = 512 texels across the
//    caster's bounding box, so a rotation of the view direction by one degree moves the silhouette
//    by tan(1 deg) x caster height -- on the order of a texel for anything tank-sized;
//  - the error it permits is NOT uniform on the ground. A shadow's tip sits at h.cot(elevation),
//    and d(cot)/d(angle) grows as 1/sin^2, so one degree of light movement slides the tip by 0.023h
//    at a 60 degree sun but 0.124h at the 22 degree flattest light this cycle produces (see
//    SUN_HANDOVER_HIGH_DEG) -- a quarter of a terrain cell for a 20-unit caster. Two degrees, the
//    loose end of the range this was scoped at, would be half a cell there. So the tight end of the
//    range is the right end;
//  - it is also cheap. Over a 20-minute cycle the direction travels roughly 200 degrees, so a one
//    degree threshold steps about 200 times -- once every 4 seconds of wall clock, or once per
//    ~120 frames at 30 fps. There is no need to be looser than this.
//
// The comparison is against the CURRENTLY PUBLISHED value rather than against an accumulated
// ideal, which is what makes the error bounded rather than drifting: every published direction is
// within the threshold of the true one.
#define SHADOW_LIGHT_STEP_DEGREES	1.0f

void W3DShadowManager::updateSunLightPosition(const Vector3 &lightRay)
{
	// The ray travels sky -> ground; the shadow system wants a position, i.e. up towards the light.
	Vector3 toLight(-lightRay.X, -lightRay.Y, -lightRay.Z);
	if (toLight.Length2() < 1e-8f)
		return;
	toLight.Normalize();

	Vector3 current = LightPosWorld[0];
	const Real currentLen2 = current.Length2();
	if (currentLen2 > 1e-8f)
	{
		current /= WWMath::Sqrt(currentLen2);
		const Real cosStep = WWMath::Cos(SHADOW_LIGHT_STEP_DEGREES * (3.14159265f / 180.0f));
		if (Vector3::Dot_Product(current, toLight) >= cosStep)
			return;	// inside the threshold: the published direction is still good enough
	}

	FrameTiming::recordEvent(FrameTiming::EVENT_SHADOW_LIGHT_STEP);
	LightPosWorld[0] = toLight * SUN_DISTANCE_FROM_GROUND;
}

void W3DShadowManager::setTimeOfDay(TimeOfDay tod)
{
	// TheSuperHackers @perf andytraber 12/09/2026 Stand down while the cycle is driving the light.
	//
	// This is called from W3DGameClient::setTimeOfDay at each nominal time-of-day boundary, and it
	// would snap the shadow light back to the AUTHORED direction for that time of day -- which, now
	// that the direction is analytic, is somewhere else entirely. The quantiser above would then see
	// a large difference on the next client frame and republish, so the only effect would be one
	// frame of decal shadows pointing the authored way plus a full re-render burst, four times a
	// cycle, for nothing. With the cycle off this is still the only thing that moves the light and
	// must keep working.
	if (DayNightCycle_IsEnabled())
		return;

	//Ray to light source
	const GlobalData::TerrainLighting *ol=&TheGlobalData->m_terrainObjectsLighting[tod][0];

	Vector3 lightRay(-ol->lightPos.x,-ol->lightPos.y,-ol->lightPos.z);

	lightRay.Normalize();
	lightRay *= SUN_DISTANCE_FROM_GROUND;

	setLightPosition(0, lightRay.X, lightRay.Y, lightRay.Z);
}

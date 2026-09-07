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

// FILE: W3DLightAuthoring.h /////////////////////////////////////////////////////////////
//
// THE LOCAL-LIGHT INTENSITY CONVENTION, in one place.
//
// The content pass of the clustered lighting plan, section "Light intensity:
// re-authored in physical units (decided 2026-09-07)". Every site in the engine that
// *authors* a point or spot light calls through here, so that the convention is written
// down once rather than five times -- the same reason clustered.hlsli owns the falloff
// curve for the six shaders that consume it.
//
// -------------------------------------------------------------------------------------
// WHAT CHANGED, AND WHY THE NUMBERS HAD TO
// -------------------------------------------------------------------------------------
//
// The old model's distance ramp is LINEAR. LightEnvironmentClass::Init_From_Point_Or_Spot_Light
// and HeightMapRenderObjClass::doTheDynamicLight both compute
//
//     atten = 1 - (d - attenStart) / (attenEnd - attenStart)      clamped to [0,1]
//
// so an authored colour means "this is the brightness delivered anywhere inside attenStart,
// ramping to nothing at attenEnd". attenStart is therefore not decoration: it is literally
// the distance at which the authored number is true, and that is the one fact this whole
// conversion rests on.
//
// clustered.hlsli's ClusterLightRadiance is honest inverse square -- radiance = colour / d^2,
// windowed to reach zero at the range. Under it the *same* colour is worth 1/400 of itself
// at twenty world units, i.e. black. The plan considered rescaling inside Pack_Light and
// rejected it: the falloff is not a tuning knob and a fudge factor buried in the packer
// would have to be unlearned by every future light author. So the numbers move instead, and
// they move here.
//
// The conversion is E = I / d^2 read backwards. A fixture that should deliver colour B at
// distance r carries a luminous intensity
//
//     I = B * r^2
//
// and r -- the REFERENCE DISTANCE -- is the judgement. It is not automatically attenStart:
// see W3DDisplay::createLightPulse, where attenStart is a hardcoded 1 that FXList.cpp passes
// for every explosion in the game and which describes nothing about the fixture. Each caller
// picks r for what its light is *for*, and says so where it picks it. This function only
// owns the squaring.
//
// -------------------------------------------------------------------------------------
// WHY IT IS CONDITIONAL, AND WHEN THE CONDITION GOES AWAY
// -------------------------------------------------------------------------------------
//
// There is ONE LightClass object per light and, until C7, TWO consumers reading it with
// incompatible conventions: Pack_Light wants intensity, LightEnvironmentClass and the
// terrain's vertex bake want brightness. A light authored in intensity and handed to the
// old path reads tens of times too bright; a light authored in brightness and handed to the
// clustered path reads black. Nothing can satisfy both, and the CPU path is deliberately
// still live (it is C7 that deletes it).
//
// So the convention follows the consumer that is actually running. That keeps two
// properties which are worth more than tidiness:
//
//   * With UseClusteredLighting = no, every light carries exactly the number it carried
//     before this pass, so the "0 differing pixels" gate the plan applies to every stage
//     before C7 still means something.
//   * With it on, the clustered path sees physical units and can be judged on its own
//     terms -- while the old path, reading the same numbers as brightness, over-lights.
//     That over-lighting is the double-counting the plan already expects from running both
//     models at once, only louder; it is a C7 deletion, not a tuning error here.
//
// When C7 removes LightEnvironmentClass's point/spot handling and doTheDynamicLight, the
// branch below has one consumer left and collapses to the multiply.
//
// -------------------------------------------------------------------------------------
// AMBIENT
// -------------------------------------------------------------------------------------
//
// Pack_Light never reads a light's ambient -- it packs Get_Diffuse() * Get_Intensity() and
// nothing else -- so under the clustered path a local light's ambient contributes exactly
// zero. Its only consumers are the terrain vertex bake (HeightMap.cpp, which adds
// factor*ambient with no N.L at all) and W3DRoadBuffer, both of which C7 deletes. Callers
// therefore fold whatever the ambient meant into the authored colour and zero the field,
// rather than leaving a second, flat, unfalloffed copy of the same light on the ground.
// Authoring_Local_Light_Ambient below is that decision, so it reads the same way at every
// site.

#pragma once

#ifndef __W3DLIGHTAUTHORING_H_
#define __W3DLIGHTAUTHORING_H_

#include "Lib/BaseType.h"
#include "Common/GlobalData.h"
#include "WWMath/vector3.h"

//-------------------------------------------------------------------------------------------------
/** True when local lights should carry luminous intensity rather than brightness.
	*
	* THE OPTIONS.INI SWITCH ALONE, deliberately, and NOT W3DShaderManager::isClusteredLightingActive().
	* That predicate is the right one for binding buffers and for gating a shader, because it also
	* asks whether the three buffers exist -- but it is a per-frame answer, and one of the sites
	* below authors a light ONCE, at map load (W3DTerrainVisual::load), before the light list has
	* had an Update() to create its buffer in. A per-frame predicate read at load time would put
	* every map light in the legacy convention for the whole run, and the symptom would be map
	* lights that are merely too dim rather than anything that looks like a fault.
	*
	* A light's units have to be a property of the run, not of the frame it was created on. The
	* option cannot change mid-run; the buffers can. */
//-------------------------------------------------------------------------------------------------
inline Bool localLightsAreAuthoredAsIntensity()
{
	return (TheGlobalData != nullptr && TheGlobalData->m_useClusteredLighting) ? TRUE : FALSE;
}

//-------------------------------------------------------------------------------------------------
/** The colour to store in a local light's diffuse.
	*
	* @param colourAtReference  the brightness the fixture should deliver at referenceDistance
	* @param referenceDistance  where that brightness is true, in world units -- the caller's
	*                           judgement, and the thing worth arguing about
	*
	* Returns colourAtReference * referenceDistance^2 under the clustered path (luminous
	* intensity), and colourAtReference unchanged under the old linear ramp, which already
	* means "this brightness, out to attenStart". */
//-------------------------------------------------------------------------------------------------
inline Vector3 authoredLocalLightColor(const Vector3 &colourAtReference, Real referenceDistance)
{
	if (!localLightsAreAuthoredAsIntensity())
		return colourAtReference;

	// No lower bound on the reference distance and no clamp on the result. A caller that
	// hands in zero gets a black light, which is a visible, findable failure; a silent floor
	// would turn a bad reference distance into a light that is merely the wrong brightness,
	// and this project has spent whole phases on numbers that were quietly wrong rather than
	// obviously missing. Every caller below picks its reference distance explicitly and none
	// can reach zero.
	return colourAtReference * (referenceDistance * referenceDistance);
}

//-------------------------------------------------------------------------------------------------
/** The same thing where the re-authored fixture colour is not simply the old one squared up.
	*
	* Two sites need this: the police beacon, whose fixture output is the old diffuse plus the
	* old ambient wash, and any site whose legacy value folded in a factor that has moved
	* elsewhere. Spelling the legacy value out at the call rather than deriving it is what keeps
	* "UseClusteredLighting = no is byte-identical" checkable by reading the call site. */
//-------------------------------------------------------------------------------------------------
inline Vector3 authoredLocalLightColor(const Vector3 &legacyDiffuse,
	const Vector3 &colourAtReference, Real referenceDistance)
{
	if (!localLightsAreAuthoredAsIntensity())
		return legacyDiffuse;
	return colourAtReference * (referenceDistance * referenceDistance);
}

//-------------------------------------------------------------------------------------------------
/** What to store in a local light's ambient. Zero once the clustered path is live -- see the
	* AMBIENT note at the top of this file -- and the caller's own legacy value until then. */
//-------------------------------------------------------------------------------------------------
inline Vector3 authoredLocalLightAmbient(const Vector3 &legacyAmbient)
{
	if (!localLightsAreAuthoredAsIntensity())
		return legacyAmbient;
	return Vector3(0.0f, 0.0f, 0.0f);
}

#endif // __W3DLIGHTAUTHORING_H_

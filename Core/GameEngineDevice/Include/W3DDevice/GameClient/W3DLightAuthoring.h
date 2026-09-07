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
// The old model's distance ramp was LINEAR. LightEnvironmentClass::Init_From_Point_Or_Spot_Light
// and HeightMapRenderObjClass::doTheDynamicLight -- both deleted by C7, so this is now a
// statement about the shipped data rather than about live code -- each computed
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
// IT USED TO BE CONDITIONAL. C7 REMOVED THE CONDITION.
// -------------------------------------------------------------------------------------
//
// There is ONE LightClass object per light, and for the length of the clustered bring-up
// there were TWO consumers reading it with incompatible conventions: Pack_Light wanted
// intensity, LightEnvironmentClass and the terrain's vertex bake wanted brightness. A light
// authored in intensity and handed to the old path reads tens of times too bright; one
// authored in brightness and handed to the clustered path reads black. Nothing satisfies
// both, so these functions branched on the options.ini switch and gave each run a single
// convention.
//
// C7 deleted the CPU path -- LightEnvironmentClass's point/spot handling, Render_Seg's
// per-drawable light walk, doTheDynamicLight, the local-light branch of the terrain's static
// bake and W3DRoadBuffer's lit road copy -- and the UseClusteredLighting switch with it. One
// consumer is left, so the branch is gone and the conversion is unconditional. The functions
// stay: they are still the one place the convention is written down, and a call site that
// reads authoredLocalLightColor(colour, refDist) still states its reference distance where
// somebody can argue with it.
//
// -------------------------------------------------------------------------------------
// AMBIENT
// -------------------------------------------------------------------------------------
//
// Pack_Light never reads a light's ambient -- it packs Get_Diffuse() * Get_Intensity() and
// nothing else -- so under the clustered path a local light's ambient contributes exactly
// zero, and since C7 nothing else reads it either. Callers therefore fold whatever the
// ambient meant into the authored colour and zero the field, rather than leaving a second,
// flat, unfalloffed copy of the same light on the ground. authoredLocalLightAmbient below is
// that decision, so it reads the same way at every site.

#pragma once

#ifndef __W3DLIGHTAUTHORING_H_
#define __W3DLIGHTAUTHORING_H_

#include "Lib/BaseType.h"
#include "Common/GlobalData.h"
#include "WWMath/vector3.h"

//-------------------------------------------------------------------------------------------------
/** Always true since C7 deleted the other consumer. Kept as a named predicate because the
	* handful of call sites that still ask read better for asking it by name than for a bare
	* TRUE, and because it is where the answer would change again if a second consumer ever
	* came back.
	*
	* NOT W3DShaderManager::isClusteredLightingActive(), and that distinction survives the
	* collapse: that predicate is a per-frame answer about whether the three buffers exist,
	* and one site below authors a light ONCE, at map load (W3DTerrainVisual::load), before
	* the light list has had an Update() to create its buffer in. A light's units have to be
	* a property of the run, not of the frame it was created on. */
//-------------------------------------------------------------------------------------------------
inline Bool localLightsAreAuthoredAsIntensity()
{
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
/** The colour to store in a local light's diffuse.
	*
	* @param colourAtReference  the brightness the fixture should deliver at referenceDistance
	* @param referenceDistance  where that brightness is true, in world units -- the caller's
	*                           judgement, and the thing worth arguing about
	*
	* Returns colourAtReference * referenceDistance^2 -- the luminous intensity
	* clustered.hlsli's inverse-square falloff expects. */
//-------------------------------------------------------------------------------------------------
inline Vector3 authoredLocalLightColor(const Vector3 &colourAtReference, Real referenceDistance)
{
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
	* elsewhere. Nothing returns legacyDiffuse any more -- C7 removed the branch that could --
	* but the argument stays, spelled out at the call, because it is the only remaining record
	* of what the shipped content used to carry, and that is where any future argument about
	* these numbers has to start. */
//-------------------------------------------------------------------------------------------------
inline Vector3 authoredLocalLightColor(const Vector3 &legacyDiffuse,
	const Vector3 &colourAtReference, Real referenceDistance)
{
	(void)legacyDiffuse;
	return colourAtReference * (referenceDistance * referenceDistance);
}

//-------------------------------------------------------------------------------------------------
/** What to store in a local light's ambient: nothing at all -- see the AMBIENT note at the top
	* of this file. The caller's legacy value is passed in and discarded, for the same reason
	* authoredLocalLightColor still takes one. */
//-------------------------------------------------------------------------------------------------
inline Vector3 authoredLocalLightAmbient(const Vector3 &legacyAmbient)
{
	(void)legacyAmbient;
	return Vector3(0.0f, 0.0f, 0.0f);
}

#endif // __W3DLIGHTAUTHORING_H_

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

#include "Lib/BaseType.h"
#include "Common/GameType.h"

// TheSuperHackers @feature andytraber 08/09/2026 Dynamic day-night cycle with creeping shadows.
// Configurable via Options.ini DayNightCycleDuration (default 20 min, 0 disables).
// Respects 16h Day (2/3) vs 8h Night (1/3) cycle proportions and begins from the map's authored settings.

void DayNightCycle_Init();
void DayNightCycle_Reset();
// The cosmetic half: interpolate the sun and push it at the lights. Called once per
// rendered frame from GameClient::update.
void DayNightCycle_Update(UnsignedInt logicFrame);

// The boundary half: cross a nominal time of day, flip MODELCONDITION_NIGHT on every
// drawable, ask for the terrain re-bake. Called from GameLogic::update and NOWHERE ELSE --
// the model-condition change reaches code that refuses to run outside a logic update, and
// calling it from the client is what used to crash the game at nightfall. See the comment
// on the definition.
void DayNightCycle_UpdateLogic(UnsignedInt logicFrame);
void DayNightCycle_SetTimeOfDay(TimeOfDay tod);

// TheSuperHackers @feature andytraber 13/09/2026 While the cycle runs it decides per drawable when
// MODELCONDITION_NIGHT -- and with it the drawable's lights -- goes on and off, jittered across a
// band of sun elevation at dusk and dawn. GameClient::setTimeOfDay asks this first; FALSE means the
// cycle is not running and the caller should apply tod to every drawable itself, as it always did.
Bool DayNightCycle_SyncDrawables(TimeOfDay nominalTod);

Bool DayNightCycle_IsEnabled();
Real DayNightCycle_GetCurrentGameHour();
TimeOfDay DayNightCycle_GetCurrentTimeOfDay();

// For the F10 frame-timing readout: where the cycle is, so a lighting cost can be read
// against the moment that produced it.
Real DayNightCycle_GetBlendAlpha();			///< 0..1 between the two key times of day
Int  DayNightCycle_GetDurationMinutes();	///< the configured cycle length; <= 0 means disabled

// TheSuperHackers @feature andytraber 12/09/2026 THE ORBIT. The light direction is analytic now
// and no longer comes from the authored TerrainLighting tables (those remain the colour source),
// so the geometry has to be readable from outside or a wrong-looking shadow cannot be attributed.
// All three report the light that was actually PUBLISHED, i.e. the sun/moon blend -- during the
// dusk and dawn handover that is neither body exactly, which is the honest answer because it is
// also where the shadows are.
Real DayNightCycle_GetSunElevationDegrees();	///< degrees above the horizon of the active body
Real DayNightCycle_GetLightAzimuthDegrees();	///< degrees about +Z from +X towards +Y, towards the body
Real DayNightCycle_GetMoonBlend();				///< 0 = the sun owns the light, 1 = the moon does
Bool DayNightCycle_IsMoonLit();					///< the moon is the dominant body

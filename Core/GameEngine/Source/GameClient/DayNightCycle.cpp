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

#include "PreRTS.h"

#include "GameClient/DayNightCycle.h"
#include "Common/OptionPreferences.h"
#include "Common/GlobalData.h"
#include "Common/GameCommon.h"
#include "Common/GameEngine.h"
#include "GameClient/Display.h"
#include "GameClient/GameClient.h"
#include "GameClient/Drawable.h"
#include "GameLogic/GameLogic.h"
#include "WWMath/vector3.h"
#include "WWMath/matrix3d.h"

#include <math.h>

// TheSuperHackers @feature andytraber 08/09/2026 Dynamic day-night cycle with creeping shadows.
// Continuous solar orbit interpolation respecting the 16h Day (2/3) vs 8h Night (1/3) ratio.

static Bool s_cycleEnabled = FALSE;
static Bool s_initialized = FALSE;
static Int  s_durationMinutes = 20;
static UnsignedInt s_startOffsetFrames = 0;
static TimeOfDay s_initialTod = TIME_OF_DAY_MORNING;
static TimeOfDay s_lastNominalTod = TIME_OF_DAY_INVALID;
static Real s_currentGameHour = 0.0f;

// Cached map authored lighting for all four times of day
static GlobalData::TerrainLighting s_mapTerrainLighting[TIME_OF_DAY_COUNT][MAX_GLOBAL_LIGHTS];
static GlobalData::TerrainLighting s_mapObjectsLighting[TIME_OF_DAY_COUNT][MAX_GLOBAL_LIGHTS];

static Bool s_forceTerrainBake = FALSE;

static Vector3 interpolateLightDirection(const Coord3D &dirA, const Coord3D &dirB, Real alpha)
{
	Vector3 vA(dirA.x, dirA.y, dirA.z);
	Vector3 vB(dirB.x, dirB.y, dirB.z);

	Real lenA = vA.Length();
	Real lenB = vB.Length();

	if (lenA < 1e-4f) vA.Set(0.0f, 0.0f, -1.0f);
	else vA /= lenA;

	if (lenB < 1e-4f) vB.Set(0.0f, 0.0f, -1.0f);
	else vB /= lenB;

	Real dot = Vector3::Dot_Product(vA, vB);
	dot = clamp(-1.0f, dot, 1.0f);

	Vector3 result;
	if (dot > 0.9995f)
	{
		result = vA * (1.0f - alpha) + vB * alpha;
	}
	else if (dot < -0.9995f)
	{
		Vector3 ortho(0.0f, 1.0f, 0.0f);
		if (fabsf(vA.Y) > 0.9f)
			ortho.Set(1.0f, 0.0f, 0.0f);
		Vector3 axis;
		Vector3::Cross_Product(vA, ortho, &axis);
		axis.Normalize();
		Real angle = 3.14159265f * alpha;
		Matrix3D rot(axis, angle);
		result = rot.Rotate_Vector(vA);
	}
	else
	{
		Real theta = acosf(dot);
		Real sinTheta = sinf(theta);
		Real wA = sinf((1.0f - alpha) * theta) / sinTheta;
		Real wB = sinf(alpha * theta) / sinTheta;
		result = vA * wA + vB * wB;
	}

	result.Normalize();

	// In Generals coordinate space, Z is up. Light rays shine downwards from the sky,
	// so lightPos.z must remain negative (pointing towards the ground) to keep the sun/moon above the horizon.
	if (result.Z > -0.05f)
	{
		result.Z = -0.05f;
		result.Normalize();
	}

	Real targetLen = (1.0f - alpha) * lenA + alpha * lenB;
	if (targetLen < 0.1f)
		targetLen = 1.0f;
	result *= targetLen;

	return result;
}

static void ensureValidLighting(TimeOfDay tod)
{
	Real sumDiffuse = s_mapTerrainLighting[tod][0].diffuse.red
	                + s_mapTerrainLighting[tod][0].diffuse.green
	                + s_mapTerrainLighting[tod][0].diffuse.blue;
	Real sumAmbient = s_mapTerrainLighting[tod][0].ambient.red
	                + s_mapTerrainLighting[tod][0].ambient.green
	                + s_mapTerrainLighting[tod][0].ambient.blue;

	if (sumDiffuse < 1e-4f && sumAmbient < 1e-4f)
	{
		// Map did not author lighting for this time of day, supply defaults based on phase
		switch (tod)
		{
		case TIME_OF_DAY_MORNING:
			s_mapTerrainLighting[tod][0].ambient.red = 0.25f; s_mapTerrainLighting[tod][0].ambient.green = 0.25f; s_mapTerrainLighting[tod][0].ambient.blue = 0.22f;
			s_mapTerrainLighting[tod][0].diffuse.red = 0.85f; s_mapTerrainLighting[tod][0].diffuse.green = 0.80f; s_mapTerrainLighting[tod][0].diffuse.blue = 0.65f;
			s_mapTerrainLighting[tod][0].lightPos.x = 0.60f;  s_mapTerrainLighting[tod][0].lightPos.y = 0.30f;  s_mapTerrainLighting[tod][0].lightPos.z = -0.74f;
			break;
		case TIME_OF_DAY_AFTERNOON:
			s_mapTerrainLighting[tod][0].ambient.red = 0.30f; s_mapTerrainLighting[tod][0].ambient.green = 0.30f; s_mapTerrainLighting[tod][0].ambient.blue = 0.30f;
			s_mapTerrainLighting[tod][0].diffuse.red = 1.00f; s_mapTerrainLighting[tod][0].diffuse.green = 0.95f; s_mapTerrainLighting[tod][0].diffuse.blue = 0.85f;
			s_mapTerrainLighting[tod][0].lightPos.x = 0.20f;  s_mapTerrainLighting[tod][0].lightPos.y = -0.40f; s_mapTerrainLighting[tod][0].lightPos.z = -0.89f;
			break;
		case TIME_OF_DAY_EVENING:
			s_mapTerrainLighting[tod][0].ambient.red = 0.22f; s_mapTerrainLighting[tod][0].ambient.green = 0.18f; s_mapTerrainLighting[tod][0].ambient.blue = 0.20f;
			s_mapTerrainLighting[tod][0].diffuse.red = 0.90f; s_mapTerrainLighting[tod][0].diffuse.green = 0.55f; s_mapTerrainLighting[tod][0].diffuse.blue = 0.30f;
			s_mapTerrainLighting[tod][0].lightPos.x = -0.70f; s_mapTerrainLighting[tod][0].lightPos.y = -0.30f; s_mapTerrainLighting[tod][0].lightPos.z = -0.65f;
			break;
		case TIME_OF_DAY_NIGHT:
		default:
			// TheSuperHackers @feature andytraber 09/09/2026 Calibrate nocturnal ambient and diffuse for realistic darkness.
			s_mapTerrainLighting[tod][0].ambient.red = 0.030f; s_mapTerrainLighting[tod][0].ambient.green = 0.035f; s_mapTerrainLighting[tod][0].ambient.blue = 0.055f;
			s_mapTerrainLighting[tod][0].diffuse.red = 0.150f; s_mapTerrainLighting[tod][0].diffuse.green = 0.180f; s_mapTerrainLighting[tod][0].diffuse.blue = 0.260f;
			s_mapTerrainLighting[tod][0].lightPos.x = -0.40f;  s_mapTerrainLighting[tod][0].lightPos.y = 0.50f;   s_mapTerrainLighting[tod][0].lightPos.z = -0.77f;
			break;
		}
		s_mapObjectsLighting[tod][0] = s_mapTerrainLighting[tod][0];
	}

	for (Int i = 0; i < MAX_GLOBAL_LIGHTS; ++i)
	{
		if (s_mapTerrainLighting[tod][i].lightPos.z > -0.05f)
			s_mapTerrainLighting[tod][i].lightPos.z = -0.70f;
		if (s_mapObjectsLighting[tod][i].lightPos.z > -0.05f)
			s_mapObjectsLighting[tod][i].lightPos.z = -0.70f;
	}
}

void DayNightCycle_Init()
{
	OptionPreferences prefs;
	s_durationMinutes = prefs.getDayNightCycleDuration();

	if (s_durationMinutes <= 0)
	{
		s_cycleEnabled = FALSE;
		s_initialized = TRUE;
		return;
	}

	s_cycleEnabled = TRUE;
	s_initialized = TRUE;

	s_initialTod = TheGlobalData ? TheGlobalData->m_timeOfDay : TIME_OF_DAY_AFTERNOON;
	if (s_initialTod < TIME_OF_DAY_FIRST || s_initialTod >= TIME_OF_DAY_COUNT)
		s_initialTod = TIME_OF_DAY_AFTERNOON;

	s_lastNominalTod = s_initialTod;

	// Cache author-configured map lighting for all times of day
	for (Int tod = TIME_OF_DAY_FIRST; tod < TIME_OF_DAY_COUNT; ++tod)
	{
		for (Int i = 0; i < MAX_GLOBAL_LIGHTS; ++i)
		{
			s_mapTerrainLighting[tod][i] = TheGlobalData->m_terrainLighting[tod][i];
			s_mapObjectsLighting[tod][i] = TheGlobalData->m_terrainObjectsLighting[tod][i];
		}
		ensureValidLighting((TimeOfDay)tod);
	}

	// 24-hour clock division:
	// Morning:   0.0h to  6.0h (6 hours, 25%)
	// Afternoon: 6.0h to 12.0h (6 hours, 25%)
	// Evening:  12.0h to 16.0h (4 hours, 16.67%)
	// Night:    16.0h to 24.0h (8 hours, 33.33%)
	// Day = 16h (2/3), Night = 8h (1/3)
	Real startHour = 6.0f; // default Afternoon
	switch (s_initialTod)
	{
	case TIME_OF_DAY_MORNING:   startHour = 0.0f;  break;
	case TIME_OF_DAY_AFTERNOON: startHour = 6.0f;  break;
	case TIME_OF_DAY_EVENING:   startHour = 12.0f; break;
	case TIME_OF_DAY_NIGHT:     startHour = 16.0f; break;
	default: break;
	}

	const UnsignedInt totalCycleFrames = (UnsignedInt)s_durationMinutes * 60u * (UnsignedInt)LOGICFRAMES_PER_SECOND;
	if (totalCycleFrames > 0)
	{
		s_startOffsetFrames = (UnsignedInt)WWMath::Round((startHour / 24.0f) * (Real)totalCycleFrames);
	}
	else
	{
		s_startOffsetFrames = 0;
	}

	s_forceTerrainBake = FALSE;
	s_currentGameHour = startHour;
}

void DayNightCycle_Reset()
{
	s_initialized = FALSE;
	s_cycleEnabled = FALSE;
	s_startOffsetFrames = 0;
	s_lastNominalTod = TIME_OF_DAY_INVALID;
	s_forceTerrainBake = FALSE;
}

void DayNightCycle_SetTimeOfDay(TimeOfDay tod)
{
	if (tod < TIME_OF_DAY_FIRST || tod >= TIME_OF_DAY_COUNT)
		return;

	Real targetHour = 6.0f;
	switch (tod)
	{
	case TIME_OF_DAY_MORNING:   targetHour = 0.0f;  break;
	case TIME_OF_DAY_AFTERNOON: targetHour = 6.0f;  break;
	case TIME_OF_DAY_EVENING:   targetHour = 12.0f; break;
	case TIME_OF_DAY_NIGHT:     targetHour = 16.0f; break;
	default: break;
	}

	const UnsignedInt totalCycleFrames = (UnsignedInt)s_durationMinutes * 60u * (UnsignedInt)LOGICFRAMES_PER_SECOND;
	if (totalCycleFrames > 0 && TheGameLogic)
	{
		UnsignedInt currentFrame = TheGameLogic->getFrame();
		UnsignedInt wantedCyclePos = (UnsignedInt)WWMath::Round((targetHour / 24.0f) * (Real)totalCycleFrames);
		s_startOffsetFrames = (wantedCyclePos + totalCycleFrames - (currentFrame % totalCycleFrames)) % totalCycleFrames;
	}

	s_forceTerrainBake = TRUE;
}

void DayNightCycle_Update(UnsignedInt logicFrame)
{
	if (!s_initialized)
	{
		DayNightCycle_Init();
	}

	if (!s_cycleEnabled)
		return;

	if (!TheGameLogic || !TheGameLogic->isInGame())
		return;

	if (TheGameEngine && (TheGameEngine->isTimeFrozen() || TheGameEngine->isGameHalted()))
		return;

	const UnsignedInt totalCycleFrames = (UnsignedInt)s_durationMinutes * 60u * (UnsignedInt)LOGICFRAMES_PER_SECOND;
	if (totalCycleFrames == 0)
		return;

	UnsignedInt currentCycleFrame = (logicFrame + s_startOffsetFrames) % totalCycleFrames;
	Real normalizedTime = (Real)currentCycleFrame / (Real)totalCycleFrames;
	Real currentHour = normalizedTime * 24.0f;
	s_currentGameHour = currentHour;

	TimeOfDay keyA, keyB;
	Real alpha = 0.0f;
	TimeOfDay nominalTod = TIME_OF_DAY_MORNING;

	if (currentHour < 6.0f)
	{
		keyA = TIME_OF_DAY_MORNING;
		keyB = TIME_OF_DAY_AFTERNOON;
		alpha = currentHour / 6.0f;
		nominalTod = TIME_OF_DAY_MORNING;
	}
	else if (currentHour < 12.0f)
	{
		keyA = TIME_OF_DAY_AFTERNOON;
		keyB = TIME_OF_DAY_EVENING;
		alpha = (currentHour - 6.0f) / 6.0f;
		nominalTod = TIME_OF_DAY_AFTERNOON;
	}
	else if (currentHour < 16.0f)
	{
		keyA = TIME_OF_DAY_EVENING;
		keyB = TIME_OF_DAY_NIGHT;
		alpha = (currentHour - 12.0f) / 4.0f;
		nominalTod = TIME_OF_DAY_EVENING;
	}
	else
	{
		keyA = TIME_OF_DAY_NIGHT;
		keyB = TIME_OF_DAY_MORNING;
		alpha = (currentHour - 16.0f) / 8.0f;
		nominalTod = TIME_OF_DAY_NIGHT;
	}

	alpha = clamp(0.0f, alpha, 1.0f);

	Bool updateTerrainMesh = FALSE;

	// On nominal phase changes (e.g. crossing into night or day) or debug TOD toggles,
	// trigger drawable headlights, ambient audio, and re-bake CPU terrain vertex colors
	if (nominalTod != s_lastNominalTod || s_forceTerrainBake)
	{
		s_lastNominalTod = nominalTod;
		s_forceTerrainBake = FALSE;
		TheWritableGlobalData->m_timeOfDay = nominalTod;

		if (TheGameClient)
		{
			TheGameClient->setTimeOfDay(nominalTod);
		}

		updateTerrainMesh = TRUE;
	}

	GlobalData::TerrainLighting interpTerrain[MAX_GLOBAL_LIGHTS];
	GlobalData::TerrainLighting interpObjects[MAX_GLOBAL_LIGHTS];

	for (Int i = 0; i < MAX_GLOBAL_LIGHTS; ++i)
	{
		const GlobalData::TerrainLighting &tA = s_mapTerrainLighting[keyA][i];
		const GlobalData::TerrainLighting &tB = s_mapTerrainLighting[keyB][i];

		interpTerrain[i].diffuse.red   = tA.diffuse.red   * (1.0f - alpha) + tB.diffuse.red   * alpha;
		interpTerrain[i].diffuse.green = tA.diffuse.green * (1.0f - alpha) + tB.diffuse.green * alpha;
		interpTerrain[i].diffuse.blue  = tA.diffuse.blue  * (1.0f - alpha) + tB.diffuse.blue  * alpha;

		interpTerrain[i].ambient.red   = tA.ambient.red   * (1.0f - alpha) + tB.ambient.red   * alpha;
		interpTerrain[i].ambient.green = tA.ambient.green * (1.0f - alpha) + tB.ambient.green * alpha;
		interpTerrain[i].ambient.blue  = tA.ambient.blue  * (1.0f - alpha) + tB.ambient.blue  * alpha;

		Vector3 tDir = interpolateLightDirection(tA.lightPos, tB.lightPos, alpha);
		interpTerrain[i].lightPos.x = tDir.X;
		interpTerrain[i].lightPos.y = tDir.Y;
		interpTerrain[i].lightPos.z = tDir.Z;

		const GlobalData::TerrainLighting &oA = s_mapObjectsLighting[keyA][i];
		const GlobalData::TerrainLighting &oB = s_mapObjectsLighting[keyB][i];

		interpObjects[i].diffuse.red   = oA.diffuse.red   * (1.0f - alpha) + oB.diffuse.red   * alpha;
		interpObjects[i].diffuse.green = oA.diffuse.green * (1.0f - alpha) + oB.diffuse.green * alpha;
		interpObjects[i].diffuse.blue  = oA.diffuse.blue  * (1.0f - alpha) + oB.diffuse.blue  * alpha;

		interpObjects[i].ambient.red   = oA.ambient.red   * (1.0f - alpha) + oB.ambient.red   * alpha;
		interpObjects[i].ambient.green = oA.ambient.green * (1.0f - alpha) + oB.ambient.green * alpha;
		interpObjects[i].ambient.blue  = oA.ambient.blue  * (1.0f - alpha) + oB.ambient.blue  * alpha;

		Vector3 oDir = interpolateLightDirection(oA.lightPos, oB.lightPos, alpha);
		interpObjects[i].lightPos.x = oDir.X;
		interpObjects[i].lightPos.y = oDir.Y;
		interpObjects[i].lightPos.z = oDir.Z;

		TheWritableGlobalData->m_terrainDiffuse[i]  = interpTerrain[i].diffuse;
		TheWritableGlobalData->m_terrainAmbient[i]  = interpTerrain[i].ambient;
		TheWritableGlobalData->m_terrainLightPos[i] = interpTerrain[i].lightPos;
	}

	// TheSuperHackers @perf andytraber 08/09/2026 Decouple terrain vertex baking from continuous shadow cycle.
	// Directional shadow maps and 3D objects update smoothly on the GPU every frame at 60+ FPS.
	// Terrain vertex buffers on the CPU are only re-baked on major phase boundaries (or TOD toggles),
	// completely eliminating CPU stutter and restoring full gameplay framerate.
	if (TheDisplay)
	{
		TheDisplay->updateSceneLighting(interpObjects, updateTerrainMesh);
	}
}

Bool DayNightCycle_IsEnabled()
{
	return s_cycleEnabled;
}

Real DayNightCycle_GetCurrentGameHour()
{
	return s_currentGameHour;
}

TimeOfDay DayNightCycle_GetCurrentTimeOfDay()
{
	return s_lastNominalTod;
}

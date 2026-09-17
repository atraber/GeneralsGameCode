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
#include "Common/FrameTiming.h"
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

// Published for the F10 readout only. The cycle is otherwise write-only from the outside
// -- nothing in the engine asks it what time it is -- and a lighting change you cannot put
// a clock next to is a change you cannot attribute.
static Real s_currentAlpha = 0.0f;
static Vector3 s_currentLightDir(0.0f, 0.0f, -1.0f);
static Real s_currentMoonWeight = 0.0f;

// Cached map authored lighting for all four times of day
static GlobalData::TerrainLighting s_mapTerrainLighting[TIME_OF_DAY_COUNT][MAX_GLOBAL_LIGHTS];
static GlobalData::TerrainLighting s_mapObjectsLighting[TIME_OF_DAY_COUNT][MAX_GLOBAL_LIGHTS];

static Bool s_forceTerrainBake = FALSE;

// TheSuperHackers @fix andytraber 12/09/2026 Raised by the LOGIC step at a nominal
// time-of-day boundary and consumed by the client step on the next frame it draws. The two
// halves of a boundary happen on different threads now (see DayNightCycle_UpdateLogic), and
// this is the one thing that has to cross between them.
static Bool s_pendingTerrainBake = FALSE;

// Where the sun was on the last LOGIC step, for the per-drawable light switch (see WHEN THE LIGHTS
// COME ON). Kept apart from the client's copies above because the switch compares one logic frame
// against the one before it, and the client runs at a different rate.
static Bool s_lightsStateValid = FALSE;
static Real s_lightsHour = 0.0f;
static Real s_lightsSunElevationDeg = 0.0f;

static const Real PI_F       = 3.14159265f;
static const Real DEG_TO_RAD = PI_F / 180.0f;
static const Real RAD_TO_DEG = 180.0f / PI_F;

// ================================================================================================
// THE SUN AND MOON ORBIT
//
// TheSuperHackers @feature andytraber 12/09/2026 The light DIRECTION is analytic; the authored
// TerrainLighting tables are the COLOUR source and nothing else.
//
// Why it had to change. The four authored lightPos vectors run z = -0.65 to -0.89, which is 40
// to 63 degrees above the horizon, and ensureValidLighting then clamped anything flatter than
// z = -0.05 back to -0.70. A low sun was unreachable by construction: measured +52.9 degrees at
// nominal NIGHT, i.e. the "moon" sat where the midday sun would. Nightfall changed the colour
// and never the shadow geometry, which is the half of it people actually read.
//
// SPACE. Z is up, and lightPos is the direction the light RAY TRAVELS -- from the sky down
// towards the ground. So z is NEGATIVE while the body is above the horizon, and every consumer
// negates it to get "towards the light" (W3DView's two sun-frustum fits, W3DShaderManager,
// W3DVolumetricFog, W3DDisplay::updateSceneLighting). Getting that sign wrong lights the scene
// from underneath, which looks like a shading bug rather than a direction bug and costs a day.
//
// AZIMUTH is degrees about +Z measured from +X towards +Y. There is no compass on a Generals map,
// so these are not east and west; the numbers below were chosen to reproduce the sweep the
// authored tables already described -- morning 207 deg, afternoon 117 deg, evening 23 deg -- so
// midday shadows still fall roughly where each map's baked terrain lighting was authored for.
// ================================================================================================

// --- THE SUN (user requirement 1: a realistic orbit, rising and setting; requirement 2: shadows
// follow it). The daylight hours are [0, SUN_SET_HOUR) out of 24, which is the same 16h day / 8h
// night split the phase table in DayNightCycle_Init already assumes -- so sunrise is hour 0 and
// sunset is hour 16, and the two meet across the wrap with no seam.
static const Real SUN_SET_HOUR             = 16.0f;	///< hours 0..this are daylight; 2/3 of the cycle

// TheSuperHackers @tweak andytraber 13/09/2026 A TILTED ORBIT. The sun used to be a half sine in
// elevation (0..68..0) with the azimuth swept linearly, which is an equatorial sun: eight of the
// sixteen daylight hours above 48 degrees, where a shadow is under 0.9x its caster and its tip
// barely moves, and then all the lengthening crammed into the last hour. Measured on a 1-unit
// caster over a 20-minute cycle, the shadow tip moved 280x faster at dusk than at noon.
//
// Now the sun runs on the circle a real sun draws at a mid latitude: sin(el) = sin(lat)sin(dec) +
// cos(lat)cos(dec)cos(H). The hour angle H is remapped so that daylight still spans game hours
// 0..SUN_SET_HOUR and night the rest, so the 16h/8h split and every schedule keyed to it survive.
// The elevation keeps running below the horizon at night; the handover below keys on that.
static const Real SUN_LATITUDE_DEG         = 45.0f;	///< tilt of the orbit; peak elevation is 90 - lat + dec = 55 deg
static const Real SUN_DECLINATION_DEG      = 10.0f;	///< season: a little summer, so the day arc is wider than half a turn
static const Real SUN_NOON_AZIMUTH_DEG     = 120.0f;	///< where the sun stands at noon; rise lands ~225 and set ~15, the sweep the authored keys described (207 -> 23)

// --- THE MOON. The same 30..55 degree arc as before, but it now sets out from where the sun went
// down and ends where the sun comes up, travelling the far side of the sky. The light therefore
// changes elevation at the handover and hardly changes azimuth, instead of reversing.
static const Real MOON_MIN_ELEVATION_DEG   = 30.0f;	///< the floor. cot(30) = 1.73, so a moon shadow is never longer than ~1.7x its caster
static const Real MOON_PEAK_ELEVATION_DEG  = 55.0f;	///< at midnight

// --- THE HANDOVER: FADE OUT, SWAP, FADE IN. TheSuperHackers @tweak andytraber 13/09/2026
//
// It used to cross-fade the light DIRECTION from sun to moon over the last 10 degrees of sunset,
// with the moon roughly opposite the sun -- so the shadows swung round ~130 degrees at twenty
// times the sun's own rate. Now the SHADOWS fade instead. As the sun drops from SUN_SHADOW_FADE
// HIGH to LOW its shadows fade to nothing while still pointing where the sun is; the direction
// then moves to the moon only inside +/- LIGHT_SWAP_HALF_BAND_DEG of the horizon, where both
// shadow strengths are zero; and once the sun is MOON_SHADOW_FADE_LOW below the horizon the moon's
// shadows fade back in. Dawn is the same in reverse, for free, because it is all keyed to the
// sun's signed elevation. The swap band must stay inside both fade lows, or a swinging shadow
// shows.
//
// At the default 20-minute cycle: sun shadows fade over ~60 s, ~40 s with no cast shadows, moon
// shadows fade in over ~35 s (night hours pass faster than day hours).
//
// The flattest light published is ~4 degrees, reached while its shadows are gone. W3DView floors
// the draw-window widening at 10 degrees so that does not buy a wide window for invisible shadows.
static const Real SUN_SHADOW_FADE_LOW_DEG   = 4.0f;	///< sun shadows are gone at or below this sun elevation
static const Real SUN_SHADOW_FADE_HIGH_DEG  = 14.0f;	///< and at full strength at or above it
static const Real MOON_SHADOW_FADE_LOW_DEG  = 4.0f;	///< moon shadows start once the sun is this far BELOW the horizon
static const Real MOON_SHADOW_FADE_HIGH_DEG = 14.0f;	///< and are at full strength this far below
static const Real LIGHT_SWAP_HALF_BAND_DEG  = 4.0f;	///< the direction moves sun -> moon between +this and -this sun elevation

// --- NIGHT INTENSITY. TheSuperHackers @tweak andytraber 13/09/2026 There is no extra night dim any
// more. P7 multiplied diffuse by 0.55 and ambient by 0.85 under the moon, on top of the authored
// NIGHT colour, and judged by eye the night was too dark to play in. The authored NIGHT entry is
// the brightness again, as it was before P7.

// The invariant, not the mechanism. Several consumers assume a downward ray -- W3DWater treats
// -lightPos.z <= 0 as "no sun contribution", the terrain vertex bake dots it against upward
// normals -- so nothing from strictly below the horizon may ever be published. With the
// parameters above this never fires (the floor is about SUN_SHADOW_FADE_LOW_DEG, see the handover);
// it is here so that retuning any of them cannot quietly break the consumers.
static const Real HORIZON_MIN_ELEVATION_DEG = 2.0f;

// Hermite ease, clamped. The handover needs C1 continuity at BOTH ends: a linear ramp leaves a
// corner in the light's angular velocity at each end of the window, and a corner in the velocity
// of a shadow is exactly the "pop" a cross-fade is supposed to remove.
static Real smoothStep01(Real t)
{
	t = clamp(0.0f, t, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

// A light RAY from where the body sits in the sky. Note the negation: the vector points from the
// body towards the ground, per the SPACE note above.
static Vector3 rayFromAzimuthElevation(Real azimuthDeg, Real elevationDeg)
{
	const Real az = azimuthDeg * DEG_TO_RAD;
	const Real el = elevationDeg * DEG_TO_RAD;
	const Real cosEl = cosf(el);
	return Vector3(-cosEl * cosf(az), -cosEl * sinf(az), -sinf(el));
}

// Shortest-arc interpolation of two unit directions. Slerped and not lerped because a linear
// blend of two directions far apart dips towards the origin in the middle, and re-normalising
// that makes the result accelerate through the middle of the swing -- visible as the shadow
// whipping round at the handover, where the two bodies are over 100 degrees apart.
static Vector3 slerpUnit(const Vector3 &vA, const Vector3 &vB, Real alpha)
{
	Real dot = Vector3::Dot_Product(vA, vB);
	dot = clamp(-1.0f, dot, 1.0f);

	Vector3 result;
	if (dot > 0.9995f)
	{
		// Too close to share a well-conditioned great circle; a plain lerp is within float noise.
		result = vA * (1.0f - alpha) + vB * alpha;
	}
	else if (dot < -0.9995f)
	{
		// Antipodal: the great circle is not unique, so pick any perpendicular axis and rotate.
		Vector3 ortho(0.0f, 1.0f, 0.0f);
		if (fabsf(vA.Y) > 0.9f)
			ortho.Set(1.0f, 0.0f, 0.0f);
		Vector3 axis;
		Vector3::Cross_Product(vA, ortho, &axis);
		axis.Normalize();
		Matrix3D rot(axis, PI_F * alpha);
		result = rot.Rotate_Vector(vA);
	}
	else
	{
		const Real theta = acosf(dot);
		const Real sinTheta = sinf(theta);
		const Real wA = sinf((1.0f - alpha) * theta) / sinTheta;
		const Real wB = sinf(alpha * theta) / sinTheta;
		result = vA * wA + vB * wB;
	}

	result.Normalize();
	return result;
}

/// Where both bodies are, and which of them owns the light right now.
struct OrbitState
{
	Vector3 sunRay;				///< direction of travel of sunlight
	Vector3 moonRay;			///< direction of travel of moonlight
	Real    sunElevationDeg;	///< SIGNED: negative while the sun is below the horizon. sunRay itself never dips below it
	Real    moonElevationDeg;
	Real    sunAzimuthDeg;		///< kept so the handover can interpolate in spherical space -- see the publish site
	Real    moonAzimuthDeg;
	Real    moonWeight;			///< 0 = the sun owns the light direction, 1 = the moon does
	Real    shadowStrength;		///< 0..1, the cast-shadow fade of whichever body is up
};

// Degrees of azimuth the sun is past noon, positive in the afternoon, for hour angle H. The
// standard horizontal-coordinates formula, measured from the noon meridian.
static Real sunAzimuthFromNoonDeg(Real hourAngleRad, Real latRad, Real decRad)
{
	return atan2f(sinf(hourAngleRad),
	              cosf(hourAngleRad) * sinf(latRad) - tanf(decRad) * cosf(latRad)) * RAD_TO_DEG;
}

static void computeOrbit(Real currentHour, OrbitState &out)
{
	const Real lat = SUN_LATITUDE_DEG * DEG_TO_RAD;
	const Real dec = SUN_DECLINATION_DEG * DEG_TO_RAD;

	// THE SUN. The hour angle at which this orbit meets the horizon, then daylight hours mapped
	// linearly onto [-that, +that] and night hours onto the rest of the turn -- so the elevation is
	// continuous through the night, and exactly zero at hour 0 and at SUN_SET_HOUR.
	const Real halfDayDeg = acosf(clamp(-1.0f, -tanf(lat) * tanf(dec), 1.0f)) * RAD_TO_DEG;
	Real hourAngleDeg;
	if (currentHour < SUN_SET_HOUR)
		hourAngleDeg = -halfDayDeg + 2.0f * halfDayDeg * (currentHour / SUN_SET_HOUR);
	else
		hourAngleDeg = halfDayDeg + (360.0f - 2.0f * halfDayDeg) * ((currentHour - SUN_SET_HOUR) / (24.0f - SUN_SET_HOUR));
	const Real H = hourAngleDeg * DEG_TO_RAD;

	const Real sinEl = sinf(lat) * sinf(dec) + cosf(lat) * cosf(dec) * cosf(H);
	out.sunElevationDeg = asinf(clamp(-1.0f, sinEl, 1.0f)) * RAD_TO_DEG;
	// Game azimuth DEcreases through the day (the authored keys ran 207 -> 117 -> 23), hence the minus.
	out.sunAzimuthDeg = SUN_NOON_AZIMUTH_DEG - sunAzimuthFromNoonDeg(H, lat, dec);
	// The ray is never built from below the horizon; the handover has taken the light off the sun
	// long before, and nothing downstream may be handed an upward ray.
	out.sunRay = rayFromAzimuthElevation(out.sunAzimuthDeg, out.sunElevationDeg > 0.0f ? out.sunElevationDeg : 0.0f);

	// THE MOON. Its arc spans the night, from the sun's setting azimuth round the far side of the
	// sky to its rising azimuth. During the day it waits at whichever end the next or last handover
	// needs: at the setting point after noon, at the rising point before it.
	const Real setFromNoonDeg = sunAzimuthFromNoonDeg(halfDayDeg * DEG_TO_RAD, lat, dec);
	Real moonT;
	if (currentHour >= SUN_SET_HOUR)
		moonT = (currentHour - SUN_SET_HOUR) / (24.0f - SUN_SET_HOUR);
	else
		moonT = (currentHour < SUN_SET_HOUR * 0.5f) ? 1.0f : 0.0f;
	out.moonElevationDeg = MOON_MIN_ELEVATION_DEG
	                     + (MOON_PEAK_ELEVATION_DEG - MOON_MIN_ELEVATION_DEG) * sinf(PI_F * moonT);
	out.moonAzimuthDeg = (SUN_NOON_AZIMUTH_DEG - setFromNoonDeg) - moonT * (360.0f - 2.0f * setFromNoonDeg);
	out.moonRay = rayFromAzimuthElevation(out.moonAzimuthDeg, out.moonElevationDeg);

	// WHO OWNS THE LIGHT DIRECTION, and how strong its shadows are. See THE HANDOVER.
	out.moonWeight = 1.0f - smoothStep01(
		(out.sunElevationDeg + LIGHT_SWAP_HALF_BAND_DEG) / (2.0f * LIGHT_SWAP_HALF_BAND_DEG));
	const Real sunShadow = smoothStep01(
		(out.sunElevationDeg - SUN_SHADOW_FADE_LOW_DEG) / (SUN_SHADOW_FADE_HIGH_DEG - SUN_SHADOW_FADE_LOW_DEG));
	const Real moonShadow = smoothStep01(
		(-out.sunElevationDeg - MOON_SHADOW_FADE_LOW_DEG) / (MOON_SHADOW_FADE_HIGH_DEG - MOON_SHADOW_FADE_LOW_DEG));
	out.shadowStrength = (sunShadow > moonShadow) ? sunShadow : moonShadow;
}

// ================================================================================================
// THE COLOUR SCHEDULE
//
// TheSuperHackers @tweak andytraber 13/09/2026 The four authored colour keys follow the SUN now, not
// the clock. They used to sit at fixed hours -- AFTERNOON at 6, EVENING at 12, NIGHT at 16 -- which
// matched the old sine sun and not the tilted orbit: at hour 12 the sun was still 35 degrees up and
// the ground already brown, and by the time the light raked (sun under 14 degrees, hour ~14.3) the
// colour was most of the way to night blue. The long shadows and the warm light never coincided.
//
// Now, by sun elevation, with the morning key on the rising side of the day and the evening key on
// the setting side:
//
//   above COLOUR_HIGH_SUN_DEG          AFTERNOON
//   COLOUR_LOW_SUN_DEG .. HIGH         low-sun key <-> AFTERNOON   (eased)
//   horizon .. COLOUR_LOW_SUN_DEG      MORNING or EVENING, held -- the warm light and the rake together
//   COLOUR_NIGHT_FULL_DEG .. horizon   NIGHT <-> low-sun key       (eased; dusk continues after sunset)
//   below COLOUR_NIGHT_FULL_DEG        NIGHT
//
// With the orbit above that is roughly: afternoon 4..12, evening ramp 12..14.9, evening held
// 14.9..16, into night by 16.9, night held to 23.1, dawn to 0, morning held to 1.15, into afternoon by
// 4. Retuning the orbit moves all of it with the sun, as the lights and the shadow fade already do.
//
// The NOMINAL time of day (drawables, ambient audio, water textures, TheGlobalData->m_timeOfDay) is
// the key the colour is closer to: it changes where a blend passes its halfway point. That runs on
// the LOGIC thread, so it compares the clock against hours precomputed once from the orbit
// (computeColourSchedule) instead of evaluating the elevation there.
// ================================================================================================
static const Real COLOUR_HIGH_SUN_DEG   = 35.0f;	///< full afternoon colour at or above this sun elevation
static const Real COLOUR_LOW_SUN_DEG    = 10.0f;	///< full morning/evening colour between the horizon and this
static const Real COLOUR_NIGHT_FULL_DEG = -12.0f;	///< full night colour at or below this (negative: below the horizon)

// Hours at which the nominal time of day changes, ascending through the day. Filled by
// computeColourSchedule; the defaults are the old clock positions, used only if the orbit is never
// evaluated.
static Real s_hourAfternoonBegins = 6.0f;
static Real s_hourEveningBegins   = 12.0f;
static Real s_hourNightBegins     = 16.0f;
static Real s_hourMorningBegins   = 24.0f;
static Real s_hourNightFull       = 16.0f;

static Bool isMorningSideOfDay(Real hour)
{
	// Noon is halfway through the daylight hours and midnight halfway through the night.
	return hour < SUN_SET_HOUR * 0.5f || hour >= SUN_SET_HOUR + (24.0f - SUN_SET_HOUR) * 0.5f;
}

// Which two authored keys the colour sits between, and how far, for a sun at this elevation.
static void colourKeysFromSun(Real hour, Real sunElevationDeg, TimeOfDay &keyA, TimeOfDay &keyB, Real &alpha)
{
	const TimeOfDay lowSunKey = isMorningSideOfDay(hour) ? TIME_OF_DAY_MORNING : TIME_OF_DAY_EVENING;
	if (sunElevationDeg >= COLOUR_LOW_SUN_DEG)
	{
		keyA = lowSunKey;
		keyB = TIME_OF_DAY_AFTERNOON;
		alpha = smoothStep01((sunElevationDeg - COLOUR_LOW_SUN_DEG) / (COLOUR_HIGH_SUN_DEG - COLOUR_LOW_SUN_DEG));
	}
	else if (sunElevationDeg >= 0.0f)
	{
		keyA = lowSunKey;
		keyB = lowSunKey;
		alpha = 0.0f;
	}
	else
	{
		keyA = TIME_OF_DAY_NIGHT;
		keyB = lowSunKey;
		alpha = smoothStep01((sunElevationDeg - COLOUR_NIGHT_FULL_DEG) / (0.0f - COLOUR_NIGHT_FULL_DEG));
	}
}

// The hour in [loHour, hiHour] at which the sun reaches targetDeg. The span must be one on which the
// elevation only rises or only falls: [0, noon], [noon, midnight] or [midnight, 24].
static Real hourAtSunElevation(Real targetDeg, Real loHour, Real hiHour)
{
	OrbitState orbit;
	computeOrbit(loHour, orbit);
	const Bool belowAtLo = orbit.sunElevationDeg < targetDeg;
	for (Int i = 0; i < 32; ++i)
	{
		const Real mid = 0.5f * (loHour + hiHour);
		computeOrbit(mid, orbit);
		if ((orbit.sunElevationDeg < targetDeg) == belowAtLo)
			loHour = mid;
		else
			hiHour = mid;
	}
	return 0.5f * (loHour + hiHour);
}

static void computeColourSchedule()
{
	const Real noon = SUN_SET_HOUR * 0.5f;
	const Real midnight = SUN_SET_HOUR + (24.0f - SUN_SET_HOUR) * 0.5f;
	// smoothStep01 passes one half at the middle of its span, so these are the halfway points.
	const Real dayMidDeg = 0.5f * (COLOUR_LOW_SUN_DEG + COLOUR_HIGH_SUN_DEG);
	const Real nightMidDeg = 0.5f * COLOUR_NIGHT_FULL_DEG;

	s_hourAfternoonBegins = hourAtSunElevation(dayMidDeg, 0.0f, noon);
	s_hourEveningBegins   = hourAtSunElevation(dayMidDeg, noon, SUN_SET_HOUR);
	s_hourNightBegins     = hourAtSunElevation(nightMidDeg, SUN_SET_HOUR, midnight);
	s_hourMorningBegins   = hourAtSunElevation(nightMidDeg, midnight, 24.0f);
	s_hourNightFull       = hourAtSunElevation(COLOUR_NIGHT_FULL_DEG, SUN_SET_HOUR, midnight);

	DEBUG_LOG(("DAYNIGHT colour schedule: afternoon %.2f, evening %.2f, night %.2f (full %.2f), morning %.2f",
		s_hourAfternoonBegins, s_hourEveningBegins, s_hourNightBegins, s_hourNightFull, s_hourMorningBegins));
}

static TimeOfDay nominalTimeOfDayAtHour(Real hour)
{
	if (hour >= s_hourMorningBegins || hour < s_hourAfternoonBegins)
		return TIME_OF_DAY_MORNING;
	if (hour < s_hourEveningBegins)
		return TIME_OF_DAY_AFTERNOON;
	if (hour < s_hourNightBegins)
		return TIME_OF_DAY_EVENING;
	return TIME_OF_DAY_NIGHT;
}

// Where the clock starts when a map (or a script, or the debug toggle) asks for a time of day. Each
// lands inside that phase's nominal window, and where the colour is that key's: MORNING at sunrise,
// where the morning key is held; AFTERNOON where it always was; NIGHT where the night colour is full.
// EVENING is the exception and starts where the evening window opens rather than where its colour
// peaks, because from the peak it would be night within a couple of game hours.
static Real startHourForTimeOfDay(TimeOfDay tod)
{
	switch (tod)
	{
	case TIME_OF_DAY_MORNING:   return 0.0f;
	case TIME_OF_DAY_AFTERNOON: return 6.0f;
	case TIME_OF_DAY_EVENING:   return s_hourEveningBegins + 0.05f;	// a margin so frame rounding cannot land a frame early
	case TIME_OF_DAY_NIGHT:     return s_hourNightFull;
	default:                    return 6.0f;
	}
}

// Table-driven direction interpolation, which is now used ONLY for the fill lights at index >= 1.
// Light 0 is the sun/moon and takes its direction from the orbit above; the fills are authored as
// a fixed rig relative to the map and there is nothing analytic to say about where they point.
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

	Vector3 result = slerpUnit(vA, vB, alpha);

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
		// Map did not author lighting for this time of day, supply defaults based on phase.
		//
		// TheSuperHackers @feature andytraber 12/09/2026 The lightPos values in here are no longer
		// read for light 0 -- the orbit supplies that direction now -- but they are kept rather
		// than deleted because they are the only record of the direction each phase was authored
		// to be lit from, and the colours beside them, which ARE still live, were tuned against
		// them. Anyone retuning SUN_NOON_AZIMUTH_DEG should read them first.
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
			s_mapTerrainLighting[tod][0].ambient.red = 0.12f; s_mapTerrainLighting[tod][0].ambient.green = 0.14f; s_mapTerrainLighting[tod][0].ambient.blue = 0.20f;
			s_mapTerrainLighting[tod][0].diffuse.red = 0.25f; s_mapTerrainLighting[tod][0].diffuse.green = 0.30f; s_mapTerrainLighting[tod][0].diffuse.blue = 0.45f;
			s_mapTerrainLighting[tod][0].lightPos.x = -0.40f; s_mapTerrainLighting[tod][0].lightPos.y = 0.50f;  s_mapTerrainLighting[tod][0].lightPos.z = -0.77f;
			break;
		}
		s_mapObjectsLighting[tod][0] = s_mapTerrainLighting[tod][0];
	}

	// TheSuperHackers @feature andytraber 12/09/2026 The clamp now starts at index 1, and that is
	// the whole of the P7 rework of it.
	//
	// It exists to stop authored data producing a light from below the horizon, and it did that by
	// snapping z back to -0.70 -- 44 degrees up. Applied to index 0 it was also the thing that
	// made a low sun unreachable, so now that light 0's direction is analytic (see the orbit block
	// above) the clamp must not be allowed to fight it. Deleting it outright would be wrong,
	// though: the fill lights at index >= 1 still take their direction from these tables, and the
	// reason the floor was wanted has not gone away. So the floor stays exactly where it is for
	// them, and light 0 is exempt because its direction no longer comes from here at all.
	//
	// Light 0's equivalent floor lives at the publish site instead (HORIZON_MIN_ELEVATION_DEG),
	// where it can be stated as an angle rather than as a magic z.
	for (Int i = 1; i < MAX_GLOBAL_LIGHTS; ++i)
	{
		if (s_mapTerrainLighting[tod][i].lightPos.z > -0.05f)
			s_mapTerrainLighting[tod][i].lightPos.z = -0.70f;
		if (s_mapObjectsLighting[tod][i].lightPos.z > -0.05f)
			s_mapObjectsLighting[tod][i].lightPos.z = -0.70f;
	}
}

// ================================================================================================
// THE LOOK
//
// TheSuperHackers @tweak andytraber 14/09/2026 The authored keys were blended exactly as the map
// wrote them, and a map's four keys are not a day/night set. Measured over one cycle on Lights Out,
// a night map: MORNING is a byte copy of NIGHT, whose ambient is (0.09, 0.16, 0.50) -- three times
// the blue of the afternoon's, flat and unshadowed -- and EVENING is a dim brown. The scene's mean
// level ran noon 136, sunset 50, sunrise 44, night 68-87: the night was brighter than the sunset,
// the dawn got DARKER as the sun rose, and at golden hour the snow turned to mud. The cycle has to
// own the brightness; the map can only lend it a colour cast.
//
// Two layers:
//  1. REPAIRED KEYS, at Init. A daytime key that is a copy of NIGHT, or no stronger than it, is
//     rebuilt from the afternoon key -- warmed for morning and evening. An afternoon that is itself
//     night-like falls back to a neutral daylight row.
//  2. THE ENVELOPE, every frame. After the keys are blended, light 0's ambient and diffuse are
//     scaled so that their brightest channel sits at a fraction of the afternoon key's, chosen by
//     sun elevation (LOOK_ANCHORS), keeping the blended hue. The same table desaturates the night,
//     warms the sun and cools the sky at low sun, sets the tone map exposure (a bright day), and
//     says how far into night the scene is (night bloom threshold, and the units' ambient lift that
//     keeps them readable against a dark ground).
//
// W3D_DAYNIGHT_LOOK=0 turns both layers off, so the look can be A/B'd on one binary.
// ================================================================================================
struct LookAnchor
{
	Real elevationDeg;
	Real ambient;		///< brightest ambient channel, as a fraction of the afternoon key's
	Real diffuse;		///< brightest diffuse channel, as a fraction of the afternoon key's
	Real desaturate;	///< 0..1 pull of the ambient towards grey (half of it for the diffuse)
	Real sunWarmth;		///< 0..1 of SUN_WARM_TINT multiplied into the diffuse
	Real skyCool;		///< 0..1 of SKY_COOL_TINT multiplied into the ambient
	Real exposure;		///< tone map exposure, linear
	Real night;			///< 0 = day, 1 = full night
	Real fill;			///< the fill lights' diffuse, relative to the sun's own scale
};

// Tuned 14/09/2026 against the published values (DAYNIGHT publish), not by eye alone:
//  - FILL. Lights Out's two night fills sum to 0.41 blue against the moon's 0.32, so scaling them
//    with the sun left the night ground lit mostly by saturated blue fills. At night they are cut to a
//    third of the sun's scale, so the moon is the light that reads.
//  - AMBIENT ABOVE 1 AT LOW SUN. At 10 degrees flat ground takes sin(10) = 0.17 of the sun, so golden
//    hour is carried by the sky; with the afternoon's ambient it measured darker than the night.
static const LookAnchor LOOK_ANCHORS[] =
{
	//  sun el    amb    dif    desat  warm   cool   expo   night  fill
	{ -12.0f,   0.45f, 0.30f, 0.50f, 0.00f, 0.00f, 1.00f, 1.00f, 0.35f },	// full night: dark, the moon clearly the strongest light
	{   0.0f,   1.00f, 0.65f, 0.15f, 0.50f, 0.60f, 1.00f, 0.30f, 0.80f },	// horizon: the sky still lit
	{  10.0f,   1.35f, 1.00f, 0.00f, 0.60f, 0.60f, 1.12f, 0.00f, 1.00f },	// golden hour: warm sun, bright cool shade
	{  35.0f,   1.00f, 1.00f, 0.00f, 0.25f, 0.25f, 1.25f, 0.00f, 1.00f },	// day
};
static const Int  LOOK_ANCHOR_COUNT = sizeof(LOOK_ANCHORS) / sizeof(LOOK_ANCHORS[0]);
static const Real SUN_WARM_TINT[3] = { 1.00f, 0.90f, 0.75f };
static const Real SKY_COOL_TINT[3] = { 0.88f, 0.95f, 1.12f };
static const Real NIGHT_UNIT_AMBIENT_BOOST = 1.5f;	///< units' ambient at full night, relative to the ground's
static const Real LOOK_SCALE_MIN = 0.25f;			///< the envelope never scales a colour further than this...
static const Real LOOK_SCALE_MAX = 2.0f;			///< ...either way, so a black or blown-out key stays recognisable

static Bool s_lookEnabled = TRUE;
static Real s_refAmbientMax[2] = { 0.22f, 0.22f };	///< [0] terrain set, [1] objects set: the afternoon key's brightest channel
static Real s_refDiffuseMax[2] = { 1.0f, 1.0f };

static LookAnchor lookAt(Real elevationDeg)
{
	if (elevationDeg <= LOOK_ANCHORS[0].elevationDeg)
		return LOOK_ANCHORS[0];
	for (Int i = 1; i < LOOK_ANCHOR_COUNT; ++i)
	{
		const LookAnchor &b = LOOK_ANCHORS[i];
		if (elevationDeg < b.elevationDeg)
		{
			const LookAnchor &a = LOOK_ANCHORS[i - 1];
			const Real t = smoothStep01((elevationDeg - a.elevationDeg) / (b.elevationDeg - a.elevationDeg));
			LookAnchor r;
			r.elevationDeg = elevationDeg;
			r.ambient    = a.ambient    + (b.ambient    - a.ambient)    * t;
			r.diffuse    = a.diffuse    + (b.diffuse    - a.diffuse)    * t;
			r.desaturate = a.desaturate + (b.desaturate - a.desaturate) * t;
			r.sunWarmth  = a.sunWarmth  + (b.sunWarmth  - a.sunWarmth)  * t;
			r.skyCool    = a.skyCool    + (b.skyCool    - a.skyCool)    * t;
			r.exposure   = a.exposure   + (b.exposure   - a.exposure)   * t;
			r.night      = a.night      + (b.night      - a.night)      * t;
			r.fill       = a.fill       + (b.fill       - a.fill)       * t;
			return r;
		}
	}
	return LOOK_ANCHORS[LOOK_ANCHOR_COUNT - 1];
}

static Real maxChannel(const RGBColor &c)
{
	Real m = c.red;
	if (c.green > m) m = c.green;
	if (c.blue > m) m = c.blue;
	return m;
}

static void setColour(RGBColor &c, Real r, Real g, Real b)
{
	c.red = r; c.green = g; c.blue = b;
}

static void scaleColour(RGBColor &c, Real s)
{
	c.red *= s; c.green *= s; c.blue *= s;
}

static void tintColour(RGBColor &c, const Real tint[3], Real amount)
{
	c.red   *= 1.0f + (tint[0] - 1.0f) * amount;
	c.green *= 1.0f + (tint[1] - 1.0f) * amount;
	c.blue  *= 1.0f + (tint[2] - 1.0f) * amount;
}

static void desaturateColour(RGBColor &c, Real amount)
{
	const Real grey = (c.red + c.green + c.blue) / 3.0f;
	c.red   += (grey - c.red)   * amount;
	c.green += (grey - c.green) * amount;
	c.blue  += (grey - c.blue)  * amount;
}

static Real lookScale(Real current, Real target)
{
	if (current < 1e-4f)
		return 1.0f;
	return clamp(LOOK_SCALE_MIN, target / current, LOOK_SCALE_MAX);
}

// Light 0, the sun or moon. Returns the diffuse scale so the fill lights can follow it.
static Real applyLookToSun(GlobalData::TerrainLighting &l, Int set, const LookAnchor &look)
{
	desaturateColour(l.ambient, look.desaturate);
	desaturateColour(l.diffuse, look.desaturate * 0.5f);
	tintColour(l.diffuse, SUN_WARM_TINT, look.sunWarmth);
	tintColour(l.ambient, SKY_COOL_TINT, look.skyCool);
	scaleColour(l.ambient, lookScale(maxChannel(l.ambient), s_refAmbientMax[set] * look.ambient));
	const Real diffuseScale = lookScale(maxChannel(l.diffuse), s_refDiffuseMax[set] * look.diffuse);
	scaleColour(l.diffuse, diffuseScale);
	return diffuseScale;
}

// Fill lights have no brightness of their own to aim for; they follow the sun's scale, cut further at
// night by look.fill, and desaturate as fully as the sky does.
static void applyLookToFill(GlobalData::TerrainLighting &l, const LookAnchor &look, Real diffuseScale)
{
	desaturateColour(l.diffuse, look.desaturate);
	scaleColour(l.diffuse, diffuseScale * look.fill);
}

static Bool sameColour(const RGBColor &a, const RGBColor &b)
{
	return fabsf(a.red - b.red) < 0.01f && fabsf(a.green - b.green) < 0.01f && fabsf(a.blue - b.blue) < 0.01f;
}

static Real keyStrength(const GlobalData::TerrainLighting &l)
{
	return maxChannel(l.ambient) + maxChannel(l.diffuse);
}

static Bool keyIsNightLike(const GlobalData::TerrainLighting &key, const GlobalData::TerrainLighting &night)
{
	if (sameColour(key.ambient, night.ambient) && sameColour(key.diffuse, night.diffuse))
		return TRUE;
	return keyStrength(key) <= keyStrength(night);
}

// Colours only; light 0's direction is the orbit's, and the fills keep theirs.
static void repairKeys(GlobalData::TerrainLighting table[][MAX_GLOBAL_LIGHTS], const char *setName)
{
	const GlobalData::TerrainLighting &night = table[TIME_OF_DAY_NIGHT][0];

	GlobalData::TerrainLighting &afternoon = table[TIME_OF_DAY_AFTERNOON][0];
	if (keyIsNightLike(afternoon, night))
	{
		setColour(afternoon.ambient, 0.22f, 0.22f, 0.24f);
		setColour(afternoon.diffuse, 1.00f, 0.97f, 0.90f);
		DEBUG_LOG(("DAYNIGHT look: %s AFTERNOON key is night-like, replaced by neutral daylight", setName));
	}

	static const Real MORNING_AMBIENT_TINT[3] = { 0.95f, 0.95f, 1.10f };
	static const Real MORNING_DIFFUSE_TINT[3] = { 0.95f, 0.78f, 0.60f };
	GlobalData::TerrainLighting &morning = table[TIME_OF_DAY_MORNING][0];
	if (keyIsNightLike(morning, night))
	{
		morning.ambient = afternoon.ambient;
		morning.diffuse = afternoon.diffuse;
		tintColour(morning.ambient, MORNING_AMBIENT_TINT, 1.0f);
		tintColour(morning.diffuse, MORNING_DIFFUSE_TINT, 1.0f);
		DEBUG_LOG(("DAYNIGHT look: %s MORNING key is night-like, rebuilt from AFTERNOON", setName));
	}

	static const Real EVENING_AMBIENT_TINT[3] = { 0.90f, 0.90f, 1.10f };
	static const Real EVENING_DIFFUSE_TINT[3] = { 0.90f, 0.63f, 0.40f };
	GlobalData::TerrainLighting &evening = table[TIME_OF_DAY_EVENING][0];
	if (keyIsNightLike(evening, night))
	{
		evening.ambient = afternoon.ambient;
		evening.diffuse = afternoon.diffuse;
		tintColour(evening.ambient, EVENING_AMBIENT_TINT, 1.0f);
		tintColour(evening.diffuse, EVENING_DIFFUSE_TINT, 1.0f);
		DEBUG_LOG(("DAYNIGHT look: %s EVENING key is night-like, rebuilt from AFTERNOON", setName));
	}
}

void DayNightCycle_Init()
{
	OptionPreferences prefs;
	s_durationMinutes = prefs.getDayNightCycleDuration();

	computeColourSchedule();

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

		const GlobalData::TerrainLighting &t = s_mapTerrainLighting[tod][0];
		const GlobalData::TerrainLighting &o = s_mapObjectsLighting[tod][0];
		DEBUG_LOG(("DAYNIGHT key %d: terrain amb %.3f %.3f %.3f dif %.3f %.3f %.3f | objects amb %.3f %.3f %.3f dif %.3f %.3f %.3f",
			tod, t.ambient.red, t.ambient.green, t.ambient.blue, t.diffuse.red, t.diffuse.green, t.diffuse.blue,
			o.ambient.red, o.ambient.green, o.ambient.blue, o.diffuse.red, o.diffuse.green, o.diffuse.blue));
	}

	// THE LOOK: repair the keys, then take the reference levels from the (repaired) afternoon.
	const char *lookEnv = ::getenv("W3D_DAYNIGHT_LOOK");
	s_lookEnabled = !(lookEnv != nullptr && ::atoi(lookEnv) == 0);
	if (s_lookEnabled)
	{
		repairKeys(s_mapTerrainLighting, "terrain");
		repairKeys(s_mapObjectsLighting, "objects");
	}
	s_refAmbientMax[0] = max(maxChannel(s_mapTerrainLighting[TIME_OF_DAY_AFTERNOON][0].ambient), 0.05f);
	s_refDiffuseMax[0] = max(maxChannel(s_mapTerrainLighting[TIME_OF_DAY_AFTERNOON][0].diffuse), 0.30f);
	s_refAmbientMax[1] = max(maxChannel(s_mapObjectsLighting[TIME_OF_DAY_AFTERNOON][0].ambient), 0.05f);
	s_refDiffuseMax[1] = max(maxChannel(s_mapObjectsLighting[TIME_OF_DAY_AFTERNOON][0].diffuse), 0.30f);
	DEBUG_LOG(("DAYNIGHT look %s: reference ambient %.3f / %.3f, diffuse %.3f / %.3f (terrain / objects)",
		s_lookEnabled ? "ON" : "OFF", s_refAmbientMax[0], s_refAmbientMax[1], s_refDiffuseMax[0], s_refDiffuseMax[1]));

	// Daylight is hours 0..SUN_SET_HOUR (16h, 2/3) and night the rest; where each nominal phase
	// begins and ends follows the sun -- see THE COLOUR SCHEDULE.
	const Real startHour = startHourForTimeOfDay(s_initialTod);

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
	s_pendingTerrainBake = FALSE;
	s_lightsStateValid = FALSE;
	s_currentGameHour = startHour;
}

void DayNightCycle_Reset()
{
	s_initialized = FALSE;
	s_cycleEnabled = FALSE;
	s_lightsStateValid = FALSE;
	if (TheWritableGlobalData)
	{
		TheWritableGlobalData->m_sunShadowStrength = 1.0f;
		TheWritableGlobalData->m_sceneExposure = 1.0f;
		TheWritableGlobalData->m_nightWeight = 0.0f;
	}
	s_startOffsetFrames = 0;
	s_lastNominalTod = TIME_OF_DAY_INVALID;
	s_forceTerrainBake = FALSE;
	s_pendingTerrainBake = FALSE;
}

void DayNightCycle_SetTimeOfDay(TimeOfDay tod)
{
	if (tod < TIME_OF_DAY_FIRST || tod >= TIME_OF_DAY_COUNT)
		return;

	const Real targetHour = startHourForTimeOfDay(tod);

	const UnsignedInt totalCycleFrames = (UnsignedInt)s_durationMinutes * 60u * (UnsignedInt)LOGICFRAMES_PER_SECOND;
	if (totalCycleFrames > 0 && TheGameLogic)
	{
		UnsignedInt currentFrame = TheGameLogic->getFrame();
		UnsignedInt wantedCyclePos = (UnsignedInt)WWMath::Round((targetHour / 24.0f) * (Real)totalCycleFrames);
		s_startOffsetFrames = (wantedCyclePos + totalCycleFrames - (currentFrame % totalCycleFrames)) % totalCycleFrames;
	}

	s_forceTerrainBake = TRUE;
}

// Where the cycle is, from the logic frame alone. Both halves derive their answer from
// this, so the client's interpolation and the logic's boundary test can never disagree
// about what time it is.
static Bool computeCycleState(UnsignedInt logicFrame, TimeOfDay &nominalTod, Real &currentHour)
{
	if (!s_initialized)
	{
		DayNightCycle_Init();
	}

	if (!s_cycleEnabled)
		return FALSE;

	if (!TheGameLogic || !TheGameLogic->isInGame())
		return FALSE;

	if (TheGameEngine && (TheGameEngine->isTimeFrozen() || TheGameEngine->isGameHalted()))
		return FALSE;

	const UnsignedInt totalCycleFrames = (UnsignedInt)s_durationMinutes * 60u * (UnsignedInt)LOGICFRAMES_PER_SECOND;
	if (totalCycleFrames == 0)
		return FALSE;

	UnsignedInt currentCycleFrame = (logicFrame + s_startOffsetFrames) % totalCycleFrames;
	Real normalizedTime = (Real)currentCycleFrame / (Real)totalCycleFrames;
	currentHour = normalizedTime * 24.0f;
	nominalTod = nominalTimeOfDayAtHour(currentHour);
	return TRUE;
}

// ================================================================================================
// WHEN THE LIGHTS COME ON
//
// TheSuperHackers @feature andytraber 13/09/2026 MODELCONDITION_NIGHT is what turns a vehicle's or a
// building's lights on: the HEADLIGHT sub-objects un-hide, the bone-parented spot and point lights in
// W3DModelDraw::updateModelDynamicLights follow it, and night model states swap in. It used to be set
// on every drawable in the same frame at the nominal NIGHT boundary -- hour 16, with the sun already
// on the horizon -- and cleared on all of them at once at hour 0.
//
// Now each drawable has its own threshold inside a band of SUN ELEVATION, so the lights come on while
// it is getting dark, roughly together but one operator after another, and go off the same way at
// dawn. Elevation rather than the clock so that retuning the orbit moves the switch with the light it
// answers to. The threshold is a pure function of the drawable's ID: nothing is stored per drawable,
// and the same drawable always reacts at the same moment.
//
// At the default 20-minute cycle the dusk band spans about 60 seconds of wall clock, dawn about 45.
// ================================================================================================
static const Real LIGHTS_ON_FIRST_DEG  = 14.0f;	///< dusk: the most attentive operators switch on at this sun elevation...
static const Real LIGHTS_ON_LAST_DEG   = 3.0f;	///< ...and the last have by the time the sun is this low
static const Real LIGHTS_OFF_FIRST_DEG = 4.0f;	///< dawn: the first switch off once the sun clears this...
static const Real LIGHTS_OFF_LAST_DEG  = 16.0f;	///< ...and the last once it clears this
static const Real LIGHTS_BAND_TOP_DEG  = 16.0f;	///< above this at both ends of a step, no drawable can change its mind

// 0..1, triangular (the mean of two hashes), so most operators react near the middle of the band and
// a few noticeably early or late. A uniform spread reads as a mechanical ramp.
static Real lightsJitter01(DrawableID id, UnsignedInt salt)
{
	UnsignedInt x = (UnsignedInt)id * 0x9E3779B1u + salt * 0x85EBCA77u;
	x ^= x >> 16; x *= 0x7feb352du;
	x ^= x >> 15; x *= 0x846ca68bu;
	x ^= x >> 16;
	const Real a = (Real)(x & 0xFFFFu) / 65535.0f;
	const Real b = (Real)((x >> 16) & 0xFFFFu) / 65535.0f;
	return 0.5f * (a + b);
}

static Bool drawableWantsNight(DrawableID id, Real hour, Real sunElevationDeg)
{
	if (sunElevationDeg <= 0.0f)
		return TRUE;

	// The first half of the daylight hours is the sun climbing, so it is dawn's band that applies.
	// Separate salts so an operator who is late to switch on is not also late to switch off.
	if (hour < SUN_SET_HOUR * 0.5f)
	{
		const Real off = LIGHTS_OFF_FIRST_DEG + (LIGHTS_OFF_LAST_DEG - LIGHTS_OFF_FIRST_DEG) * lightsJitter01(id, 2u);
		return sunElevationDeg < off;
	}

	const Real on = LIGHTS_ON_FIRST_DEG + (LIGHTS_ON_LAST_DEG - LIGHTS_ON_FIRST_DEG) * lightsJitter01(id, 1u);
	return sunElevationDeg < on;
}

// The time of day a drawable is told. A drawable whose lights are still on is told NIGHT whatever the
// nominal phase says, and one whose lights are off is never told NIGHT -- setTimeOfDay derives the
// model condition from exactly that comparison.
static TimeOfDay drawableTimeOfDay(Bool night, TimeOfDay nominalTod)
{
	if (night)
		return TIME_OF_DAY_NIGHT;
	return (nominalTod == TIME_OF_DAY_NIGHT) ? TIME_OF_DAY_EVENING : nominalTod;
}

// The LEVEL sweep, at a nominal boundary: every drawable is told, as GameClient::setTimeOfDay always
// did -- ambient sounds key on the phase as well as on night, so skipping unchanged drawables here
// would leave them on the old phase. replaceModelConditionFlags is a no-op for an unchanged flag.
//
// That this runs only at boundaries and the per-frame sweep only acts on a drawable's own crossing is
// deliberate. A map can author a single object as always-day or always-night (Object.cpp,
// TheKey_objectTime), and a level sweep every frame would erase that on the first frame of the match;
// the boundaries already overrode it before this change, four times a cycle, and still do.
Bool DayNightCycle_SyncDrawables(TimeOfDay nominalTod)
{
	if (!s_cycleEnabled || !s_lightsStateValid || !TheGameClient)
		return FALSE;

	for (Drawable *draw = TheGameClient->firstDrawable(); draw; draw = draw->getNextDrawable())
	{
		const Bool night = drawableWantsNight(draw->getID(), s_lightsHour, s_lightsSunElevationDeg);
		draw->setTimeOfDay(drawableTimeOfDay(night, nominalTod));
	}
	return TRUE;
}

Bool DayNightCycle_DrawableWantsNight(DrawableID id, Bool &night)
{
	if (!s_cycleEnabled || !s_lightsStateValid)
		return FALSE;
	night = drawableWantsNight(id, s_lightsHour, s_lightsSunElevationDeg);
	return TRUE;
}

// The EDGE sweep, every logic step: only drawables whose own threshold the sun crossed since the last
// step are touched. A drawable created mid-band takes the global phase like any new drawable and falls
// into line at the next boundary -- a unit rolled out at dusk has not had its lights switched on yet.
static void syncDrawablesCrossingThreshold(Real prevHour, Real prevElevationDeg,
                                           Real hour, Real elevationDeg, TimeOfDay nominalTod)
{
	if (!TheGameClient)
		return;
	if (prevElevationDeg >= LIGHTS_BAND_TOP_DEG && elevationDeg >= LIGHTS_BAND_TOP_DEG)
		return;
	if (prevElevationDeg <= 0.0f && elevationDeg <= 0.0f)
		return;

	for (Drawable *draw = TheGameClient->firstDrawable(); draw; draw = draw->getNextDrawable())
	{
		const DrawableID id = draw->getID();
		const Bool night = drawableWantsNight(id, hour, elevationDeg);
		if (night == drawableWantsNight(id, prevHour, prevElevationDeg))
			continue;
		if (draw->getModelConditionFlags().test(MODELCONDITION_NIGHT) == night)
			continue;
		draw->setTimeOfDay(drawableTimeOfDay(night, nominalTod));
	}
}

// THE BOUNDARY HALF, AND IT MUST RUN ON THE LOGIC THREAD.
//
// TheSuperHackers @fix andytraber 12/09/2026 This used to sit inside the client update
// below, and that is what crashed the game about thirteen minutes into every match --
// see the day/night cycle cost investigation for the stack and the locals.
//
// setTimeOfDay puts MODELCONDITION_NIGHT on every drawable, which walks into
// W3DModelDraw::setModelState -> ModelConditionInfo::validateWeaponBarrelInfo(). That
// function refuses to do anything unless isValidTimeToCalcLogicStuff() -- i.e. unless
// TheGameLogic is inside its own update -- and it returns WITHOUT setting BARRELS_VALID,
// leaving the barrel vector empty. rebuildWeaponRecoilInfo, called immediately after,
// then sizes the per-barrel recoil vector from that empty vector. The logic side later
// fills the barrels for real and nothing re-runs the rebuild, so the next shot indexes a
// two-barrel weapon into a zero-length recoil vector.
//
// Nothing here is cosmetic-only work that could stay on the client: the model condition
// change IS logic-adjacent state, and the engine says so by refusing to compute it
// anywhere else. The smooth interpolation, which is genuinely cosmetic and costs 0.006ms,
// stays in the client update below.
void DayNightCycle_UpdateLogic(UnsignedInt logicFrame)
{
	FRAME_TIMING_SCOPE(PHASE_DAYNIGHT);

	TimeOfDay nominalTod;
	Real currentHour;
	if (!computeCycleState(logicFrame, nominalTod, currentHour))
		return;

	// The sun's elevation for the light switch. Pure arithmetic over the hour, the same call the
	// client half makes, so both halves agree about where the sun is.
	OrbitState orbit;
	computeOrbit(currentHour, orbit);

	const Bool hadLightsState = s_lightsStateValid;
	const Real prevHour = s_lightsHour;
	const Real prevElevationDeg = s_lightsSunElevationDeg;
	s_lightsHour = currentHour;
	s_lightsSunElevationDeg = orbit.sunElevationDeg;
	s_lightsStateValid = TRUE;

	// On nominal phase changes (e.g. crossing into night or day) or debug TOD toggles,
	// trigger drawable headlights, ambient audio, and re-bake CPU terrain vertex colors
	if (nominalTod != s_lastNominalTod || s_forceTerrainBake)
	{
		s_lastNominalTod = nominalTod;
		s_forceTerrainBake = FALSE;
		FrameTiming::recordEvent(FrameTiming::EVENT_TOD_CHANGE);
		TheWritableGlobalData->m_timeOfDay = nominalTod;

		// Still the one call: water, shadows and the display take the phase from here, and the
		// drawables are routed back through DayNightCycle_SyncDrawables.
		if (TheGameClient)
		{
			TheGameClient->setTimeOfDay(nominalTod);
		}

		s_pendingTerrainBake = TRUE;
	}
	else if (hadLightsState)
	{
		syncDrawablesCrossingThreshold(prevHour, prevElevationDeg, currentHour, orbit.sunElevationDeg, nominalTod);
	}
}

// The cosmetic half: interpolate the sun and hand it to the display, every rendered frame.
void DayNightCycle_Update(UnsignedInt logicFrame)
{
	FRAME_TIMING_SCOPE(PHASE_DAYNIGHT);

	TimeOfDay nominalTod;
	Real currentHour;
	if (!computeCycleState(logicFrame, nominalTod, currentHour))
		return;

	s_currentGameHour = currentHour;

	// TheSuperHackers @feature andytraber 12/09/2026 The orbit, and the one place it is evaluated.
	// This belongs on the CLIENT side with the rest of the interpolation: it is pure arithmetic
	// over the logic frame number, it changes no model condition and touches nothing the logic
	// thread owns. The boundary work that DOES touch logic state is in DayNightCycle_UpdateLogic
	// and must stay there -- moving it here is what crashed the game at nightfall.
	OrbitState orbit;
	computeOrbit(currentHour, orbit);

	// (13/09/2026: the moon now sets out from where the sun went down, and the swing happens only
	// while both shadow strengths are zero, so the azimuth gap described below is a few degrees now,
	// not 150. The argument for interpolating the two angles separately still holds.)
	//
	// The light that is actually published. ELEVATION AND AZIMUTH SEPARATELY, and not a slerp of
	// the two rays, which is what this did first and which was wrong in a way only the log showed.
	//
	// The bodies are about 150 degrees apart in azimuth at dusk, because the moon is roughly
	// opposite the sun. The great circle between two such directions does not stay low -- it arcs
	// up over the pole -- so slerping them made the published elevation climb during the handover
	// instead of falling. Measured on the track: at dusk it went +30.7 -> +35.2 degrees as the
	// blend opened, and at dawn it reached +60.7 while both bodies were below +40. The shadows got
	// SHORTER as the sun set, which is precisely backwards.
	//
	// Interpolating the two angles instead keeps the elevation monotonic between the two bodies
	// (so dusk falls to the sun's flattest and then rises to the moon's floor, in that order) and
	// takes the azimuth the short way round, which is the horizontal sweep a real dusk has.
	const Real blendElevationDeg = orbit.sunElevationDeg
		+ (orbit.moonElevationDeg - orbit.sunElevationDeg) * orbit.moonWeight;
	// Shortest signed way round, so the sweep never takes the 210-degree path when a 150-degree
	// one exists.
	Real azimuthDeltaDeg = orbit.moonAzimuthDeg - orbit.sunAzimuthDeg;
	while (azimuthDeltaDeg > 180.0f)  azimuthDeltaDeg -= 360.0f;
	while (azimuthDeltaDeg < -180.0f) azimuthDeltaDeg += 360.0f;
	const Real blendAzimuthDeg = orbit.sunAzimuthDeg + azimuthDeltaDeg * orbit.moonWeight;

	Vector3 lightRay = rayFromAzimuthElevation(blendAzimuthDeg, blendElevationDeg);

	// The horizon invariant. See HORIZON_MIN_ELEVATION_DEG -- with the current parameters this
	// cannot fire, and it is here so that changing them cannot silently break the consumers that
	// assume a downward ray.
	const Real horizonMinZ = -sinf(HORIZON_MIN_ELEVATION_DEG * DEG_TO_RAD);
	if (lightRay.Z > horizonMinZ)
	{
		lightRay.Z = horizonMinZ;
		lightRay.Normalize();
	}

	s_currentLightDir = lightRay;
	s_currentMoonWeight = orbit.moonWeight;
	// TheSuperHackers @feature andytraber 13/09/2026 The cast-shadow fade; W3DView hands it to every
	// receiver as the shadow strength.
	TheWritableGlobalData->m_sunShadowStrength = orbit.shadowStrength;

	// The colour, from the sun's elevation -- see THE COLOUR SCHEDULE.
	TimeOfDay keyA, keyB;
	Real alpha;
	colourKeysFromSun(currentHour, orbit.sunElevationDeg, keyA, keyB, alpha);
	s_currentAlpha = alpha;

	const LookAnchor look = lookAt(orbit.sunElevationDeg);
	Real terrainDiffuseScale = 1.0f;
	Real objectsDiffuseScale = 1.0f;

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

		// Light 0 is the sun/moon and takes the analytic direction; the fills keep theirs from the
		// tables. Note that the TERRAIN and the OBJECTS set now get the IDENTICAL vector for light
		// 0, which they did not before: the shadow map and the shader sun read
		// m_terrainLightPos[0] while the decal shadows and the scene lights are driven from
		// m_terrainObjectsLighting[0], and a map that authored those two differently had its cast
		// shadows and its shading disagreeing about where the sun was.
		if (i == 0)
		{
			interpTerrain[i].lightPos.x = lightRay.X;
			interpTerrain[i].lightPos.y = lightRay.Y;
			interpTerrain[i].lightPos.z = lightRay.Z;
		}
		else
		{
			const Vector3 tDir = interpolateLightDirection(tA.lightPos, tB.lightPos, alpha);
			interpTerrain[i].lightPos.x = tDir.X;
			interpTerrain[i].lightPos.y = tDir.Y;
			interpTerrain[i].lightPos.z = tDir.Z;
		}

		const GlobalData::TerrainLighting &oA = s_mapObjectsLighting[keyA][i];
		const GlobalData::TerrainLighting &oB = s_mapObjectsLighting[keyB][i];

		interpObjects[i].diffuse.red   = oA.diffuse.red   * (1.0f - alpha) + oB.diffuse.red   * alpha;
		interpObjects[i].diffuse.green = oA.diffuse.green * (1.0f - alpha) + oB.diffuse.green * alpha;
		interpObjects[i].diffuse.blue  = oA.diffuse.blue  * (1.0f - alpha) + oB.diffuse.blue  * alpha;

		interpObjects[i].ambient.red   = oA.ambient.red   * (1.0f - alpha) + oB.ambient.red   * alpha;
		interpObjects[i].ambient.green = oA.ambient.green * (1.0f - alpha) + oB.ambient.green * alpha;
		interpObjects[i].ambient.blue  = oA.ambient.blue  * (1.0f - alpha) + oB.ambient.blue  * alpha;

		if (i == 0)
		{
			interpObjects[i].lightPos.x = lightRay.X;
			interpObjects[i].lightPos.y = lightRay.Y;
			interpObjects[i].lightPos.z = lightRay.Z;
		}
		else
		{
			const Vector3 oDir = interpolateLightDirection(oA.lightPos, oB.lightPos, alpha);
			interpObjects[i].lightPos.x = oDir.X;
			interpObjects[i].lightPos.y = oDir.Y;
			interpObjects[i].lightPos.z = oDir.Z;
		}

		// THE LOOK -- see the block above DayNightCycle_Init. Light 0 is processed first, so the fills
		// can take its diffuse scale.
		if (s_lookEnabled)
		{
			if (i == 0)
			{
				terrainDiffuseScale = applyLookToSun(interpTerrain[i], 0, look);
				objectsDiffuseScale = applyLookToSun(interpObjects[i], 1, look);
			}
			else
			{
				applyLookToFill(interpTerrain[i], look, terrainDiffuseScale);
				applyLookToFill(interpObjects[i], look, objectsDiffuseScale);
			}
		}

		TheWritableGlobalData->m_terrainDiffuse[i]  = interpTerrain[i].diffuse;
		TheWritableGlobalData->m_terrainAmbient[i]  = interpTerrain[i].ambient;
		TheWritableGlobalData->m_terrainLightPos[i] = interpTerrain[i].lightPos;

		// TheSuperHackers @fix andytraber 12/09/2026 ...and the objects set, which nothing used
		// to write. Trees and props read it (W3DTreeBuffer, W3DPropBuffer); without this they
		// sample the authored m_terrainObjectsLighting[tod] and snap between four states at the
		// boundaries while everything around them creeps.
		TheWritableGlobalData->m_terrainObjectsCurrent[i] = interpObjects[i];
	}

	// TheSuperHackers @perf andytraber 08/09/2026 Decouple terrain vertex baking from continuous shadow cycle.
	// Directional shadow maps and 3D objects update smoothly on the GPU every frame at 60+ FPS.
	// Terrain vertex buffers on the CPU are only re-baked on major phase boundaries (or TOD toggles),
	// completely eliminating CPU stutter and restoring full gameplay framerate.
	TheWritableGlobalData->m_sceneExposure = s_lookEnabled ? look.exposure : 1.0f;
	TheWritableGlobalData->m_nightWeight = s_lookEnabled ? look.night : 0.0f;

	// The units' ambient is lifted at night so they stay readable against a dark ground. Only the copy
	// handed to the scene, which is what lights units: m_terrainObjectsCurrent above also lights trees
	// and props, and those glowing against the ground would read as wrong rather than as readable.
	if (s_lookEnabled)
		scaleColour(interpObjects[0].ambient, 1.0f + (NIGHT_UNIT_AMBIENT_BOOST - 1.0f) * look.night);

	// What was actually published, every 150 logic frames, so a retune is read off the numbers rather
	// than off a screenshot.
	static UnsignedInt s_lastLookLogFrame = 0xFFFFFFFFu;
	if (logicFrame % 150u == 0u && logicFrame != s_lastLookLogFrame)
	{
		s_lastLookLogFrame = logicFrame;
		const GlobalData *gd = TheGlobalData;
		DEBUG_LOG(("DAYNIGHT publish f%u hour %.2f sun %.1f keys %d->%d a%.2f | expo %.2f night %.2f | lights %d | "
			"amb %.3f %.3f %.3f | dif0 %.3f %.3f %.3f | dif1 %.3f %.3f %.3f | dif2 %.3f %.3f %.3f | unitAmb %.3f %.3f %.3f",
			logicFrame, currentHour, orbit.sunElevationDeg, (Int)keyA, (Int)keyB, alpha, gd->m_sceneExposure, gd->m_nightWeight,
			gd->m_numGlobalLights,
			gd->m_terrainAmbient[0].red, gd->m_terrainAmbient[0].green, gd->m_terrainAmbient[0].blue,
			gd->m_terrainDiffuse[0].red, gd->m_terrainDiffuse[0].green, gd->m_terrainDiffuse[0].blue,
			gd->m_terrainDiffuse[1].red, gd->m_terrainDiffuse[1].green, gd->m_terrainDiffuse[1].blue,
			gd->m_terrainDiffuse[2].red, gd->m_terrainDiffuse[2].green, gd->m_terrainDiffuse[2].blue,
			interpObjects[0].ambient.red, interpObjects[0].ambient.green, interpObjects[0].ambient.blue));
	}

	if (TheDisplay)
	{
		TheDisplay->updateSceneLighting(interpObjects, s_pendingTerrainBake);
		s_pendingTerrainBake = FALSE;
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

Real DayNightCycle_GetBlendAlpha()
{
	return s_currentAlpha;
}

Int DayNightCycle_GetDurationMinutes()
{
	return s_durationMinutes;
}

// Degrees above the horizon, from the PUBLISHED light *ray*. lightPos points from sky to
// ground (z negative above the horizon -- see the SPACE note on the orbit block), so the
// elevation is the negated z of the normalized direction. Worth having on screen next to the
// hour: the hour says where the cycle thinks it is and the elevation says where the shadows
// actually are, and the two have disagreed before -- that disagreement is precisely what P7
// was. Note this reports the ACTIVE body, and during the handover neither body exactly: it is
// the blend, which is what the shadows are doing.
Real DayNightCycle_GetSunElevationDegrees()
{
	Vector3 d = s_currentLightDir;
	const Real len = d.Length();
	if (len < 1e-4f)
		return 0.0f;
	d /= len;
	return asinf(clamp(-1.0f, -d.Z, 1.0f)) * RAD_TO_DEG;
}

// Degrees about +Z from +X towards +Y, of the direction TOWARDS the active body -- so it reads
// as "where the light is coming from" and not "where it is going". Wrapped to [0, 360).
// Elevation alone cannot tell a sunrise from a sunset, and the azimuth is also the half that
// says which way a shadow points, which is the thing a screenshot is being checked against.
Real DayNightCycle_GetLightAzimuthDegrees()
{
	const Vector3 &d = s_currentLightDir;
	if (d.Length2() < 1e-8f)
		return 0.0f;
	Real deg = atan2f(-d.Y, -d.X) * RAD_TO_DEG;
	if (deg < 0.0f)
		deg += 360.0f;
	return deg;
}

Real DayNightCycle_GetMoonBlend()
{
	return s_currentMoonWeight;
}

Bool DayNightCycle_IsMoonLit()
{
	return s_currentMoonWeight >= 0.5f;
}

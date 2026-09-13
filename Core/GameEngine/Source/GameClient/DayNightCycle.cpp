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
static const Real SUN_PEAK_ELEVATION_DEG   = 68.0f;	///< solar noon. The authored afternoon key was 63 deg, so midday barely moves
static const Real SUN_RISE_AZIMUTH_DEG     = 210.0f;	///< where it comes up; matches the authored morning key (207 deg)
static const Real SUN_AZIMUTH_SWEEP_DEG    = -180.0f;	///< half a turn across the day, in the direction the authored keys swept

// --- THE MOON (requirements 3, 5, 6, 7, 8). Its own arc, deliberately wider than the night and
// phase-shifted earlier, plus a hard floor on elevation.
static const Real MOON_RISE_HOUR           = 13.0f;	///< req 6: up three hours BEFORE the sun sets at 16
static const Real MOON_SET_HOUR            = 26.0f;	///< req 7: two hours past sunrise at hour 24, so it is still up when the sun returns
static const Real MOON_MIN_ELEVATION_DEG   = 30.0f;	///< req 8: the floor. cot(30) = 1.73, so a moon shadow is never longer than ~1.7x its caster
static const Real MOON_PEAK_ELEVATION_DEG  = 55.0f;	///< req 6: and it still has 25 degrees of arc to travel, so it visibly moves
static const Real MOON_RISE_AZIMUTH_DEG    = 210.0f;	///< the same arc as the sun, just phase-shifted; so it rises where the sun rises
static const Real MOON_AZIMUTH_SWEEP_DEG   = -180.0f;

// --- THE HANDOVER (requirement 3, and the no-pop half of requirement 6). Driven by the SUN'S
// ELEVATION and deliberately not by the clock, because the two things that must be bounded -- how
// long the longest shadow gets, and how fast the direction swings -- are both functions of
// elevation. Below SUN_HANDOVER_LOW_DEG the moon owns the light completely; above
// SUN_HANDOVER_HIGH_DEG the sun does; in between they are slerped.
//
// Three consequences, and the second is the trade-off these two numbers ARE:
//
//  - the handover is automatically symmetric at dawn, with no second window and no hour-wrap
//    arithmetic, because the sun's elevation is symmetric about noon;
//
//  - SUN_HANDOVER_HIGH_DEG IS the flattest direction the cycle ever publishes. Above it the light
//    is the pure sun, so the minimum is reached exactly at it; below it the blend pulls towards a
//    moon that is always at least MOON_MIN_ELEVATION_DEG up, which takes the light back UP rather
//    than down.
//
//    RETUNED 22 -> 10 AFTER MEASURING, and this is the important entry in this note. At 22 the
//    logged track over two full cycles never published a light below +21.7 degrees and the sun's
//    lowest elevation as the active body was +25.6 -- so the sun did not set, which is the exact
//    fault this whole feature exists to remove. The model swept 0..68..0 correctly; the handover
//    simply took the light off it while it was still high, and the 22 was then documented as a
//    feature rather than recognised as the bug. At 10 degrees, cot(10) = 5.7, so the flattest
//    light in the cycle casts a shadow nearly six times its caster and reads unmistakably as
//    sunset, against 1.17x for the best the authored tables could ever do.
//
//    It does NOT go lower, and the reason is a real limit rather than caution: at 4 degrees
//    cot is 14.3, and a shadow fourteen times its caster's height exceeds what the shadow map
//    can cover even at LOW_ANGLE_DRAW_WIDTH -- it would be clipped, not drawn, and the draw
//    window would pay for the attempt. 10 is where the geometry stops paying off;
//
//  - but the window WIDTH is what sets how fast the direction swings, and the two bodies are over
//    130 degrees apart in azimuth at dusk because the moon is roughly opposite the sun -- so the
//    handover really does reverse the shadow direction, as dusk does in life, and all these
//    numbers can choose is how long it takes. Measured over the whole cycle at a 20-minute
//    setting, for a 20-unit caster (a terrain cell is 10 units):
//
//        lo / hi      longest shadow    peak direction rate    peak shadow-tip speed
//         6 / 18         3.1 x h           4.04 deg/s               3.84 units/s
//         4 / 22         2.5 x h           2.47 deg/s               1.82 units/s     <-- chosen
//         3 / 24         2.2 x h           1.99 deg/s               1.32 units/s
//         2 / 30         1.8 x h           1.54 deg/s               0.79 units/s
//
//    against 0.225 deg/s for the sun's own unhurried sweep. 4/22 keeps twice the authored rake
//    while holding the dusk swing to about a fifth of a terrain cell per second over roughly 35
//    seconds of wall clock. It is a sweep, not a pop, and it is the one part of this worth putting
//    eyes on before it ships.
//    The width is narrower than the table above explored (10 degrees of elevation rather than 18),
//    so the swing is correspondingly quicker: at the DEFAULT 20-minute cycle the dusk handover
//    takes about 37 seconds of wall clock, and at the 3-minute setting the test harness uses it
//    takes about 6. The 3-minute figure is a harness artefact and not what a player sees; judge
//    the swing at the default.
static const Real SUN_HANDOVER_LOW_DEG     = 0.0f;	///< the sun contributes nothing once it is ON the horizon -- "once the sun sets, the shadows follow the moon", literally
static const Real SUN_HANDOVER_HIGH_DEG    = 10.0f;	///< at or above this the moon contributes nothing; also the flattest light in the cycle, cot(10) = 5.7x caster

// --- NIGHT INTENSITY (requirement 4). The hue still comes from the authored NIGHT entry; this is
// a single scale on top of it, keyed to the same moon weight that owns the direction, so the dim
// and the swing happen together. Diffuse is cut much harder than ambient on purpose: ambient is
// the floor that keeps unlit geometry readable, and a night nobody can play in is not a feature.
//
// WHERE THIS BITES, and it is the reason these two are the numbers most likely to need a look
// sign-off. Most shipped maps author all four times of day, so the thing being scaled is that
// map's own NIGHT entry and 0.55 is a straightforward "half as bright again". But a map that
// authored NOTHING falls back to the defaults in ensureValidLighting, whose NIGHT row was already
// deliberately calibrated dark, and the two then STACK: diffuse lands at 0.083/0.099/0.143 and
// ambient at 0.026/0.030/0.047, which is about a twelfth of the afternoon diffuse and a tenth of
// its ambient. If night turns out unplayable it will be on one of those maps, and the fix is this
// constant and not the fallback table.
static const Real MOON_DIFFUSE_SCALE       = 0.55f;
static const Real MOON_AMBIENT_SCALE       = 0.85f;

// The invariant, not the mechanism. Several consumers assume a downward ray -- W3DWater treats
// -lightPos.z <= 0 as "no sun contribution", the terrain vertex bake dots it against upward
// normals -- so nothing from strictly below the horizon may ever be published. With the
// parameters above this never fires (the floor is SUN_HANDOVER_HIGH_DEG, see the note there);
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
	Real    sunElevationDeg;	///< 0 while the sun is down; never negative, see below
	Real    moonElevationDeg;
	Real    sunAzimuthDeg;		///< kept so the handover can interpolate in spherical space -- see the publish site
	Real    moonAzimuthDeg;
	Real    moonWeight;			///< 0 = the sun owns the light, 1 = the moon does
};

static void computeOrbit(Real currentHour, OrbitState &out)
{
	// THE SUN. One half sine over the daylight hours, so it leaves and meets the horizon exactly
	// at the two ends and peaks at solar noon in the middle. Outside [0, SUN_SET_HOUR) the
	// parameter is CLAMPED rather than allowed to run on, which pins the sun to the horizon at
	// the point it set (or is about to rise from) instead of carrying it below. Nothing reads a
	// sub-horizon sun -- the handover has taken the light off it long before -- and a direction
	// that is never used but is quietly invalid is a trap for the next reader.
	Real dayT = clamp(0.0f, currentHour / SUN_SET_HOUR, 1.0f);
	out.sunElevationDeg = SUN_PEAK_ELEVATION_DEG * sinf(PI_F * dayT);
	out.sunAzimuthDeg   = SUN_RISE_AZIMUTH_DEG + dayT * SUN_AZIMUTH_SWEEP_DEG;
	out.sunRay = rayFromAzimuthElevation(out.sunAzimuthDeg, out.sunElevationDeg);

	// THE MOON. The same shape of arc with its own window, shifted so that it is already well up
	// before the sun goes (requirement 6) and still up after the sun comes back (requirement 7).
	// The sine rides on MOON_MIN_ELEVATION_DEG rather than starting at zero, so the moon never
	// approaches the horizon at all -- which is requirement 8 expressed as geometry rather than
	// as a clamp, and is also why it cannot "set" during the night however long the night is.
	//
	// The window is a superset of the hours in which the moon has any weight, and that containment
	// is an invariant rather than a coincidence: weight needs sunElevation < SUN_HANDOVER_HIGH_DEG,
	// which at 10 degrees is hours [15.25, 24) and [0, 0.75), and the arc covers [13, 24) and
	// [0, 2.0). So the clamps below are belt and braces, never the behaviour. Raise
	// SUN_HANDOVER_HIGH_DEG and MOON_SET_HOUR has to follow it, or the moon spends the last minutes
	// of the dawn handover frozen at the end of its arc.
	Real moonHour = currentHour;
	if (moonHour < MOON_RISE_HOUR)
		moonHour += 24.0f;
	const Real moonT = clamp(0.0f, (moonHour - MOON_RISE_HOUR) / (MOON_SET_HOUR - MOON_RISE_HOUR), 1.0f);
	out.moonElevationDeg = MOON_MIN_ELEVATION_DEG
	                     + (MOON_PEAK_ELEVATION_DEG - MOON_MIN_ELEVATION_DEG) * sinf(PI_F * moonT);
	out.moonAzimuthDeg = MOON_RISE_AZIMUTH_DEG + moonT * MOON_AZIMUTH_SWEEP_DEG;
	out.moonRay = rayFromAzimuthElevation(out.moonAzimuthDeg, out.moonElevationDeg);

	// WHO OWNS THE LIGHT. See the SUN_HANDOVER_* note: elevation, not clock.
	out.moonWeight = 1.0f - smoothStep01(
		(out.sunElevationDeg - SUN_HANDOVER_LOW_DEG) / (SUN_HANDOVER_HIGH_DEG - SUN_HANDOVER_LOW_DEG));
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
		// them. Anyone retuning SUN_RISE_AZIMUTH_DEG should read them first.
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
	s_pendingTerrainBake = FALSE;
	s_currentGameHour = startHour;
}

void DayNightCycle_Reset()
{
	s_initialized = FALSE;
	s_cycleEnabled = FALSE;
	s_startOffsetFrames = 0;
	s_lastNominalTod = TIME_OF_DAY_INVALID;
	s_forceTerrainBake = FALSE;
	s_pendingTerrainBake = FALSE;
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

// Where the cycle is, from the logic frame alone. Both halves derive their answer from
// this, so the client's interpolation and the logic's boundary test can never disagree
// about what time it is.
static Bool computeCycleState(UnsignedInt logicFrame, TimeOfDay &keyA, TimeOfDay &keyB,
                              Real &alpha, TimeOfDay &nominalTod, Real &currentHour)
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

	keyA = TIME_OF_DAY_MORNING;
	keyB = TIME_OF_DAY_AFTERNOON;
	alpha = 0.0f;
	nominalTod = TIME_OF_DAY_MORNING;

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
	return TRUE;
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

	TimeOfDay keyA, keyB, nominalTod;
	Real alpha, currentHour;
	if (!computeCycleState(logicFrame, keyA, keyB, alpha, nominalTod, currentHour))
		return;

	// On nominal phase changes (e.g. crossing into night or day) or debug TOD toggles,
	// trigger drawable headlights, ambient audio, and re-bake CPU terrain vertex colors
	if (nominalTod != s_lastNominalTod || s_forceTerrainBake)
	{
		s_lastNominalTod = nominalTod;
		s_forceTerrainBake = FALSE;
		FrameTiming::recordEvent(FrameTiming::EVENT_TOD_CHANGE);
		TheWritableGlobalData->m_timeOfDay = nominalTod;

		if (TheGameClient)
		{
			TheGameClient->setTimeOfDay(nominalTod);
		}

		s_pendingTerrainBake = TRUE;
	}
}

// The cosmetic half: interpolate the sun and hand it to the display, every rendered frame.
void DayNightCycle_Update(UnsignedInt logicFrame)
{
	FRAME_TIMING_SCOPE(PHASE_DAYNIGHT);

	TimeOfDay keyA, keyB, nominalTod;
	Real alpha, currentHour;
	if (!computeCycleState(logicFrame, keyA, keyB, alpha, nominalTod, currentHour))
		return;

	s_currentGameHour = currentHour;
	s_currentAlpha = alpha;

	// TheSuperHackers @feature andytraber 12/09/2026 The orbit, and the one place it is evaluated.
	// This belongs on the CLIENT side with the rest of the interpolation: it is pure arithmetic
	// over the logic frame number, it changes no model condition and touches nothing the logic
	// thread owns. The boundary work that DOES touch logic state is in DayNightCycle_UpdateLogic
	// and must stay there -- moving it here is what crashed the game at nightfall.
	OrbitState orbit;
	computeOrbit(currentHour, orbit);

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

	// KNOWN, DELIBERATE, AND NOT FIXED HERE: the colour schedule and the orbit are out of phase.
	//
	// The colour still comes from the four authored keys on the clock positions this file has always
	// used -- EVENING at hour 12, NIGHT at hour 16 -- while the orbit puts the sun at 48 degrees at
	// hour 12 and on the horizon at 16. So the evening orange peaks in what is geometrically
	// mid-afternoon, and by the time the light actually rakes (the handover, hours 14.3 to 15.4) the
	// colour is already 58 to 86 per cent of the way to the night key. The raking shadows and the
	// warm light do not coincide, which is the opposite of what makes an evening read.
	//
	// Moving the EVENING key to hour 14 and NIGHT to 15.5 would line them up and is a two-line
	// change to computeCycleState. It is NOT made here because it re-times every map's authored
	// colours, including the nominal time-of-day boundaries that drive headlights, ambient audio and
	// the terrain re-bake -- a separate behaviour change that deserves its own look sign-off rather
	// than being smuggled in under a direction change.

	// Requirement 4: night is dimmer, not merely bluer. Keyed to the same weight that owns the
	// direction so the dim and the swing arrive together, and applied as a scale on top of the
	// authored colour so a map that authored its own NIGHT entry keeps its hue.
	const Real diffuseScale = 1.0f - orbit.moonWeight * (1.0f - MOON_DIFFUSE_SCALE);
	const Real ambientScale = 1.0f - orbit.moonWeight * (1.0f - MOON_AMBIENT_SCALE);

	GlobalData::TerrainLighting interpTerrain[MAX_GLOBAL_LIGHTS];
	GlobalData::TerrainLighting interpObjects[MAX_GLOBAL_LIGHTS];

	for (Int i = 0; i < MAX_GLOBAL_LIGHTS; ++i)
	{
		const GlobalData::TerrainLighting &tA = s_mapTerrainLighting[keyA][i];
		const GlobalData::TerrainLighting &tB = s_mapTerrainLighting[keyB][i];

		interpTerrain[i].diffuse.red   = (tA.diffuse.red   * (1.0f - alpha) + tB.diffuse.red   * alpha) * diffuseScale;
		interpTerrain[i].diffuse.green = (tA.diffuse.green * (1.0f - alpha) + tB.diffuse.green * alpha) * diffuseScale;
		interpTerrain[i].diffuse.blue  = (tA.diffuse.blue  * (1.0f - alpha) + tB.diffuse.blue  * alpha) * diffuseScale;

		interpTerrain[i].ambient.red   = (tA.ambient.red   * (1.0f - alpha) + tB.ambient.red   * alpha) * ambientScale;
		interpTerrain[i].ambient.green = (tA.ambient.green * (1.0f - alpha) + tB.ambient.green * alpha) * ambientScale;
		interpTerrain[i].ambient.blue  = (tA.ambient.blue  * (1.0f - alpha) + tB.ambient.blue  * alpha) * ambientScale;

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

		interpObjects[i].diffuse.red   = (oA.diffuse.red   * (1.0f - alpha) + oB.diffuse.red   * alpha) * diffuseScale;
		interpObjects[i].diffuse.green = (oA.diffuse.green * (1.0f - alpha) + oB.diffuse.green * alpha) * diffuseScale;
		interpObjects[i].diffuse.blue  = (oA.diffuse.blue  * (1.0f - alpha) + oB.diffuse.blue  * alpha) * diffuseScale;

		interpObjects[i].ambient.red   = (oA.ambient.red   * (1.0f - alpha) + oB.ambient.red   * alpha) * ambientScale;
		interpObjects[i].ambient.green = (oA.ambient.green * (1.0f - alpha) + oB.ambient.green * alpha) * ambientScale;
		interpObjects[i].ambient.blue  = (oA.ambient.blue  * (1.0f - alpha) + oB.ambient.blue  * alpha) * ambientScale;

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

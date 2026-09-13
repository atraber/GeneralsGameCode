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

// TheSuperHackers @feature andytraber 29/08/2026 On-screen readout for FrameTiming.

#include "PreRTS.h"

#include "Common/FrameTiming.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/FrameTimingDisplay.h"
#include "GameClient/GameFont.h"
#include "GameClient/DayNightCycle.h"


#if defined(RTS_DEBUG)


enum { NUM_TIMING_LINES = 9 };

static DisplayString*	s_lines[NUM_TIMING_LINES] = { nullptr };
static UnsignedInt		s_lastTextUpdate = 0;

// The text is rebuilt four times a second. Every frame is unreadable, and the two second
// cadence the classic stats use is far too slow for a number you are actively tuning
// against -- by the time it moves you have forgotten what you changed. The graph below
// updates every frame regardless, which is where the fast detail belongs anyway.
static const UnsignedInt TEXT_UPDATE_INTERVAL_MS = 250;

static const Int GRAPH_X = 3;
static const Int GRAPH_HEIGHT = 48;
static const Int GRAPH_PAD = 4;


static void ensureLines()
{
	if (s_lines[0] != nullptr)
		return;

	GameFont* font = TheFontLibrary->getFont("FixedSys", 8, FALSE);
	for (Int i = 0; i < NUM_TIMING_LINES; ++i)
	{
		s_lines[i] = TheDisplayStringManager->newDisplayString();
		if (s_lines[i] != nullptr)
			s_lines[i]->setFont(font);
	}
}


void shutdownFrameTimingOverlay()
{
	for (Int i = 0; i < NUM_TIMING_LINES; ++i)
	{
		if (s_lines[i] != nullptr)
		{
			TheDisplayStringManager->freeDisplayString(s_lines[i]);
			s_lines[i] = nullptr;
		}
	}
}


/// The strip graph: one column per frame of work time, oldest at the left, with the frame
/// budget drawn across it. This is the half of the readout that answers "does it hitch",
/// which no average can -- a frame in twenty at triple the cost is plainly visible here and
/// invisible in a mean.
static void drawGraph( const FrameTiming::Snapshot& snap, Int x, Int y )
{
	Real history[FrameTiming::HISTORY_SIZE];
	const Int count = FrameTiming::getWorkHistory(history, FrameTiming::HISTORY_SIZE);
	if (count <= 0)
		return;

	const Int graphWidth = FrameTiming::HISTORY_SIZE;

	// Scaled to the window's worst frame, not to the budget. Anchoring to the budget was the
	// first instinct -- it keeps the bars in one place -- but at a 30fps cap with the frame
	// costing a third of its budget it squashed everything into the bottom quarter of the
	// graph and hid the variation the graph exists to show. The worst frame is stable enough
	// in practice: it only moves when a new worst arrives, and then holds for the whole
	// window. The headroom above it keeps a fresh spike from being clipped flat.
	Real scaleMs = snap.workMaxMs * 1.15f;
	if (scaleMs < 0.001f)
		scaleMs = 1.0f;

	// Batched: this is a couple of hundred rectangles and it is drawn inside the very phase
	// it is reporting, so unbatched it would add its own draw calls to the number on screen.
	// Restored rather than simply ended, in case the caller was already batching.
	const Bool wasBatching = TheDisplay->isBatching();
	if (!wasBatching)
		TheDisplay->beginBatch();

	// Backdrop, so the bars read over terrain as well as over sky.
	TheDisplay->drawFillRect(x - 1, y - 1, graphWidth + 2, GRAPH_HEIGHT + 2,
		GameMakeColor(0, 0, 0, 140));

	for (Int i = 0; i < count; ++i)
	{
		const Real ms = history[i];
		Int barHeight = REAL_TO_INT(ms / scaleMs * GRAPH_HEIGHT);
		barHeight = max(1, min(GRAPH_HEIGHT, barHeight));

		UnsignedInt color;
		if (snap.budgetMs > 0.0f && ms > snap.budgetMs)
			color = GameMakeColor(255, 64, 48, 255);			// missed the budget
		else if (snap.budgetMs > 0.0f && ms > snap.budgetMs * 0.75f)
			color = GameMakeColor(255, 208, 64, 255);			// inside the budget, no margin
		else
			color = GameMakeColor(96, 224, 128, 255);

		TheDisplay->drawFillRect(x + i, y + GRAPH_HEIGHT - barHeight, 1, barHeight, color);
	}

	// The budget line appears only once the frame is close enough to it to fit on the graph,
	// which is exactly when it is worth seeing. Far below budget it would pin to the top edge
	// and say nothing.
	if (snap.budgetMs > 0.0f)
	{
		const Int budgetY = y + GRAPH_HEIGHT - REAL_TO_INT(snap.budgetMs / scaleMs * GRAPH_HEIGHT);
		if (budgetY >= y && budgetY < y + GRAPH_HEIGHT)
			TheDisplay->drawFillRect(x, budgetY, graphWidth, 1, GameMakeColor(255, 255, 255, 160));
	}

	if (!wasBatching)
		TheDisplay->endBatch();
}


static Int s_overlayEnabled = -1;	///< -1 until the environment has been asked

void toggleFrameTimingOverlay()
{
	s_overlayEnabled = isFrameTimingOverlayEnabled() ? 0 : 1;
}

Bool isFrameTimingOverlayEnabled()
{
	// W3D_FRAME_TIMING_OVERLAY=1 brings the readout up already on. An unattended run has
	// nobody to press F10 and does not acquire the keyboard anyway, so without this the
	// only way to see the overlay in a captured frame was to hack the toggle and rebuild.
	if (s_overlayEnabled < 0)
	{
		const char* env = ::getenv("W3D_FRAME_TIMING_OVERLAY");
		s_overlayEnabled = (env != nullptr && ::atoi(env) > 0) ? 1 : 0;
	}
	return s_overlayEnabled != 0;
}



// TheSuperHackers @instrument andytraber 12/09/2026 The unattended half of the readout.
// Everything above is drawn for somebody looking at the screen. This writes the same
// figures to the log on a timer, so a replay run can be left alone for the twenty minutes
// a day-night cycle takes and still be readable afterwards -- which is the only way to
// see a cost that arrives with nightfall.
void logFrameTimingSnapshot()
{
	static Int s_intervalMs = -1;
	if (s_intervalMs < 0)
	{
		const char* env = ::getenv("W3D_FRAME_TIMING_LOG");
		const Int seconds = (env != nullptr) ? ::atoi(env) : 0;
		s_intervalMs = (seconds > 0) ? (seconds * 1000) : 0;
	}
	if (s_intervalMs == 0)
		return;

	static UnsignedInt s_lastLog = 0;
	const UnsignedInt now = timeGetTime();
	if (s_lastLog != 0 && (now - s_lastLog) < (UnsignedInt)s_intervalMs)
		return;
	s_lastLog = now;

	FrameTiming::Snapshot snap;
	FrameTiming::getSnapshot(snap);
	if (snap.sampleCount == 0)
		return;

	DEBUG_LOG(("FRAME TIMING: work %.2fms p95 %.2f max %.2f | wait %.2f | budget %.2f | n=%d",
		snap.workMs, snap.workP95Ms, snap.workMaxMs, snap.waitMs, snap.budgetMs, snap.sampleCount));

	// Mean and worst side by side for every phase, because the two say different things and
	// the phases that matter here are only visible in one of them.
	for (Int p = 0; p < FrameTiming::PHASE_COUNT; ++p)
	{
		if (snap.phaseMs[p] < 0.005f && snap.phaseMaxMs[p] < 0.5f)
			continue;
		DEBUG_LOG(("  phase %-10s mean %7.3f ms   worst %8.2f ms",
			FrameTiming::getPhaseName((FrameTiming::Phase)p), snap.phaseMs[p], snap.phaseMaxMs[p]));
	}

	for (Int c = 0; c < FrameTiming::COUNTER_COUNT; ++c)
	{
		if (snap.counterLast[c] == 0 && snap.counterMax[c] == 0)
			continue;
		DEBUG_LOG(("  count %-12s %8d   peak %8d",
			FrameTiming::getCounterName((FrameTiming::Counter)c), snap.counterLast[c], snap.counterMax[c]));
	}

	// Every event, by name, so adding one does not silently miss the unattended readout --
	// which is exactly what happened to the terrdirty causes the first time round.
	for (Int e = 0; e < FrameTiming::EVENT_COUNT; ++e)
	{
		DEBUG_LOG(("  event %-18s %u",
			FrameTiming::getEventName((FrameTiming::Event)e), snap.eventCount[e]));
	}

	if (DayNightCycle_IsEnabled())
	{
		// TheSuperHackers @instrument andytraber 12/09/2026 Body, elevation and azimuth, because a
		// replay run has nobody at the keyboard and this log is the only record of where the
		// analytic sun actually was at the moment a cost or an artefact appeared.
		const Real hour = DayNightCycle_GetCurrentGameHour();
		DEBUG_LOG(("  daynight  %02d:%02d  tod %d  blend %.3f  body %s  el %+.1f deg  az %.1f deg  moonblend %.3f  cycle %d min",
			(Int)hour, (Int)((hour - (Real)(Int)hour) * 60.0f),
			(Int)DayNightCycle_GetCurrentTimeOfDay(),
			DayNightCycle_GetBlendAlpha(),
			DayNightCycle_IsMoonLit() ? "moon" : "sun",
			DayNightCycle_GetSunElevationDegrees(),
			DayNightCycle_GetLightAzimuthDegrees(),
			DayNightCycle_GetMoonBlend(),
			DayNightCycle_GetDurationMinutes()));
	}
	else
	{
		DEBUG_LOG(("  daynight  off"));
	}
}


void drawFrameTimingOverlay( Int topY )
{
	if (TheDisplay == nullptr || TheDisplayStringManager == nullptr || TheFontLibrary == nullptr)
		return;

	ensureLines();
	if (s_lines[0] == nullptr)
		return;

	FrameTiming::Snapshot snap;
	FrameTiming::getSnapshot(snap);
	if (snap.sampleCount == 0)
		return;

	const UnsignedInt now = timeGetTime();
	if (now - s_lastTextUpdate >= TEXT_UPDATE_INTERVAL_MS || s_lastTextUpdate == 0)
	{
		s_lastTextUpdate = now;

		UnicodeString line;

		// Headline: the cost, the tail, and how much of the budget is going spare. The
		// idle percentage is the number the plain FPS counter cannot give you -- capped at
		// 30 it reads 30 whether the renderer had 3ms of slack or 26.
		const Real idlePercent = (snap.frameMs > 0.0f) ? (snap.waitMs / snap.frameMs * 100.0f) : 0.0f;
		if (snap.budgetMs > 0.0f)
		{
			line.format(L"work %.2fms  p95 %.2f  max %.2f   |   wait %.2fms (%.0f%% idle)   |   budget %.2fms",
				snap.workMs, snap.workP95Ms, snap.workMaxMs, snap.waitMs, idlePercent, snap.budgetMs);
		}
		else
		{
			line.format(L"work %.2fms  p95 %.2f  max %.2f   |   uncapped, %.1f fps",
				snap.workMs, snap.workP95Ms, snap.workMaxMs,
				(snap.frameMs > 0.0f) ? (1000.0f / snap.frameMs) : 0.0f);
		}
		s_lines[0]->setText(line);

		UnicodeString phases;
		phases.format(L"logic %.2f  client %.2f  daynight %.2f  draw %.2f  lights %.2f  cluster %.2f  shadow %.2f  depth %.2f",
			snap.phaseMs[FrameTiming::PHASE_LOGIC],
			snap.phaseMs[FrameTiming::PHASE_CLIENT],
			snap.phaseMs[FrameTiming::PHASE_DAYNIGHT],
			snap.phaseMs[FrameTiming::PHASE_DRAW],
			snap.phaseMs[FrameTiming::PHASE_LIGHTLIST],
			snap.phaseMs[FrameTiming::PHASE_LIGHTCLUSTER],
			snap.phaseMs[FrameTiming::PHASE_SHADOWMAP],
			snap.phaseMs[FrameTiming::PHASE_DEPTHPREPASS]);
		s_lines[1]->setText(phases);

		phases.format(L"scene %.2f  fog %.2f  postfx %.2f  ui %.2f  gpuwait %.2f   (cpu time; gpuwait = stalled on GPU)",
			snap.phaseMs[FrameTiming::PHASE_SCENE],
			snap.phaseMs[FrameTiming::PHASE_VOLUMETRICFOG],
			snap.phaseMs[FrameTiming::PHASE_POSTFX],
			snap.phaseMs[FrameTiming::PHASE_UI],
			snap.phaseMs[FrameTiming::PHASE_PRESENT]);
		s_lines[2]->setText(phases);

		// GEOMETRY SUBMISSION. Three passes over the same scene, and the totals say which one
		// is worth attacking. The frame-wide draw count the classic stats overlay shows is the
		// sum of these and cannot separate them. The multiplier at the end is the number to
		// watch when a feature quietly adds another whole pass over the geometry -- as the
		// volumetric fog did to the camera depth prepass, which until then only ran when SSR
		// was on and now runs on every frame of every scene.
		const Int shadowDraws = snap.counterLast[FrameTiming::COUNTER_DRAWS_SHADOW];
		const Int depthDraws  = snap.counterLast[FrameTiming::COUNTER_DRAWS_DEPTH];
		const Int sceneDraws  = snap.counterLast[FrameTiming::COUNTER_DRAWS_SCENE];
		const Int totalDraws  = shadowDraws + depthDraws + sceneDraws;
		UnicodeString draws;
		draws.format(L"draws  shadow %d  depth %d  scene %d  = %d   (x%.1f the scene alone)",
			shadowDraws, depthDraws, sceneDraws, totalDraws,
			(sceneDraws > 0) ? ((Real)totalDraws / (Real)sceneDraws) : 0.0f);
		s_lines[3]->setText(draws);

		// THE DAY-NIGHT CYCLE, and what it sets off. Once the cycle is running the hour is the
		// independent variable of every other number on this overlay: a cost that only appears
		// after dark is not one you can find by staring at a mean. The three event totals are
		// the expensive things the cycle triggers, each with the worst frame it cost in the
		// window -- the mean is useless for something that happens four times in twenty
		// minutes, which is exactly why these hitches went unmeasured.
		UnicodeString night;
		static const wchar_t* todNames[TIME_OF_DAY_COUNT] =
			{ L"invalid", L"morning", L"afternoon", L"evening", L"night" };
		const TimeOfDay tod = DayNightCycle_GetCurrentTimeOfDay();
		const wchar_t* todName = (tod >= 0 && tod < TIME_OF_DAY_COUNT) ? todNames[tod] : L"?";
		if (DayNightCycle_IsEnabled())
		{
			const Real hour = DayNightCycle_GetCurrentGameHour();
			// TheSuperHackers @instrument andytraber 12/09/2026 The ACTIVE BODY and its azimuth.
			// The direction is analytic now (the day/night cycle cost investigation, P7), so the elevation
			// alone is no longer enough to say where the light is: it cannot tell a rising sun
			// from a setting one, and it cannot tell the sun from the moon at all -- both can read
			// +40 degrees. The body name plus the azimuth is what a screenshot of a wrong-looking
			// shadow has to be checked against, and the blend figure is what says whether the
			// handover is the explanation.
			night.format(L"daynight %02d:%02d %s  blend %.2f  %s el %+.1f az %.0f (moon %.2f)  cycle %dmin   |   todchange %u  terrbake %u (worst %.0fms)  envbake %u (worst %.0fms +mips %.0fms)",
				(Int)hour, (Int)((hour - (Real)(Int)hour) * 60.0f), todName,
				DayNightCycle_GetBlendAlpha(),
				DayNightCycle_IsMoonLit() ? L"moon" : L"sun",
				DayNightCycle_GetSunElevationDegrees(),
				DayNightCycle_GetLightAzimuthDegrees(),
				DayNightCycle_GetMoonBlend(),
				DayNightCycle_GetDurationMinutes(),
				snap.eventCount[FrameTiming::EVENT_TOD_CHANGE],
				snap.eventCount[FrameTiming::EVENT_TERRAIN_BAKE],
				snap.phaseMaxMs[FrameTiming::PHASE_TERRAINBAKE],
				snap.eventCount[FrameTiming::EVENT_ENVMAP_BAKE],
				snap.phaseMaxMs[FrameTiming::PHASE_ENVMAP],
				snap.phaseMaxMs[FrameTiming::PHASE_ENVMIPS]);
		}
		else
		{
			night.format(L"daynight off (%s)   |   terrbake %u (worst %.0fms)  envbake %u (worst %.0fms +mips %.0fms)",
				todName,
				snap.eventCount[FrameTiming::EVENT_TERRAIN_BAKE],
				snap.phaseMaxMs[FrameTiming::PHASE_TERRAINBAKE],
				snap.eventCount[FrameTiming::EVENT_ENVMAP_BAKE],
				snap.phaseMaxMs[FrameTiming::PHASE_ENVMAP],
				snap.phaseMaxMs[FrameTiming::PHASE_ENVMIPS]);
		}
		s_lines[4]->setText(night);

		// THE LIGHT POPULATION behind the two light phases above. dropped > 0 means the light
		// list is too small for the scene and lights are being discarded without a word.
		// poolscan is what the dynamic light pool costs to allocate out of -- entries walked
		// per frame, which is requests times pool size, because the allocator is a linear scan
		// and the pool never shrinks. Watch dynpool across a whole night, not at one moment.
		UnicodeString lights;
		lights.format(L"lights  local %d (peak %d)  culled %d  dropped %d   |   dynpool %d  lit %d  poolscan %d (peak %d)",
			snap.counterLast[FrameTiming::COUNTER_LOCAL_LIGHTS],
			snap.counterMax[FrameTiming::COUNTER_LOCAL_LIGHTS],
			snap.counterLast[FrameTiming::COUNTER_LIGHTS_CULLED],
			snap.counterLast[FrameTiming::COUNTER_LIGHTS_DROPPED],
			snap.counterLast[FrameTiming::COUNTER_DYNLIGHT_POOL],
			snap.counterLast[FrameTiming::COUNTER_DYNLIGHT_ON],
			snap.counterLast[FrameTiming::COUNTER_DYNLIGHT_SCAN],
			snap.counterMax[FrameTiming::COUNTER_DYNLIGHT_SCAN]);
		s_lines[5]->setText(lights);

		// WHY the terrain keeps re-baking. Requests coalesce into bakes, so these are causes
		// and not a partition of the bake count.
		//
		// TheSuperHackers @instrument andytraber 12/09/2026 The bake/patch split is on this line
		// because it only means anything next to the causes. A height change now takes the patch
		// path and a lighting change still takes the bake path, so the shape to look for is
		// "deform in the hundreds, patches in the tens, bakes in single figures" -- and the two
		// worst-frame figures side by side are the claim itself, in milliseconds. Read together
		// they also diagnose a failure: deform high with patches at zero means the new
		// notification never reached the render object, and both high means something is still
		// escalating a geometry change into a whole-window re-light.
		UnicodeString dirty;
		dirty.format(L"terrdirty  tod %u  window %u  deform %u  of %u total   ->  %u bakes (worst %.0fms)  %u patches (worst %.2fms, %d cells last)",
			snap.eventCount[FrameTiming::EVENT_TERRDIRTY_TOD],
			snap.eventCount[FrameTiming::EVENT_TERRDIRTY_WINDOW],
			snap.eventCount[FrameTiming::EVENT_TERRDIRTY_DEFORM],
			snap.eventCount[FrameTiming::EVENT_TERRDIRTY_ALL],
			snap.eventCount[FrameTiming::EVENT_TERRAIN_BAKE],
			snap.phaseMaxMs[FrameTiming::PHASE_TERRAINBAKE],
			snap.eventCount[FrameTiming::EVENT_TERRAIN_PATCH],
			snap.phaseMaxMs[FrameTiming::PHASE_TERRAINPATCH],
			snap.counterLast[FrameTiming::COUNTER_TERRPATCH_CELLS]);
		s_lines[6]->setText(dirty);

		// TheSuperHackers @instrument andytraber 12/09/2026 The same shape of question for the
		// projected shadows, and on its own line rather than appended to the one above: a cause
		// count, an effect count, and the peak that says the cap held.
		//
		// shadowstep is how often the quantised sun direction was republished (the cause);
		// shadowtex is how many shadow textures that actually cost to re-render (the effect);
		// peak is the worst single frame in the window and must never exceed
		// MAX_SHADOW_TEXTURE_RENDERS_PER_FRAME. If peak sits at the cap for long stretches the
		// budget is the bottleneck and the shadows are lagging the sun; if shadowtex climbs while
		// shadowstep does not, something other than the sun is invalidating textures. And if
		// shadowstep climbs while shadowtex stays at 0, no shipped asset uses SHADOW_PROJECTION
		// at all -- which is itself worth knowing, and is why these are two totals and not one.
		UnicodeString shadows;
		shadows.format(L"shadows  step %u  texrender %u (peak %d/frame)",
			snap.eventCount[FrameTiming::EVENT_SHADOW_LIGHT_STEP],
			snap.eventCount[FrameTiming::EVENT_SHADOW_TEX_RENDER],
			snap.counterMax[FrameTiming::COUNTER_SHADOW_TEX_RENDER]);
		s_lines[7]->setText(shadows);

		// The GPU line refuses to show a number more often than it shows one, and that is the
		// point. A timestamp figure taken under the frame rate cap on this machine has already
		// been confidently, reproducibly backwards -- see WW3D2/gputimer.h -- so the readout
		// says why it is withholding rather than printing something that will be believed.
		UnicodeString gpu;
		if (!snap.gpuSupported)
		{
			gpu.format(L"gpu  n/a -- device has no timestamp queries");
		}
		else if (!snap.gpuMeasurable)
		{
			gpu.format(L"gpu  n/a -- uncap the fps limit to measure (the GPU clocks down under it)");
		}
		else if (snap.gpuSampleCount == 0)
		{
			gpu.format(L"gpu  waiting for a clean frame (%u discarded as disjoint)",
				snap.gpuDisjointFrames);
		}
		else
		{
			gpu.format(L"gpu %.2fms  shadow %.2f  depth %.2f  scene %.2f  postfx %.2f  ui %.2f  (n=%d, %u disjoint)",
				snap.gpuTotalMs,
				snap.gpuPhaseMs[FrameTiming::PHASE_SHADOWMAP],
				snap.gpuPhaseMs[FrameTiming::PHASE_DEPTHPREPASS],
				snap.gpuPhaseMs[FrameTiming::PHASE_SCENE],
				snap.gpuPhaseMs[FrameTiming::PHASE_POSTFX],
				snap.gpuPhaseMs[FrameTiming::PHASE_UI],
				snap.gpuSampleCount, snap.gpuDisjointFrames);
		}
		s_lines[8]->setText(gpu);
	}

	const Color textColor = GameMakeColor(255, 255, 255, 255);
	const Color dropColor = GameMakeColor(0, 0, 0, 255);

	Int x = 3;
	Int y = topY;
	Int w, h;
	for (Int i = 0; i < NUM_TIMING_LINES; ++i)
	{
		s_lines[i]->draw(x, y, textColor, dropColor);
		s_lines[i]->getSize(&w, &h);
		y += h;
	}

	drawGraph(snap, GRAPH_X, y + GRAPH_PAD);
}


#endif	// defined(RTS_DEBUG)

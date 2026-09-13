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

// TheSuperHackers @feature andytraber 29/08/2026 Per-phase frame cost measurement.
//
// This measures how long a frame's *work* takes, which is not what the FPS counter
// measures. FrameRateLimit::wait() sleeps and then spins until the frame's budget is
// used up, so at a 30 fps cap every frame reads 33.3ms whether the renderer spent 4ms
// or 30ms getting there. The frame interval only ever answers "did it keep up". To see
// cost you have to time the work and account for the wait separately, which is what the
// PHASE_WAIT bucket is for: work = interval - wait, and the headroom is the difference.
//
// Times are *exclusive*: a phase entered inside another phase is subtracted from its
// parent, so the buckets sum to the frame instead of double counting. That is the only
// reason nesting PHASE_SCENE inside PHASE_DRAW inside PHASE_CLIENT reads sensibly.
//
// What these numbers are, and are not: this is CPU time. D3D9 queues work several
// frames deep, so when the GPU is the bottleneck the stall does not land on the phase
// that caused it -- it lands wherever the driver next blocks, which is almost always
// Present. That is why PHASE_PRESENT is labelled "gpuwait" in the readout rather than
// being presented as the cost of presenting. A big scene number means submission is
// expensive; a big gpuwait means the GPU is behind, but not which phase put it there.
// Attributing GPU cost to a phase needs timestamp queries, which is a separate job --
// and on this machine a suspect one, see the notes in the graphics measurement log.

#pragma once

#include "Common/GameCommon.h"


// The vocabulary -- what can be measured, counted and recorded -- is declared in both
// configurations, so a call site naming a counter or an event compiles in release too and
// needs no #ifdef of its own. Only the machinery below is debug-only.
namespace FrameTiming
{

	// PER-FRAME COUNTERS. Not times: these are the populations that drive the times, and a
	// cost without the population that caused it cannot be read. A frame spending 3ms in the
	// light list is saying something quite different at 12 lights than at 900. Written during
	// the frame with setCounter/addCounter and cleared by endFrame, so a counter nobody wrote
	// this frame reads 0 rather than repeating the last frame that did.
	enum Counter CPP_11(: Int)
	{
		COUNTER_LOCAL_LIGHTS = 0,	///< local lights uploaded to the GPU light list this frame
		COUNTER_LIGHTS_CULLED,		///< candidates the camera frustum threw away
		COUNTER_LIGHTS_DROPPED,		///< survivors that lost the capacity contest -- nonzero means the list is too small
		COUNTER_DYNLIGHT_POOL,		///< W3DDynamicLights the scene has ever allocated. It only grows; a fleet of headlights is what grows it
		COUNTER_DYNLIGHT_ON,		///< of those, how many are enabled right now
		COUNTER_DYNLIGHT_SCAN,		///< pool entries walked by getADynamicLight this frame. This is the O(n) the pool is made of: requests x pool size
		COUNTER_DRAWS_SHADOW,		///< draw calls submitted by the sun's depth pass
		COUNTER_DRAWS_DEPTH,		///< draw calls submitted by the camera depth prepass
		COUNTER_DRAWS_SCENE,		///< draw calls submitted by the main scene pass

		// TheSuperHackers @instrument andytraber 12/09/2026 The area the regional height-map
		// update re-lit this frame, in terrain cells. This is the number that says the cheap
		// path is actually cheap: the full bake it replaces is always the whole draw window
		// (129x129 = 16641 cells, or 257x257 = 66049 on a low sun), so a two-digit figure here
		// against a bake count that has stopped climbing is the whole of the P4 result. A
		// count of *requests* could not say this -- nine foundation cells coalescing into one
		// 9x9 rect and nine scattered craters coalescing into a map-sized one look identical
		// from the request side and differ by three orders of magnitude in cost.
		COUNTER_TERRPATCH_CELLS,

		// TheSuperHackers @instrument andytraber 12/09/2026 Projected-shadow textures re-rendered
		// THIS FRAME. A per-frame counter and not an event total, because the question it answers
		// is "did the budget hold": each one is a render of the caster into a 512x512 target plus a
		// 512x512 surface copy, and the whole point of capping them is that the peak over the
		// window stays at the cap instead of climbing with the number of shadowed models on screen.
		// Read counterMax, not counterLast -- the re-renders arrive in bursts when the sun's
		// quantised direction steps, and the mean between two steps is zero.
		COUNTER_SHADOW_TEX_RENDER,

		COUNTER_COUNT
	};

	// EVENT TOTALS. Monotonic for the life of the process, because these count things that
	// happen a handful of times a session; a per-frame or windowed view of them reads zero
	// almost always. What you want is "has it happened, and how often since I started
	// looking", next to the phase max that says what one of them cost.
	enum Event CPP_11(: Int)
	{
		EVENT_TOD_CHANGE = 0,		///< nominal time-of-day boundaries crossed
		EVENT_TERRAIN_BAKE,			///< full terrain vertex-lighting re-bakes actually PERFORMED
		// TheSuperHackers @instrument andytraber 12/09/2026 Regional height-map updates actually
		// PERFORMED, the cheap counterpart of the line above. The pair is what has to be read
		// together: after the P4 fix a session full of base building should show terrdirty:deform
		// still in the hundreds, this in the tens, and EVENT_TERRAIN_BAKE back down to the handful
		// of genuine lighting changes. If deform is high and this is zero the new notification is
		// not wired up; if both this and TERRAIN_BAKE are high, something is still escalating a
		// geometry change into a lighting one.
		EVENT_TERRAIN_PATCH,
		EVENT_ENVMAP_BAKE,			///< procedural cubemap re-bakes

		// WHY a terrain re-bake was asked for. Several requests coalesce into one bake (they
		// only set a flag), so these do not sum to EVENT_TERRAIN_BAKE -- and that is the
		// point: a bake count on its own says a hitch happened and nothing about what to go
		// and fix. Inferring the cause from "no time-of-day boundary crossed, so it must be
		// the sun" was wrong once already; these were added so it cannot be wrong again.
		EVENT_TERRDIRTY_TOD,		///< a time-of-day change (the cycle, or the debug key)
		EVENT_TERRDIRTY_WINDOW,		///< the terrain draw window was resized (see widenTerrainDrawSizeForShadows)
		EVENT_TERRDIRTY_DEFORM,		///< the height field was lowered -- a building foundation or a crater. MEASURED at 647 per 5.5 minutes of base building, which is what made this the largest hitch in the frame; it now asks for a regional update (EVENT_TERRAIN_PATCH) rather than a whole-window re-light
		EVENT_TERRDIRTY_ALL,		///< EVERY staticLightingChanged call, named or not. Subtract the two above to get the unaccounted-for ones -- counting "other" directly would need the cause threaded through a virtual, and this says the same thing without it

		// TheSuperHackers @instrument andytraber 12/09/2026 The projected-shadow re-render, cause
		// and effect, as two separate totals. The cause is the quantised sun direction stepping;
		// the effect is how many shadow textures that step actually cost to re-render. They are
		// counted apart on purpose: if the renders climb while the steps do not, the threshold is
		// fine and something else is invalidating textures, and the opposite says the threshold is
		// too tight. Deducing one from the other is how the terrain re-bake was mis-attributed.
		EVENT_SHADOW_LIGHT_STEP,	///< the quantised shadow light direction was republished
		EVENT_SHADOW_TEX_RENDER,	///< projected-shadow textures actually re-rendered, ever

		EVENT_COUNT
	};
}	// namespace FrameTiming


// Debug builds only. The measurement is an instrument, not a feature: it costs a QPC at
// every phase boundary and a few kilobytes of history, and nothing in a shipping build
// reads it. In release the scope macro expands to nothing and endFrame is an inline no-op,
// so the call sites need no guards of their own and the optimizer removes them entirely.
#if defined(RTS_DEBUG)


namespace FrameTiming
{

	enum Phase CPP_11(: Int)
	{
		PHASE_LOGIC = 0,		///< TheGameLogic->UPDATE()
		PHASE_CLIENT,			///< TheGameClient->UPDATE(), less the display draw and the day/night step below
		PHASE_DAYNIGHT,			///< DayNightCycle_Update: the interpolation, and everything updateSceneLighting sets off. Its own bucket because it runs every client frame and was invisible inside "client"
		PHASE_DRAW,				///< the display draw, less the render phases below
		PHASE_LIGHTLIST,		///< C3's clustered light list rebuild (the clustered lighting plan): enumerate, frustum-cull, pack, upload -- once a frame, before the shadow map
		PHASE_LIGHTCLUSTER,		///< C4's cluster grid build: scatter those lights into the screen froxels and upload. Its own bucket and not folded into the one above, because C6 replaces the CPU builder with a compute dispatch and the before/after cost has to be comparable
		PHASE_SHADOWMAP,		///< the sun's depth pass
		PHASE_DEPTHPREPASS,		///< the camera depth pass SSR needs
		PHASE_SCENE,			///< the main scene render
		PHASE_VOLUMETRICFOG,	///< volumetric fog froxel scatter, ray integration and composite
		PHASE_TERRAINBAKE,		///< the CPU re-bake of terrain vertex lighting (staticLightingChanged -> a full updateBlock). Once per nominal time-of-day boundary: a hitch, not a cost, so read phaseMaxMs and never the mean
		// TheSuperHackers @instrument andytraber 12/09/2026 The regional path that replaced the
		// bake above for a mere height change (heightMapChanged -> doPartialUpdate). A sibling
		// bucket rather than a share of PHASE_TERRAINBAKE, because the whole claim being made is
		// that these two have different costs: folding them together would hide exactly the
		// difference the change exists to produce, and a worst-frame figure here of a fraction
		// of a millisecond next to the 26-47ms the bake used to cost is the proof.
		PHASE_TERRAINPATCH,
		PHASE_ENVMAP,			///< the procedural reflection cubemap bake: the CPU texel loop that writes one face. Same shape as the above -- rare, expensive, and only visible in the max
		PHASE_ENVMIPS,			///< Generate_DX8_Mips on the finished cube, nested inside the above. MEASURED 12/09/2026 at under 0.5ms worst, i.e. it is NOT the unexplained residual it was added to test for -- kept as the control that says so
		PHASE_ENVUPLOAD,		///< Map/Unmap of one cube face, nested inside PHASE_ENVMAP. The cube is GFX_USAGE_DEFAULT, so a map is a CPU scratch buffer and the unmap is an UpdateSubresource of 256KB onto a texture the scene is sampling every frame. Six of those in one frame can be renamed once; one a frame for six frames cannot, which is the standing hypothesis for why one face costs ~30ms when six cost 96
		PHASE_POSTFX,			///< the screen filter chain
		PHASE_UI,				///< in-game UI, control bar, mouse
		PHASE_PRESENT,			///< End_Render; in practice mostly waiting on the GPU
		PHASE_WAIT,				///< the frame limiter's sleep+spin. Not work.

		PHASE_COUNT
	};

	enum { HISTORY_SIZE = 240 };	///< frames of work time kept for the graph and percentiles

	/// One frame's worth of measurement, plus the window statistics over it.
	struct Snapshot
	{
		Real phaseMs[PHASE_COUNT];	///< mean exclusive time per phase over the window
		Real workMs;				///< mean frame work (everything but the wait)
		Real workP95Ms;				///< 95th percentile work, the number hitches show up in
		Real workMaxMs;				///< worst work frame in the window
		Real waitMs;				///< mean time spent in the frame limiter
		Real frameMs;				///< mean total frame period (work + wait)
		Real budgetMs;				///< the frame budget from the fps cap, or 0 when uncapped
		Int  sampleCount;			///< frames in the window; < HISTORY_SIZE while filling

		/// Worst frame in the window, per phase. The mean is the wrong instrument for anything
		/// that happens once every few hundred frames: a 400ms terrain re-bake averaged over a
		/// 240 frame window reads as 1.7ms and looks like nothing at all. This is the number
		/// that says which phase owns a hitch.
		Real phaseMaxMs[PHASE_COUNT];

		Int  counterLast[COUNTER_COUNT];	///< the most recently completed frame
		Int  counterMax[COUNTER_COUNT];		///< worst frame in the window
		UnsignedInt eventCount[EVENT_COUNT];///< totals since process start

		// GPU side. Filled by the device layer from D3D9 timestamp queries; see
		// WW3D2/gputimer.h for why these are the numbers most likely to be lying to you.
		// Only the sibling render phases carry GPU spans -- bracketing a phase that submits
		// no GPU work measures whatever happened to be in flight, which is worse than
		// measuring nothing. gpuTotalMs is the whole frame's GPU span.
		Real gpuPhaseMs[PHASE_COUNT];
		Real gpuTotalMs;
		Int  gpuSampleCount;		///< resolved, non-disjoint frames in the window
		Bool gpuSupported;			///< the device could create timestamp queries
		Bool gpuMeasurable;			///< false while the fps limiter is on; see below
		UnsignedInt gpuDisjointFrames;	///< frames thrown away for a mid-frame clock change
	};

	/// Hook the device layer installs so the same scopes can bracket GPU work. FrameTiming
	/// knows nothing about D3D and must not: this is the whole of the coupling.
	typedef void (*PhaseBracketHook)( Phase phase, Bool begin );
	void setPhaseBracketHook( PhaseBracketHook hook );

	/// Hand back one frame's resolved GPU timings. phaseMs is indexed by Phase; slots the
	/// device layer did not bracket must be 0.
	void submitGpuFrame( const Real* phaseMs, Real totalMs );

	/// Tell the readout what the GPU timer is capable of right now.
	void setGpuStatus( Bool supported, UnsignedInt disjointFrames );

	/// Begin/end a phase. Nesting is allowed and produces exclusive times.
	void beginPhase( Phase phase );
	void endPhase( Phase phase );

	/// Per-frame counters. setCounter for a population sampled once (a pool size), addCounter
	/// for one accumulated across several call sites within a frame (scan steps). Both are
	/// cleared by endFrame.
	void setCounter( Counter counter, Int value );
	void addCounter( Counter counter, Int delta );

	/// Record that a rare, expensive thing happened. Totals only; never reset.
	void recordEvent( Event event );

	/// Display names, for the readout and the log dump.
	const char* getCounterName( Counter counter );
	const char* getEventName( Event event );

	/// Close the frame and roll it into the history. Called by FramePacer::update once
	/// the limiter has returned, with the frame period the limiter measured.
	void endFrame( Real framePeriodSeconds );

	/// Fill in the window statistics. Cheap enough to call at the readout's refresh rate,
	/// not per frame -- it sorts a copy of the window to find the percentile.
	void getSnapshot( Snapshot& out );

	/// Copy the work-time history, oldest first, for the strip graph. Returns how many
	/// samples were written (up to maxCount, and up to how many have been collected).
	Int getWorkHistory( Real* out, Int maxCount );

	/// Display name for a phase, for the readout and the log dump.
	const char* getPhaseName( Phase phase );

	/// RAII scope. Use the FRAME_TIMING_SCOPE macro rather than this directly.
	class Scope
	{
	public:
		Scope( Phase phase ) : m_phase(phase) { beginPhase(phase); }
		~Scope() { endPhase(m_phase); }
	private:
		Phase m_phase;
		Scope( const Scope& );
		Scope& operator=( const Scope& );
	};

}	// namespace FrameTiming

#define FRAME_TIMING_SCOPE(phase) FrameTiming::Scope _frameTimingScope_(FrameTiming::phase)


#else	// !defined(RTS_DEBUG)


namespace FrameTiming
{
	// Kept so the call sites need no preprocessor guard of their own. The arguments are
	// still evaluated, which is deliberate: a counter fed by a cheap accessor stays
	// readable at the call site, and anything expensive enough to matter belongs behind
	// an RTS_DEBUG block at the call site anyway.
	inline void endFrame( Real ) {}
	inline void setCounter( Counter, Int ) {}
	inline void addCounter( Counter, Int ) {}
	inline void recordEvent( Event ) {}
}

#define FRAME_TIMING_SCOPE(phase) ((void)0)


#endif	// defined(RTS_DEBUG)

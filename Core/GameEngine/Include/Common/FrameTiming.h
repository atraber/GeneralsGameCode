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
		PHASE_CLIENT,			///< TheGameClient->UPDATE(), less the display draw below
		PHASE_DRAW,				///< the display draw, less the render phases below
		PHASE_SHADOWMAP,		///< the sun's depth pass
		PHASE_SCENE,			///< the main scene render
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
	};

	/// Begin/end a phase. Nesting is allowed and produces exclusive times.
	void beginPhase( Phase phase );
	void endPhase( Phase phase );

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
	// Kept so FramePacer::update needs no preprocessor guard around its one call.
	inline void endFrame( Real ) {}
}

#define FRAME_TIMING_SCOPE(phase) ((void)0)


#endif	// defined(RTS_DEBUG)

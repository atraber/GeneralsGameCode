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
// See FrameTiming.h for what the numbers mean and what they do not.

#include "PreRTS.h"
#include "Common/FrameTiming.h"
#include "Common/FramePacer.h"

#include <algorithm>


#if defined(RTS_DEBUG)


namespace FrameTiming
{

	// State is file static rather than a subsystem singleton on purpose. It has to be
	// usable before TheGameEngine exists and after it is torn down (the load screen draws
	// frames too), it owns no resources, and a measurement instrument that can itself fail
	// to initialise is worse than useless. The cost is one QPC per phase boundary -- about
	// a dozen per frame, tens of nanoseconds each -- so it always runs and the history is
	// already populated when the readout is switched on, instead of being empty for a
	// second the moment you most want to look at it.

	enum { MAX_DEPTH = 16 };

	struct StackEntry
	{
		Phase phase;
		Int64 startTicks;
		Int64 childTicks;	///< time attributed to nested phases, subtracted on the way out
	};

	static StackEntry	s_stack[MAX_DEPTH];
	static Int			s_depth = 0;
	static Int64		s_phaseTicks[PHASE_COUNT] = { 0 };

	static Real			s_workHistory[HISTORY_SIZE] = { 0 };
	static Real			s_phaseHistory[PHASE_COUNT][HISTORY_SIZE] = { { 0 } };
	static Real			s_frameHistory[HISTORY_SIZE] = { 0 };
	static Int			s_historyHead = 0;		///< next slot to write
	static Int			s_historyCount = 0;		///< how many slots hold real samples

	static Int64		s_freq = 0;

	static PhaseBracketHook	s_bracketHook = nullptr;

	// GPU history is kept separately from the CPU history and advances on its own clock: a
	// frame only lands here once its timestamps have resolved and come back non-disjoint, so
	// the two windows cover the same stretch of time but not the same count of frames.
	static Real			s_gpuPhaseHistory[PHASE_COUNT][HISTORY_SIZE] = { { 0 } };
	static Real			s_gpuTotalHistory[HISTORY_SIZE] = { 0 };
	static Int			s_gpuHead = 0;
	static Int			s_gpuCount = 0;
	static Bool			s_gpuSupported = FALSE;
	static UnsignedInt	s_gpuDisjointFrames = 0;

	static const char* s_phaseNames[PHASE_COUNT] =
	{
		"logic", "client", "draw", "lights", "cluster", "shadow", "depth", "scene", "postfx", "ui", "gpuwait", "wait"
	};


	static Int64 nowTicks()
	{
		LARGE_INTEGER tick;
		QueryPerformanceCounter(&tick);
		return tick.QuadPart;
	}

	static Int64 timerFrequency()
	{
		if (s_freq == 0)
		{
			LARGE_INTEGER freq;
			QueryPerformanceFrequency(&freq);
			s_freq = freq.QuadPart;
		}
		return s_freq;
	}

	static Real ticksToMs( Int64 ticks )
	{
		return (Real)((double)ticks * 1000.0 / (double)timerFrequency());
	}


	void beginPhase( Phase phase )
	{
		// Overflowing the stack silently drops the phase rather than corrupting the ones
		// already on it. Nesting never gets close to MAX_DEPTH; this is here so a stray
		// unbalanced begin cannot take the instrument down with it.
		if (s_depth >= MAX_DEPTH)
			return;

		StackEntry& entry = s_stack[s_depth++];
		entry.phase = phase;
		entry.startTicks = nowTicks();
		entry.childTicks = 0;

		if (s_bracketHook != nullptr)
			s_bracketHook(phase, TRUE);
	}

	void endPhase( Phase phase )
	{
		if (s_depth <= 0)
			return;

		// Tolerate a mismatch instead of unwinding to it: the RAII scope makes crossed
		// phases impossible in practice, and popping blind on a mismatch would misattribute
		// every phase above it. Dropping the sample loses one frame of one number.
		if (s_stack[s_depth - 1].phase != phase)
			return;

		const StackEntry& entry = s_stack[--s_depth];
		const Int64 elapsed = nowTicks() - entry.startTicks;

		// Exclusive: this phase keeps what its children did not use...
		s_phaseTicks[phase] += elapsed - entry.childTicks;

		// ...and the whole span counts as a child of whatever encloses it.
		if (s_depth > 0)
			s_stack[s_depth - 1].childTicks += elapsed;

		if (s_bracketHook != nullptr)
			s_bracketHook(phase, FALSE);
	}


	void setPhaseBracketHook( PhaseBracketHook hook )
	{
		s_bracketHook = hook;
	}


	void submitGpuFrame( const Real* phaseMs, Real totalMs )
	{
		for (Int i = 0; i < PHASE_COUNT; ++i)
			s_gpuPhaseHistory[i][s_gpuHead] = phaseMs[i];
		s_gpuTotalHistory[s_gpuHead] = totalMs;

		s_gpuHead = (s_gpuHead + 1) % HISTORY_SIZE;
		if (s_gpuCount < HISTORY_SIZE)
			++s_gpuCount;
	}


	void setGpuStatus( Bool supported, UnsignedInt disjointFrames )
	{
		s_gpuSupported = supported;
		s_gpuDisjointFrames = disjointFrames;
	}

	void endFrame( Real framePeriodSeconds )
	{
		// An unbalanced frame would smear its phases into the next one. Reset the stack
		// rather than carry it: one bad frame beats an indefinitely wrong readout.
		s_depth = 0;

		Real workMs = 0.0f;
		for (Int i = 0; i < PHASE_COUNT; ++i)
		{
			const Real ms = ticksToMs(s_phaseTicks[i]);
			s_phaseHistory[i][s_historyHead] = ms;
			if (i != PHASE_WAIT)
				workMs += ms;
			s_phaseTicks[i] = 0;
		}

		s_workHistory[s_historyHead] = workMs;

		// The frame period comes from the limiter rather than from summing the phases,
		// because the phases only cover what is instrumented -- anything between the end of
		// one frame's last phase and the start of the next belongs in the period and in
		// nothing else. work + wait therefore need not equal it exactly, and the gap is
		// itself worth seeing.
		s_frameHistory[s_historyHead] = framePeriodSeconds * 1000.0f;

		s_historyHead = (s_historyHead + 1) % HISTORY_SIZE;
		if (s_historyCount < HISTORY_SIZE)
			++s_historyCount;
	}

	void getSnapshot( Snapshot& out )
	{
		memset(&out, 0, sizeof(out));
		out.sampleCount = s_historyCount;

		if (s_historyCount == 0)
			return;

		const Int count = s_historyCount;

		Real workSum = 0.0f;
		Real frameSum = 0.0f;
		Real workMax = 0.0f;
		for (Int i = 0; i < count; ++i)
		{
			workSum += s_workHistory[i];
			frameSum += s_frameHistory[i];
			workMax = max(workMax, s_workHistory[i]);
		}

		out.workMs = workSum / count;
		out.frameMs = frameSum / count;
		out.workMaxMs = workMax;

		for (Int p = 0; p < PHASE_COUNT; ++p)
		{
			Real sum = 0.0f;
			for (Int i = 0; i < count; ++i)
				sum += s_phaseHistory[p][i];
			out.phaseMs[p] = sum / count;
		}
		out.waitMs = out.phaseMs[PHASE_WAIT];

		// p95 over a copy. A mean hides exactly the frames worth looking at, and the max
		// alone cannot tell a single hitch from a fifth of them being slow.
		Real sorted[HISTORY_SIZE];
		memcpy(sorted, s_workHistory, sizeof(Real) * count);
		const Int idx = min(count - 1, (Int)(count * 0.95f));
		std::nth_element(sorted, sorted + idx, sorted + count);
		out.workP95Ms = sorted[idx];

		out.budgetMs = 0.0f;
		const Bool limited = (TheFramePacer != nullptr &&
							  TheFramePacer->isActualFramesPerSecondLimitEnabled());
		if (limited)
		{
			const Int limit = TheFramePacer->getActualFramesPerSecondLimit();
			if (limit > 0)
				out.budgetMs = 1000.0f / (Real)limit;
		}

		out.gpuSupported = s_gpuSupported;
		out.gpuDisjointFrames = s_gpuDisjointFrames;
		out.gpuSampleCount = s_gpuCount;

		// The refusal that makes the rest of it worth reading. Under a frame rate cap half
		// the frame is idle, the GPU clocks down, and heavier work makes it boost -- which is
		// how a 2.25x pixel increase once measured 18.6% *faster* here, reproducibly. The
		// disjoint query catches a clock change inside one frame; it cannot catch a clock
		// that is simply low and steady for the whole run. Nothing but uncapping does.
		out.gpuMeasurable = !limited;

		if (s_gpuCount > 0)
		{
			for (Int p = 0; p < PHASE_COUNT; ++p)
			{
				Real sum = 0.0f;
				for (Int i = 0; i < s_gpuCount; ++i)
					sum += s_gpuPhaseHistory[p][i];
				out.gpuPhaseMs[p] = sum / s_gpuCount;
			}
			Real total = 0.0f;
			for (Int i = 0; i < s_gpuCount; ++i)
				total += s_gpuTotalHistory[i];
			out.gpuTotalMs = total / s_gpuCount;
		}
	}

	Int getWorkHistory( Real* out, Int maxCount )
	{
		const Int count = min(maxCount, s_historyCount);
		if (count <= 0)
			return 0;

		// Oldest first. The ring is only full once HISTORY_SIZE frames have gone by; until
		// then the samples start at 0 and the head is the write cursor, not a wrap point.
		const Int start = (s_historyCount < HISTORY_SIZE)
			? (s_historyCount - count)
			: ((s_historyHead + HISTORY_SIZE - count) % HISTORY_SIZE);

		for (Int i = 0; i < count; ++i)
			out[i] = s_workHistory[(start + i) % HISTORY_SIZE];

		return count;
	}

	const char* getPhaseName( Phase phase )
	{
		if (phase < 0 || phase >= PHASE_COUNT)
			return "?";
		return s_phaseNames[phase];
	}

}	// namespace FrameTiming


#endif	// defined(RTS_DEBUG)

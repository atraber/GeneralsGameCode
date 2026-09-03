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

// TheSuperHackers @feature andytraber 30/08/2026 GPU-side phase timing via D3D9 timestamp
// queries. The companion to the CPU-side FrameTiming, which can only ever say that the GPU
// is behind (its stall lands on Present) and never which pass put it there.
//
// READ THIS BEFORE TRUSTING A NUMBER OUT OF HERE.
//
// Timestamp queries on this machine have already produced a confidently wrong answer once:
// a 1280x720 vs 1920x1080 control, identical shaders, measured 2.25x the pixels as 18.6%
// *faster*, reproducibly, far outside the noise. That is the signature of the GPU clock
// changing underneath the measurement -- at a 30fps cap half the frame is idle, so the GPU
// downclocks, and heavier work makes it boost and shortens every delta.
//
// Two defences are built in here, and both matter:
//
//  1. Every frame is wrapped in a D3DQUERYTYPE_TIMESTAMPDISJOINT query, and a frame that
//     comes back disjoint is thrown away rather than displayed. That query exists precisely
//     to say "the clock changed, these timestamps are not comparable" -- which is the fault
//     the old measurement had no way to see. Disjoint frames are counted so the readout can
//     say how many were lost instead of quietly averaging fewer samples.
//
//  2. Nothing is reported while the frame rate limiter is on. That is the condition that
//     lets the GPU idle down in the first place, and it is checked by the caller, not here.
//
// **Neither defence is sufficient, and it is worth knowing why.** Turning the game's limiter
// off does not stop the frame being paced: windowed presentation is synchronised to the
// desktop refresh whatever FullScreen_PresentationInterval says, so an "uncapped" run still
// lands on 60fps with the GPU idle for part of every frame -- which is the same condition
// that lets it clock down. And the disjoint query came back clean for 240 consecutive frames
// on the run that first exercised this, so a zero disjoint count is evidence of nothing much:
// it catches a clock that changes *inside* one frame, never one that is simply low and steady
// for the whole run.
//
// So treat a difference smaller than the spread across repeated runs as nothing, and trust
// the instrument only as far as a positive control says you may. The cheap one is resolution:
// if 720p -> 1080p does not cost more, it is lying again and no conclusion drawn from it is
// worth keeping.
//
// Debug builds only, like everything that reads it.

#pragma once

#include "always.h"

#if defined(RTS_DEBUG)

class GpuTimer
{
public:

	// Slots are opaque here on purpose. WW3D2 sits below the game engine and must not know
	// what a "phase" is; the caller owns the mapping and passes an index.
	enum { MAX_SLOTS = 16 };

	/// Whether the device could create timestamp queries at all. False on hardware or under
	/// a runtime that does not support them, in which case every other call is a no-op.
	static bool Is_Supported();

	/// Open a frame. Issues the disjoint + frequency queries. Must be paired with End_Frame.
	static void Begin_Frame();

	/// Bracket one slot's GPU work. Timestamps are written into the frame opened above;
	/// calls outside a Begin_Frame/End_Frame pair are ignored.
	static void Begin_Slot( int slot );
	static void End_Slot( int slot );

	/// Close the frame and hand it to the GPU. Results become readable a few frames later.
	static void End_Frame();

	/// Collect the oldest frame whose results have arrived, without ever blocking on the GPU
	/// -- a timing instrument that stalls the pipeline to read itself changes the thing it is
	/// measuring. Returns false when nothing is ready yet or the frame was disjoint.
	///
	/// out_ms is filled for slots 0..MAX_SLOTS-1; slots never bracketed read 0.
	static bool Resolve( float* out_ms, float& out_total_ms );

	/// How many frames have been discarded for coming back disjoint, since the last reset.
	static unsigned Get_Disjoint_Count();
	static void Reset_Disjoint_Count();

	/// Release every query object. Must be called before the device is reset -- a live
	/// query is one more thing that can make Reset fail with D3DERR_INVALIDCALL, and this
	/// codebase has already lost a session to exactly that class of leak. The pool recreates
	/// itself lazily on the next Begin_Frame, so there is no matching reacquire.
	static void Release();
};

#endif // defined(RTS_DEBUG)

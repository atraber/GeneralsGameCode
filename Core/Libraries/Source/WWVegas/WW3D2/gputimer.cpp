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

// TheSuperHackers @feature andytraber 30/08/2026 See gputimer.h for what these numbers are
// worth and the two ways they have already been wrong.

#include "gputimer.h"

#if defined(RTS_DEBUG)

#include "dx8wrapper.h"


namespace
{
	// Frames in flight. The GPU is typically one to three frames behind the CPU, so reading
	// back the frame just submitted would mean blocking on it -- which is the one thing a
	// timing instrument must not do. Four gives the results time to land, so every Resolve
	// is a poll that either succeeds immediately or returns false.
	enum { FRAME_DEPTH = 4 };

	// A slot can be entered more than once in a frame and the visits need to be summed, not
	// spanned. PHASE_POSTFX is the case that forces this: the filter chain runs once before
	// the scene and once after it, so bracketing from the first open to the last close would
	// report the whole scene as post-processing. Each visit gets its own timestamp pair and
	// Resolve adds the durations.
	enum { MAX_SPANS_PER_SLOT = 3 };
	enum { STAMPS_PER_SLOT = MAX_SPANS_PER_SLOT * 2 };

	enum { STAMPS_PER_FRAME = GpuTimer::MAX_SLOTS * STAMPS_PER_SLOT + 2 };
	enum { STAMP_FRAME_BEGIN = GpuTimer::MAX_SLOTS * STAMPS_PER_SLOT };
	enum { STAMP_FRAME_END   = GpuTimer::MAX_SLOTS * STAMPS_PER_SLOT + 1 };

	inline int stampIndex( int slot, int span, int end )
	{
		return slot * STAMPS_PER_SLOT + span * 2 + end;
	}

	struct FrameQueries
	{
		IDirect3DQuery9*	disjoint;
		IDirect3DQuery9*	frequency;
		IDirect3DQuery9*	stamps[STAMPS_PER_FRAME];
		bool				stampIssued[STAMPS_PER_FRAME];
		int					spanCount[GpuTimer::MAX_SLOTS];	///< closed spans this frame
		bool				spanOpen[GpuTimer::MAX_SLOTS];	///< a span is currently open
		bool				pending;		///< issued, not yet read back
	};

	FrameQueries	s_frames[FRAME_DEPTH];
	int				s_writeIndex = 0;		///< frame currently being recorded
	int				s_readIndex = 0;		///< oldest frame awaiting readback
	bool			s_created = false;
	bool			s_supported = true;		///< until the device says otherwise
	bool			s_inFrame = false;
	unsigned		s_disjointCount = 0;


	void destroyPool()
	{
		for (int f = 0; f < FRAME_DEPTH; ++f)
		{
			FrameQueries& fq = s_frames[f];
			if (fq.disjoint)  { fq.disjoint->Release();  fq.disjoint = nullptr; }
			if (fq.frequency) { fq.frequency->Release(); fq.frequency = nullptr; }
			for (int i = 0; i < STAMPS_PER_FRAME; ++i)
			{
				if (fq.stamps[i]) { fq.stamps[i]->Release(); fq.stamps[i] = nullptr; }
			}
			fq.pending = false;
		}
		s_created = false;
		s_inFrame = false;
		s_writeIndex = 0;
		s_readIndex = 0;
	}


	bool createPool()
	{
		if (s_created)
			return true;
		if (!s_supported)
			return false;

		IDirect3DDevice9* dev = DX8Wrapper::_Get_D3D_Device8();
		if (dev == nullptr)
			return false;

		memset(s_frames, 0, sizeof(s_frames));

		for (int f = 0; f < FRAME_DEPTH; ++f)
		{
			FrameQueries& fq = s_frames[f];
			if (FAILED(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &fq.disjoint)) ||
				FAILED(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &fq.frequency)))
			{
				// Not an error worth shouting about -- plenty of hardware and every
				// reference rasterizer declines. Give up permanently and stay silent.
				destroyPool();
				s_supported = false;
				return false;
			}
			for (int i = 0; i < STAMPS_PER_FRAME; ++i)
			{
				if (FAILED(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &fq.stamps[i])))
				{
					destroyPool();
					s_supported = false;
					return false;
				}
			}
		}

		s_created = true;
		return true;
	}


	/// Non-blocking read. D3DGETDATA_FLUSH is deliberately not passed: it would push the
	/// command buffer to get an answer sooner, which is exactly the interference this is
	/// built to avoid.
	bool tryGetData( IDirect3DQuery9* q, void* dest, DWORD size )
	{
		if (q == nullptr)
			return false;
		return q->GetData(dest, size, 0) == S_OK;
	}
}


bool GpuTimer::Is_Supported()
{
	return s_supported;
}


void GpuTimer::Begin_Frame()
{
	if (!createPool())
		return;

	// A frame that never reached End_Frame -- an early-out somewhere in the draw path -- is
	// abandoned here rather than allowed to wedge the recorder for the rest of the run. Its
	// stamps are simply overwritten and the write cursor does not advance, so the only thing
	// lost is that one frame's timings.
	s_inFrame = false;

	FrameQueries& fq = s_frames[s_writeIndex];

	// The slot this is about to overwrite may still be in flight if the readback has fallen
	// behind (a stalled or very deep pipeline). Dropping it is correct -- the alternative is
	// waiting on the GPU.
	fq.pending = false;
	for (int i = 0; i < STAMPS_PER_FRAME; ++i)
		fq.stampIssued[i] = false;
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		fq.spanCount[i] = 0;
		fq.spanOpen[i] = false;
	}

	fq.disjoint->Issue(D3DISSUE_BEGIN);
	fq.frequency->Issue(D3DISSUE_END);
	fq.stamps[STAMP_FRAME_BEGIN]->Issue(D3DISSUE_END);
	fq.stampIssued[STAMP_FRAME_BEGIN] = true;

	s_inFrame = true;
}


void GpuTimer::Begin_Slot( int slot )
{
	if (!s_inFrame || slot < 0 || slot >= MAX_SLOTS)
		return;
	FrameQueries& fq = s_frames[s_writeIndex];
	if (fq.spanOpen[slot] || fq.spanCount[slot] >= MAX_SPANS_PER_SLOT)
		return;		// already inside one, or out of room -- later visits go unmeasured

	const int idx = stampIndex(slot, fq.spanCount[slot], 0);
	fq.stamps[idx]->Issue(D3DISSUE_END);
	fq.stampIssued[idx] = true;
	fq.spanOpen[slot] = true;
}


void GpuTimer::End_Slot( int slot )
{
	if (!s_inFrame || slot < 0 || slot >= MAX_SLOTS)
		return;
	FrameQueries& fq = s_frames[s_writeIndex];
	if (!fq.spanOpen[slot])
		return;		// never opened, or already closed

	const int idx = stampIndex(slot, fq.spanCount[slot], 1);
	fq.stamps[idx]->Issue(D3DISSUE_END);
	fq.stampIssued[idx] = true;
	fq.spanOpen[slot] = false;
	++fq.spanCount[slot];
}


void GpuTimer::End_Frame()
{
	if (!s_inFrame)
		return;

	FrameQueries& fq = s_frames[s_writeIndex];
	fq.stamps[STAMP_FRAME_END]->Issue(D3DISSUE_END);
	fq.stampIssued[STAMP_FRAME_END] = true;
	fq.disjoint->Issue(D3DISSUE_END);
	fq.pending = true;

	s_inFrame = false;
	s_writeIndex = (s_writeIndex + 1) % FRAME_DEPTH;
}


bool GpuTimer::Resolve( float* out_ms, float& out_total_ms )
{
	out_total_ms = 0.0f;
	for (int i = 0; i < MAX_SLOTS; ++i)
		out_ms[i] = 0.0f;

	if (!s_created)
		return false;

	FrameQueries& fq = s_frames[s_readIndex];
	if (!fq.pending)
		return false;

	BOOL disjoint = FALSE;
	if (!tryGetData(fq.disjoint, &disjoint, sizeof(disjoint)))
		return false;		// still in flight; try again next frame

	// From here the frame is consumed either way.
	fq.pending = false;
	s_readIndex = (s_readIndex + 1) % FRAME_DEPTH;

	if (disjoint)
	{
		// The clock changed mid-frame, so these timestamps are not comparable with each
		// other. This is the check whose absence made the earlier measurements meaningless.
		++s_disjointCount;
		return false;
	}

	UINT64 freq = 0;
	if (!tryGetData(fq.frequency, &freq, sizeof(freq)) || freq == 0)
		return false;

	UINT64 frameBegin = 0, frameEnd = 0;
	if (!tryGetData(fq.stamps[STAMP_FRAME_BEGIN], &frameBegin, sizeof(frameBegin)) ||
		!tryGetData(fq.stamps[STAMP_FRAME_END], &frameEnd, sizeof(frameEnd)))
		return false;

	const double toMs = 1000.0 / (double)freq;

	for (int slot = 0; slot < MAX_SLOTS; ++slot)
	{
		double slotMs = 0.0;
		for (int span = 0; span < fq.spanCount[slot]; ++span)
		{
			const int b = stampIndex(slot, span, 0);
			const int e = stampIndex(slot, span, 1);
			if (!fq.stampIssued[b] || !fq.stampIssued[e])
				continue;

			UINT64 begin = 0, end = 0;
			if (!tryGetData(fq.stamps[b], &begin, sizeof(begin)) ||
				!tryGetData(fq.stamps[e], &end, sizeof(end)))
				continue;

			if (end > begin)
				slotMs += (double)(end - begin) * toMs;
		}
		out_ms[slot] = (float)slotMs;
	}

	if (frameEnd > frameBegin)
		out_total_ms = (float)((double)(frameEnd - frameBegin) * toMs);

	return true;
}


unsigned GpuTimer::Get_Disjoint_Count()
{
	return s_disjointCount;
}


void GpuTimer::Reset_Disjoint_Count()
{
	s_disjointCount = 0;
}


void GpuTimer::Release()
{
	destroyPool();
}

#endif // defined(RTS_DEBUG)

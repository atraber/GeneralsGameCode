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


#if defined(RTS_DEBUG)


enum { NUM_TIMING_LINES = 3 };

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


static Bool s_overlayEnabled = FALSE;

void toggleFrameTimingOverlay()
{
	s_overlayEnabled = !s_overlayEnabled;
}

Bool isFrameTimingOverlayEnabled()
{
	return s_overlayEnabled;
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
		phases.format(L"logic %.2f  client %.2f  draw %.2f  shadow %.2f  depth %.2f",
			snap.phaseMs[FrameTiming::PHASE_LOGIC],
			snap.phaseMs[FrameTiming::PHASE_CLIENT],
			snap.phaseMs[FrameTiming::PHASE_DRAW],
			snap.phaseMs[FrameTiming::PHASE_SHADOWMAP],
			snap.phaseMs[FrameTiming::PHASE_DEPTHPREPASS]);
		s_lines[1]->setText(phases);

		phases.format(L"scene %.2f  postfx %.2f  ui %.2f  gpuwait %.2f   (cpu time; gpuwait = stalled on GPU)",
			snap.phaseMs[FrameTiming::PHASE_SCENE],
			snap.phaseMs[FrameTiming::PHASE_POSTFX],
			snap.phaseMs[FrameTiming::PHASE_UI],
			snap.phaseMs[FrameTiming::PHASE_PRESENT]);
		s_lines[2]->setText(phases);
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

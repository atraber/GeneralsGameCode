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
// Lives in the device independent layer, not in W3DDisplay, because it needs nothing but
// display strings and filled rectangles -- so both games get the same one.

#pragma once

#include "Common/GameCommon.h"

// Debug builds only, like the collector it reads. Callers guard their call sites rather
// than getting no-op stubs here: the readout is drawn from one place per game and toggled
// from one place, so two #if blocks are cheaper than pretending the thing exists.
#if defined(RTS_DEBUG)

/// Show or hide the readout. Independent of the classic debug stats -- both can be up at
/// once, and the readout moves below them when they are.
void toggleFrameTimingOverlay();
Bool isFrameTimingOverlayEnabled();

/// Draw the readout. Call once per frame, after the scene and the UI so nothing paints over
/// it. topY is where it may start drawing, which the caller pushes down past the classic
/// debug stats when those are also on screen.
void drawFrameTimingOverlay( Int topY );

/// Release the readout's display strings. Called when the display shuts down.
void shutdownFrameTimingOverlay();

// Write the same numbers the overlay shows to the debug log, once every
// W3D_FRAME_TIMING_LOG seconds. The overlay answers "what is it doing now" and needs
// somebody at the keyboard to press F10; an unattended run has neither, and a
// regression that only appears twelve minutes into a session is exactly the kind
// nobody is watching for when it arrives. Unset or 0 and this costs one integer test
// per frame.
void logFrameTimingSnapshot();

#endif // defined(RTS_DEBUG)

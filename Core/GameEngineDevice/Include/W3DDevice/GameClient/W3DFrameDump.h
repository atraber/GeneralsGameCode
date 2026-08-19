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

#pragma once

#include "Lib/BaseType.h"

// TheSuperHackers @feature andytraber 17/08/2026 Scheduled back-buffer captures, so an
// unattended replay leaves pictures of the frames worth looking at behind it.
//
// The schedule is in logic frames (-dumpFrames, -dumpEvery; -dumpTimes is converted to
// logic frames when it is parsed). The logic frame is what makes a capture repeatable: it
// is the game's own clock, so the same frame number is the same moment of the same game
// on every run, where a wall-clock second is whatever the machine happened to reach.
//
// The frame passed in is the one counted from the start of the watched game -- see
// getUnattendedRunFrame() -- and not the raw logic frame, so a schedule written for a replay
// starting at frame 0 means the same thing for a save game resuming at frame 18000.
//
// Call once per rendered frame, from inside the render block and before the frame is
// presented. Nothing is captured here -- this only decides that this frame is wanted and
// asks DX8Wrapper for the one instant at which the finished back buffer can be read.
void W3D_UpdateFrameDump(UnsignedInt runFrame);

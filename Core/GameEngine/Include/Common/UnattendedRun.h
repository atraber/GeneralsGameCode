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
#include "Common/AsciiString.h"

// TheSuperHackers @feature andytraber 19/08/2026 The clock every schedule of an unattended
// run is written against.
//
// -quitAtFrame, the frame dump and the camera script all name a logic frame, because that is
// the game's own clock: frame 900 is the in-game clock's 00:00:30.00 on every run, where a
// wall-clock second is whatever the machine reached. A replay starts its game at logic frame
// 0, so there the logic frame and the schedule are the same number and always have been.
//
// A save game is not like that. GameLogic::xfer restores m_frame, so a game saved ten minutes
// in resumes at logic frame ~18000, and every absolute schedule written for it -- quit at 900,
// capture at 300 -- has already gone by before the first frame is drawn. This counts from the
// frame the watched game actually started on: 0 for a replay, the restored frame for a save,
// so one schedule means the same thing whichever kind of run it is.
//
// Returns 0 while no game is running, which is what the shell and the load screen are.
UnsignedInt getUnattendedRunFrame();

// Load the save game named by -loadGame and hand it to the running engine. Call at the very
// end of GameEngine::init(), for the same reason the replay is started there: this resets the
// engine, and a resetSubsystems() afterwards would undo it. Returns whether the game started.
Bool startUnattendedSaveGame(const AsciiString& filename);

// The shutdown an unattended run wants: leave the same state behind an ordinary exit would,
// then quit. Defined per game in GameEngine.cpp.
void quitUnattendedRun();

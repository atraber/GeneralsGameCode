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
#include "PreRTS.h"

#include "Common/UnattendedRun.h"

#include "Common/GameState.h"
#include "Common/GlobalData.h"

#include "GameLogic/GameLogic.h"

// The logic frame the watched game started on. Latched on the first frame that is actually in
// a game rather than when the run was set up, because that is the earliest moment a save
// game's restored frame can be read: GameStateMap::xfer starts the new game before the block
// carrying m_frame has been loaded, so anything sampled during the load would read 0.
static UnsignedInt s_baseFrame = 0;
static Bool s_baseFrameValid = FALSE;

UnsignedInt getUnattendedRunFrame()
{
	// The shell map is a game too, and an interactive session is in one from the moment the
	// main menu appears. Latching there would make every schedule count from the shell map
	// instead of from the game the run is actually watching.
	if (TheGameLogic == nullptr || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame())
		return 0;

	const UnsignedInt frame = TheGameLogic->getFrame();

	if (!s_baseFrameValid)
	{
		s_baseFrameValid = TRUE;
		s_baseFrame = frame;

		if (frame != 0)
			DEBUG_LOG(("Unattended run: the watched game resumes at logic frame %d, schedules count from there", frame));
	}

	// A game that ends and is followed by another keeps the first one's base, rather than
	// silently restarting every schedule that has already run.
	return frame >= s_baseFrame ? frame - s_baseFrame : 0;
}

Bool startUnattendedSaveGame(const AsciiString& filename)
{
	if (TheGameState == nullptr || TheGameLogic == nullptr)
	{
		DEBUG_CRASH(("-loadGame: the engine is not far enough along to load a save game"));
		return FALSE;
	}

	if (TheGameState->doesSaveGameExist(filename) == FALSE)
	{
		DEBUG_LOG(("-loadGame: no save game '%s' in %s",
			filename.str(), TheGameState->getSaveDirectory().str()));
		return FALSE;
	}

	AvailableGameInfo gameInfo;
	gameInfo.filename = filename;
	gameInfo.next = nullptr;
	gameInfo.prev = nullptr;

	// loadGame() reads the type out of this to decide whether the file is a mission save, which
	// carries no game state and starts its map from scratch. Note the asymmetry in the two calls
	// below: this one wants the path, loadGame() wants the leaf.
	TheGameState->getSaveGameInfoFromFile(
		TheGameState->getFilePathInSaveDirectory(filename), &gameInfo.saveGameInfo);

	// What the shell's own load button does before loading from the main menu, and the command
	// line arrives at the same place: no game running, nothing on screen yet. The mode and
	// difficulty given here are placeholders -- GameStateMap::xfer restores the real ones out of
	// the save file -- but the window and the hidden shell it sets up are not.
	TheGameLogic->prepareNewGame(GAME_SINGLE_PLAYER, DIFFICULTY_NORMAL, 0);

	const SaveCode code = TheGameState->loadGame(gameInfo);
	if (code != SC_OK)
	{
		DEBUG_LOG(("-loadGame: '%s' failed to load (SaveCode %d)", filename.str(), (Int)code));
		return FALSE;
	}

	DEBUG_LOG(("-loadGame: loaded '%s', map '%s'",
		filename.str(), TheGameState->getSaveGameInfo()->pristineMapName.str()));

	return TRUE;
}

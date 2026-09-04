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

#include "GameClient/CameraScript.h"

#include "Common/GlobalData.h"
#include "Common/UnattendedRun.h"

#include "GameClient/CommandXlat.h"   // FilterTypes / FilterModes, for the filter cue
#include "GameClient/Display.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/View.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/TerrainLogic.h"

#include <vector>
#include <algorithm>

enum CameraCueType CPP_11(: Int)
{
	CUE_SETPOS,
	CUE_ANGLE,
	CUE_ZOOM,
	CUE_PITCH,
	CUE_MOVETO,
	CUE_LOOKAT,
	CUE_RESET,
	CUE_ROTATE,
	CUE_WAYPOINT,
	CUE_PATH,
	CUE_FOLLOW,
	CUE_UNFOLLOW,
	CUE_HUD,
	CUE_DEBUGVIS,
	CUE_FILTER,
	CUE_QUIT,
};

struct CameraCue
{
	UnsignedInt frame;
	CameraCueType type;
	Real args[5];
	AsciiString name;
};

// Sorted by time, and only ever walked forwards. Parsed once on the first update, because the
// command line cannot change afterwards and the file is read from the host filesystem rather
// than the game's, so there is nothing to wait for.
static std::vector<CameraCue> s_cues;
static size_t s_nextCue = 0;
static Bool s_initialized = FALSE;
static Bool s_enabled = FALSE;

static const Int MAX_CUE_TOKENS = 8;

// ------------------------------------------------------------------------------------------------
// Parsing
// ------------------------------------------------------------------------------------------------

static Bool cueSortByFrame(const CameraCue& a, const CameraCue& b)
{
	return a.frame < b.frame;
}

static Bool toReal(const char* token, Real& out)
{
	char* end = nullptr;
	const double value = strtod(token, &end);
	if (end == token || *end != '\0')
		return FALSE;

	out = (Real)value;
	return TRUE;
}

// Anything a cue leaves off is 0, which every duration reads as "at once".
static Real optionalReal(char** tokens, Int count, Int index)
{
	Real value = 0.0f;
	if (index < count && toReal(tokens[index], value))
		return value;

	return 0.0f;
}

static Bool requireReals(char** tokens, Int count, Int first, Int howMany, Real* out,
	const char* source, Int line)
{
	if (first + howMany > count)
	{
		DEBUG_LOG(("CAMERA SCRIPT: %s line %d: '%s' wants %d more number(s)",
			source, line, tokens[1], howMany));
		return FALSE;
	}

	for (Int i = 0; i < howMany; ++i)
	{
		if (!toReal(tokens[first + i], out[i]))
		{
			DEBUG_LOG(("CAMERA SCRIPT: %s line %d: '%s' is not a number",
				source, line, tokens[first + i]));
			return FALSE;
		}
	}

	return TRUE;
}

// One cue, already split into tokens. tokens[0] is the time, tokens[1] the command. A cue that
// does not parse is dropped with a line of log and the rest of the script still runs: a run
// that has already loaded a save game is expensive to throw away over one bad line.
static void parseCue(char** tokens, Int count, const char* source, Int line)
{
	Real seconds = 0.0f;
	if (!toReal(tokens[0], seconds) || seconds < 0.0f)
	{
		DEBUG_LOG(("CAMERA SCRIPT: %s line %d: '%s' is not a time in seconds",
			source, line, tokens[0]));
		return;
	}

	if (count < 2)
	{
		DEBUG_LOG(("CAMERA SCRIPT: %s line %d: a time with no command", source, line));
		return;
	}

	CameraCue cue;
	// The logic frame rate is fixed, so seconds and frames are the same schedule written two
	// ways. -dumpTimes converts the same way, which is what lets a cue and a capture written
	// for the same second name the same moment.
	cue.frame = (UnsignedInt)(seconds * LOGICFRAMES_PER_SECONDS_REAL + 0.5f);
	for (Int i = 0; i < 5; ++i)
		cue.args[i] = 0.0f;

	const char* command = tokens[1];

	if (stricmp(command, "setpos") == 0)
	{
		cue.type = CUE_SETPOS;
		if (!requireReals(tokens, count, 2, 2, cue.args, source, line))
			return;
	}
	else if (stricmp(command, "angle") == 0)
	{
		cue.type = CUE_ANGLE;
		if (!requireReals(tokens, count, 2, 1, cue.args, source, line))
			return;
	}
	else if (stricmp(command, "zoom") == 0 || stricmp(command, "pitch") == 0)
	{
		cue.type = (stricmp(command, "zoom") == 0) ? CUE_ZOOM : CUE_PITCH;
		if (!requireReals(tokens, count, 2, 1, cue.args, source, line))
			return;
		cue.args[1] = optionalReal(tokens, count, 3);	// duration
		cue.args[2] = optionalReal(tokens, count, 4);	// ease in
		cue.args[3] = optionalReal(tokens, count, 5);	// ease out
	}
	else if (stricmp(command, "moveto") == 0 || stricmp(command, "lookat") == 0 ||
		stricmp(command, "reset") == 0)
	{
		cue.type = (stricmp(command, "moveto") == 0) ? CUE_MOVETO :
			(stricmp(command, "lookat") == 0) ? CUE_LOOKAT : CUE_RESET;
		if (!requireReals(tokens, count, 2, 2, cue.args, source, line))
			return;
		cue.args[2] = optionalReal(tokens, count, 4);	// duration
		cue.args[3] = optionalReal(tokens, count, 5);	// ease in
		cue.args[4] = optionalReal(tokens, count, 6);	// ease out
	}
	else if (stricmp(command, "rotate") == 0)
	{
		cue.type = CUE_ROTATE;
		if (!requireReals(tokens, count, 2, 1, cue.args, source, line))
			return;
		cue.args[1] = optionalReal(tokens, count, 3);	// duration
		cue.args[2] = optionalReal(tokens, count, 4);	// ease in
		cue.args[3] = optionalReal(tokens, count, 5);	// ease out
	}
	else if (stricmp(command, "waypoint") == 0 || stricmp(command, "path") == 0)
	{
		cue.type = (stricmp(command, "waypoint") == 0) ? CUE_WAYPOINT : CUE_PATH;
		if (count < 3)
		{
			DEBUG_LOG(("CAMERA SCRIPT: %s line %d: '%s' wants a waypoint name",
				source, line, command));
			return;
		}
		cue.name = tokens[2];
		cue.args[0] = optionalReal(tokens, count, 3);	// duration
		cue.args[1] = optionalReal(tokens, count, 4);	// ease in
		cue.args[2] = optionalReal(tokens, count, 5);	// ease out
	}
	else if (stricmp(command, "follow") == 0)
	{
		cue.type = CUE_FOLLOW;
		if (count < 3)
		{
			DEBUG_LOG(("CAMERA SCRIPT: %s line %d: 'follow' wants an object name", source, line));
			return;
		}
		cue.name = tokens[2];
	}
	else if (stricmp(command, "unfollow") == 0)
	{
		cue.type = CUE_UNFOLLOW;
	}
	else if (stricmp(command, "hud") == 0)
	{
		cue.type = CUE_HUD;
		if (count < 3 || (stricmp(tokens[2], "on") != 0 && stricmp(tokens[2], "off") != 0))
		{
			DEBUG_LOG(("CAMERA SCRIPT: %s line %d: 'hud' wants on or off", source, line));
			return;
		}
		cue.args[0] = (stricmp(tokens[2], "on") == 0) ? 1.0f : 0.0f;
	}
	// The mode numbers are literals, not the DebugVisMode enumerators: that enum lives
	// in WW3D2 and the game layer reaches the visualizations through Display, which is
	// device independent on purpose. Names are what a script should use; the numbers
	// are here so a mode added later can be reached before it is given one.
	else if (stricmp(command, "debugvis") == 0)
	{
		cue.type = CUE_DEBUGVIS;
		if (count < 3)
		{
			DEBUG_LOG(("CAMERA SCRIPT: %s line %d: 'debugvis' wants a mode number or name", source, line));
			return;
		}
		Real val = 0.0f;
		if (toReal(tokens[2], val))
		{
			cue.args[0] = val;
		}
		else if (stricmp(tokens[2], "off") == 0)
		{
			cue.args[0] = 0.0f;
		}
		else if (stricmp(tokens[2], "overdraw") == 0)
		{
			cue.args[0] = 1.0f;
		}
		else if (stricmp(tokens[2], "wireframe") == 0)
		{
			cue.args[0] = 2.0f;
		}
		else if (stricmp(tokens[2], "normals") == 0 || stricmp(tokens[2], "normal") == 0)
		{
			cue.args[0] = 3.0f;
		}
		else if (stricmp(tokens[2], "bloom") == 0)
		{
			cue.args[0] = 4.0f;
		}
		else if (stricmp(tokens[2], "shroud") == 0)
		{
			cue.args[0] = 5.0f;
		}
		else if (stricmp(tokens[2], "shadow") == 0)
		{
			cue.args[0] = 6.0f;
		}
		else if (stricmp(tokens[2], "depth") == 0)
		{
			cue.args[0] = 7.0f;
		}
		else if (stricmp(tokens[2], "technique") == 0)
		{
			cue.args[0] = 8.0f;
		}
		else
		{
			DEBUG_LOG(("CAMERA SCRIPT: %s line %d: unknown debugvis mode '%s'", source, line, tokens[2]));
			return;
		}
	}
	// Screen filters, which nothing else can reach in an unattended run.
	//
	// They are turned on by demo meta-messages bound to keys -- MSG_META_DEMO_TOGGLE_BW_VIEW
	// and its neighbours -- so a replay that was recorded without somebody pressing one draws
	// none of them, and none of the shipped corpus does. That made every screen filter a path
	// a change could be made to, reported as verified because no counter moved, and never
	// once executed. This is the hand on that key.
	//
	// Two enums, one word: the filter type and its mode are always set together and there is
	// no useful pairing that crosses them, so the cue names the pair.
	else if (stricmp(command, "filter") == 0)
	{
		cue.type = CUE_FILTER;
		if (count < 3)
		{
			DEBUG_LOG(("CAMERA SCRIPT: %s line %d: 'filter' wants a name", source, line));
			return;
		}
		const char* f = tokens[2];
		if      (stricmp(f, "off") == 0)            { cue.args[0] = FT_NULL_FILTER;            cue.args[1] = FM_NULL_MODE; }
		else if (stricmp(f, "bw") == 0)             { cue.args[0] = FT_VIEW_BW_FILTER;         cue.args[1] = FM_VIEW_BW_BLACK_AND_WHITE; }
		else if (stricmp(f, "bwred") == 0)          { cue.args[0] = FT_VIEW_BW_FILTER;         cue.args[1] = FM_VIEW_BW_RED_AND_WHITE; }
		else if (stricmp(f, "bwgreen") == 0)        { cue.args[0] = FT_VIEW_BW_FILTER;         cue.args[1] = FM_VIEW_BW_GREEN_AND_WHITE; }
		else if (stricmp(f, "crossfade") == 0)      { cue.args[0] = FT_VIEW_CROSSFADE;         cue.args[1] = FM_VIEW_CROSSFADE_CIRCLE; }
		else if (stricmp(f, "crossfademask") == 0)  { cue.args[0] = FT_VIEW_CROSSFADE;         cue.args[1] = FM_VIEW_CROSSFADE_FB_MASK; }
		else if (stricmp(f, "motionblur") == 0)     { cue.args[0] = FT_VIEW_MOTION_BLUR_FILTER; cue.args[1] = FM_VIEW_MB_IN_AND_OUT_ALPHA; }
		else if (stricmp(f, "motionblurpan") == 0)  { cue.args[0] = FT_VIEW_MOTION_BLUR_FILTER; cue.args[1] = FM_VIEW_MB_PAN_ALPHA; }
		else if (stricmp(f, "default") == 0)        { cue.args[0] = FT_VIEW_DEFAULT;           cue.args[1] = FM_VIEW_DEFAULT; }
		else if (stricmp(f, "bloom") == 0)          { cue.args[0] = FT_VIEW_BLOOM;             cue.args[1] = FM_VIEW_BLOOM; }
		else
		{
			DEBUG_LOG(("CAMERA SCRIPT: %s line %d: unknown filter '%s'", source, line, f));
			return;
		}
	}
	else if (stricmp(command, "quit") == 0)
	{
		cue.type = CUE_QUIT;
	}
	else
	{
		DEBUG_LOG(("CAMERA SCRIPT: %s line %d: unknown command '%s'", source, line, command));
		return;
	}

	s_cues.push_back(cue);
}

// Cues are separated by newlines and by ';', so the same grammar reads from a file and from a
// single command line argument. Splits in place, which is why the caller owns a writable copy.
static void parseText(char* text, const char* source)
{
	Int line = 1;
	char* cursor = text;

	while (*cursor != '\0')
	{
		char* statement = cursor;

		while (*cursor != '\0' && *cursor != '\n' && *cursor != '\r' && *cursor != ';')
			++cursor;

		const char terminator = *cursor;
		if (terminator != '\0')
			*cursor++ = '\0';

		// A comment runs to the end of its statement.
		char* comment = strchr(statement, '#');
		if (comment != nullptr)
			*comment = '\0';

		char* tokens[MAX_CUE_TOKENS];
		Int count = 0;
		char* token = statement;
		while (*token != '\0' && count < MAX_CUE_TOKENS)
		{
			while (*token == ' ' || *token == '\t' || *token == ',')
				++token;
			if (*token == '\0')
				break;

			tokens[count++] = token;

			while (*token != '\0' && *token != ' ' && *token != '\t' && *token != ',')
				++token;
			if (*token != '\0')
				*token++ = '\0';
		}

		if (count > 0)
			parseCue(tokens, count, source, line);

		if (terminator == '\n')
			++line;
	}
}

static Bool readFile(const AsciiString& path, std::vector<char>& buffer)
{
	FILE* file = fopen(path.str(), "rb");
	if (file == nullptr)
		return FALSE;

	fseek(file, 0, SEEK_END);
	const long size = ftell(file);
	fseek(file, 0, SEEK_SET);

	if (size < 0)
	{
		fclose(file);
		return FALSE;
	}

	buffer.resize((size_t)size + 1);
	const size_t read = fread(&buffer[0], 1, (size_t)size, file);
	buffer[read] = '\0';
	fclose(file);

	return TRUE;
}

static void initialize()
{
	s_initialized = TRUE;

	if (TheGlobalData->m_cameraScriptFile.isNotEmpty())
	{
		std::vector<char> buffer;
		if (readFile(TheGlobalData->m_cameraScriptFile, buffer))
			parseText(&buffer[0], TheGlobalData->m_cameraScriptFile.str());
		else
			DEBUG_LOG(("CAMERA SCRIPT: cannot read '%s'", TheGlobalData->m_cameraScriptFile.str()));
	}

	if (TheGlobalData->m_cameraScriptText.isNotEmpty())
	{
		std::vector<char> buffer(TheGlobalData->m_cameraScriptText.getLength() + 1);
		strcpy(&buffer[0], TheGlobalData->m_cameraScriptText.str());
		parseText(&buffer[0], "-camera");
	}

	// Cues written for the same time keep the order they were written in, which is the only way
	// a pair like "setpos then angle" means what it looks like.
	std::stable_sort(s_cues.begin(), s_cues.end(), cueSortByFrame);

	s_enabled = !s_cues.empty();
	if (!s_enabled)
		return;

	DEBUG_LOG(("CAMERA SCRIPT: %d cues, the last one at logic frame %d",
		(Int)s_cues.size(), s_cues.back().frame));

	// The replay camera writes the recorded player's camera every frame it gets a message for,
	// which would win every argument with a cue. A script is an explicit statement about where
	// the camera should be, so it takes the wheel off the recording.
	if (TheGlobalData->m_useCameraInReplay)
	{
		TheWritableGlobalData->m_useCameraInReplay = FALSE;
		DEBUG_LOG(("CAMERA SCRIPT: turned the replay camera off, the script drives instead"));
	}

	if (TheGlobalData->m_headless)
		DEBUG_LOG(("CAMERA SCRIPT: -headless has no real view, so the cues will do nothing"));
}

// ------------------------------------------------------------------------------------------------
// Running
// ------------------------------------------------------------------------------------------------

static Int millisecondsOf(Real seconds)
{
	return (Int)(seconds * 1000.0f);
}

static void executeCue(const CameraCue& cue)
{
	View* view = TheTacticalView;

	switch (cue.type)
	{
		case CUE_SETPOS:
		{
			Coord3D pos;
			pos.x = cue.args[0];
			pos.y = cue.args[1];
			pos.z = 0.0f;
			// In user mode, as the replay camera is, so the camera goes on adjusting its height
			// over terrain elevations instead of holding the height of wherever it came from.
			view->userSetPosition(pos);
			view->userResetPivotToGround();
			break;
		}

		case CUE_ANGLE:
			view->userSetAngle(DEG_TO_RADF(cue.args[0]));
			break;

		case CUE_ZOOM:
			view->zoomCamera(cue.args[0], millisecondsOf(cue.args[1]),
				millisecondsOf(cue.args[2]), millisecondsOf(cue.args[3]));
			break;

		case CUE_PITCH:
			view->pitchCamera(cue.args[0], millisecondsOf(cue.args[1]),
				millisecondsOf(cue.args[2]), millisecondsOf(cue.args[3]));
			break;

		case CUE_MOVETO:
		{
			Coord3D pos;
			pos.x = cue.args[0];
			pos.y = cue.args[1];
			pos.z = 0.0f;
			view->moveCameraTo(&pos, millisecondsOf(cue.args[2]), 0, true,
				millisecondsOf(cue.args[3]), millisecondsOf(cue.args[4]));
			break;
		}

		case CUE_LOOKAT:
		{
			Coord3D pos;
			pos.x = cue.args[0];
			pos.y = cue.args[1];
			pos.z = 0.0f;
			view->rotateCameraTowardPosition(&pos, millisecondsOf(cue.args[2]),
				millisecondsOf(cue.args[3]), millisecondsOf(cue.args[4]), FALSE);
			break;
		}

		case CUE_RESET:
		{
			Coord3D pos;
			pos.x = cue.args[0];
			pos.y = cue.args[1];
			pos.z = 0.0f;
			view->resetCamera(&pos, millisecondsOf(cue.args[2]),
				millisecondsOf(cue.args[3]), millisecondsOf(cue.args[4]));
			break;
		}

		case CUE_ROTATE:
			view->rotateCamera(cue.args[0], millisecondsOf(cue.args[1]),
				millisecondsOf(cue.args[2]), millisecondsOf(cue.args[3]));
			break;

		case CUE_WAYPOINT:
		case CUE_PATH:
		{
			Waypoint* waypoint = (TheTerrainLogic != nullptr) ?
				TheTerrainLogic->getWaypointByName(cue.name) : nullptr;
			if (waypoint == nullptr)
			{
				DEBUG_LOG(("CAMERA SCRIPT: the loaded map has no waypoint '%s'", cue.name.str()));
				break;
			}

			if (cue.type == CUE_WAYPOINT)
			{
				Coord3D pos = *waypoint->getLocation();
				view->moveCameraTo(&pos, millisecondsOf(cue.args[0]), 0, true,
					millisecondsOf(cue.args[1]), millisecondsOf(cue.args[2]));
			}
			else
			{
				view->moveCameraAlongWaypointPath(waypoint, millisecondsOf(cue.args[0]), 0, true,
					millisecondsOf(cue.args[1]), millisecondsOf(cue.args[2]));
			}
			break;
		}

		case CUE_FOLLOW:
		{
			Object* object = (TheScriptEngine != nullptr) ?
				TheScriptEngine->getUnitNamed(cue.name) : nullptr;
			if (object == nullptr)
			{
				DEBUG_LOG(("CAMERA SCRIPT: the loaded game has no object named '%s'", cue.name.str()));
				break;
			}

			view->setCameraLock(object->getID());
			view->snapToCameraLock();
			view->setSnapMode(View::LOCK_FOLLOW, 0.0f);
			break;
		}

		case CUE_UNFOLLOW:
			view->setCameraLock(INVALID_ID);
			break;

		case CUE_HUD:
			if (cue.args[0] != 0.0f)
				ShowControlBar();
			else
				HideControlBar();
			break;

		case CUE_DEBUGVIS:
#if defined(RTS_DEBUG)
			if (TheDisplay != nullptr)
			{
				TheDisplay->cycleDebugVisualization(0);
				if (cue.args[0] > 0.0f)
					TheDisplay->cycleDebugVisualization((Int)cue.args[0]);
			}
#endif
			break;

		case CUE_FILTER:
			// Mode before type: setViewFilter is what starts the filter running, and a
			// filter that starts before its mode is set draws one frame of the mode the
			// previous one left behind. The demo meta-messages set them in this order too.
			view->setViewFilterMode((FilterModes)(Int)cue.args[1]);
			view->setViewFilter((FilterTypes)(Int)cue.args[0]);
			break;

		case CUE_QUIT:
			quitUnattendedRun();
			break;
	}
}

void CameraScript_Update(UnsignedInt runFrame)
{
	if (!s_initialized)
		initialize();

	if (!s_enabled || s_nextCue >= s_cues.size())
		return;

	// Nothing to point a camera at until a game is running, and runFrame is 0 for the whole of
	// the shell and the load screen, where every cue written for time 0 would look due.
	if (TheGameLogic == nullptr || !TheGameLogic->isInGame() || TheTacticalView == nullptr)
		return;

	// Fire everything the clock has reached. A cue whose frame was never a rendered frame --
	// the logic clock outruns the render clock in fast mode -- fires at the first frame past
	// it rather than being skipped.
	while (s_nextCue < s_cues.size() && s_cues[s_nextCue].frame <= runFrame)
	{
		const CameraCue& cue = s_cues[s_nextCue];
		++s_nextCue;

		DEBUG_LOG(("CAMERA SCRIPT: frame %d, cue %d of %d (written for frame %d)",
			runFrame, (Int)s_nextCue, (Int)s_cues.size(), cue.frame));

		executeCue(cue);
	}
}

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

#include "W3DDevice/GameClient/W3DFrameDump.h"
#include "W3DDevice/GameClient/W3DScreenshot.h"
#include "Common/GlobalData.h"
#include "WW3D2/dx8wrapper.h"
#include <vector>
#include <algorithm>

// The frames still to be captured, ascending, and how far through them we are. Parsed once
// from TheGlobalData on the first update, because the command line cannot change afterwards.
static std::vector<UnsignedInt> s_dumpFrames;
static size_t s_nextDumpFrame = 0;
static UnsignedInt s_lastPeriodicFrame = 0;
static Bool s_initialized = FALSE;
static Bool s_enabled = FALSE;

// Each run writes into its own folder. Overwriting the previous run's files would be the
// more convenient default right up to the first time a capture silently fails to be written
// and the stale file from the last run gets read as this run's result.
static char s_runDirectory[_MAX_PATH];

// What the pending capture is called. The callback runs later in the same frame, so this
// never has to hold more than one.
static char s_pendingLeafname[_MAX_FNAME];

static void parseFrameList(const AsciiString& spec, std::vector<UnsignedInt>& frames)
{
	const char* cursor = spec.str();
	while (cursor != nullptr && *cursor != '\0')
	{
		char* end = nullptr;
		const unsigned long frame = strtoul(cursor, &end, 10);
		if (end == cursor)
			break;	// not a number; give up rather than spin

		frames.push_back((UnsignedInt)frame);

		cursor = end;
		while (*cursor == ',' || *cursor == ' ')
			++cursor;
	}
}

static void initialize()
{
	s_initialized = TRUE;

	parseFrameList(TheGlobalData->m_frameDumpFrames, s_dumpFrames);
	std::sort(s_dumpFrames.begin(), s_dumpFrames.end());
	s_dumpFrames.erase(std::unique(s_dumpFrames.begin(), s_dumpFrames.end()), s_dumpFrames.end());

	s_enabled = !s_dumpFrames.empty() || TheGlobalData->m_frameDumpEvery > 0;
	if (!s_enabled)
		return;

	SYSTEMTIME st;
	GetLocalTime(&st);
	sprintf(s_runDirectory, "Screenshots\\FrameDump\\%04d%02d%02d_%02d%02d%02d\\",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

	DEBUG_LOG(("FRAME DUMP: writing to <UserData>\\%s (%d requested frames, every %d)",
		s_runDirectory, (Int)s_dumpFrames.size(), TheGlobalData->m_frameDumpEvery));
}

// Created on the first capture rather than up front, so a run that turns out to reach none of
// its requested frames -- a replay that ends before them -- leaves no empty folder behind.
// Created here rather than left to the screenshot thread, because the shadow map is written
// straight to disk by the caller and cannot wait on that thread to get there.
static void ensureRunDirectory()
{
	static Bool created = FALSE;
	if (created)
		return;
	created = TRUE;

	char pathname[_MAX_PATH];
	strlcpy(pathname, TheGlobalData->getPath_UserData().str(), ARRAY_SIZE(pathname));
	const size_t rootLength = strlen(pathname);
	strlcat(pathname, s_runDirectory, ARRAY_SIZE(pathname));
	for (size_t i = rootLength; pathname[i] != '\0'; ++i)
	{
		if (pathname[i] == '\\')
		{
			pathname[i] = '\0';
			CreateDirectory(pathname, nullptr);
			pathname[i] = '\\';
		}
	}
}

static void frameDumpCallback(void* /*userData*/)
{
	ensureRunDirectory();

	W3D_TakeCompressedScreenshotNamed(SCREENSHOT_PNG, 0, s_runDirectory, s_pendingLeafname, FALSE);

	if (TheGlobalData->m_frameDumpShadowMap)
	{
		char pathname[_MAX_PATH];
		sprintf(pathname, "%s%s%s_shadowmap.png",
			TheGlobalData->getPath_UserData().str(), s_runDirectory, s_pendingLeafname);
		if (!DX8Wrapper::Dump_Shadow_Map(pathname))
			DEBUG_LOG(("FRAME DUMP: no shadow map to write for %s", s_pendingLeafname));
	}
}

void W3D_UpdateFrameDump(UnsignedInt runFrame)
{
	if (!s_initialized)
		initialize();

	if (!s_enabled)
		return;

	// Frame 0 is the shell and the load screen, where the run clock has not started and every
	// frame would look due. Nothing before the watched game is running is worth capturing.
	if (runFrame == 0)
		return;

	Bool due = FALSE;

	// Requested frames. Rendered frames and logic frames do not advance together -- and in
	// TiVo fast mode only every thirtieth logic frame is drawn at all -- so a requested frame
	// is very often never a rendered frame. Match the first rendered frame at or past it, and
	// retire every request the clock has already gone by so a skipped one cannot fire late.
	while (s_nextDumpFrame < s_dumpFrames.size() && s_dumpFrames[s_nextDumpFrame] <= runFrame)
	{
		++s_nextDumpFrame;
		due = TRUE;
	}

	if (TheGlobalData->m_frameDumpEvery > 0 &&
		runFrame - s_lastPeriodicFrame >= (UnsignedInt)TheGlobalData->m_frameDumpEvery)
	{
		s_lastPeriodicFrame = runFrame;
		due = TRUE;
	}

	if (!due)
		return;

	// Named for the frame actually captured, not the one that was asked for, so the file says
	// which moment of the game it is a picture of.
	sprintf(s_pendingLeafname, "frame_%06u", runFrame);
	DX8Wrapper::Request_Post_Scene_Callback(frameDumpCallback, nullptr);
}

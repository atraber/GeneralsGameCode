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

#include "GameClient/Display.h"

void W3D_TakeCompressedScreenshot(ScreenshotFormat format, Int jpegQuality);

// TheSuperHackers @feature andytraber 17/08/2026 Write the back buffer under a name and a
// subfolder the caller chooses, instead of a timestamp under Screenshots.
//
// announce == FALSE keeps the "screenshot taken" message off the screen. That is not
// cosmetic for an automated capture: the message is drawn into the frames that follow it,
// so an announced capture would appear inside the next captured frame.
//
// leafname carries no extension; the one matching format is appended. subDirectory is
// relative to the user data folder and its components are created as needed.
void W3D_TakeCompressedScreenshotNamed(ScreenshotFormat format, Int jpegQuality,
	const char* subDirectory, const char* leafname, Bool announce);

// Called once per frame on the main thread to show messages for screenshots
// that the screenshot thread has finished writing.
void W3D_UpdateScreenshotMessages();


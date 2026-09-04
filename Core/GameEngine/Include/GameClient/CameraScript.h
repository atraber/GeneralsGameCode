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

// TheSuperHackers @feature andytraber 19/08/2026 A camera timeline for an unattended run.
//
// A replay carries the camera the recording player was using; a game watched live -- one
// started by -loadGame -- carries nobody, and without this the camera sits wherever the save
// left it for the whole run. This is the missing hand on the mouse: a list of cues, each at a
// stated time, each calling one of the camera moves the map scripts already use.
//
// The cues come from -cameraScript <file> and from -camera "<cues>" on the command line, both
// in the same grammar and both accumulating. One cue per line (or per ';' in the inline form),
// '#' starts a comment, and the first field of every cue is a time in seconds:
//
//   # t(s)  command                     what it does
//   0.0     setpos    2400 1800         put the camera pivot here, at once
//   0.0     angle     45                face this compass angle, in degrees
//   0.0     zoom      0.7               1.0 is the default height, less is closer in
//   0.0     pitch     1.0 2.0           the scripted pitch factor, over 2 seconds
//   5.0     moveto    3100 2200 3.0     glide the pivot here over 3 seconds
//   5.0     moveto    3100 2200 3 .5 .5 the same, easing in and out for half a second each
//   12.0    lookat    3300 2400 2.0     rotate to face this point over 2 seconds
//   16.0    rotate    0.25 4.0          spin a quarter turn about the pivot over 4 seconds
//   20.0    waypoint  AttackPoint 3.0   glide to a waypoint of the loaded map
//   24.0    path      PatrolPath 10.0   glide along a waypoint path of the loaded map
//   30.0    reset     2400 1800 2.0     move there and return angle, pitch and zoom to default
//   34.0    follow    Bulldozer01       lock the camera onto a named object of the map
//   40.0    unfollow
//   40.0    hud       off               hide the control bar, for a clean capture
//   42.0    filter    bw                turn on a screen filter: bw, bwred, bwgreen,
//                                       crossfade, crossfademask, motionblur, motionblurpan,
//                                       default, bloom, or off
//   45.0    quit                        end the run here
//
// Every duration is in seconds and may be left off, in which case the move happens at once.
// Times are on the same clock as -dumpTimes and -quitAtFrame -- see getUnattendedRunFrame() --
// so a cue and a capture written for the same second are the same moment of the same game on
// every run.
//
// Call once per client update, before the view is updated, so a cue takes effect on the frame
// it was written for rather than the one after.
void CameraScript_Update(UnsignedInt runFrame);

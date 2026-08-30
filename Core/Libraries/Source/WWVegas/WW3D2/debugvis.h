/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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

#include "always.h"

/*
** DebugVisMode -- which in-game debug visualization is on screen, if any.
**
** One key cycles through these (F11 by default; see MetaMap::generateMetaMap), so
** they form a single ordered list rather than a set of independent toggles. That is
** deliberate: a pile of independent toggles produces combinations nobody designed --
** the technique tint drawn under the shadow-map tile with the bloom overlay on top --
** and each of these modes is meant to be looked at on its own.
**
** The mode lives here, in WW3D2, rather than in TheGlobalData, because the two
** halves that read it sit on opposite sides of the engine: the per-draw modes are
** applied inside DX8Wrapper::Apply_Render_State_Changes, which is below the game
** engine and cannot see it, while the overlay modes are drawn from W3DDisplay, which
** is above. A value in GlobalData would have to be mirrored down here every frame;
** a value here is readable from both, and the game layer reaches it through
** Display::cycleDebugVisualization rather than by including this header.
**
** None of this is a rendering feature: every mode is available only in RTS_DEBUG
** builds, and DEBUG_VIS_OFF is the only value a release build can ever hold.
*/
enum DebugVisMode CPP_11(: int)
{
	// Draw the frame normally. The only mode a non-debug build has.
	DEBUG_VIS_OFF = 0,

	// ---- Draw inspectors: redraw the scene itself to say something about its draws ----

	// Every draw flat-shaded by the MeshTechnique it declared, and by the pipeline
	// that actually drew it.
	//
	// Answers the two questions that have cost the most time here. "Is this mesh
	// drawn by one pipeline?" -- a mesh that flickers between two colours frame to
	// frame is a mesh being split, which is what z-fights. And "what is left on
	// fixed function?" -- those draw in a colour nothing else uses, so the remaining
	// work is a thing you can point at on screen rather than a number in a census.

	// How many times each pixel was written, as accumulated brightness.
	//
	// Fill cost is invisible in a finished frame by construction: a pixel covered forty
	// times looks exactly like a pixel covered once. It is also the measurement the
	// usual instrument cannot make here -- the 30 Hz cap means wall-clock time ranks
	// nothing -- so a spatial picture of where the layers pile up is the only way to
	// find out whether the smoke, the sorted pass, or the terrain is the one spending
	// the frame.
	DEBUG_VIS_OVERDRAW,

	// The scene in wireframe, flat-shaded.
	//
	// Triangle density, read as line density: which meshes are paying for detail nobody
	// can see at this camera height, and where the terrain's own tessellation is going.
	DEBUG_VIS_WIREFRAME,

	// Camera-space surface normals as RGB, for the geometry that has them.
	//
	// A wrong normal is the quiet cause of a whole class of shading complaints -- a
	// surface lit from the wrong side, a PBR highlight that will not sit still, a mesh
	// that goes black at one camera angle -- and none of those name the normal as the
	// culprit. Flat colour across a curved surface means the normals are not there;
	// hard discontinuities across a smooth one mean they disagree across an edge.
	//
	// Deliberately last in the cycle, because it is the one mode with partial coverage:
	// it needs a vertex normal and a draw on the mesh path, so terrain, roads, water and
	// pre-lit geometry are shown flat rather than guessed at. See the legend.
	DEBUG_VIS_NORMALS,

	// ---- Buffer inspectors: draw something the frame was built from, over the top ----
	// What the bloom bright pass selected, in false colour over the scene.
	//
	// Where bloom comes from is the one thing about it that cannot be read off the
	// finished frame: the glow is blurred over everything near it, so a highlight that
	// blooms and a neighbour that merely sits under someone else's glow look alike. The
	// bright pass knows the difference and this shows its answer directly.
	DEBUG_VIS_BLOOM,
	// The shroud field itself, as a top-down tile.
	//
	// Every shroud bug so far has been a sampling bug -- geometry reading the field with
	// the wrong coordinates -- and those are invisible on a map whose shroud is uniform,
	// because sampling a flat field at the wrong place returns the right answer. This
	// shows what is actually in the field, so "the shroud looks fine" can be separated
	// from "the shroud is flat here and would look fine either way".
	DEBUG_VIS_SHROUD,
	// The sun's depth map, unpacked and drawn as a corner tile.
	//
	// The map is the input to every cast shadow, and almost every shadow bug is
	// visible in it before it is visible on the ground: a caster missing from the
	// map is a caster culled by the wrong frustum, and depth that saturates to flat
	// white or flat black is a sun projection whose near/far do not bracket the
	// scene. Reading it off the finished frame instead means guessing which of those
	// produced the same black roof.
	DEBUG_VIS_SHADOW_MAP,

	// The camera-space depth prepass, linearised, as a corner tile.
	//
	// This is what screen-space reflections march along, and a depth buffer that is
	// subtly wrong -- inverted, mis-scaled, or built from a projection that disagrees
	// with the one the scene was drawn with -- produces reflections that are merely
	// odd rather than obviously broken. Linearised because the stored value is
	// post-projection z/w, which spends almost its whole range in the first few metres
	// and shows the entire visible map as one shade just short of 1.
	DEBUG_VIS_DEPTH,

	DEBUG_VIS_COUNT
};

/*
** Human-readable mode name, for the on-screen banner and the log.
*/
const char * Debug_Vis_Mode_Name(DebugVisMode mode);

/*
** The colour a draw is flat-shaded with in DEBUG_VIS_MESH_TECHNIQUE, as 0xAARRGGBB.
**
** `fixedFunction` is what the dispatch decided for this particular draw, not what the
** asset declared, and it wins: MESH_TECHNIQUE_FIXED_FUNCTION names geometry that is
** *meant* to stay behind, while a surface that lands on fixed function anyway is a
** capability gap, and telling those apart on screen is most of the point. So a
** declared-FF draw and a fell-back draw get distinguishable colours rather than one.
*/

/*
** Legend for the mode, as a caller-formatted single line. Null for modes that have no
** legend (the shadow map tile labels itself).
*/
const char * Debug_Vis_Mode_Legend(DebugVisMode mode);

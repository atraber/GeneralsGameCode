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

#include "debugvis.h"
#include "meshtechnique.h"

const char * Debug_Vis_Mode_Name(DebugVisMode mode)
{
	switch (mode) {
		case DEBUG_VIS_OFF:				return "Off";
		case DEBUG_VIS_BLOOM:			return "Bloom bright pass";
		case DEBUG_VIS_SHROUD:			return "Shroud field";
		case DEBUG_VIS_SHADOW_MAP:		return "Shadow map";
		case DEBUG_VIS_DEPTH:			return "Camera depth";
		case DEBUG_VIS_MESH_TECHNIQUE:	return "Mesh technique";
		case DEBUG_VIS_OVERDRAW:		return "Overdraw";
		case DEBUG_VIS_WIREFRAME:		return "Wireframe";
		case DEBUG_VIS_NORMALS:			return "Normals";
		case DEBUG_VIS_CLUSTERS:		return "Cluster occupancy";
		case DEBUG_VIS_CLUSTER_OVERFLOW:return "Cluster overflow";
		default:						return "?";
	}
}

const char * Debug_Vis_Mode_Legend(DebugVisMode mode)
{
	switch (mode) {
		case DEBUG_VIS_MESH_TECHNIQUE:
			return "surface=green  prelit=blue  effect=yellow  "
				   "unclassified=grey  declared-FF=magenta  fell back to FF=red";
		case DEBUG_VIS_BLOOM:
			// The threshold is not quoted because it is not fixed: the bright pass is fed a
			// number that has to move with the scene buffer. What the tint means is constant.
			return "blue=just over threshold  cyan/yellow=well over  white=far over  "
				   "unpainted=does not bloom";
		case DEBUG_VIS_SHROUD:
			return "tile is the shroud field, map top-down; "
				   "black=shrouded  white=clear  contours every 1/16";
		case DEBUG_VIS_OVERDRAW:
			// No count is quoted against a colour on purpose. The accumulation goes
			// through the tone map like everything else in the scene, so the mapping from
			// layers to brightness is not fixed and a legend claiming otherwise would be
			// wrong under HDR. Brighter is always more, which is the part that holds.
			return "brightness = layers written; red->yellow->white as it piles up";
		case DEBUG_VIS_NORMALS:
			return "camera-space normal as RGB; "
				   "grey = no normal or not on the mesh path (terrain, roads, water, pre-lit)";
		case DEBUG_VIS_CLUSTERS:
			// The scale is quoted because unlike the overdraw ramp this one is fixed: its
			// ceiling is the index-list stride, not the frame's own maximum, so the same
			// colour means the same count in every frame and between two runs.
			return "lights in the busiest depth slice of each tile, log scale to the "
				   "64-light stride; blue=1 cyan=3 green=7 yellow=20 white=64+  "
				   "magenta = the index list disagrees with the count";
		case DEBUG_VIS_CLUSTER_OVERFLOW:
			return "red = a slice of this tile holds more than the 64-light stride  "
				   "orange = exactly 64, one light from overflowing  "
				   "magenta = the index list disagrees with the count";
		default:
			return nullptr;
	}
}


unsigned Debug_Vis_Technique_Color(int technique, bool fixedFunction)
{
	if (fixedFunction) {
		// Two different problems, two colours. Magenta is geometry the classifier
		// declared FIXED_FUNCTION: expected, and the size of the remaining burn-down.
		// Red is a draw whose asset said it could be shaded and which fell back
		// anyway -- a capability gap in the shaders, and the more interesting of the
		// two, so it gets the more alarming colour.
		if (technique == MESH_TECHNIQUE_FIXED_FUNCTION)
			return 0xFFFF00FF;   // magenta: declared fixed function
		return 0xFFFF2020;       // red: wanted the shader, did not get it
	}

	switch (technique) {
		case MESH_TECHNIQUE_SURFACE:	return 0xFF40C040;   // green
		case MESH_TECHNIQUE_PRELIT:		return 0xFF4080E0;   // blue
		case MESH_TECHNIQUE_EFFECT:		return 0xFFE0D040;   // yellow
		// No technique was declared, so this did not come through the mesh renderer:
		// terrain, water, the shroud, particles, 2D. Grey rather than a hue of its
		// own -- it is the backdrop the other four are being read against, and most
		// of a typical frame is this.
		case MESH_TECHNIQUE_UNCLASSIFIED:
		default:						return 0xFF808080;
	}
}

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

const char * Debug_Vis_Mode_Name(DebugVisMode mode)
{
	switch (mode) {
		case DEBUG_VIS_OFF:				return "Off";
		case DEBUG_VIS_BLOOM:			return "Bloom bright pass";
		case DEBUG_VIS_SHROUD:			return "Shroud field";
		case DEBUG_VIS_OVERDRAW:		return "Overdraw";
		case DEBUG_VIS_WIREFRAME:		return "Wireframe";
		case DEBUG_VIS_NORMALS:			return "Normals";
		default:						return "?";
	}
}

const char * Debug_Vis_Mode_Legend(DebugVisMode mode)
{
	switch (mode) {
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
		default:
			return nullptr;
	}
}


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

#include <ctype.h>

/*
** Meshes that are rotor discs but were never flagged as such.
**
** A spinning rotor or propeller is drawn as a flat, alpha-blended, depth-write-off quad,
** which is a decal's signature exactly -- a tyre track and a rotor disc arrive at the
** wrapper byte-identical, so no render state can tell them apart. The artist's answer is
** W3D_MESH_FLAG_CAST_SHADOW, and DX8MeshRendererClass hands it to the depth pass as
** m_bMeshCastsShadow.
**
** On some assets the artist forgot. Counted over every W3D shipped with the game -- 8,897
** models, 70,559 meshes -- exactly 25 models contain a soft-blended, depth-write-off mesh
** whose name reads as a rotor, and every one of them is an aircraft. They split cleanly by
** naming convention rather than by anything about the geometry:
**
**   flagged:   AVChinook.PROPS01/02, NVHelix.NVHELIX_PROPS01/02, AVComanche.AVCOMANCHE_PROP
**   unflagged: PROPELLER01..04 on AvCargoPln, NVCargoPln, UVCargoPln, the CINE_CPlane set,
**              and PROPELLER01/02 on AVComanche
**
** So the same artists flagged their "PROPS" discs and left their "PROPELLER" discs clear,
** on the same models. The GLA cargo plane that delivers the anthrax bomb is one of the
** unflagged ones, and it flew over with a body shadow and no propellers in it.
**
** Nothing else in the shipped set matches: no building vent, glass pane, headlight, glow
** plane or searchlight beam is named this way, and the ones that could be confused for a
** rotor by render state alone (LIGHT, HEADLIGHT, SPOTLIGHT, *GLOW, SEARCHLIGHTBEAM) are
** all named for what they are. That is why the name is safe to read here and the geometry
** is not -- admitting every soft-blended depth-write-off mesh would admit 3,494 of them.
**
** Applied at load, in MeshModelClass::Load_W3D, so the renderer's per-draw path stays a
** flag test. It means the legacy volumetric shadow system sees the same answer, which is
** what it already sees for the flagged discs on the same aircraft.
*/
inline bool Mesh_Name_Reads_As_Rotor_Disc(const char *mesh_name)
{
	if (mesh_name == nullptr) {
		return false;
	}

	// Uppercase compare: W3D mesh names are stored as the artist typed them, and the
	// shipped set is not consistent about case (CBGerbl01.w3d against ABArFrcCmd.W3D).
	static const char *const ROTOR_NAMES[] = { "PROPELLER", "ROTOR" };

	for (int n = 0; n < (int)(sizeof(ROTOR_NAMES) / sizeof(ROTOR_NAMES[0])); ++n) {
		const char *const needle = ROTOR_NAMES[n];
		for (const char *at = mesh_name; *at != '\0'; ++at) {
			int i = 0;
			while (needle[i] != '\0' && at[i] != '\0' &&
				toupper((unsigned char)at[i]) == needle[i]) {
				++i;
			}
			if (needle[i] == '\0') {
				return true;
			}
		}
	}

	return false;
}

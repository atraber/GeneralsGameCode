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

// Which backend the game draws through.
//
// This file is the whole of the choice, and it is deliberately the smallest file in the
// renderer: one environment variable, one switch, two factory calls. Gfx_Create_Adapter
// used to live at the bottom of gfxdevice_d3d9.cpp, which was fine while naming the only
// backend there was; with two of them, a file that includes both API headers is exactly
// the file that must not exist -- d3d9_compat.h #defines CreateTexture, CreateVertexBuffer
// and CreateIndexBuffer as fixed-arity macros, and they would rewrite the D3D11 calls of
// the same name. So each backend keeps its own translation unit and this one includes
// neither API.
//
// Why an environment variable and not an Options.ini key: the replay harness already sets
// environment variables, so a measured run needs nothing new to select a backend -- and a
// setting the player can reach from a menu is a promise this phase is not ready to make.
// D3D9 is the default and stays the default; only the exact string "d3d11" selects the
// other one, so a typo falls back to the backend that works rather than to a black screen.

#include "gfxdevice.h"
#include "gfxdevice_d3d9.h"
#include "gfxdevice_d3d11.h"
#include "WWDebug/wwdebug.h"

#include <stdlib.h>
#include <string.h>

namespace
{
	// Read once. Which backend is running cannot change inside a run -- the shader loader
	// asks after startup, and an answer that could change between two calls would be a
	// different kind of bug entirely.
	GfxBackendKind Read_Requested_Backend()
	{
		const char * requested = getenv("W3D_BACKEND");
		if (requested != nullptr && _stricmp(requested, "d3d11") == 0)
			return GFX_BACKEND_D3D11;
		return GFX_BACKEND_D3D9;
	}
}

GfxBackendKind Gfx_Active_Backend()
{
	static const GfxBackendKind kind = Read_Requested_Backend();
	return kind;
}

GfxAdapterClass * Gfx_Create_Adapter()
{
	if (Gfx_Active_Backend() == GFX_BACKEND_D3D11) {
		WWDEBUG_SAY(("BACKEND: W3D_BACKEND=d3d11 -- creating the Direct3D 11 adapter"));
		GfxAdapterClass * adapter = Gfx_Create_Adapter_D3D11();
		if (adapter != nullptr)
			return adapter;
		// Deliberately not a silent fall back to D3D9. A run that asked for D3D11 and
		// quietly got D3D9 would produce a census that looks like a working backend, and
		// this whole phase is measured by censuses.
		WWDEBUG_SAY(("BACKEND: the Direct3D 11 adapter could not be created. Not falling "
			"back to D3D9 -- a run that asked for d3d11 and measured d3d9 is worse than a "
			"run that does not start."));
		return nullptr;
	}

	WWDEBUG_SAY(("BACKEND: Direct3D 9"));
	return Gfx_Create_Adapter_D3D9();
}

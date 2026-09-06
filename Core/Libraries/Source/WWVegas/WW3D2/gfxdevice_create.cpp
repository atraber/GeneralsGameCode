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
// There is one. Phase 10 removed the Direct3D 9 backend, and with it the environment
// variable, the switch and the enum that used to choose between two. What is left is the
// one place a concrete backend is named on the way in -- which is worth keeping as a file
// of its own rather than dissolving into ww3d.cpp, because "which backend, and how it is
// created" is a question a third backend will ask again, and this is where its answer goes.
//
// Deliberately no fall back and no default. If the adapter cannot be created the game must
// fail loudly: a run that silently drew through something other than what it asked for
// would produce a census that looks like a working backend, and every claim this port has
// made is a census.
//
// This translation unit still includes no graphics API header, and that is now free rather
// than carefully arranged. Until this phase it was arranged: d3d9_compat.h #defined
// CreateTexture, CreateVertexBuffer, CreateIndexBuffer, SetStreamSource, SetIndices and
// DrawIndexedPrimitive as fixed-arity macros, so any translation unit that saw both headers
// would have had its D3D11 calls of those names silently rewritten. Those macros went with
// the backend.

#include "gfxdevice.h"
#include "gfxdevice_d3d11.h"
#include "WWDebug/wwdebug.h"

GfxAdapterClass * Gfx_Create_Adapter()
{
	WWDEBUG_SAY(("BACKEND: Direct3D 11"));
	GfxAdapterClass * adapter = Gfx_Create_Adapter_D3D11();
	if (adapter == nullptr) {
		WWDEBUG_SAY(("BACKEND: the Direct3D 11 adapter could not be created. There is no "
			"second backend to fall back to, and there deliberately is no software path -- "
			"a run that does not start is better than a run that measures something else."));
	}
	return adapter;
}

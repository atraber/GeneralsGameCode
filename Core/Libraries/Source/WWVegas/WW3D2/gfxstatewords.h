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

// The engine's tracked-state vocabulary.
//
// This replaces d3d9_compat.h, which was two unrelated things wearing one name: a D3D8-to-
// D3D9 call-shape shim, which died with the D3D9 backend, and the state *numbers*, which
// cannot die because they are the engine's own intermediate representation.
// DX8Wrapper::RenderStates[256] is indexed by D3DRS_*, TextureStageStates[][] by D3DTSS_*,
// and those words are named at some 800 call sites. The seam passes them through unchanged
// and every backend translates at its own boundary.
//
// WHAT THIS HEADER DOES NOT DO, and it is worth being blunt about it. It does not make the
// vocabulary neutral. The numbers still come from <d3d9types.h> and are still spelled
// D3DRS_ZFUNC. Transcribing them into a header of literal values was considered and is not
// possible today: <d3dx9.h> is still included directly by fourteen files for the D3DX
// matrix maths that Phase 4.2 deliberately deferred (Matrix4x4 is column-major and the move
// is a numerical change, not a mechanical one), <d3dx9.h> includes <d3d9.h>, and a
// transcribed header would then collide with d3d9types.h on every enum -- redeclaring
// D3DRENDERSTATETYPE is a hard error, not a redefinition warning. The D3DX maths move has
// to happen first. Until then, this is what is honestly available:
//
//   * <d3d9types.h> and NOT <d3d9.h>. This header brings in the numbers, the small
//     by-value structs and the format enums, and brings in no COM interface at all. So no
//     file outside a backend can reach IDirect3DDevice9 through it, which is a rule the
//     compiler enforces rather than one a census checks after the fact.
//   * none of the D3D8 shim. The fixed-arity CreateTexture / CreateVertexBuffer /
//     CreateIndexBuffer / SetStreamSource / SetIndices / DrawIndexedPrimitive macros are
//     gone -- and note what that buys: they silently rewrote any call of those names, which
//     is why gfxdevice_create.cpp was forbidden to include both API headers. That hazard
//     no longer exists. The D3DVSD_* vertex-shader-declaration tokens are gone too; they
//     had zero readers.
//
// What is left below is the handful of constants D3D8 had and D3D9 does not, which the
// tracked-state arrays are still indexed by.

// d3d9types.h is written to be included after the Windows headers -- it names LONG, DWORD,
// HWND and HMONITOR and includes nothing that would define them. d3d9.h used to bring those
// in on its way past; this header has to say so itself, which is the one thing that gets
// worse by not including the API header.
#include <windows.h>
#include <d3d9types.h>
// ...and the capability *bit* names, which are in the caps header rather than the types
// one. ShaderClass::Apply -- the fixed-function combiner selection, which submits no draws
// and is a phase of its own to remove -- tests two dozen D3DTEXOPCAPS_ bits one at a time
// against the neutral GfxDeviceCaps::FixedFunctionCombineOps word. Types and constants
// only; no interface is declared in either of these.
#include <d3d9caps.h>

// The ten texture-stage states that became sampler states in D3D9. The engine still writes
// them as stage states, because TextureStageStates[stage][state] is what the whole sampler
// path is built on and D3D9's own numbering left these slots free.
#define D3DTSS_ADDRESSU ((D3DTEXTURESTAGESTATETYPE)13)
#define D3DTSS_ADDRESSV ((D3DTEXTURESTAGESTATETYPE)14)
#define D3DTSS_BORDERCOLOR ((D3DTEXTURESTAGESTATETYPE)15)
#define D3DTSS_MAGFILTER ((D3DTEXTURESTAGESTATETYPE)16)
#define D3DTSS_MINFILTER ((D3DTEXTURESTAGESTATETYPE)17)
#define D3DTSS_MIPFILTER ((D3DTEXTURESTAGESTATETYPE)18)
#define D3DTSS_MIPMAPLODBIAS ((D3DTEXTURESTAGESTATETYPE)19)
#define D3DTSS_MAXMIPLEVEL ((D3DTEXTURESTAGESTATETYPE)20)
#define D3DTSS_MAXANISOTROPY ((D3DTEXTURESTAGESTATETYPE)21)
#define D3DTSS_ADDRESSW ((D3DTEXTURESTAGESTATETYPE)25)

// Render states D3D8 had and D3D9 dropped, kept at numbers that are free inside
// RenderStates[256]. Only D3DRS_ZBIAS has a live writer -- the two terrain shroud passes --
// and the D3D11 backend converts it at RS_COMPAT_ZBIAS. The rest are written once at
// startup by Set_Default_Global_Render_States and read by the state audit, which is why
// they still need slots rather than being deleted outright.
#define D3DRS_LINEPATTERN               ((D3DRENDERSTATETYPE)220)
#define D3DRS_SOFTWAREVERTEXPROCESSING  ((D3DRENDERSTATETYPE)221)
#define D3DRS_ZVISIBLE                  ((D3DRENDERSTATETYPE)222)
#define D3DRS_PATCHSEGMENTS             ((D3DRENDERSTATETYPE)223)
#define D3DRS_ZBIAS                     ((D3DRENDERSTATETYPE)224)
#define D3DRS_EDGEANTIALIAS             ((D3DRENDERSTATETYPE)225)
#define D3DRS_PATCHEDGESTYLE            ((D3DRENDERSTATETYPE)226)

// Two filter modes D3D8 named and D3D9 does not. No filter is ever set to either; they are
// here because the state audit's name table prints every value it can be handed, and a
// number with no name in that table reads as a defect in the audit.
#define D3DTEXF_FLATCUBIC               ((D3DTEXTUREFILTERTYPE)4)
#define D3DTEXF_GAUSSIANCUBIC           ((D3DTEXTUREFILTERTYPE)5)

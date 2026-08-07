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
#include "WW3D2/shader.h"

/*
** MeshTechnique -- what kind of thing a mesh pass is.
**
** This is the asset's answer to "how should I be drawn", decided once when the mesh
** type is registered with the renderer and stored on the polygon renderer, rather
** than inferred per draw from render state inside Apply_Render_State_Changes.
**
** The distinction it draws is deliberately coarse. A technique says what the surface
** *is*; it does not say which shader ends up bound, because that also depends on
** things which are not properties of the asset -- which pass of the frame is being
** drawn (shadow depth, camera depth, normal), what the ShaderRouting option allows,
** and whether an ORM map has finished loading. Those are applied at dispatch. One
** technique can therefore reach several shaders, and the same shader can serve
** several techniques, which is the usual technique/variant split.
**
** Why per registration rather than per draw: the inputs are properties of the mesh's
** material description, and that description only changes through
** Enable_Alternate_Material_Description, which calls TheDX8MeshRenderer.Invalidate()
** and re-registers every mesh type. So a value cached here cannot go stale, and the
** per-draw work becomes a lookup instead of a dozen state reads and a chain of
** exclusions.
*/
enum MeshTechnique CPP_11(: int)
{
	// No technique was declared for this draw. Everything that does not come through
	// the mesh renderer lands here -- terrain, roads, water, the shroud, decals,
	// particles, 2D -- and keeps whatever routing the wrapper decides for itself.
	// Those callers get techniques of their own later; until then this is the honest
	// value, not a default that pretends to describe them.
	MESH_TECHNIQUE_UNCLASSIFIED = 0,

	// A surface: opaque or alpha-tested geometry that occludes what is behind it and
	// expects to be lit. Buildings, vehicles, terrain props, cut-out foliage. This is
	// the technique the programmable path exists for, and the one that reaches the
	// PBR, detail and plain lit shaders depending on what is available at dispatch.
	MESH_TECHNIQUE_SURFACE,

	// Geometry whose colour is already decided -- it lives in the vertex colours or in
	// the material, and no lighting equation should touch it. Roads and tank tracks
	// (pre-lit, no vertex normal) are one half of this; the other half is meshes drawn
	// with lighting switched off entirely, which is how the W3D mouse cursors and
	// similar interface models are authored.
	//
	// Keeping this separate from SURFACE is the whole point of the enum. A pre-lit
	// mesh shaded as if it were a surface loses the only thing that gave it its
	// colour: the PBR shader has no emissive term and ignores the vertex colour except
	// for its alpha, so a red cursor arrow came out grey.
	MESH_TECHNIQUE_PRELIT,

	// Blended geometry that is not a surface at all: rotor discs, light shafts, glows,
	// beams, smoke. Its appearance comes from its texture and its blend equation, not
	// from a lighting model, and nothing in it writes depth in any pass.
	//
	// The test is a property of the mesh, not of the pass, which is what makes a mesh
	// impossible to split across two pipelines: an ordinary building has blended
	// passes too, but something in it writes depth, so the whole mesh is a SURFACE and
	// every pass of it goes the same way.
	MESH_TECHNIQUE_EFFECT,

	// A pass the programmable path cannot reproduce -- an unrecognised blend, or a
	// vertex format with no position. Stays on fixed function.
	MESH_TECHNIQUE_FIXED_FUNCTION,

	MESH_TECHNIQUE_COUNT
};

/*
** Human-readable technique name, for the routing census and the load-time dump.
*/
const char * Mesh_Technique_Name(MeshTechnique technique);

class MeshModelClass;
class VertexMaterialClass;
class TextureClass;

/*
** Decide a mesh pass's technique from the asset alone.
**
** Called once per polygon renderer when the mesh type is registered. Everything it
** reads is a property of the mesh's material description: the pass's ShaderClass, its
** vertex material, the vertex format, and whether any pass of the mesh writes depth.
** Nothing it reads is device state, a render-pass flag or a user option, which is why
** the answer keeps for the life of the registration.
*/
MeshTechnique Classify_Mesh_Technique(
	const MeshModelClass * mmc,
	unsigned fvf,
	ShaderClass shader,
	const VertexMaterialClass * material,
	const TextureClass * stage1Texture);

/*
** Log what the classifier has decided so far: a count per technique with a few mesh
** names each, emitted only when something new has been classified.
**
** This is the audit the per-draw check cannot give. That check only ever sees meshes
** the current scene draws, so a technique with no examples on one map reads as zero
** whether the classifier never assigns it or nothing there happens to use it.
*/
void Mesh_Technique_Report_Registrations();

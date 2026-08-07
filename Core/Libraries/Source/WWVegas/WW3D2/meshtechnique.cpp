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

#include "meshtechnique.h"

#include "WW3D2/dx8fvf.h"
#include "WW3D2/meshgeometry.h"
#include "WW3D2/meshmdl.h"
#include "WW3D2/shader.h"
#include "WW3D2/texture.h"
#include "WW3D2/vertmaterial.h"

//-----------------------------------------------------------------------------
// Registration census.
//
// What the classifier actually decided, per technique, with a few mesh names each.
// This is the artifact that makes a declared technique auditable: a table of what
// every mesh was classified as, produced without having to see it on screen.
//
// It answers a question the per-draw check cannot. That check only sees meshes the
// current scene draws, so a technique with no examples in a replay reads as zero
// there whether the classifier is declining to assign it or nothing on the map
// happens to use it. Counting at registration separates the two.
//-----------------------------------------------------------------------------
namespace {
	enum { TECH_SAMPLES = 6 };
	unsigned s_registered[MESH_TECHNIQUE_COUNT] = { 0 };
	const char * s_samples[MESH_TECHNIQUE_COUNT][TECH_SAMPLES] = { { nullptr } };
	int s_sampleCount[MESH_TECHNIQUE_COUNT] = { 0 };
	bool s_dirty = false;

	void Note_Registration(MeshTechnique technique, const char * name)
	{
		if (technique < 0 || technique >= MESH_TECHNIQUE_COUNT) return;
		++s_registered[technique];
		s_dirty = true;
		if (name == nullptr || *name == '\0') return;
		for (int i = 0; i < s_sampleCount[technique]; ++i)
			if (s_samples[technique][i] == name) return;
		if (s_sampleCount[technique] < TECH_SAMPLES)
			s_samples[technique][s_sampleCount[technique]++] = name;
	}
}

void Mesh_Technique_Report_Registrations()
{
	// Only when something new has been classified, so a settled scene stays quiet and
	// a burst of registrations after a load is visible.
	if (!s_dirty) return;
	s_dirty = false;

	WWDEBUG_SAY(("TECHNIQUE REGISTRATIONS (batches classified so far):"));
	for (int t = 0; t < MESH_TECHNIQUE_COUNT; ++t) {
		if (s_registered[t] == 0) continue;
		WWDEBUG_SAY(("  %-16s %6u  e.g. %s %s %s %s",
			Mesh_Technique_Name((MeshTechnique)t), s_registered[t],
			s_sampleCount[t] > 0 ? s_samples[t][0] : "-",
			s_sampleCount[t] > 1 ? s_samples[t][1] : "",
			s_sampleCount[t] > 2 ? s_samples[t][2] : "",
			s_sampleCount[t] > 3 ? s_samples[t][3] : ""));
	}
}

const char * Mesh_Technique_Name(MeshTechnique technique)
{
	switch (technique) {
		case MESH_TECHNIQUE_UNCLASSIFIED:	return "unclassified";
		case MESH_TECHNIQUE_SURFACE:		return "surface";
		case MESH_TECHNIQUE_PRELIT:			return "prelit";
		case MESH_TECHNIQUE_EFFECT:			return "effect";
		case MESH_TECHNIQUE_FIXED_FUNCTION:	return "fixed-function";
		default:							return "?";
	}
}

/*
** Does any pass of this mesh write depth?
**
** This is what separates a surface from an effect, and it has to be asked of the
** mesh rather than of the pass. A building has blended passes; something in it also
** writes depth, so the blended pass is a layer on a surface. A rotor disc, a light
** shaft, a smoke puff writes depth in no pass at all, because it is not a surface.
**
** Being a property of the mesh, the answer is the same for every pass of it -- which
** is what makes splitting a mesh across two pipelines impossible rather than merely
** avoided. Two pipelines do not compute identical depth, and a mesh drawn by both
** z-fights with itself.
**
** Polygon 0's shader covers both the single-shader and the per-polygon (shader array)
** cases, matching what the renderer already did per draw.
*/
static bool Mesh_Has_Depth_Writing_Pass(const MeshModelClass * mmc)
{
	MeshModelClass * m = const_cast<MeshModelClass *>(mmc);
	for (int p = 0; p < m->Get_Pass_Count(); ++p) {
		if (m->Get_Shader(0, p).Get_Depth_Mask() == ShaderClass::DEPTH_WRITE_ENABLE)
			return true;
	}
	return false;
}

static MeshTechnique Classify_Mesh_Technique_Impl(
	const MeshModelClass * mmc,
	unsigned fvf,
	ShaderClass shader,
	const VertexMaterialClass * material,
	const TextureClass * stage1Texture)
{
	if (mmc == nullptr)
		return MESH_TECHNIQUE_UNCLASSIFIED;

	// No position, no geometry the programmable path can transform. Sorting buffers
	// reach the renderer with no FVF of their own; those are classified from the mesh
	// all the same, and it is the dispatch that decides whether a sorted draw is
	// eligible for a given shader.
	if (fvf != 0 && !(fvf & D3DFVF_XYZ))
		return MESH_TECHNIQUE_FIXED_FUNCTION;

	// Effect geometry: blended in a way that carries no coverage, on a mesh that is not
	// a surface at all.
	//
	// Two kinds of blend qualify. Additive, whose alpha is brightness rather than
	// coverage, so nothing that reasons about coverage applies to it. And soft --
	// blending on with no alpha test -- which has no silhouette either, the alpha *test*
	// being what gives cut-out foliage a real one.
	//
	// Both are then asked the same question about the mesh, and that is the point. A
	// building carries additive glows and blended overlays; something in it also writes
	// depth, so those passes are layers on a surface and belong wherever the surface
	// goes. A rotor disc, a light shaft, a smoke puff writes depth in no pass, because
	// it is not a surface, and the whole of it stays on fixed function.
	//
	// Asking the pass instead is what splits a mesh. Until this was unified the additive
	// test was per pass, so a building's glow classified EFFECT while the rest of it
	// classified SURFACE, the two were drawn by pipelines that do not compute identical
	// depth, and the coincident passes z-fought: 9 meshes on chinooks.rep, every one of
	// them additive, CBNRIVERHO_N.RIVERHOUSE and ABPWRPLANT_N.CYLINDER01 among them. The
	// soft-blend test had already been through exactly this and been fixed the same way.
	const bool blendWithoutCoverage =
		shader.Is_Additive_Blend() ||
		(shader.Is_Blend_Enabled() &&
		 shader.Get_Alpha_Test() == ShaderClass::ALPHATEST_DISABLE);
	if (blendWithoutCoverage && !Mesh_Has_Depth_Writing_Pass(mmc))
		return MESH_TECHNIQUE_EFFECT;

	// Blends the shaders cannot composite the same way stay where they are. The
	// frame-buffer blend is applied by hardware after the pixel shader, so opaque,
	// standard alpha, additive and multiply all composite identically either way;
	// anything else is not reproduced and is not guessed at.
	const bool reproducibleBlend =
		!shader.Is_Blend_Enabled() ||
		shader.Is_Standard_Alpha_Blend() ||
		shader.Is_Additive_Blend() ||
		shader.Is_Multiply_Blend();
	if (!reproducibleBlend)
		return MESH_TECHNIQUE_FIXED_FUNCTION;

	// Pre-lit: the colour is already decided and no lighting equation should touch it.
	//
	// Two ways an asset says so. Geometry with no vertex normal has nothing to light
	// with -- roads and tank tracks are DX8_FVF_XYZDUV1, and the fixed-function
	// pipeline gives them N.L == 0 for every light and falls back to emissive plus
	// ambient. And a material with lighting switched off says it outright, which is
	// how the W3D interface models (the mouse cursors) are authored: their colour is
	// in the vertex diffuse and the material emissive.
	//
	// Either way the mesh must not reach a shader that re-lights it from scratch.
	const bool hasNormal = (fvf & D3DFVF_NORMAL) != 0;
	const bool lit = material != nullptr &&
		const_cast<VertexMaterialClass *>(material)->Get_Lighting();
	if (!hasNormal || !lit)
		return MESH_TECHNIQUE_PRELIT;

	// A lit, blend-reproducible, depth-participating mesh pass. Which shader it gets --
	// PBR on its own ORM map or the default one, the stage-1 detail combine, or the
	// plain lit shader -- is settled at dispatch, where the routing options, the
	// texture-coordinate sources and ORM residency are known. stage1Texture is
	// forwarded for the census rather than used here, for exactly that reason: a
	// second texture selects a variant, it does not change what the surface is.
	(void)stage1Texture;
	return MESH_TECHNIQUE_SURFACE;
}

MeshTechnique Classify_Mesh_Technique(
	const MeshModelClass * mmc,
	unsigned fvf,
	ShaderClass shader,
	const VertexMaterialClass * material,
	const TextureClass * stage1Texture)
{
	const MeshTechnique technique =
		Classify_Mesh_Technique_Impl(mmc, fvf, shader, material, stage1Texture);
	Note_Registration(technique,
		mmc != nullptr ? const_cast<MeshModelClass *>(mmc)->Get_Name() : nullptr);
	return technique;
}

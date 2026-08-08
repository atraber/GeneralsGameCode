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
	// How many of the above the assets declared for themselves rather than had inferred.
	// Reported next to the totals so the two are never confused: a technique that is
	// mostly inferred is mostly a guess, however confident the table looks.
	unsigned s_declaredCount = 0;

	void Note_Declaration() { ++s_declaredCount; }

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

	unsigned total = 0;
	for (int t = 0; t < MESH_TECHNIQUE_COUNT; ++t) total += s_registered[t];
	WWDEBUG_SAY(("TECHNIQUE REGISTRATIONS (%u batches: %u declared by the asset, "
				 "%u inferred):", total, s_declaredCount, total - s_declaredCount));
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

/*
** A technique declared by the asset itself, in the mesh's W3D user text.
**
** Everything above this is inference: reasonable rules applied to blend state, vertex
** format and material flags to work out what an artist meant. It is right on the assets
** measured, but it is still a guess, and where it guesses wrong there has until now been
** nowhere to say so except by changing the rules for everyone.
**
** The user text chunk is part of the W3D format and is already loaded per mesh
** (MeshGeometryClass reads W3D_CHUNK_MESH_USER_TEXT), so a mesh can carry
**
**     technique=prelit
**
** and be believed. Anything the asset does not declare falls through to the inference,
** which is what every shipped mesh does today.
**
** A value that is not a technique name is reported rather than ignored: a typo in an
** asset should be visible at load, not silently render as something else.
*/
static bool Parse_Declared_Technique(const char * userText, MeshTechnique & declared)
{
	if (userText == nullptr) return false;
	const char * key = strstr(userText, "technique=");
	if (key == nullptr) return false;
	const char * value = key + 10;   // strlen("technique=")

	static const struct { const char * name; MeshTechnique technique; } names[] = {
		{ "surface",        MESH_TECHNIQUE_SURFACE        },
		{ "prelit",         MESH_TECHNIQUE_PRELIT         },
		{ "effect",         MESH_TECHNIQUE_EFFECT         },
		{ "fixed-function", MESH_TECHNIQUE_FIXED_FUNCTION },
	};
	for (int i = 0; i < (int)(sizeof(names)/sizeof(names[0])); ++i) {
		const size_t len = strlen(names[i].name);
		if (strncmp(value, names[i].name, len) == 0) {
			// The whole token has to match, so that "surfacex" is a typo rather than
			// a surface.
			const char end = value[len];
			if (end == '\0' || end == ' ' || end == '\t' || end == '\r' || end == '\n' ||
				end == ';' || end == ',') {
				declared = names[i].technique;
				return true;
			}
		}
	}
	WWDEBUG_SAY(("TECHNIQUE: unrecognised declaration in mesh user text: \"%s\" "
				 "-- falling back to classification", key));
	return false;
}

#ifdef RTS_DEBUG
/*
** Self-test for the declaration parser.
**
** No mesh in the shipped asset set carries user text -- measured, zero across a full
** replay -- so this parser would otherwise be code that has never once run, waiting to
** be wrong the first time somebody authors an asset that uses it. It costs one pass over
** a dozen strings at startup to know it works.
*/
static void Self_Test_Declaration_Parser()
{
	struct Case { const char * text; bool expectMatch; MeshTechnique expect; };
	static const Case cases[] = {
		{ "technique=surface",             true,  MESH_TECHNIQUE_SURFACE        },
		{ "technique=prelit",              true,  MESH_TECHNIQUE_PRELIT         },
		{ "technique=effect",              true,  MESH_TECHNIQUE_EFFECT         },
		{ "technique=fixed-function",      true,  MESH_TECHNIQUE_FIXED_FUNCTION },
		{ "author=bob technique=prelit",   true,  MESH_TECHNIQUE_PRELIT         },
		{ "technique=prelit; lod=2",       true,  MESH_TECHNIQUE_PRELIT         },
		{ "technique=surfacex",            false, MESH_TECHNIQUE_UNCLASSIFIED   },
		{ "technique=",                    false, MESH_TECHNIQUE_UNCLASSIFIED   },
		{ "shadow=on",                     false, MESH_TECHNIQUE_UNCLASSIFIED   },
		{ "",                              false, MESH_TECHNIQUE_UNCLASSIFIED   },
	};
	int failures = 0;
	for (int i = 0; i < (int)(sizeof(cases)/sizeof(cases[0])); ++i) {
		MeshTechnique got = MESH_TECHNIQUE_UNCLASSIFIED;
		const bool matched = Parse_Declared_Technique(cases[i].text, got);
		if (matched != cases[i].expectMatch ||
			(matched && got != cases[i].expect)) {
			++failures;
			WWDEBUG_SAY(("TECHNIQUE SELF-TEST FAILED: \"%s\" -> matched=%d value=%s "
				"(expected matched=%d value=%s)",
				cases[i].text, (int)matched, Mesh_Technique_Name(got),
				(int)cases[i].expectMatch, Mesh_Technique_Name(cases[i].expect)));
		}
	}
	// Also exercise the null path, which no case above can reach.
	MeshTechnique dummy = MESH_TECHNIQUE_UNCLASSIFIED;
	if (Parse_Declared_Technique(nullptr, dummy)) {
		++failures;
		WWDEBUG_SAY(("TECHNIQUE SELF-TEST FAILED: null user text matched"));
	}
	WWDEBUG_SAY(("TECHNIQUE SELF-TEST: %d cases, %d failures",
		(int)(sizeof(cases)/sizeof(cases[0])) + 1, failures));
}
#endif

MeshTechnique Classify_Mesh_Technique(
	const MeshModelClass * mmc,
	unsigned fvf,
	ShaderClass shader,
	const VertexMaterialClass * material,
	const TextureClass * stage1Texture)
{
#ifdef RTS_DEBUG
	static bool selfTested = false;
	if (!selfTested) { selfTested = true; Self_Test_Declaration_Parser(); }
#endif

	// The asset's own answer wins where it gives one.
	const char * userText =
		mmc != nullptr ? const_cast<MeshModelClass *>(mmc)->Get_User_Text() : nullptr;

#ifdef RTS_DEBUG
	// Report any user text at all, once per distinct string, whether or not it declares
	// a technique. Nothing in the shipped asset set is known to use this chunk, so
	// without this the parser above is untested code that silently never runs -- and a
	// mesh that does carry user text in some other format is worth seeing before the
	// convention is settled.
	if (userText != nullptr && *userText != '\0') {
		static const char * seen[16] = { nullptr };
		static int seenCount = 0;
		bool isNew = true;
		for (int i = 0; i < seenCount; ++i)
			if (seen[i] == userText) { isNew = false; break; }
		if (isNew && seenCount < 16) {
			seen[seenCount++] = userText;
			WWDEBUG_SAY(("TECHNIQUE: mesh %s carries user text \"%s\"",
				const_cast<MeshModelClass *>(mmc)->Get_Name(), userText));
		}
	}
#endif

	MeshTechnique declared;
	if (mmc != nullptr && Parse_Declared_Technique(userText, declared)) {
		Note_Registration(declared, const_cast<MeshModelClass *>(mmc)->Get_Name());
		Note_Declaration();
		return declared;
	}

	const MeshTechnique technique =
		Classify_Mesh_Technique_Impl(mmc, fvf, shader, material, stage1Texture);
	Note_Registration(technique,
		mmc != nullptr ? const_cast<MeshModelClass *>(mmc)->Get_Name() : nullptr);
	return technique;
}

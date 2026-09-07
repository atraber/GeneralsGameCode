/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
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

// TheSuperHackers @feature andytraber 06/09/2026 GpuLightListClass -- see W3DGpuLightList.h
// for what this is and the clustered lighting plan's C3 section for why.
//
// VERIFICATION RECIPE (the plan's gate for this stage: "the buffer's contents are dumped
// and match a CPU-side recomputation of the same frame's light set"):
//   1. Build a debug configuration and run with a light population, e.g.
//        set W3D_SYNTHETIC_LIGHTS=200
//      so there is something in the buffer worth checking (0 lights verifies trivially).
//   2. Break in a debugger (or add a one-off call somewhere reachable, such as a console
//      command) and call W3DDisplay::m_3DScene->getGpuLightList().Verify_Upload().
//   3. It logs PASS or FAIL through WWDEBUG_SAY and returns the same as a bool. FAIL means
//      either Pack_Light or the upload disagrees with what is actually sitting in the GPU
//      buffer -- Map_Buffer(GFX_MAP_READ) reads it back for real, it does not consult the
//      CPU array a second time.
// Never call Verify_Upload() from the render path: GFX_MAP_READ stalls the pipeline (see
// gfxdevice.h's Map_Buffer comment). This is a break-and-inspect tool, not instrumentation.
//
// C5.1'S SUN-EQUIVALENCE CONTROL (the plan's gate for the shader stage, and the one it
// calls "the single most valuable control in the whole plan"). Full reasoning at
// Collect_Sun_Check_Lights below; the recipe is:
//
//   1. Debug build. Clustered lighting must actually be running -- all three structured
//      buffers created (W3DShaderManager::isClusteredLightingActive). If it is not, the
//      stand-in lights are uploaded and then consumed by nobody, and the directional term
//      is NOT suppressed either -- deliberately, so a mis-run reads as "nothing happened"
//      and not as "the control failed". It is logged, once.
//   2. Two runs of the SAME BINARY over the same replay frame, one variable apart:
//        run A:  (nothing set)                         -- the directional path
//        run B:  set W3D_CLUSTER_SUN_CHECK=1           -- the punctual path
//      A rebuild between them invalidates the comparison; see the replay test harness documentation
//      and the "a rebuild is not a re-run" rule.
//   3. Compare the two captures over the PBR meshes only. Terrain, roads, water, particles
//      and the UI are not on this path yet (C5.2/C5.3) and are unchanged by definition, so
//      any difference on them is harness noise and is itself worth knowing.
//
//   PASS looks like: differences confined to PBR mesh pixels, at most 1 of 255 per channel,
//   with no spatial structure -- scattered single-LSB rounding, not a shape. That residual
//   is the stand-in light's own inverse-square variation across the map (see the note on
//   SUN_CHECK_DISTANCE), and it is bounded at about a tenth of a level by construction.
//   FAIL, and what each kind means:
//     - a uniform ratio over every lit pixel (~3.14, or ~1/3.14): the PI convention. The
//       punctual path is not applying LIGHT_IRRADIANCE, or is applying it twice.
//     - a ratio that varies with brightness rather than being constant: a SPACE error --
//       something added in linear that the other path adds in gamma, or the reverse. This
//       is the SHADOW_MIN bug's shape.
//     - lit pixels correct but shadowed ones not: shadowFill or the cloud shade is reaching
//       one term and not the other.
//     - units black: the stand-in lights never reached the shader. Check the census line
//       and whether the buffers bound (W3DShaderManager::isClusteredLightingActive).
//   Do not run it together with W3D_SYNTHETIC_LIGHTS: those add lights the directional run
//   does not have, and the difference then measures them instead.

#include <stdlib.h>
#include <string.h>

#include "W3DDevice/GameClient/W3DGpuLightList.h"
#include "W3DDevice/GameClient/W3DClusterGrid.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "W3DDevice/GameClient/W3DDynamicLight.h"
#include "WW3D2/camera.h"
#include "WW3D2/light.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/robjlist.h"
#include "WWMath/colmath.h"
#include "WWMath/colmathfrustum.h"
#include "WWMath/frustum.h"
#include "WWMath/aabox.h"
#include "WWMath/matrix3d.h"

#ifdef RTS_DEBUG
#include "Common/GlobalData.h"	// TheGlobalData, for the sun check's "...but the option is off" note
#include "WW3D2/ww3d.h"
#include "WW3D2/colorspace.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "W3DDevice/GameClient/W3DLightAuthoring.h"	// authoredLocalLightColor -- synthetic lights carry the same units as authored content
#endif

namespace
{
	// LightClass has one outer spot angle (Get_Spot_Angle()) and a Phong-style falloff
	// exponent (Get_Spot_Exponent()), not the two-cone model GpuLight wants for a soft
	// edge -- see gpulight.h's file comment for why the record grew to 64 bytes over
	// this. Shrinking the outer angle for the inner cosine gives a plausible-looking
	// cone; it is not a reproduction of SpotExponent's falloff curve, and real unit-light
	// content authoring an actual inner angle is separate work (plan section 5).
	const float SPOT_INNER_FRACTION = 0.75f;

	// Matches LightEnvironmentClass::Add_Light's own near-black reject
	// (lightenvironment.cpp), so a light this path drops is one the CPU model it is
	// replacing would also have ignored -- which keeps the two directly comparable while
	// both exist side by side (C3 through C7).
	const float BLACK_LIGHT_THRESHOLD = 0.05f;

	// Packs a point or spot LightClass (or W3DDynamicLight, which is-a LightClass) into a
	// GpuLight and hands back its culling sphere. Returns false for a light this stage
	// does not upload at all: directional (the sun stays outside the cluster path -- plan
	// section 0.1's "the global/sun lights and the ambient stay exactly where they are"),
	// near-black, or a degenerate (non-positive) range.
	bool Pack_Light(const LightClass & light, GpuLight & out, SphereClass & outSphere)
	{
		if (light.Get_Type() == LightClass::DIRECTIONAL)
			return false;

		Vector3 diffuse;
		light.Get_Diffuse(&diffuse);
		const float intensity = light.Get_Intensity();
		const Vector3 color = diffuse * intensity;
		if (color.X < BLACK_LIGHT_THRESHOLD && color.Y < BLACK_LIGHT_THRESHOLD && color.Z < BLACK_LIGHT_THRESHOLD)
			return false;

		const float range = light.Get_Attenuation_Range();
		if (range <= 0.0f)
			return false;

		const Vector3 pos = light.Get_Position();
		out.posRange.Set(pos.X, pos.Y, pos.Z, range);

		const bool isSpot = (light.Get_Type() == LightClass::SPOT);
		out.colorType.Set(color.X, color.Y, color.Z, isSpot ? 1.0f : 0.0f);

		if (isSpot)
		{
			// SpotDirection is stored in the light's own object space -- see
			// lightenvironment.cpp's Init_From_Point_Or_Spot_Light, which rotates it the
			// same way before using it -- and has to go through the light's own transform
			// to become the world-space direction GpuLight carries.
			Vector3 spotDirLocal;
			light.Get_Spot_Direction(spotDirLocal);
			Vector3 spotDirWorld;
			Matrix3D::Rotate_Vector(light.Get_Transform(), spotDirLocal, &spotDirWorld);
			spotDirWorld.Normalize();

			const float cosOuter = light.Get_Spot_Angle_Cos();
			out.spotDirCos.Set(spotDirWorld.X, spotDirWorld.Y, spotDirWorld.Z, cosOuter);

			// .x is the synthesized smoothstep convenience (kept for a shader that wants
			// it); .y is LightClass's own authored falloff, unmodified -- see gpulight.h's
			// spotInner comment for why .y, not .x, is the one that reproduces the map
			// author's actual look.
			const float cosInner = WWMath::Fast_Cos(light.Get_Spot_Angle() * SPOT_INNER_FRACTION);
			out.spotInner.Set(cosInner, light.Get_Spot_Exponent(), 0.0f, 0.0f);
		}
		else
		{
			// -1 in spotDirCos.w and spotInner.x: any real cosine compares greater than
			// -1, so a shader that forgets to branch on colorType.w and runs the spot
			// cone test on a point light anyway degrades to "always inside the cone"
			// rather than a NaN or a silently wrong cutoff. 0 in spotInner.y for the same
			// reason applied to the Phong falloff itself: pow(cosTheta, 0) == 1 for any
			// cosTheta > 0, i.e. no angular attenuation at all, which is what a point
			// light's true omnidirectional emission needs. (cosTheta == 0 exactly, where
			// that identity breaks down, only arises by also reading spotDirCos.xyz's own
			// zeroed-and-unused value above -- the same branch-skipping mistake the -1
			// already exists to cover, not a new hazard this field introduces.)
			out.spotDirCos.Set(0.0f, 0.0f, 0.0f, -1.0f);
			out.spotInner.Set(-1.0f, 0.0f, 0.0f, 0.0f);
		}

		// Extends to the light's attenuation radius -- see LightClass::Get_Obj_Space_Bounding_Sphere
		// (light.h) -- which is exactly the sphere Render_Seg already culls this same
		// light with today (W3DScene.cpp, around line 786), only against the camera
		// frustum here instead of a per-object sphere.
		outSphere = light.Get_Bounding_Sphere();
		return true;
	}
}	// namespace

GpuLightListClass::GpuLightListClass()
	: m_buffer(nullptr)
	, m_bufferCreateFailed(false)
	, m_lightCount(0)
#ifdef RTS_DEBUG
	, m_censusEnumerated(0)
	, m_censusCulled(0)
	, m_censusDropped(0)
	, m_sunCheckActive(false)
#endif
{
}

GpuLightListClass::~GpuLightListClass()
{
	// RTS3DScene (the owner) is released before WW3D::Shutdown() -- see W3DDisplay::init's
	// teardown order -- so the device is still alive here whenever this buffer exists.
	// The null check is only for the path where Ensure_Buffer_Created() never ran.
	if (m_buffer != nullptr && DX8Wrapper::Gfx != nullptr)
		DX8Wrapper::Gfx->Release_Buffer(m_buffer);
	m_buffer = nullptr;
}

void GpuLightListClass::Ensure_Buffer_Created()
{
	if (m_buffer != nullptr || m_bufferCreateFailed)
		return;
	// RTS3DScene is constructed before WW3D::Init brings the device up (W3DDisplay::init
	// creates m_3DScene, then calls WW3D::Init further down the same function), so on the
	// very first Update() there may be nothing to create this against yet. Trying again
	// next frame is cheap and correct; the buffer only ever needs to exist once.
	if (DX8Wrapper::Gfx == nullptr)
		return;

	m_buffer = DX8Wrapper::Gfx->Create_Structured_Buffer((unsigned)sizeof(GpuLight), (unsigned)GPU_LIGHT_CAPACITY, GFX_BUFFER_DYNAMIC);
	if (m_buffer == nullptr)
	{
		m_bufferCreateFailed = true;
		WWDEBUG_SAY(("GpuLightListClass: Create_Structured_Buffer failed for %u x %u bytes "
			"(LightBuffer). Clustered lighting has nothing to upload to for the rest of "
			"this run; the existing per-object lighting model is untouched and keeps "
			"drawing regardless.",
			(unsigned)GPU_LIGHT_CAPACITY, (unsigned)sizeof(GpuLight)));
	}
}

void GpuLightListClass::Update(RTS3DScene & scene, CameraClass & camera)
{
	Ensure_Buffer_Created();

	m_lightCount = 0;
#ifdef RTS_DEBUG
	m_censusEnumerated = 0;
	m_censusCulled = 0;
	m_censusDropped = 0;
	m_sunCheckActive = false;
#endif

	const FrustumClass & frustum = camera.Get_Frustum();
	const Vector3 cameraPos = camera.Get_Position();

	Collect_Lights(scene, frustum, cameraPos);

	Upload();
	Write_Frame_Constants(camera);

#ifdef RTS_DEBUG
	Report_Census();
#endif
}

void GpuLightListClass::Collect_Lights(RTS3DScene & scene, const FrustumClass & frustum, const Vector3 & cameraPos)
{
	// Each source below only ever appends -- see the class comment in W3DGpuLightList.h for
	// why a new source should be a new method here and not a rewrite of one of these.
	Collect_Scene_Lights(scene, frustum, cameraPos);
	// ...and with it, every bone-parented HLOD light. There is deliberately no
	// Collect_Hlod_Lights() here: W3DModelDraw allocates those through
	// RTS3DScene::getADynamicLight(), so they are already in m_dynamicLightList and adding
	// a source of their own would enumerate each one twice.
	Collect_Dynamic_Lights(scene, frustum, cameraPos);
#ifdef RTS_DEBUG
	Collect_Synthetic_Lights(frustum, cameraPos);
	Collect_Sun_Check_Lights(scene);
#endif
}

void GpuLightListClass::Collect_Scene_Lights(RTS3DScene & scene, const FrustumClass & frustum, const Vector3 & cameraPos)
{
	// The scene's own placed lights -- the same RefRenderObjListClass Render_Seg walks
	// per drawable today (W3DScene.cpp, around line 782).
	RefRenderObjListIterator it(&scene.getLightList());
	for (it.First(); !it.Is_Done(); it.Next())
	{
		LightClass * light = (LightClass *)it.Peek_Obj();
		if (light != nullptr)
			Consider_Light(*light, frustum, cameraPos);
	}
}

void GpuLightListClass::Collect_Dynamic_Lights(RTS3DScene & scene, const FrustumClass & frustum, const Vector3 & cameraPos)
{
	// Muzzle flashes, explosions and the like. Render_Seg skips a disabled one
	// (W3DScene.cpp, around line 800) and so do we.
	RefRenderObjListIterator it(scene.getDynamicLights());
	for (it.First(); !it.Is_Done(); it.Next())
	{
		W3DDynamicLight * light = (W3DDynamicLight *)it.Peek_Obj();
		if (light != nullptr && light->isEnabled())
			Consider_Light(*light, frustum, cameraPos);
	}
}

void GpuLightListClass::Consider_Light(const LightClass & light, const FrustumClass & frustum, const Vector3 & cameraPos)
{
	GpuLight packed;
	SphereClass sphere;
	if (!Pack_Light(light, packed, sphere))
		return;
	Cull_And_Insert(packed, sphere, frustum, cameraPos);
}

void GpuLightListClass::Cull_And_Insert(const GpuLight & light, const SphereClass & sphere,
	const FrustumClass & frustum, const Vector3 & cameraPos)
{
#ifdef RTS_DEBUG
	++m_censusEnumerated;
#endif

	// Against the camera frustum, not any per-object sphere -- this is the saving C3
	// makes over the CPU model it replaces (plan section 0.1): one test per light here,
	// where Render_Seg pays one test per (light, drawable) pair, every drawable, every
	// frame.
	if (CollisionMath::Overlap_Test(frustum, sphere) == CollisionMath::OUTSIDE)
	{
#ifdef RTS_DEBUG
		++m_censusCulled;
#endif
		return;
	}

	const float distanceSq = (sphere.Center - cameraPos).Length2();
	Insert_Light(light, distanceSq);
}

void GpuLightListClass::Insert_Light(const GpuLight & light, float distanceSq)
{
	if (m_lightCount < GPU_LIGHT_CAPACITY)
	{
		m_cpuLights[m_lightCount] = light;
		m_distanceSq[m_lightCount] = distanceSq;
		++m_lightCount;
		return;
	}

	// At capacity (4096 -- the plan's "must not fall over at" ceiling). Keep the nearest
	// GPU_LIGHT_CAPACITY lights, not the strongest: proximity to the camera is what
	// predicts how many clusters a light actually touches (plan section 1.3's argument
	// for scatter over gather in the first place), so keeping near lights protects the
	// thing that is actually scarce downstream -- the 64-light-per-cluster index stride
	// C6 will build -- better than keeping bright-but-distant ones would.
	//
	// The O(capacity) scan below only runs once the scene has exceeded 4096 *visible,
	// culled* local lights, which is far past both today's content (there is none yet;
	// see W3D_SYNTHETIC_LIGHTS) and the plan's 1500-light design target. It is a
	// correctness backstop, not a hot path, and is not worth a heap for that reason.
	unsigned farthestIndex = 0;
	float farthestDistanceSq = m_distanceSq[0];
	for (unsigned i = 1; i < GPU_LIGHT_CAPACITY; ++i)
	{
		if (m_distanceSq[i] > farthestDistanceSq)
		{
			farthestDistanceSq = m_distanceSq[i];
			farthestIndex = i;
		}
	}

	if (distanceSq < farthestDistanceSq)
	{
		m_cpuLights[farthestIndex] = light;
		m_distanceSq[farthestIndex] = distanceSq;
	}

	// Either the incumbent farthest light or this new one loses -- exactly one record
	// never makes it into the uploaded set either way, so the drop counts unconditionally.
#ifdef RTS_DEBUG
	++m_censusDropped;
#endif
}

void GpuLightListClass::Upload()
{
	if (m_buffer == nullptr || DX8Wrapper::Gfx == nullptr || m_lightCount == 0)
		return;	// count==0: nothing to upload, and b1's light count of 0 means no shader
				// will read past index 0 anyway once C5 exists -- so last frame's stale
				// contents, if any, are inert.

	void * data = nullptr;
	if (!DX8Wrapper::Gfx->Map_Buffer(m_buffer, GFX_MAP_WRITE_DISCARD, &data))
		return;
	memcpy(data, m_cpuLights, m_lightCount * sizeof(GpuLight));
	DX8Wrapper::Gfx->Unmap_Buffer(m_buffer);
}

void GpuLightListClass::Write_Frame_Constants(CameraClass & camera)
{
	// THE SINGLE WRITER of the whole b1 block (frameconstants.hlsli). C2 established one
	// Set_Frame_Constants call per frame, always from offset 0; splitting the block across
	// several call sites would mean whichever one ran last silently won -- exactly the
	// "two shaders disagreeing about a shared value" failure mode this project keeps
	// re-learning (see gpulight.h's file comment). C4 extended this function, as that
	// comment said it should, rather than adding a second call anywhere.
	//
	// The cluster fields come from ClusterGridClass::Compute_Params, which is static and
	// derives everything from the camera alone. That is deliberate and is what removes an
	// ordering hazard: the cluster grid itself is rebuilt AFTER this runs (W3DView::draw
	// updates the light list first), so reading the live grid's parameters here would
	// publish last frame's grid on any frame the viewport or the clip planes changed --
	// and a b1 block describing a grid one frame stale is a uniform tile offset, which is
	// the hardest kind of wrong to notice. Two callers of one pure function agree by
	// construction instead.
	ClusterGridClass::ClusterGridParams cluster;
	ClusterGridClass::Compute_Params(camera, cluster);

	Vector4 frameConstants[9];
	if (cluster.valid)
	{
		frameConstants[0].Set((float)cluster.tileWidth, (float)cluster.tileHeight,
			(float)cluster.sliceCount, (float)cluster.gridX);					// ClusterParams
		frameConstants[1].Set(cluster.depthScale, cluster.depthBias,
			cluster.zNear, cluster.zFar);										// ClusterDepth
	}
	else
	{
		// A degenerate camera (no viewport yet, or zNear >= zFar). Zeroed rather than
		// left holding the last good grid: clustergrid.hlsli's ClusterGridValid() tests
		// for exactly this and a stale-but-plausible block would defeat it.
		frameConstants[0].Set(0.0f, 0.0f, 0.0f, 0.0f);
		frameConstants[1].Set(0.0f, 0.0f, 0.0f, 0.0f);
	}

	const Vector3 forward = camera.Get_Forward_Dir();
	frameConstants[2].Set(forward.X, forward.Y, forward.Z, (float)m_lightCount);	// CameraForward; .w is C3's own field

	// C5.1's two gates, in ClusterLimits.z and .w.
	//
	// **ZERO IS OFF FOR BOTH, and that polarity is what makes this stage verifiable.** Every
	// path that leaves the b1 block unwritten -- a frame before the first Update(), a
	// degenerate camera, a menu, a device that could not create the buffers -- reads zero in
	// the shader, which means "no clustered contribution, directional term as it always
	// was", i.e. exactly the frame that existed before C5. The plan's gate here is a replay
	// with the option off showing 0 differing pixels, and that claim only means something
	// because there is no state in which off is a guess.
	//
	// isClusteredLightingActive() is the SAME predicate W3DView::draw binds the buffers
	// with, deliberately: the shader's gate and the binding cannot then disagree, and a
	// frame that says "on" in b1 while nothing is bound would read every buffer as zero --
	// which looks exactly like "no lights near this pixel" and has no visible symptom at all
	// (the hazard C1's note names).
	const float clusteredOn = W3DShaderManager::isClusteredLightingActive() ? 1.0f : 0.0f;
	float suppressDirectional = 0.0f;
#ifdef RTS_DEBUG
	// ...and only when stand-in lights were actually injected THIS frame. See
	// Collect_Sun_Check_Lights: switching the directional term off on a frame that injected
	// nothing compares a lit frame against a black one, which is a difference that says
	// nothing about the irradiance convention the control exists to measure.
	if (m_sunCheckActive && clusteredOn > 0.0f)
		suppressDirectional = 1.0f;
#endif

	if (cluster.valid)
	{
		frameConstants[3].Set(cluster.viewportX, cluster.viewportY,
			cluster.viewportWidth, cluster.viewportHeight);						// ClusterScreen
		frameConstants[4].Set((float)cluster.gridY,
			(float)ClusterGridClass::CLUSTER_MAX_LIGHTS,
			clusteredOn, suppressDirectional);									// ClusterLimits
	}
	else
	{
		// A degenerate camera. The gates go to zero along with the addressing, which is the
		// right answer and not merely the tidy one: with no valid grid there is no cluster
		// to read, so the shader must fall back to the frame it drew before C5 -- and a
		// suppressed directional term with nothing standing in for it would be a black unit.
		frameConstants[3].Set(0.0f, 0.0f, 0.0f, 0.0f);
		frameConstants[4].Set(0.0f, 0.0f, 0.0f, 0.0f);
	}

	// C6's four: the projection's lateral terms and the camera's view matrix.
	//
	// THESE ARE THE BUILDER'S FIELDS AND NO READER WANTS THEM -- every consumer goes from a
	// pixel position to a cluster, which needs nothing here. clusterassign_cs.hlsl goes the
	// other way, from a world-space light sphere to a set of clusters, and b1 is the only
	// constant buffer a compute shader can see (Dispatch has no per-draw setup step to bind
	// anything else from). Nine of the sixteen vec4 holds are now in use.
	//
	// Written from the SAME camera, in the same call, as the fields above -- which is what
	// makes them agree with the CPU oracle bit for bit. ClusterGridClass::Update takes its
	// own copy of Get_View_Matrix() a moment later in the same frame and nothing moves the
	// camera in between; if that ever stops being true, the oracle and the shader start
	// binning against two different cameras and every disagreement they report is that,
	// not a binning bug.
	if (cluster.valid)
	{
		frameConstants[5].Set(cluster.projXScale, cluster.projXOffset,
			cluster.projYScale, cluster.projYOffset);							// ClusterProj

		// Three rows of a Matrix3D; the implicit fourth is (0,0,0,1) and is not sent. The
		// sign of view-space z is NOT corrected here -- the shader negates the third row's
		// result itself, exactly as ClusterGridClass::Build does, because the forward-
		// positive frame is the builders' private convention and not something to bake into
		// a matrix other readers might later want.
		const Matrix3D & view = camera.Get_View_Matrix();
		frameConstants[6].Set(view[0][0], view[0][1], view[0][2], view[0][3]);	// ClusterView0
		frameConstants[7].Set(view[1][0], view[1][1], view[1][2], view[1][3]);	// ClusterView1
		frameConstants[8].Set(view[2][0], view[2][1], view[2][2], view[2][3]);	// ClusterView2
	}
	else
	{
		// A degenerate camera, same as above: zeroed rather than stale. The compute shader
		// refuses the frame on ClusterGridValid() before it reads any of these, so what
		// they hold only has to be harmless, and zero is the value every other gate in this
		// block already means "off" by.
		frameConstants[5].Set(0.0f, 0.0f, 0.0f, 0.0f);
		frameConstants[6].Set(0.0f, 0.0f, 0.0f, 0.0f);
		frameConstants[7].Set(0.0f, 0.0f, 0.0f, 0.0f);
		frameConstants[8].Set(0.0f, 0.0f, 0.0f, 0.0f);
	}

	DX8Wrapper::Set_Frame_Constants(frameConstants, 9);
}

#ifdef RTS_DEBUG

bool GpuLightListClass::Verify_Upload() const
{
	if (m_buffer == nullptr)
	{
		WWDEBUG_SAY(("GpuLightListClass::Verify_Upload: no LightBuffer to check -- the "
			"device has not created one this run (or has none)."));
		return false;
	}
	if (m_lightCount == 0)
	{
		WWDEBUG_SAY(("GpuLightListClass::Verify_Upload: PASS trivially -- 0 lights were "
			"uploaded this frame, so there is nothing to compare. Set "
			"W3D_SYNTHETIC_LIGHTS to get a population worth checking."));
		return true;
	}

	// GFX_MAP_READ stalls the pipeline until every draw and dispatch queued ahead of it
	// has finished -- see gfxdevice.h's Map_Buffer comment -- which is why this function
	// exists at all rather than folding the check into Upload(): nothing on the render
	// path may call this, ever, and a debug-only function nobody calls automatically is
	// how that stays true.
	void * data = nullptr;
	if (!DX8Wrapper::Gfx->Map_Buffer(m_buffer, GFX_MAP_READ, &data))
	{
		WWDEBUG_SAY(("GpuLightListClass::Verify_Upload: FAIL -- Map_Buffer(GFX_MAP_READ) itself failed."));
		return false;
	}

	const bool match = (memcmp(data, m_cpuLights, m_lightCount * sizeof(GpuLight)) == 0);
	DX8Wrapper::Gfx->Unmap_Buffer(m_buffer);

	WWDEBUG_SAY(("GpuLightListClass::Verify_Upload: %s -- %u GpuLight record(s), %u bytes, "
		"read back from the GPU buffer and compared to the CPU array Update() packed them "
		"from.", match ? "PASS" : "FAIL", m_lightCount, (unsigned)(m_lightCount * sizeof(GpuLight))));
	return match;
}

void GpuLightListClass::Report_Census()
{
	static unsigned frames = 0;
	const unsigned REPORT_INTERVAL = 100;	// same cadence as the D3D11 backend's own census (gfxdevice_d3d11.cpp)
	if (++frames < REPORT_INTERVAL)
		return;
	frames = 0;

	WWDEBUG_SAY(("GpuLightListClass CENSUS: %u enumerated, %u frustum-culled, %u uploaded, %u dropped (capacity %u)",
		m_censusEnumerated, m_censusCulled, m_lightCount, m_censusDropped,
		(unsigned)GPU_LIGHT_CAPACITY));
}

namespace
{
	// A small, deterministic hash -- a Murmur3-style finalizer, nothing fancier is needed
	// since this only has to look uncorrelated and repeat exactly, not resist attack.
	// Same (seed, index, salt) always produces the same float on any run of this binary,
	// which is the whole property this synthetic population exists to have: two runs
	// with the same W3D_SYNTHETIC_LIGHTS produce the identical base layout.
	unsigned Hash_U32(unsigned x)
	{
		x ^= x >> 16; x *= 0x7feb352du;
		x ^= x >> 15; x *= 0x846ca68bu;
		x ^= x >> 16;
		return x;
	}

	float Hash01(unsigned seed, unsigned index, unsigned salt)
	{
		const unsigned h = Hash_U32(seed ^ (index * 0x9E3779B1u) ^ (salt * 0x85EBCA77u));
		return (float)(h & 0x00FFFFFFu) * (1.0f / (float)0x01000000u);	// [0, 1)
	}

	// One synthetic light. Base placement, colour, type and range are a pure function of
	// (seed, index); frameCounter only drives a slow wobble on top, so the base
	// population is bit-identical between runs and only the animation moves. frameCounter
	// rather than wall-clock time: this project's replay verification is already
	// frame-indexed (the replay test harness documentation), and reading a wall clock here would
	// throw that determinism away for no benefit.
	void Generate_Synthetic_Light(unsigned index, unsigned seed, unsigned frameCounter,
		const AABoxClass & footprint, GpuLight & outLight, SphereClass & outSphere)
	{
		const float u = Hash01(seed, index, 1) * 2.0f - 1.0f;
		const float v = Hash01(seed, index, 2) * 2.0f - 1.0f;
		const float baseX = footprint.Center.X + u * footprint.Extent.X;
		const float baseY = footprint.Center.Y + v * footprint.Extent.Y;
		const float range = 20.0f + Hash01(seed, index, 4) * 20.0f;	// plan: "plausible ranges (20-40 world units)"

		const float phase = Hash01(seed, index, 7) * 2.0f * WWMATH_PI;
		const float timeSeconds = (float)frameCounter * (1.0f / 30.0f);	// nominal clock; only needs to move smoothly, not track real elapsed time
		const float pulse = 0.85f + 0.15f * WWMath::Fast_Sin(timeSeconds * 0.8f + phase * 1.3f);

		// HEIGHT: measured off the terrain, and a FRACTION OF THE LIGHT'S OWN RANGE.
		//
		// Both halves of that were wrong when this generator was first written, and
		// between them they made the whole synthetic population invisible -- which is a
		// worse failure than a wrong-looking one, because nothing about it says "the test
		// content is broken" rather than "the clustered path draws nothing".
		//
		// The height was a fixed 40 units above a Z picked from the terrain bounding
		// box's vertical extent -- so a light sat ~40 units over the ground while
		// carrying a range of 20-40. ClusterLightRadiance returns false at d >= range, so
		// most of the population could not reach the ground at all, and the rest reached
		// it exactly where the smooth window has already fallen to nothing. The lights
		// were binned into clusters, uploaded and counted by every census the whole time;
		// they simply had no receiver inside their own reach. Every count C3-C6 reported
		// was still true -- which is exactly why counts alone could not catch this.
		//
		// Tying the lift to the range instead keeps a fixture at a sensible height for
		// its size and guarantees the ground beneath it is well inside the falloff: at
		// 0.35 of the range the window (1 - (d/range)^4)^2 is still 0.97.
		const float lift = range * 0.35f;
		const float bob  = WWMath::Fast_Sin(timeSeconds * 0.5f + phase) * (lift * 0.1f);
		float groundZ = footprint.Center.Z;
		if (TheTerrainRenderObject != nullptr)
			groundZ = TheTerrainRenderObject->getHeightMapHeight(baseX, baseY, nullptr);
		const Vector3 pos(baseX, baseY, groundZ + lift + bob);

		// COLOUR: an intensity, through the same convention every authored light now uses.
		//
		// The other half of the invisibility, and the more instructive half. This was a
		// raw HSV value of 1.6 -- a BRIGHTNESS -- written straight into colorType at a
		// time when nothing downstream divided by distance. The re-authoring pass moved
		// every real fixture to luminous intensity (I = B * r^2, W3DLightAuthoring.h) and
		// this generator was not part of that pass, so it went on handing a brightness to
		// a consumer that had started reading intensities. Under atten = win / d^2 a
		// value of 1.6 arriving from ~10 units away is 0.016 -- a fraction of one 8-bit
		// level, before the range problem above even applies.
		//
		// So it goes through authoredLocalLightColor like everything else, with the lift
		// as the reference distance: "this bright on the ground directly underneath",
		// which is the one distance about a test fixture anybody can judge by eye.
		Vector3 color;
		const Vector3 hsv(Hash01(seed, index, 5) * 360.0f, 0.85f, 1.6f);	// brightness AT `lift`; value > 1 keeps it reading clearly against ambient
		HSV_To_RGB(color, hsv);
		color *= pulse;
		color = authoredLocalLightColor(color, lift);

		const bool isSpot = Hash01(seed, index, 6) < 0.25f;

		outLight.posRange.Set(pos.X, pos.Y, pos.Z, range);
		outLight.colorType.Set(color.X, color.Y, color.Z, isSpot ? 1.0f : 0.0f);

		if (isSpot)
		{
			// A loose downward cone with lateral jitter -- meant to read as a spotlight
			// aimed at the ground, nothing more specific than that.
			Vector3 dir(Hash01(seed, index, 8) * 0.6f - 0.3f, Hash01(seed, index, 9) * 0.6f - 0.3f, -1.0f);
			dir.Normalize();
			const float outerAngle = DEG_TO_RADF(20.0f + Hash01(seed, index, 10) * 25.0f);
			outLight.spotDirCos.Set(dir.X, dir.Y, dir.Z, WWMath::Fast_Cos(outerAngle));
			outLight.spotInner.Set(WWMath::Fast_Cos(outerAngle * SPOT_INNER_FRACTION), 0.0f, 0.0f, 0.0f);
		}
		else
		{
			outLight.spotDirCos.Set(0.0f, 0.0f, 0.0f, -1.0f);
			outLight.spotInner.Set(-1.0f, 0.0f, 0.0f, 0.0f);
		}

		outSphere.Init(pos, range);
	}
}	// namespace

void GpuLightListClass::Collect_Synthetic_Lights(const FrustumClass & frustum, const Vector3 & cameraPos)
{
	// Debug-only test content: C3-C6 all need lights to exist before any real content
	// does (unit-authored lights are separate work -- plan section "No unit light
	// content"). Off by default; opt in with
	//     set W3D_SYNTHETIC_LIGHTS=500
	// which is also the population size. Follows the same env-var-gated-diagnostic shape
	// as W3D_FORCE_RESET_FRAME (ww3d.cpp): read once, cached, and armed only for a shell
	// that deliberately exported the variable, never for a build left lying around.
	static bool parsed = false;
	static unsigned syntheticCount = 0;
	if (!parsed)
	{
		parsed = true;
		const char * spec = ::getenv("W3D_SYNTHETIC_LIGHTS");
		if (spec != nullptr)
		{
			const long n = ::strtol(spec, nullptr, 10);
			const long capped = (n > (long)GPU_LIGHT_CAPACITY) ? (long)GPU_LIGHT_CAPACITY : n;
			syntheticCount = (capped > 0) ? (unsigned)capped : 0;
			if (syntheticCount > 0)
			{
				WWDEBUG_SAY(("GpuLightListClass: W3D_SYNTHETIC_LIGHTS=%u -- generating a "
					"synthetic light population for testing.", syntheticCount));
			}
		}
	}
	if (syntheticCount == 0)
		return;

	// The footprint to scatter over: the terrain's own world bounding box once a map has
	// loaded, else a placeholder centred on the origin -- so this never has to
	// special-case "no terrain yet" (menus, headless launch, before Update() has run
	// with a map loaded).
	AABoxClass footprint(Vector3(0.0f, 0.0f, 0.0f), Vector3(500.0f, 500.0f, 100.0f));
	if (TheTerrainRenderObject != nullptr)
		footprint = TheTerrainRenderObject->Get_Bounding_Box();

	const unsigned seed = 0x9E3779B9u;	// fixed: determinism is the point, see Generate_Synthetic_Light
	const unsigned frameCounter = (unsigned)WW3D::Get_Frame_Count();

	for (unsigned i = 0; i < syntheticCount; ++i)
	{
		GpuLight light;
		SphereClass sphere;
		Generate_Synthetic_Light(i, seed, frameCounter, footprint, light, sphere);
		Cull_And_Insert(light, sphere, frustum, cameraPos);
	}
}

//-------------------------------------------------------------------------------------------------
// THE C5.1 SUN-EQUIVALENCE CONTROL.
//
// The plan calls this "the single most valuable control in the whole plan", and the reason
// is narrow and specific: two bugs in unit_pbr_ps -- the missing `radiance *= PI` and
// SHADOW_MIN applied in linear -- were both a value shared between two places that were not
// in the same *space*, and a new punctual term added beside an existing directional one is
// exactly that shape again. A local light landing at a different strength from the sun of
// the same colour is not something a screenshot shows; it reads as a look decision.
//
// So: stand a clustered POINT light in for each of the scene's global DIRECTIONAL lights,
// switch the shader's own directional term off (b1's ClusterLimits.w), and let the two
// paths draw the same frame of the same binary. If they agree, the convention is right. If
// they differ by a constant factor, it is PI. If they differ by a curve rather than a
// factor, it is a space error -- something is being done in linear that belongs in gamma or
// the reverse.
//
// HOW A POINT LIGHT STANDS IN FOR A DIRECTIONAL ONE. Put it very far away along the light's
// own direction and cancel the inverse square by hand:
//
//   position = (direction toward the light) * SUN_CHECK_DISTANCE
//   colour   = the directional light's diffuse * SUN_CHECK_DISTANCE^2
//   range    = SUN_CHECK_RANGE, chosen so the smooth window is 1.0 exactly at that distance
//
// The distance is what bounds the residual, and it is why it is 1e7 and not something
// tidier. A directional light is the same everywhere; a point light at distance D varies
// across a scene of extent E by about 2E/D in irradiance. At D = 1e7 and a Generals map's
// ~4000 world units that is 8e-4 in linear, about 4e-4 after the sRGB encode -- a tenth of
// an 8-bit level, so it cannot round any pixel to a different byte. At D = 1e5 it would be
// 8% and the control would be measuring its own approximation.
//
// It deliberately does NOT bypass the attenuation, the window or the packing. Everything
// except the *value* of the falloff is the shipping path: the same GpuLight record, the same
// upload, the same cluster grid, the same shader loop, the same DirectLight(). A control
// that took a short cut around the code it is checking would only ever prove itself.
//-------------------------------------------------------------------------------------------------

void GpuLightListClass::Collect_Sun_Check_Lights(RTS3DScene & scene)
{
	// Same env-var-gated-diagnostic shape as W3D_SYNTHETIC_LIGHTS above and
	// W3D_FORCE_RESET_FRAME (ww3d.cpp): read once, cached, armed only for a shell that
	// deliberately exported it.
	static bool parsed = false;
	static bool armed = false;
	if (!parsed)
	{
		parsed = true;
		const char * spec = ::getenv("W3D_CLUSTER_SUN_CHECK");
		armed = (spec != nullptr && ::strtol(spec, nullptr, 10) != 0);
		if (armed)
		{
			WWDEBUG_SAY(("GpuLightListClass: W3D_CLUSTER_SUN_CHECK is set -- standing a "
				"clustered point light in for each of the scene's global directional lights "
				"and suppressing the shaders' own directional term. This frame is NOT the "
				"game's normal output and no capture from it is a reference for anything "
				"else."));
			// Said here rather than left to be discovered from a frame that looks wrong.
			// The check needs the clustered path actually running to have anything to
			// compare against, and if the buffers failed to create the only visible result
			// is unlit units -- which looks like the control failing rather than like it
			// never ran.
			if (!W3DShaderManager::isClusteredLightingActive())
			{
				WWDEBUG_SAY(("GpuLightListClass: ...but clustered lighting is not active "
					"(one of the three structured buffers is missing), so nothing will "
					"consume the stand-in lights and the directional term will NOT be "
					"suppressed either -- see Write_Frame_Constants."));
			}
		}
	}
	if (!armed)
		return;

	// Far enough that the residual is below an 8-bit level across a whole map; see the note
	// above. The range is three orders of magnitude beyond it so that the falloff window
	// evaluates to 1.0 exactly in float32 -- (d/range)^4 = 1e-16 there, and 1 - 1e-16 rounds
	// to 1.0 -- rather than to a number that is merely close, which would show up as a
	// uniform factor and be indistinguishable from the PI bug the control is hunting.
	const float SUN_CHECK_DISTANCE = 1.0e7f;
	const float SUN_CHECK_RANGE = 1.0e9f;
	const float gain = SUN_CHECK_DISTANCE * SUN_CHECK_DISTANCE;

	const Int globalCount = scene.getNumGlobalLights();
	for (Int i = 0; i < globalCount; ++i)
	{
		const LightClass * light = scene.getGlobalLight(i);
		if (light == nullptr || light->Get_Type() != LightClass::DIRECTIONAL)
			continue;

		Vector3 diffuse;
		light->Get_Diffuse(&diffuse);
		// Mirrors LightEnvironmentClass::Add_Light's own near-black reject, so the stand-in
		// set matches the directional set the shader would otherwise have been given. A
		// light this drops is one the CPU model drops too, and if the two disagreed about
		// which lights exist the comparison would be measuring that instead.
		if (diffuse.X < 0.05f && diffuse.Y < 0.05f && diffuse.Z < 0.05f)
			continue;

		// The direction toward the light, derived exactly as
		// LightEnvironmentClass::Init_From_Directional_Light derives it -- -Z of the light's
		// own transform -- because that is the vector that reaches unit_pbr_ps as LightDirN
		// (Set_Light_Environment negates it into a D3DLIGHT9 direction, DX8Wrapper negates
		// it back). Recomputing it from TheGlobalData->m_terrainLightPos would be a second
		// route to the same number and therefore a second thing that can drift.
		Vector3 toLight = -light->Get_Transform().Get_Z_Vector();
		if (toLight.Length2() < 1.0e-6f)
			continue;
		toLight.Normalize();

		const Vector3 pos = toLight * SUN_CHECK_DISTANCE;

		GpuLight standIn;
		standIn.posRange.Set(pos.X, pos.Y, pos.Z, SUN_CHECK_RANGE);
		// Type 0 = point. A spot would add a cone term that has nothing to do with the
		// question being asked.
		standIn.colorType.Set(diffuse.X * gain, diffuse.Y * gain, diffuse.Z * gain, 0.0f);
		standIn.spotDirCos.Set(0.0f, 0.0f, 0.0f, -1.0f);
		standIn.spotInner.Set(-1.0f, 0.0f, 0.0f, 0.0f);

		// Straight to Insert_Light, past the frustum test: a sphere of radius 1e9 contains
		// the frustum, so the test would pass, but relying on CollisionMath to behave at
		// that scale is a dependency this control does not need. A distance of 0 also makes
		// it the nearest light in the set, so it can never be the one that loses a capacity
		// contest -- which would silently turn the check off on a busy frame.
		Insert_Light(standIn, 0.0f);
		m_sunCheckActive = true;
	}
}

#endif	// RTS_DEBUG

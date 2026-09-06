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

#pragma once

#include "WW3D2/gpulight.h"
#include "WWMath/vector3.h"
#include "WWMath/sphere.h"

class RTS3DScene;
class CameraClass;
class FrustumClass;
class LightClass;
struct GfxBuffer;

// TheSuperHackers @feature andytraber 06/09/2026 The scene's clustered light list (C3 of
// the clustered lighting plan).
//
// GpuLightListClass gathers the scene's local point and spot lights, culls each one's
// bounding sphere against the camera frustum, packs the survivors into GpuLight records
// (gpulight.h) and uploads them to a GFX_BUFFER_DYNAMIC structured buffer -- once a frame,
// where the CPU lighting model this replaces (LightEnvironmentClass, see
// the clustered lighting plan section 0.1) walks the light lists once per drawable.
//
// This stage renders nothing. Nothing in the engine binds LightBuffer to a shader slot or
// reads it in a shader yet -- that starts at C5. The only thing anyone can check right now
// is that the buffer holds what Update() thinks it uploaded; see Verify_Upload().
//
// Owned by RTS3DScene as a value member (m_gpuLightList) and rebuilt by one call to
// Update() per frame, from W3DView::draw() immediately before the shadow-map pass -- see
// the FRAME_TIMING_SCOPE(PHASE_LIGHTLIST) call site there. The GPU buffer itself is
// created lazily, on first Update(): RTS3DScene is constructed before WW3D::Init brings up
// the device (see W3DDisplay::init()), so there is no device to create it against yet at
// construction time.
//
// THE ENUMERATION SEAM. Collect_Lights() appends from each light source in turn, each its
// own private Collect_*_Lights() method, specifically so a second source is a new method
// plus one more line there -- not a rewrite. There is a concrete second source already
// under construction elsewhere in this tree: bone-parented lights attached to HLOD models
// (W3D_CHUNK_HLOD_LIGHT_ARRAY / HLodLightArrayClass / HLodClass::Get_Light_Count /
// Get_Light). This stage deliberately does not touch that work -- W3DModelDraw.h/.cpp,
// hlod.h/.cpp, light.h and w3d_file.h are all off limits here -- so Collect_Lights() names
// the seam in a comment instead of wiring it up; see the .cpp.
class GpuLightListClass
{
public:
	GpuLightListClass();
	~GpuLightListClass();

	// The plan's design capacity: comfortable at ~1500 live lights, must not fall over at
	// 4096. See Insert_Light() in the .cpp for what happens to the 4097th.
	enum { GPU_LIGHT_CAPACITY = 4096 };

	// The once-a-frame entry point: enumerate, cull against camera's frustum, pack,
	// upload, and write the b1 frame-constants block -- see Write_Frame_Constants() in
	// the .cpp for why this is also the single place that writes the *whole* block, not
	// just the light count C3 owns.
	void Update(RTS3DScene & scene, CameraClass & camera);

	// How many of GPU_LIGHT_CAPACITY records were actually uploaded this frame. The same
	// number Update() writes into b1's CameraForward.w.
	unsigned Get_Light_Count() const { return m_lightCount; }

#ifdef RTS_DEBUG
	// The verification gate: read the buffer Update() just uploaded back from the GPU and
	// compare it, byte for byte, against the CPU array it was built from. Logs PASS/FAIL
	// and returns it.
	//
	// GFX_MAP_READ stalls the pipeline until every draw and dispatch queued ahead of it has
	// finished (see gfxdevice.h's Map_Buffer comment) -- that is fine for a once-off debug
	// check and would not be fine on the render path, which is why this is a separate call
	// nothing calls automatically. See W3DGpuLightList.cpp for the recipe to trigger it.
	bool Verify_Upload() const;
#endif

private:
	// -- enumeration --------------------------------------------------------------------
	void Collect_Lights(RTS3DScene & scene, const FrustumClass & frustum, const Vector3 & cameraPos);
	void Collect_Scene_Lights(RTS3DScene & scene, const FrustumClass & frustum, const Vector3 & cameraPos);
	void Collect_Dynamic_Lights(RTS3DScene & scene, const FrustumClass & frustum, const Vector3 & cameraPos);
	// Collect_Hlod_Lights() belongs here once HLodLightArrayClass lands -- see the class
	// comment above. Deliberately not declared as an empty stub: an unused private method
	// invites bit-rot (wrong signature, silently never called) faster than a comment does.

#ifdef RTS_DEBUG
	void Collect_Synthetic_Lights(const FrustumClass & frustum, const Vector3 & cameraPos);
#endif

	// -- cull, pack, capacity -------------------------------------------------------------
	void Consider_Light(const LightClass & light, const FrustumClass & frustum, const Vector3 & cameraPos);
	void Cull_And_Insert(const GpuLight & light, const SphereClass & sphere,
		const FrustumClass & frustum, const Vector3 & cameraPos);
	void Insert_Light(const GpuLight & light, float distanceSq);

	// -- device side ------------------------------------------------------------------
	void Ensure_Buffer_Created();
	void Upload();
	void Write_Frame_Constants(CameraClass & camera);

#ifdef RTS_DEBUG
	void Report_Census();
#endif

	GfxBuffer *	m_buffer;		// GFX_BUFFER_DYNAMIC structured buffer; t8's eventual source. Created lazily -- see the class comment.
	bool		m_bufferCreateFailed;	// latches after one failed Create_Structured_Buffer, so a device that cannot make this buffer is reported once and Update() stops retrying every frame

	// The CPU side. m_cpuLights is both what Upload() copies from and what Verify_Upload()
	// compares the read-back against; m_distanceSq exists only to arbitrate a capacity
	// overflow (see Insert_Light()) and is not part of the uploaded record.
	GpuLight	m_cpuLights[GPU_LIGHT_CAPACITY];
	float		m_distanceSq[GPU_LIGHT_CAPACITY];
	unsigned	m_lightCount;

#ifdef RTS_DEBUG
	unsigned	m_censusEnumerated;	// candidates offered to Cull_And_Insert this frame, before the frustum test
	unsigned	m_censusCulled;		// of those, how many failed the frustum test
	unsigned	m_censusDropped;	// of the survivors, how many lost the capacity-overflow contest (see Insert_Light)
#endif
};

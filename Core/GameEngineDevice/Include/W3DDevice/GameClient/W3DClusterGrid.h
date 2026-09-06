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
#include "WWMath/matrix3d.h"
#include "WWMath/vector3.h"

class CameraClass;
struct GfxBuffer;

// TheSuperHackers @feature andytraber 06/09/2026 The clustered light grid (C4 of
// the clustered lighting plan).
//
// The screen is divided into 64x64 pixel tiles and 24 exponentially spaced depth slices;
// each of the resulting froxels ("clusters") gets a count of the lights that reach it and
// a fixed-stride list of their indices into C3's LightBuffer. A pixel shader then finds
// its own cluster from its screen position and its view distance and iterates only that
// cluster's lights -- which is the whole point of the exercise, and is C5's job.
//
// **THIS STAGE DELIBERATELY DOES NOT WRITE THE COMPUTE SHADER.** The grid is built here,
// on the CPU, and uploaded through GFX_BUFFER_DYNAMIC buffers. C6 replaces the *producer*
// with clusterassign_cs and keeps this one behind a flag as its oracle; the acceptance
// test there is that both produce bit-identical grids. C5 (the shader that reads the
// grid) and C6 (the shader that fills it) can each be wrong on their own, and the only
// way to tell which is to have written them at different times against a reference that
// was already known good. See "How C6 swaps the producer" below for what it has to touch.
//
// The algorithm is SCATTER, not gather, even on the CPU -- iterate lights, work out each
// one's tile rectangle and slice range, append into every cluster in that box -- because
// keeping the CPU builder the same algorithmic shape as the compute shader is what makes
// C6's bit-exact comparison mean something. A gather here would agree with a scatter
// there only by coincidence.
class ClusterGridClass
{
public:
	enum
	{
		// Tile size in pixels and slice count, straight from the plan's section 1.1. At
		// 1920x1080 that is 30 x 17 x 24 = 12240 clusters; at 2560x1440, 40 x 23 x 24 =
		// 22080.
		CLUSTER_TILE_SIZE	= 64,
		CLUSTER_SLICE_COUNT	= 24,

		// The fixed stride of the light-index list, in light indices per cluster. Its
		// HLSL twin is CLUSTER_MAX_LIGHTS in Shaders/clustergrid.hlsli and it is also
		// published in b1's ClusterLimits.y so no shader has to hardcode it twice.
		//
		// A cluster that wants more than this keeps the first 64 and the rest are counted
		// as drops -- never silently truncated. Whether 64 is the right number is a
		// question for the census (Report_Census), not for an argument: raising it is a
		// one-line change and the plan says to make it a measurement.
		CLUSTER_MAX_LIGHTS	= 64
	};

	ClusterGridClass();
	~ClusterGridClass();

	// Everything the grid's addressing needs, derived from the camera. A plain POD with no
	// device dependency, because two very different callers need it: Update() below, and
	// GpuLightListClass::Write_Frame_Constants, which is the single writer of the b1 block
	// and therefore has to publish these fields without owning them.
	//
	// Compute_Params() is static and stateless for exactly that reason -- both callers
	// derive it from the same camera in the same frame, so they agree by construction
	// rather than by ordering. An earlier shape of this had Write_Frame_Constants read the
	// live grid's parameters, which meant b1 described last frame's grid on any frame the
	// viewport changed.
	struct ClusterGridParams
	{
		bool		valid;			// false when the camera is degenerate (no viewport, or zNear >= zFar)

		unsigned	tileWidth;		// px
		unsigned	tileHeight;		// px
		unsigned	gridX;			// tiles across the viewport, ceil(viewportWidth / tileWidth)
		unsigned	gridY;			// tiles down
		unsigned	sliceCount;

		// The camera's viewport, in render-target pixels -- CameraClass::Apply's own
		// D3DVIEWPORT9, recomputed from the same normalized rectangle and the same render
		// target resolution. The grid is laid over this and not over the whole target: the
		// tactical view is not full-screen.
		float		viewportX;
		float		viewportY;
		float		viewportWidth;
		float		viewportHeight;

		// Positive distances in front of the camera, from CameraClass::Get_Clip_Planes.
		float		zNear;
		float		zFar;

		// slice = floor(log(viewDist) * depthScale + depthBias). See the derivation in
		// the .cpp; depthBias folds in the -depthScale*log(zNear) term.
		float		depthScale;
		float		depthBias;

		// The projection's lateral terms, so that a pixel column can be turned back into
		// the pair of view-space rays that bound it:
		//     ndcX = projXScale * (x_view / viewDist) - projXOffset
		//     ndcY = projYScale * (y_view / viewDist) - projYOffset
		// Taken from CameraClass::Get_Projection_Matrix's Row[0][0], Row[0][2], Row[1][1]
		// and Row[1][2] -- see Compute_Params for why those four and not a stored FOV.
		float		projXScale;
		float		projXOffset;
		float		projYScale;
		float		projYOffset;

		unsigned Cluster_Count() const { return gridX * gridY * sliceCount; }

		// ---- the addressing, mirrored from Shaders/clustergrid.hlsli --------------------
		// If you change one of these, change the other in the same commit. Neither side can
		// check the other, and a disagreement does not crash: it lights a pixel from a
		// cluster belonging to some other part of the screen.

		unsigned Cluster_Index(unsigned tileX, unsigned tileY, unsigned slice) const
			{ return (slice * gridY + tileY) * gridX + tileX; }

		int Slice_Of(float viewDist) const;
		float Slice_Near_Distance(unsigned slice) const;

		// The view-space axis-aligned box of one cluster, in the FORWARD-POSITIVE frame
		// (x right, y up, z the positive distance in front of the camera -- see the
		// handedness derivation in the .cpp). This is the exact predicate both the scatter
		// and the brute-force oracle test a light's sphere against, which is what lets the
		// two be compared for equality rather than for containment.
		void Cluster_Bounds(unsigned tileX, unsigned tileY, unsigned slice,
			Vector3 & outMin, Vector3 & outMax) const;
	};

	static void Compute_Params(CameraClass & camera, ClusterGridParams & out);

	// The once-a-frame entry point. Sizes the grid, scatters `lightCount` GpuLight records
	// into it and uploads both buffers. `lights` is C3's packed array -- see
	// GpuLightListClass::Get_Lights() -- and the indices this stores are indices into it,
	// which is the same thing as indices into LightBuffer because Upload() copies it
	// verbatim.
	//
	// Called from W3DView::draw() under FRAME_TIMING_SCOPE(PHASE_LIGHTCLUSTER),
	// immediately after the light list and before the shadow-map pass.
	void Update(const GpuLight * lights, unsigned lightCount, CameraClass & camera);

	const ClusterGridParams & Get_Params() const { return m_params; }

	// The two buffers, for whoever binds them. Null until the first successful Update().
	//
	// HOW C6 SWAPS THE PRODUCER. These accessors, the b1 block and the .hlsli addressing
	// are the *consumer* contract and none of them change. What changes is one thing:
	// Ensure_Buffers_Created() asks for GFX_BUFFER_DYNAMIC because the CPU writes these,
	// and C1 established that GFX_BUFFER_DYNAMIC and GFX_BUFFER_UAV are mutually exclusive
	// (D3D11 has no usage that is both CPU-written every frame and GPU-written). So C6
	// creates the same two buffers with GFX_BUFFER_UAV | GFX_BUFFER_UINT instead, clears
	// the grid with Clear_RW_Buffer_UInt, dispatches, and leaves Get_Grid_Buffer() and
	// Get_Index_Buffer() returning the same kind of handle to the same slots. Both are
	// GFX_BUFFER_UINT already, precisely so that C6's clear is available without changing
	// the view: C1 found ClearUnorderedAccessViewUint is undefined against a structured
	// buffer, and a clear a driver quietly ignores would leave last frame's grid in place.
	GfxBuffer * Get_Grid_Buffer() const { return m_gridBuffer; }
	GfxBuffer * Get_Index_Buffer() const { return m_indexBuffer; }

#ifdef RTS_DEBUG
	// THE POSITIVE CONTROL FOR C4, and the thing that makes C6's later bit-exact
	// comparison worth anything.
	//
	// The plan's stated gate for this stage is circular -- the CPU builder is the
	// reference the debug view is checked against -- so this is the independent second
	// opinion: an O(clusters x lights) brute-force gather that tests every light against
	// every cluster's exact AABB, with no tile rectangle and no slice range anywhere in
	// it, and compares the resulting per-cluster counts against what the scatter produced.
	// The two use the identical sphere/AABB predicate, so they must agree EXACTLY; a
	// scatter box that is too small shows up as a cluster the gather found lights in and
	// the scatter did not.
	//
	// It also reads the grid buffer back off the GPU and compares that to the CPU array,
	// which closes the other half of the question ("was what we built what we uploaded").
	// GFX_MAP_READ stalls the pipeline -- see gfxdevice.h's Map_Buffer comment -- so this
	// is a run-once-on-demand call and nothing on the render path may invoke it.
	//
	// Triggered either from a debugger, or unattended with W3D_CLUSTER_VERIFY=1, which
	// runs it on the first frame that has lights to bin. See the .cpp's recipe.
	bool Verify() const;

	// Census figures for the most recent Update(), logged every REPORT_INTERVAL frames the
	// way the backend's own Report_Absorbed_State does. These are what answer "is a stride
	// of 64 right" with a number.
	void Report_Census() const;
#endif

private:
#ifdef RTS_DEBUG
	void Collect_Census();
	// Runs Verify() once, unattended, when W3D_CLUSTER_VERIFY is set -- see the .cpp. A
	// control somebody has to remember to attach a debugger for is a control that does not
	// run in the replay harness, which is the only place this project measures anything.
	void Maybe_Run_Verify(unsigned lightCount);
#endif

	void Ensure_Buffers_Created();
	void Build(const GpuLight * lights, unsigned lightCount);
	void Scatter_Light(const GpuLight & light, const Vector3 & viewPos, unsigned lightIndex);
	void Upload();

	// The sphere's silhouette across one lateral axis, as the range of (axis / distance)
	// it covers. False means "unbounded on this axis" -- the camera is inside the sphere's
	// slab, or a tangent point falls behind the camera -- in which case the caller takes
	// the whole viewport, because a projected sphere that wraps around the eye has no
	// finite screen rectangle.
	static bool Sphere_Axis_Bounds(float c, float cz, float r, float & outLo, float & outHi);

	// The one predicate. Shared by the scatter's per-cluster refinement and by Verify()'s
	// brute-force gather, deliberately: two copies of it would be free to disagree, and
	// then the control would be checking one bug against another.
	static bool Sphere_Overlaps_Box(const Vector3 & center, float radius,
		const Vector3 & boxMin, const Vector3 & boxMax);

	ClusterGridParams	m_params;
	// The camera's inverse transform for the frame being built -- the same matrix
	// CameraClass::Apply hands the device as D3DTS_VIEW. Cached for the duration of
	// Build() rather than passed down, because Scatter_Light is called once per light and
	// the debug oracle needs the same matrix afterwards.
	Matrix3D			m_viewMatrix;
	bool				m_bufferCreateFailed;	// latches so a device that cannot make these is reported once, not every frame
	unsigned			m_allocatedClusters;	// what the live buffers and CPU arrays were sized for; a resize is what a viewport change costs

	GfxBuffer *			m_gridBuffer;	// t9: one unclamped count per cluster
	GfxBuffer *			m_indexBuffer;	// t10: CLUSTER_MAX_LIGHTS light indices per cluster

	// The CPU side, and what Upload() copies from. m_counts holds the UNCLAMPED count --
	// how many lights reached the cluster, which may exceed CLUSTER_MAX_LIGHTS -- because
	// a count clamped at the stride is indistinguishable from a cluster that legitimately
	// holds exactly the stride, and then an overflow can never be seen. Every reader
	// iterates min(count, stride); clustergrid.hlsli says so where the readers are.
	unsigned *			m_counts;
	unsigned *			m_indices;

	// How many (light, cluster) pairs actually landed in the index list this frame. Not a
	// debug counter: Upload() skips the light-index list entirely when this is zero, which
	// is the whole of today's content -- no map ships a point or spot light yet, so without
	// it every frame would memcpy 3 MB describing nothing. Correct rather than merely
	// cheap: with no appends every count is zero, and no reader looks past a count.
	unsigned			m_appendCount;

#ifdef RTS_DEBUG
	// What the brute-force control re-derives its own answer from: C3's packed array and
	// its count, exactly as Update() was handed them. A pointer and not a copy -- the
	// array is GpuLightListClass's member and outlives every frame -- and Verify()
	// transforms them into view space itself rather than reusing anything the scatter
	// computed, so the two share only the sphere/box predicate they are meant to share.
	const GpuLight *	m_verifyLights;
	unsigned			m_verifyLightCount;

	unsigned	m_censusLights;			// lights offered to the scatter
	unsigned	m_censusLightsBinned;	// of those, how many landed in at least one cluster
	unsigned	m_censusDrops;			// pairs refused because the cluster was already full
	unsigned	m_censusTouched;		// clusters with a nonzero count
	unsigned	m_censusMaxOccupancy;	// the largest count in the grid, unclamped
	unsigned	m_censusOverflowed;		// clusters whose count exceeded CLUSTER_MAX_LIGHTS
	unsigned	m_censusWholeScreen;	// lights whose projected rectangle had to be taken as the whole viewport
#endif
};

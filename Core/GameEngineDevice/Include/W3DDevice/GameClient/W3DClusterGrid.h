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
// C6 HAS LANDED AND THE PRODUCER IS NOW THE GPU. `clusterassign_cs.hlsl` fills both
// buffers with one thread per light; this class clears the count buffer, dispatches, and
// keeps the CPU builder behind **W3D_CLUSTER_CPU=1** as the oracle. The acceptance test is
// that the two produce the same grid for one frame's light set, compared buffer to buffer
// on the CPU -- see Verify() below and the recipe at the top of the .cpp.
//
// Why the CPU builder was written first, and why it stays: C5 (the shader that reads the
// grid) and C6 (the shader that fills it) can each be wrong on their own, and the only way
// to tell which is to have written them at different times against a reference that was
// already known good. Deleting the oracle now would throw that reference away the moment
// it first became useful.
//
// The algorithm is SCATTER, not gather, on both sides -- iterate lights, work out each
// one's tile rectangle and slice range, append into every cluster in that box -- because
// keeping the CPU builder the same algorithmic shape as the compute shader is what makes
// the comparison mean something. A gather here would agree with a scatter there only by
// coincidence.
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

	// The once-a-frame entry point. Sizes the grid, bins `lightCount` lights into it, and
	// leaves both buffers holding this frame's answer.
	//
	// THE SAME LIGHT SET ARRIVES TWICE, in the two forms the two producers need.
	// `lightBuffer` is C3's uploaded LightBuffer and is what the compute shader reads;
	// `lights` is the CPU-side packed array behind it (GpuLightListClass::Get_Lights()) and
	// is what the CPU oracle reads. They are the same records in the same order, which is
	// the property the whole comparison rests on -- Upload() there copies the array
	// verbatim -- so an index stored here means the same light whichever producer stored
	// it.
	//
	// Called from W3DView::draw() under FRAME_TIMING_SCOPE(PHASE_LIGHTCLUSTER), immediately
	// after the light list and before the shadow-map pass. **The clustered pixel-stage
	// bindings must be (re-)established AFTER this call**, not before: binding these two
	// buffers as unordered-access views nulls any pixel-stage SRV binding of them, which is
	// C1's hazard rule doing its job. W3DView::draw calls bindClusteredLightBuffers()
	// immediately afterwards for exactly that reason.
	void Update(const GpuLight * lights, unsigned lightCount, GfxBuffer * lightBuffer,
		CameraClass & camera);

	const ClusterGridParams & Get_Params() const { return m_params; }

	// The two buffers, for whoever binds them. Null until the first successful Update().
	//
	// HOW C6 SWAPPED THE PRODUCER, since the answer is short and the question recurs. These
	// accessors, the b1 block and the .hlsli addressing are the *consumer* contract and
	// none of them changed. One thing did: Ensure_Buffers_Created() asks for
	// GFX_BUFFER_UAV where it used to ask for GFX_BUFFER_DYNAMIC, because C1 established
	// that the two are mutually exclusive -- D3D11 has no usage that is both CPU-written
	// every frame and GPU-written -- so this is a swap and not an addition. Both were
	// GFX_BUFFER_UINT from the start, precisely so that the per-frame clear was available
	// without changing the view as well: C1 found ClearUnorderedAccessViewUint undefined
	// against a *structured* buffer, and a clear a driver quietly ignores would leave last
	// frame's grid in place, which draws lights that are no longer there.
	//
	// Under W3D_CLUSTER_CPU=1 they go back to GFX_BUFFER_DYNAMIC and the CPU fills them.
	// The choice is made once, at the first allocation, and does not change for the run.
	GfxBuffer * Get_Grid_Buffer() const { return m_gridBuffer; }
	GfxBuffer * Get_Index_Buffer() const { return m_indexBuffer; }

	// Which producer this run is using. FALSE is the GPU dispatch, which is the default and
	// the point of C6; TRUE is C4's CPU scatter, selected with W3D_CLUSTER_CPU=1.
	bool Is_Cpu_Builder() const { return m_useCpuBuilder; }

#ifdef RTS_DEBUG
	// THE POSITIVE CONTROL, and after C6 it carries three checks rather than two.
	//
	// (1) THE BRUTE FORCE. The plan's stated gate for C4 was circular -- the CPU builder is
	//     the reference the debug view is checked against -- so this is the independent
	//     second opinion: an O(clusters x lights) gather that tests every light against
	//     every cluster's exact AABB, with no tile rectangle and no slice range anywhere in
	//     it, compared cluster for cluster against the CPU scatter. The two use the
	//     identical sphere/AABB predicate, so they must agree EXACTLY; a scatter box that
	//     is too small shows up as a cluster the gather found lights in and the scatter did
	//     not.
	//
	// (2) THE COUNTS. Every cluster's count, read back off the GPU and compared against the
	//     CPU array. On the CPU path that asks "was what we built what we uploaded"; on the
	//     GPU path it is **C6's acceptance test**, and it is exact -- a count is an integer
	//     and InterlockedAdd is order-independent.
	//
	// (3) THE LIGHT INDICES. Each cluster's LIVE PREFIX (min(count, stride) entries; the
	//     tails past it are stale by design on both sides and are never compared), as a
	//     SORTED MULTISET. The GPU appends in whatever order its threads arrive in and the
	//     CPU appends in light-index order, so an ordered comparison would fail on a
	//     correct shader -- see the .cpp for the choice and what it costs. Clusters that
	//     overflowed the stride are excluded from this check and counted separately,
	//     because *which* 64 of them survive is genuinely order-dependent.
	//
	// GFX_MAP_READ stalls the pipeline -- see gfxdevice.h's Map_Buffer comment -- so this
	// is a run-once-on-demand call and nothing on the render path may invoke it. It is not
	// const: on the GPU path it has to run the CPU oracle itself, which is what fills the
	// arrays everything above compares against.
	//
	// Triggered either from a debugger, or unattended with W3D_CLUSTER_VERIFY=1, which
	// runs it on the first frame that has lights to bin. See the .cpp's recipe.
	bool Verify();

	// Census figures for the most recent Update(), logged every REPORT_INTERVAL frames the
	// way the backend's own Report_Absorbed_State does. These are what answer "is a stride
	// of 64 right" with a number.
	//
	// ON THE GPU PATH MOST OF THEM COST A READ-BACK, so they are not free and are not
	// invented: occupancy, overflow and the accepted/dropped pair are only reported when
	// W3D_CLUSTER_CENSUS=1 has armed the read-back, and the line says so when they are not.
	// See Collect_Census_From_Gpu.
	void Report_Census() const;
#endif

private:
#ifdef RTS_DEBUG
	void Collect_Census();
	// The same figures, recovered from the GPU's own count buffer. Everything derivable
	// from a per-cluster count is; the two that are not (how many lights landed somewhere,
	// and how many had to take the whole viewport) live inside the scatter and are reported
	// as unavailable rather than guessed at.
	void Collect_Census_From_Gpu();
	// Runs Verify() once, unattended, when W3D_CLUSTER_VERIFY is set -- see the .cpp. A
	// control somebody has to remember to attach a debugger for is a control that does not
	// run in the replay harness, which is the only place this project measures anything.
	void Maybe_Run_Verify(unsigned lightCount);
#endif

	void Ensure_Buffers_Created();
	// The compute shader, loaded on first use rather than at startup: the buffers are made
	// lazily too (the device and the camera are not ready when W3DShaderManager::init
	// runs), and a producer that loads its two halves in two different places has two
	// failure frames instead of one.
	void Ensure_Compute_Shader();
	void Build(const GpuLight * lights, unsigned lightCount);
	void Scatter_Light(const GpuLight & light, const Vector3 & viewPos, unsigned lightIndex);
	void Upload();
	// C6's producer: clear the count buffer on the GPU, bind, dispatch one thread per
	// light, unbind. Returns false if anything was missing, in which case the frame keeps
	// whatever the buffers already held -- which the cleared counts make harmless.
	bool Dispatch_Build(GfxBuffer * lightBuffer, unsigned lightCount);

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

	// Which producer fills the buffers. Read once from W3D_CLUSTER_CPU in the constructor
	// and never again: the buffers' usage flags are chosen from it at allocation and
	// GFX_BUFFER_DYNAMIC cannot become GFX_BUFFER_UAV without recreating them, so a switch
	// mid-run would have to tear the grid down. It is a build-and-run decision, not a
	// runtime one, which is all an oracle needs to be.
	bool				m_useCpuBuilder;
	unsigned			m_computeShader;		// clusterassign_cs; 0 until loaded, and 0 forever if it will not load
	bool				m_computeLoadFailed;	// latches, same as m_bufferCreateFailed: reported once, not every frame

	GfxBuffer *			m_gridBuffer;	// t9: one unclamped count per cluster
	GfxBuffer *			m_indexBuffer;	// t10: CLUSTER_MAX_LIGHTS light indices per cluster

	// The CPU side. On the CPU path this is what Upload() copies from; on the GPU path it
	// is the ORACLE's workspace and holds nothing until Verify() runs the CPU builder into
	// it, off the render path. Allocated either way, because that is the only thing that
	// makes the comparison possible on a moment's notice, and it is 88 KB + 3 MB of host
	// memory against a feature that already spends 3 MB of device memory on the same data.
	//
	// m_counts holds the UNCLAMPED count --
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

	// Whether the six occupancy figures above describe THIS frame's grid. Always true on
	// the CPU path, where they fall out of the scatter for nothing. On the GPU path they
	// exist only in device memory and reading them costs a full pipeline stall, so they are
	// true only when W3D_CLUSTER_CENSUS=1 armed the read-back -- and the census line says
	// "unavailable" rather than printing a stale or a zeroed number when it did not.
	bool		m_censusOccupancyValid;
	// ...and that read-back's own arming flag, parsed once. Separate from
	// W3D_CLUSTER_VERIFY because they answer different questions and cost differently: the
	// verify is one stall for one frame, this is a stall every REPORT_INTERVAL frames for
	// the whole run, and it POISONS THE PHASE_LIGHTCLUSTER TIMING in that run. Never quote
	// a dispatch cost from a run with this set.
	bool		m_censusReadbackArmed;
#endif
};

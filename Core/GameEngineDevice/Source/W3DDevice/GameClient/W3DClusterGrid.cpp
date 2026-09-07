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

// TheSuperHackers @feature andytraber 06/09/2026 ClusterGridClass -- see W3DClusterGrid.h
// for what this is and the clustered lighting plan's C4 and C6 sections for why.
//
// THE PRODUCER IS THE GPU (C6). Shaders/clusterassign_cs.hlsl fills both buffers with one
// thread per light; Dispatch_Build below clears the counts, binds, dispatches and unbinds.
// The CPU scatter that C4 wrote is still here, is still exercised, and is selected with
// W3D_CLUSTER_CPU=1 -- it is the ORACLE, and the acceptance test for C6 is that the two
// produce the same grid.
//
// VERIFICATION RECIPE:
//   1. Build a debug configuration and run with a light population and the control armed:
//        set W3D_SYNTHETIC_LIGHTS=200
//        set W3D_CLUSTER_VERIFY=1
//      Leave W3D_CLUSTER_CPU unset -- that is the GPU producer, which is what is being
//      checked. W3D_DEBUG_VIS=9 selects the occupancy heat map at startup for an
//      unattended run; in a live game F11 cycles onto it (it is the 9th mode, "Cluster
//      occupancy", with "Cluster overflow" behind it).
//   2. W3D_CLUSTER_VERIFY runs Verify() once, on the first frame that has lights to bin,
//      and logs PASS or FAIL through WWDEBUG_SAY. It does three things:
//        - runs the CPU builder into its own arrays (on the GPU path; on the CPU path they
//          are already filled), then a brute-force O(clusters x lights) gather with no
//          tile rectangle and no slice range anywhere in it, compared cluster for cluster
//          against that scatter. This is C4's control and it checks the ORACLE;
//        - reads the grid buffer back and compares EVERY CLUSTER'S COUNT against the CPU
//          array. This is C6's acceptance test and it is exact;
//        - reads the light-index list back and compares each cluster's LIVE PREFIX --
//          min(count, stride) entries -- against the CPU array's, as a sorted multiset.
//      A FAIL names the first disagreeing cluster and both values.
//      PASS looks like one line reading "PASS", with zero in all three disagreement
//      counts. Run it three times on three replays; the plan's gate is all three.
//   3. The per-100-frame census line (Report_Census) gives clusters touched, max and mean
//      occupancy, overflow count and the grid dimensions in force. On the GPU path the
//      occupancy half of that costs a read-back and is only produced when
//      W3D_CLUSTER_CENSUS=1 is also set -- which stalls the pipeline every hundred frames
//      and therefore POISONS the PHASE_LIGHTCLUSTER bucket in the F10 overlay for that
//      whole run. Measure the dispatch cost in a run WITHOUT it.
// Never call Verify() from the render path: GFX_MAP_READ stalls the pipeline, and the
// brute force is quadratic by design.
//
// THE THREE ENVIRONMENT VARIABLES, in one place:
//   W3D_CLUSTER_CPU=1      build the grid on the CPU (C4's scatter) instead of dispatching.
//                          The oracle; the default is the GPU.
//   W3D_CLUSTER_VERIFY=1   run the three-part control once, on the first frame with lights.
//   W3D_CLUSTER_CENSUS=1   read the grid back every hundred frames so the census line can
//                          report occupancy on the GPU path. Costs a stall each time.
//
// ---------------------------------------------------------------------------------------
// THE DEPTH HANDEDNESS, DERIVED
// ---------------------------------------------------------------------------------------
//
// The slice formula in the plan is
//     slice = floor( log(z_view / z_near) * scale + bias )
// and it needs z_view to be a POSITIVE distance in front of the camera. This engine's
// projection is right-handed, that has already produced one real bug in the screen-space
// reflection path, and the plan says the sign must be established rather than assumed. It
// cannot be established by looking at a screenshot -- a slice index that is negated or
// reversed still produces a picture -- so it is established here from four independent
// places in the tree, three of which are already known correct because live rendering
// depends on them.
//
// (1) WW3D2/camera.cpp, CameraClass::Update_Frustum, says it outright:
//         "Forward is negative Z in our viewspace coordinate system."
//     and then builds the frustum with znear = -znear_dist, zfar = -zfar_dist, where
//     znear_dist and zfar_dist are the positive numbers Get_Clip_Planes hands out. So the
//     signed view-space z of anything in front of the camera is NEGATIVE, and
//         viewDist = -z_view
//     is the positive distance the slice formula wants.
//
// (2) WWMath/matrix4.h, Matrix4x4::Init_Perspective, which is what Update_Frustum calls,
//     sets Row[3][2] = -1 and Row[3][3] = 0. Matrix4x4 multiplies a column vector, so
//         clip.w = -z_view = viewDist
//     Clip w IS the positive view distance. That is worth stating because it is the
//     cheapest possible source for C5: a vertex shader already has it, with no matrix
//     row and no reconstruction. Its own comment agrees: "the znear and zfar parameters
//     are positive distances ... even though in the camera coordinate system, the
//     clipping planes are at negative z coordinates."
//
// (3) CameraClass::Get_D3D_Projection_Matrix rewrites only the z row to move the range to
//     0<z<1, giving (in Matrix4x4's row-major storage, column-vector convention)
//         Row[2][2] = -zfar / (zfar - znear)
//         Row[2][3] = -(zfar * znear) / (zfar - znear)
//     Transposed into D3D's row-vector spelling those are _33 and _43, and
//         clip.z  = _33 * z_view + _43
//         clip.w  = -z_view
//         ndcZ    = clip.z / clip.w = -_33 - _43 / z_view
//     so
//         z_view  = -_43 / (_33 + ndcZ)     (negative: the signed view z)
//         viewDist = _43 / (_33 + ndcZ)     (positive: both terms come out negative)
//
// (4) The check against code that is already right. dx8wrapper.cpp's depth-prepass block
//     publishes exactly those two elements through Set_Ssr_Params(1.0f, SSR_MAX_RAY,
//     camProj._33, camProj._43), and records the measured pair: _33 = -1.006,
//     _43 = -10.06. Substituting into (3):
//         ndcZ = 0 (near):  viewDist = -10.06 / (-1.006 + 0)    = 10.0
//         ndcZ = 1 (far):   viewDist = -10.06 / (-1.006 + 1.0)  = 1677
//     which is the near 10 / far 1677 that comment states. unit_pbr_ps.hlsl's viewDepth(),
//     water_ps.hlsl's viewDepth() and debugdepth_ps.hlsl all spell the same inversion as
//     abs(_43 / (_33 + ndcZ)) -- the abs() being there only so the expression survives a
//     left-handed projection, per their own comments. The shadow path is consistent with
//     this rather than a second opinion about it: shadowdepth_vs.hlsl is the shader the
//     camera depth prepass reuses, transforming by the camera's view*projection, so the
//     depth it packs is the very ndcZ (2) and (3) describe.
//
// ALL FOUR AGREE, and nothing in the tree was found saying otherwise. Conclusion:
//
//     viewDist = -z_view,  positive in front of the camera, equal to clip.w.
//
// That is what Slice_Of takes, what Cluster_Bounds's z axis measures, and what
// clustergrid.hlsli's ClusterSliceOf documents. Everything below works in a
// FORWARD-POSITIVE frame -- x right, y up, z = viewDist -- which is left-handed. It is
// derived from the engine's right-handed view space by negating z and nothing else, so
// the lateral axes keep their signs and the projection terms below need no adjustment.
// The frame is only ever used for the sphere/box tests here, where consistency is all
// that matters, and never handed to anything outside this file.

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "W3DDevice/GameClient/W3DClusterGrid.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "WW3D2/camera.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/gfxdevice.h"
#include "WW3D2/ww3d.h"
#include "WWMath/matrix3d.h"
#include "WWMath/matrix4.h"

// clusterassign_cs.hlsl's [numthreads(64,1,1)]. The two have to agree: this is what the
// light count is divided by to get a group count, and a mismatch either leaves the tail of
// the light list unbinned or dispatches threads that do nothing.
static const unsigned CLUSTER_ASSIGN_GROUP_SIZE = 64;

// The compute stage's slots, named where they are used rather than spelled as bare
// numbers at four call sites. t0/u0/u1 are what clusterassign_cs.hlsl declares; the
// compute stage has no texture stages competing for its t registers, which is why these
// start at 0 and the PIXEL stage's buffers start at GFX_FIRST_PIXEL_BUFFER_SLOT.
enum
{
	CLUSTER_CS_LIGHT_BUFFER_SLOT	= 0,	// t0: StructuredBuffer<GpuLight>
	CLUSTER_CS_GRID_SLOT			= 0,	// u0: RWBuffer<uint>, the counts
	CLUSTER_CS_INDEX_SLOT			= 1		// u1: RWBuffer<uint>, the light-index list
};

//-------------------------------------------------------------------------------------------------
// ClusterGridParams -- the addressing. Mirrored in Shaders/clustergrid.hlsli.
//-------------------------------------------------------------------------------------------------

int ClusterGridClass::ClusterGridParams::Slice_Of(float viewDist) const
{
	// The plan's formula with the constant folded:
	//     slice = floor(log(viewDist / zNear) * scale)
	//           = floor(log(viewDist) * scale - scale * log(zNear))
	//           = floor(log(viewDist) * depthScale + depthBias)
	// Clamped rather than rejected at both ends. A fragment marginally in front of the
	// near plane or past the far plane still has to be lit by something, and the end
	// slices are the honest answer; returning -1 would make a caller's array index
	// negative for a case that arises every frame at the horizon.
	if (viewDist < 1.0e-4f)
		viewDist = 1.0e-4f;
	const float s = logf(viewDist) * depthScale + depthBias;
	if (s <= 0.0f)
		return 0;
	const int slice = (int)s;
	return (slice >= (int)sliceCount) ? (int)sliceCount - 1 : slice;
}

float ClusterGridClass::ClusterGridParams::Slice_Near_Distance(unsigned slice) const
{
	// The inverse of Slice_Of at a slice boundary. Not zNear * pow(zFar/zNear, s/N)
	// written out again: that is the same number by a different route, and two routes to
	// one boundary is how the builder and the reader end up disagreeing by a hair at the
	// exact distance a light's sphere touches.
	return expf(((float)slice - depthBias) / depthScale);
}

void ClusterGridClass::ClusterGridParams::Cluster_Bounds(unsigned tileX, unsigned tileY,
	unsigned slice, Vector3 & outMin, Vector3 & outMax) const
{
	// The froxel is a truncated pyramid: its four side planes pass through the eye, so its
	// lateral extent grows linearly with distance and its axis-aligned bounding box is
	// spanned by the eight corners at the two slice faces. Taking the box rather than the
	// pyramid is a small over-estimate at the corners -- and it is *the* predicate, used
	// identically by the scatter and by the brute-force control, so the two still agree
	// exactly. The plan calls this "every cluster's exact AABB" and this is what that is.
	const float zLo = Slice_Near_Distance(slice);
	const float zHi = Slice_Near_Distance(slice + 1);

	// Tile edges, in viewport-local pixels, to NDC, to the view-space ray slopes
	// (x_view / viewDist) that bound the tile:
	//     ndcX  = 2 * px / viewportWidth - 1
	//     ratio = (ndcX + projXOffset) / projXScale
	// The right and bottom tiles are allowed to run past the viewport when its size is not
	// a whole number of tiles; the froxel is then slightly larger than the pixels it owns,
	// which is conservative in the safe direction (a light is binned that could not have
	// been seen, never the reverse).
	const float pxLo = (float)(tileX * tileWidth);
	const float pxHi = (float)((tileX + 1) * tileWidth);
	const float pyLo = (float)(tileY * tileHeight);
	const float pyHi = (float)((tileY + 1) * tileHeight);

	const float ndcXLo = 2.0f * pxLo / viewportWidth - 1.0f;
	const float ndcXHi = 2.0f * pxHi / viewportWidth - 1.0f;
	// Screen y runs down and NDC y runs up, so the tile's top edge is the larger ndcY.
	const float ndcYHi = 1.0f - 2.0f * pyLo / viewportHeight;
	const float ndcYLo = 1.0f - 2.0f * pyHi / viewportHeight;

	const float ratioXLo = (ndcXLo + projXOffset) / projXScale;
	const float ratioXHi = (ndcXHi + projXOffset) / projXScale;
	const float ratioYLo = (ndcYLo + projYOffset) / projYScale;
	const float ratioYHi = (ndcYHi + projYOffset) / projYScale;

	// Each corner is ratio * z. A ratio may be negative (left of / below the axis), so the
	// extreme is not always at the far face -- take the min and max over both faces rather
	// than assuming which one wins.
	const float x0 = ratioXLo * zLo, x1 = ratioXLo * zHi;
	const float x2 = ratioXHi * zLo, x3 = ratioXHi * zHi;
	const float y0 = ratioYLo * zLo, y1 = ratioYLo * zHi;
	const float y2 = ratioYHi * zLo, y3 = ratioYHi * zHi;

	outMin.X = WWMath::Min(WWMath::Min(x0, x1), WWMath::Min(x2, x3));
	outMax.X = WWMath::Max(WWMath::Max(x0, x1), WWMath::Max(x2, x3));
	outMin.Y = WWMath::Min(WWMath::Min(y0, y1), WWMath::Min(y2, y3));
	outMax.Y = WWMath::Max(WWMath::Max(y0, y1), WWMath::Max(y2, y3));
	outMin.Z = zLo;
	outMax.Z = zHi;
}

//-------------------------------------------------------------------------------------------------
// Sizing the grid.
//-------------------------------------------------------------------------------------------------

void ClusterGridClass::Compute_Params(CameraClass & camera, ClusterGridParams & out)
{
	memset(&out, 0, sizeof(out));
	out.tileWidth = (unsigned)CLUSTER_TILE_SIZE;
	out.tileHeight = (unsigned)CLUSTER_TILE_SIZE;
	out.sliceCount = (unsigned)CLUSTER_SLICE_COUNT;

	// The viewport in render-target pixels, recomputed exactly the way CameraClass::Apply
	// computes the D3DVIEWPORT9 it actually sets: a normalized rectangle times the render
	// target resolution, truncated. Not TheTacticalView's own width and height, and not
	// the display's -- those are the numbers the view *asked* for, and the camera is what
	// the projection was built against. A grid measured against the wrong one of the three
	// is uniformly offset from the pixels it claims to describe.
	int rtWidth = 0, rtHeight = 0, rtBits = 0;
	bool windowed = false;
	WW3D::Get_Render_Target_Resolution(rtWidth, rtHeight, rtBits, windowed);
	if (rtWidth <= 0 || rtHeight <= 0)
		return;

	Vector2 vpMin, vpMax;
	camera.Get_Viewport(vpMin, vpMax);
	out.viewportX = (float)(int)(vpMin.X * (float)rtWidth);
	out.viewportY = (float)(int)(vpMin.Y * (float)rtHeight);
	out.viewportWidth = (float)(int)((vpMax.X - vpMin.X) * (float)rtWidth);
	out.viewportHeight = (float)(int)((vpMax.Y - vpMin.Y) * (float)rtHeight);
	if (out.viewportWidth < 1.0f || out.viewportHeight < 1.0f)
		return;

	out.gridX = ((unsigned)out.viewportWidth + out.tileWidth - 1) / out.tileWidth;
	out.gridY = ((unsigned)out.viewportHeight + out.tileHeight - 1) / out.tileHeight;

	camera.Get_Clip_Planes(out.zNear, out.zFar);
	// Positive distances -- see the handedness derivation at the top of this file, point
	// (1). A degenerate pair would make log(zFar/zNear) zero or negative and every slice
	// would collapse onto one; refused rather than clamped, because a camera in that state
	// is not one this grid can describe and drawing an arbitrary grid for it would hide
	// whatever put the camera there.
	if (out.zNear <= 0.0f || out.zFar <= out.zNear)
		return;

	// scale = numSlices / log(zFar / zNear), bias = -scale * log(zNear).
	out.depthScale = (float)out.sliceCount / logf(out.zFar / out.zNear);
	out.depthBias = -out.depthScale * logf(out.zNear);

	// The projection's lateral terms. Read off the matrix rather than rebuilt from a
	// stored FOV and aspect ratio: W3DView drives the camera through Set_View_Plane and
	// Set_Aspect_Ratio and the matrix is the only place those two have actually been
	// combined. Get_Projection_Matrix is the GL-form one, but Get_D3D_Projection_Matrix
	// rewrites only row 2 (the depth row), so rows 0 and 1 are identical in both and
	// either would do.
	const Matrix4x4 & proj = camera.Get_Projection_Matrix();
	out.projXScale = proj[0][0];
	out.projXOffset = proj[0][2];
	out.projYScale = proj[1][1];
	out.projYOffset = proj[1][2];
	// Both scales are 2*near/(right-left) and 2*near/(top-bottom): positive by
	// construction for any frustum with a real extent, and a divisor everywhere below.
	if (fabsf(out.projXScale) < 1.0e-8f || fabsf(out.projYScale) < 1.0e-8f)
		return;

	out.valid = true;
}

//-------------------------------------------------------------------------------------------------
// Construction, buffers.
//-------------------------------------------------------------------------------------------------

// Whether an environment variable is set and non-zero. The same shape W3D_SYNTHETIC_LIGHTS
// and W3D_FORCE_RESET_FRAME already use, and the same reason: a flag only a debugger can
// set is a flag the replay harness cannot, and the harness is the only place this project
// measures anything.
static bool Env_Flag_Set(const char * name)
{
	const char * spec = ::getenv(name);
	return (spec != nullptr && ::atoi(spec) != 0);
}

ClusterGridClass::ClusterGridClass()
	: m_bufferCreateFailed(false)
	, m_allocatedClusters(0)
	// The GPU is the default -- that is what C6 is for -- and W3D_CLUSTER_CPU=1 is the way
	// back to C4's scatter. Read once, here, and not per frame: Ensure_Buffers_Created
	// chooses the buffers' usage flags from it, and GFX_BUFFER_DYNAMIC cannot become
	// GFX_BUFFER_UAV without recreating them.
	, m_useCpuBuilder(Env_Flag_Set("W3D_CLUSTER_CPU"))
	, m_computeShader(0)
	, m_computeLoadFailed(false)
	, m_gridBuffer(nullptr)
	, m_indexBuffer(nullptr)
	, m_counts(nullptr)
	, m_indices(nullptr)
	, m_appendCount(0)
#ifdef RTS_DEBUG
	, m_verifyLights(nullptr)
	, m_verifyLightCount(0)
	, m_censusLights(0)
	, m_censusLightsBinned(0)
	, m_censusDrops(0)
	, m_censusTouched(0)
	, m_censusMaxOccupancy(0)
	, m_censusOverflowed(0)
	, m_censusWholeScreen(0)
	, m_censusOccupancyValid(false)
	, m_censusReadbackArmed(Env_Flag_Set("W3D_CLUSTER_CENSUS"))
#endif
{
	memset(&m_params, 0, sizeof(m_params));
	if (m_useCpuBuilder)
	{
		WWDEBUG_SAY(("ClusterGridClass: W3D_CLUSTER_CPU is set -- the grid will be built on "
			"the CPU (C4's scatter) and uploaded, not dispatched. This is the ORACLE path; "
			"clusterassign_cs will not run this session."));
	}
}

ClusterGridClass::~ClusterGridClass()
{
	// Same lifetime argument as GpuLightListClass's destructor: RTS3DScene is released
	// before WW3D::Shutdown(), so the device is still alive here whenever these exist.
	if (DX8Wrapper::Gfx != nullptr)
	{
		if (m_gridBuffer != nullptr)
			DX8Wrapper::Gfx->Release_Buffer(m_gridBuffer);
		if (m_indexBuffer != nullptr)
			DX8Wrapper::Gfx->Release_Buffer(m_indexBuffer);
		if (m_computeShader != 0)
			DX8Wrapper::Release_Compute_Shader((DWORD)m_computeShader);
	}
	m_gridBuffer = nullptr;
	m_indexBuffer = nullptr;
	m_computeShader = 0;
	delete [] m_counts;
	delete [] m_indices;
	m_counts = nullptr;
	m_indices = nullptr;
}

void ClusterGridClass::Ensure_Buffers_Created()
{
	const unsigned clusters = m_params.Cluster_Count();
	if (clusters == 0)
		return;
	// The grid is re-sized when the viewport (or the slice count, which is a constant)
	// changes -- NOT per frame. Compute_Params runs every frame because it is twenty
	// floating-point operations and having it lag the camera by a frame would put every
	// tile boundary in the wrong place on the frame a resolution change lands; the
	// allocation below is the part that is conditional, and it is the part that costs.
	if (clusters == m_allocatedClusters && m_gridBuffer != nullptr && m_indexBuffer != nullptr)
		return;
	if (m_bufferCreateFailed)
		return;
	if (DX8Wrapper::Gfx == nullptr)
		return;	// no device yet; try again next frame, exactly as the light list does

	if (m_gridBuffer != nullptr)
	{
		DX8Wrapper::Gfx->Release_Buffer(m_gridBuffer);
		m_gridBuffer = nullptr;
	}
	if (m_indexBuffer != nullptr)
	{
		DX8Wrapper::Gfx->Release_Buffer(m_indexBuffer);
		m_indexBuffer = nullptr;
	}
	delete [] m_counts;
	delete [] m_indices;
	m_counts = nullptr;
	m_indices = nullptr;
	m_allocatedClusters = 0;

	// GFX_BUFFER_UINT, not structured, for both: C1 found ClearUnorderedAccessViewUint is
	// only defined against a typed or raw view, so the buffers the GPU producer clears each
	// frame have to be typed R32_UINT -- and they were typed that way from C4 onwards
	// precisely so that C6 would change the producer and NOT the view, which is what makes
	// "the same buffers, filled two ways" a comparison at all.
	//
	// The other bit is the swap C6 made: GFX_BUFFER_UAV where the CPU builder wants
	// GFX_BUFFER_DYNAMIC. A swap and not an addition, because D3D11 has no usage that is
	// both CPU-written every frame and GPU-written, and Create_Structured_Buffer refuses
	// the pair outright rather than picking one (see GfxBufferUsage in gfxdevice.h).
	const unsigned producerUsage = m_useCpuBuilder ? GFX_BUFFER_DYNAMIC : GFX_BUFFER_UAV;
	m_gridBuffer = DX8Wrapper::Gfx->Create_Structured_Buffer(sizeof(unsigned), clusters,
		producerUsage | GFX_BUFFER_UINT);
	m_indexBuffer = DX8Wrapper::Gfx->Create_Structured_Buffer(sizeof(unsigned),
		clusters * (unsigned)CLUSTER_MAX_LIGHTS, producerUsage | GFX_BUFFER_UINT);
	if (m_gridBuffer == nullptr || m_indexBuffer == nullptr)
	{
		m_bufferCreateFailed = true;
		WWDEBUG_SAY(("ClusterGridClass: Create_Structured_Buffer failed for the cluster "
			"grid (%u clusters, %u KB) or its light-index list (%u KB). Clustered lighting "
			"has nothing to fill for the rest of this run; the existing per-object lighting "
			"model is untouched and keeps drawing regardless.",
			clusters, (clusters * 4u) / 1024u,
			(clusters * (unsigned)CLUSTER_MAX_LIGHTS * 4u) / 1024u));
		if (m_gridBuffer != nullptr) { DX8Wrapper::Gfx->Release_Buffer(m_gridBuffer); m_gridBuffer = nullptr; }
		if (m_indexBuffer != nullptr) { DX8Wrapper::Gfx->Release_Buffer(m_indexBuffer); m_indexBuffer = nullptr; }
		return;
	}

	m_counts = new unsigned[clusters];
	m_indices = new unsigned[(size_t)clusters * (size_t)CLUSTER_MAX_LIGHTS];
	// Once, at allocation, and never again per frame. Nothing correct ever reads an entry
	// past its cluster's count, so the tails do not need to be right -- but Upload() copies
	// the whole array every frame, and copying never-written heap is the kind of thing that
	// is harmless until a tool decides it is not. Zero is also a legible value to find in a
	// buffer dump: an index of 0 in a cluster whose count is 0 says "untouched", where
	// 0xCDCDCDCD says nothing.
	memset(m_indices, 0, (size_t)clusters * (size_t)CLUSTER_MAX_LIGHTS * sizeof(unsigned));
	memset(m_counts, 0, clusters * sizeof(unsigned));
	m_allocatedClusters = clusters;

	// The device-side index list gets the same treatment ONCE, here, and never per frame.
	// A GFX_BUFFER_UAV buffer is created with undefined contents, and while no correct
	// reader ever looks past its cluster's count, a dump full of driver leftovers says
	// nothing where a dump full of zeroes says "untouched". Not done per frame for the
	// reason Build() gives: the shader does not clear it either, so a per-frame clear on
	// one side would make the two grids differ in the one place the comparison must ignore
	// -- and it would cost 3 MB of bandwidth a frame to do it.
	if (!m_useCpuBuilder)
		DX8Wrapper::Gfx->Clear_RW_Buffer_UInt(m_indexBuffer, 0);

	WWDEBUG_SAY(("ClusterGridClass: grid sized to %u x %u x %u = %u clusters "
		"(%u x %u px tiles over a %.0f x %.0f viewport, near %.1f far %.1f). Index list "
		"stride %u, %u KB. Producer: %s.",
		m_params.gridX, m_params.gridY, m_params.sliceCount, clusters,
		m_params.tileWidth, m_params.tileHeight,
		m_params.viewportWidth, m_params.viewportHeight, m_params.zNear, m_params.zFar,
		(unsigned)CLUSTER_MAX_LIGHTS,
		(clusters * (unsigned)CLUSTER_MAX_LIGHTS * 4u) / 1024u,
		m_useCpuBuilder ? "CPU scatter (W3D_CLUSTER_CPU)" : "clusterassign_cs dispatch"));
}

// CALLED BEFORE Ensure_Buffers_Created, and the order is load-bearing: this is where
// m_useCpuBuilder can still flip, and the buffers' usage flags are chosen from it. A
// GFX_BUFFER_UAV buffer refuses Map_Buffer's write half outright ("a GPU-written buffer
// has no CPU path in", in the D3D11 backend), so a fall back to the CPU builder AFTER the
// buffers exist would leave Upload() silently doing nothing and every cluster reading as
// empty -- which is indistinguishable from "no lights in range".
void ClusterGridClass::Ensure_Compute_Shader()
{
	if (m_computeShader != 0 || m_computeLoadFailed || m_useCpuBuilder)
		return;
	if (DX8Wrapper::Gfx == nullptr)
		return;	// no device yet; try again next frame, exactly as the buffers do

	DWORD handle = 0;
	if (FAILED(W3DShaderManager::LoadAndCreateD3DShader("shaders\\clusterassign_cs.sm5",
			nullptr, 0, W3DShaderManager::SHADER_STAGE_COMPUTE, &handle)) || handle == 0)
	{
		// Latched and said once. A backend with no compute stage answers 0 from
		// Create_Compute_Shader -- gfxdevice.h says so explicitly, and says the caller
		// "keeps whatever CPU path it has". This is that caller, and C4's builder is that
		// path: the fall back is the arrangement the seam was designed around, not a
		// courtesy.
		m_computeLoadFailed = true;
		m_useCpuBuilder = true;
		WWDEBUG_SAY(("ClusterGridClass: clusterassign_cs.sm5 did not load, or the device "
			"refused it. Falling back to the CPU builder for the rest of this run -- the "
			"grid is still correct, it is just built the slow way, and any measurement of "
			"the dispatch taken from this run is a measurement of the CPU scatter."));
		return;
	}

	m_computeShader = (unsigned)handle;
	WWDEBUG_SAY(("ClusterGridClass: clusterassign_cs.sm5 loaded. The grid is built on the "
		"GPU from here on; W3D_CLUSTER_CPU=1 selects the CPU oracle instead."));
}

//-------------------------------------------------------------------------------------------------
// The scatter.
//-------------------------------------------------------------------------------------------------

// A quarter of a pixel of slack on each edge of the candidate rectangle. NOT a fudge for a
// wrong derivation: the ratio-to-pixel mapping below and Cluster_Bounds's pixel-to-ratio one
// are algebraic inverses evaluated in float, and a sphere whose slab ends exactly on a tile
// boundary can otherwise fall on the far side of that boundary by a last-bit rounding
// difference between the two directions. A quarter pixel is far smaller than any distance a
// binning decision is entitled to care about and far larger than that rounding, and the
// per-cluster predicate throws away anything the slack lets in -- so it widens the CANDIDATE
// set only, never the answer, and the brute-force equality check is unaffected.
static const float CLUSTER_TILE_SLACK_PX = 0.25f;

void ClusterGridClass::Slab_Ratio_Bounds(float slabLo, float slabHi, float zLo, float zHi,
	float & outLo, float & outHi)
{
	// THE INVERSE OF Cluster_Bounds's LATERAL HALF, and that is the only property it has to
	// have. Cluster_Bounds takes a tile's two edge ratios and returns the box spanned by the
	// four corners at the two slice faces, which for edge ratios rLo <= rHi is
	//     boxLo = rLo * (rLo < 0 ? zHi : zLo)
	//     boxHi = rHi * (rHi < 0 ? zLo : zHi)
	// Both are increasing in the ratio, so they invert directly: a box whose upper corner
	// reaches out as far as slabLo has upper edge ratio slabLo / zHi when slabLo is
	// positive and slabLo / zLo when it is negative, and a box whose lower corner starts at
	// or before slabHi has lower edge ratio at most slabHi / zLo or slabHi / zHi. Tiles
	// outside [outLo, outHi] have boxes that cannot touch the slab on this axis at all.
	outLo = (slabLo >= 0.0f) ? (slabLo / zHi) : (slabLo / zLo);
	outHi = (slabHi >= 0.0f) ? (slabHi / zLo) : (slabHi / zHi);
}

bool ClusterGridClass::Sphere_Overlaps_Box(const Vector3 & center, float radius,
	const Vector3 & boxMin, const Vector3 & boxMax)
{
	// Closest point on the box to the centre, squared distance to it, against r^2. The one
	// predicate: the scatter's per-cluster refinement and Verify()'s brute-force gather
	// both call this and nothing else, so a disagreement between them can only come from
	// the *set of clusters offered*, which is the thing worth testing.
	float d2 = 0.0f;
	for (int axis = 0; axis < 3; ++axis)
	{
		const float v = center[axis];
		if (v < boxMin[axis])
		{
			const float d = boxMin[axis] - v;
			d2 += d * d;
		}
		else if (v > boxMax[axis])
		{
			const float d = v - boxMax[axis];
			d2 += d * d;
		}
	}
	return d2 <= radius * radius;
}

// WHY THE CANDIDATE RECTANGLE IS DERIVED FROM THE SPHERE'S SLAB, AND NOT FROM ITS
// PROJECTED SILHOUETTE. This is the fix for a real, measured defect, and the reasoning is
// left here because the wrong answer is the intuitive one.
//
// The predicate this scatter refines with -- Sphere_Overlaps_Box, the same one Verify()'s
// exhaustive gather uses -- tests the sphere against a cluster's AXIS-ALIGNED BOUNDING BOX,
// not against the froxel. Cluster_Bounds says so plainly: the froxel is a truncated pyramid
// and its AABB is spanned by the corners at BOTH slice faces, so the box is laterally wider
// than the froxel by the factor zHi / zLo -- about 1.24 per slice at 24 exponential slices
// over near 10 / far 1825.
//
// The candidate rectangle therefore has to be a superset of the tiles whose BOX the sphere
// reaches. An analytic projected silhouette is not: it is exact for the froxel, which is
// the smaller shape. The two disagree in a band one tile wide all the way around every
// light, and the measured symptom was exactly that -- 13 clusters out of 5760 where the
// scatter found no light and the exhaustive gather found one. One of them, decoded: tile
// (4,6) slice 19, box x[-214.348 .. -143.788] z[616.711 .. 766.119], against a light at
// view (-132.075, -37.027, 767.462) with radius 20.598. The light's slab reaches
// x = -152.673, which is inside the box; but its silhouette ratio range is about
// [-0.199, -0.145] and the tile's edge ratios are [-0.280, -0.233], which do not meet. The
// silhouette was right about the froxel, and the froxel was not what was being tested.
//
// So the candidate set is built from the necessary condition the predicate itself implies:
// the sphere's [centre - radius, centre + radius] slab must overlap the box's extent on
// each axis independently. That is exact for the box, needs no tangent lines, has no
// "unbounded on this axis" case to fall back from (a sphere wrapping around the eye simply
// produces a slab that covers the grid), and is one divide per axis. It is evaluated PER
// SLICE, because the box's lateral extent depends on that slice's two z faces.
//
// Conservative where it is not exact, and deliberately: the slice range is widened until
// its ends are defined by the very Slice_Near_Distance values Cluster_Bounds will use, and
// the tile rectangle carries CLUSTER_TILE_SLACK_PX of slack. The per-cluster predicate is
// what turns the candidate set into the answer, so over-covering costs a few box tests and
// under-covering is an unlit rectangle on a wall.
void ClusterGridClass::Scatter_Light(const GpuLight & light, const Vector3 & viewPos,
	unsigned lightIndex)
{
	// viewPos is already in the forward-positive frame: (x right, y up, z = viewDist).
	const float radius = light.posRange.W;
	const float dist = viewPos.Z;

	// Entirely in front of the grid's first slice face, or entirely past its last. Measured
	// against Slice_Near_Distance and not against zNear / zFar so that the test agrees with
	// the boxes to the last bit -- expf(logf(zNear)) is not obliged to be zNear -- and
	// written as negated comparisons so a NaN rejects the light rather than sailing through.
	const float gridZLo = m_params.Slice_Near_Distance(0);
	const float gridZHi = m_params.Slice_Near_Distance(m_params.sliceCount);
	if (!(dist + radius >= gridZLo))
		return;
	if (!(dist - radius <= gridZHi))
		return;

	// Slice range. Slice_Of gives the answer for the sphere's two z extremes and it is then
	// WIDENED until the boundaries actually bracket them: Slice_Of is the algebraic inverse
	// of Slice_Near_Distance but logf and expf are not each other's exact inverses in float,
	// and the boxes are built from Slice_Near_Distance. Each loop runs at most once in
	// practice and is bounded by the slice count regardless. The distances are NOT clamped to
	// zNear/zFar first: Slice_Of already clamps its own result into the grid, and clamping the
	// distances would compare a slice face against a value the sphere does not actually have.
	const float dLo = dist - radius;
	const float dHi = dist + radius;
	unsigned sliceLo = (unsigned)m_params.Slice_Of(dLo);
	unsigned sliceHi = (unsigned)m_params.Slice_Of(dHi);
	while (sliceLo > 0 && m_params.Slice_Near_Distance(sliceLo) > dLo)
		--sliceLo;
	while (sliceHi + 1 < m_params.sliceCount && m_params.Slice_Near_Distance(sliceHi + 1) < dHi)
		++sliceHi;

	// The sphere's slabs -- the very intervals Sphere_Overlaps_Box compares against the box's,
	// which is what makes the candidate set below a superset of the answer.
	const float slabXLo = viewPos.X - radius;
	const float slabXHi = viewPos.X + radius;
	const float slabYLo = viewPos.Y - radius;
	const float slabYHi = viewPos.Y + radius;

	// The grid's own extent in viewport-local pixels, which is NOT the viewport when its size
	// is not a whole number of tiles: Cluster_Bounds lets the right and bottom tiles run past
	// it (720 rows over 64-pixel tiles gives 12 rows covering 768), and those tiles' boxes
	// extend past it with them. Clamping to the viewport here would drop the last row for a
	// light below the bottom of the screen but still inside the last row's box.
	const float gridRightPx = (float)(m_params.gridX * m_params.tileWidth);
	const float gridBottomPx = (float)(m_params.gridY * m_params.tileHeight);

#ifdef RTS_DEBUG
	bool binnedAnywhere = false;
	bool wholeGrid = false;
#endif

	for (unsigned slice = sliceLo; slice <= sliceHi; ++slice)
	{
		// The same two faces Cluster_Bounds will use for every tile in this slice.
		const float zLo = m_params.Slice_Near_Distance(slice);
		const float zHi = m_params.Slice_Near_Distance(slice + 1);

		float ratioXLo, ratioXHi;
		Slab_Ratio_Bounds(slabXLo, slabXHi, zLo, zHi, ratioXLo, ratioXHi);
		// ratio -> ndc -> viewport-local pixel, the inverse of Cluster_Bounds's first half and
		// deliberately written as its inverse rather than as an independent derivation.
		const float pxLo = (m_params.projXScale * ratioXLo - m_params.projXOffset) * 0.5f
			* m_params.viewportWidth + m_params.viewportWidth * 0.5f - CLUSTER_TILE_SLACK_PX;
		const float pxHi = (m_params.projXScale * ratioXHi - m_params.projXOffset) * 0.5f
			* m_params.viewportWidth + m_params.viewportWidth * 0.5f + CLUSTER_TILE_SLACK_PX;
		// Negated comparisons so that a NaN -- which compares false against everything, and
		// would otherwise sail through both tests -- skips the slice instead of reaching the
		// cast below. `continue` and not `return`: a slice whose boxes the sphere misses
		// laterally says nothing about the next slice, whose boxes are wider.
		if (!(pxHi >= 0.0f) || !(pxLo <= gridRightPx))
			continue;
		// Clamped BEFORE the cast, not after. A light a long way off to one side but still in
		// front of the camera projects to a pixel coordinate in the millions, and
		// float-to-int conversion of a value outside int's range is undefined -- on x86 it
		// produces INT_MIN, which then passes a "is this past the right edge" test and lands as
		// tile 0 rather than as the last tile.
		const float clampedXLo = WWMath::Max(pxLo, 0.0f);
		const float clampedXHi = WWMath::Min(pxHi, gridRightPx);
		const int xLoI = (int)floorf(clampedXLo / (float)m_params.tileWidth);
		const int xHiI = (int)floorf(clampedXHi / (float)m_params.tileWidth);
		const unsigned tileXLo = (xLoI < 0) ? 0u : (unsigned)xLoI;
		const unsigned tileXHi = (xHiI >= (int)m_params.gridX)
			? m_params.gridX - 1 : (unsigned)xHiI;
		if (tileXLo > tileXHi)
			continue;

		float ratioYLo, ratioYHi;
		Slab_Ratio_Bounds(slabYLo, slabYHi, zLo, zHi, ratioYLo, ratioYHi);
		// Screen y runs down and ndc y runs up, so the smaller ratio is the larger pixel row:
		// the two come out swapped relative to x.
		const float pyBottom = m_params.viewportHeight * 0.5f
			- (m_params.projYScale * ratioYLo - m_params.projYOffset) * 0.5f
			* m_params.viewportHeight + CLUSTER_TILE_SLACK_PX;
		const float pyTop = m_params.viewportHeight * 0.5f
			- (m_params.projYScale * ratioYHi - m_params.projYOffset) * 0.5f
			* m_params.viewportHeight - CLUSTER_TILE_SLACK_PX;
		if (!(pyBottom >= 0.0f) || !(pyTop <= gridBottomPx))
			continue;
		const float clampedYTop = WWMath::Max(pyTop, 0.0f);
		const float clampedYBottom = WWMath::Min(pyBottom, gridBottomPx);
		const int yLoI = (int)floorf(clampedYTop / (float)m_params.tileHeight);
		const int yHiI = (int)floorf(clampedYBottom / (float)m_params.tileHeight);
		const unsigned tileYLo = (yLoI < 0) ? 0u : (unsigned)yLoI;
		const unsigned tileYHi = (yHiI >= (int)m_params.gridY)
			? m_params.gridY - 1 : (unsigned)yHiI;
		if (tileYLo > tileYHi)
			continue;

#ifdef RTS_DEBUG
		if (tileXLo == 0 && tileXHi == m_params.gridX - 1
			&& tileYLo == 0 && tileYHi == m_params.gridY - 1)
			wholeGrid = true;
#endif

		// The append. Refined per cluster with the exact sphere/box test rather than filling
		// the whole rectangle: the rectangle is the candidate set, not the answer, and the
		// refinement is what lets the brute-force control below compare for EQUALITY. It is
		// also what C6's shader does inside its own loop -- a compute thread that skipped the
		// refinement would over-bin the corners and the two grids would never match.
		for (unsigned ty = tileYLo; ty <= tileYHi; ++ty)
		{
			for (unsigned tx = tileXLo; tx <= tileXHi; ++tx)
			{
				Vector3 boxMin, boxMax;
				m_params.Cluster_Bounds(tx, ty, slice, boxMin, boxMax);
				if (!Sphere_Overlaps_Box(viewPos, radius, boxMin, boxMax))
					continue;

				const unsigned cluster = m_params.Cluster_Index(tx, ty, slice);
				const unsigned slot = m_counts[cluster];
#ifdef RTS_DEBUG
				// Counted here and not inside the append below: a light that reached a cluster
				// but lost the capacity contest still reached it, and calling that "not binned
				// anywhere" would hide the very case the overflow census is there to expose.
				binnedAnywhere = true;
#endif
				// The count is incremented whether or not the index fits, so it records how many
				// lights REACHED the cluster. Clamping it here would make an overflowing cluster
				// indistinguishable from one holding exactly the stride, and the overflow census
				// -- the number the plan says decides whether 64 is right -- would always read
				// zero.
				++m_counts[cluster];
				if (slot < (unsigned)CLUSTER_MAX_LIGHTS)
				{
					m_indices[(size_t)cluster * (size_t)CLUSTER_MAX_LIGHTS + slot] = lightIndex;
					++m_appendCount;
				}
#ifdef RTS_DEBUG
				else
				{
					++m_censusDrops;
				}
#endif
			}
		}
	}

#ifdef RTS_DEBUG
	if (binnedAnywhere)
		++m_censusLightsBinned;
	if (wholeGrid)
		++m_censusWholeScreen;
#endif
}

void ClusterGridClass::Build(const GpuLight * lights, unsigned lightCount)
{
	const unsigned clusters = m_params.Cluster_Count();
	memset(m_counts, 0, clusters * sizeof(unsigned));
	// The counts are cleared every frame; m_indices is NOT (it was zeroed once, at
	// allocation). Every entry a reader may touch is written before it is read, because
	// the reader iterates min(count, stride) entries and the count is what the writes
	// below produced. Clearing 3 MB a frame to make the unreachable tail look tidy is real
	// cost for no observable difference -- and clusterassign_cs does not clear it either, so
	// clearing here would make the two grids differ in the one place the comparison must
	// ignore. Verify() compares counts and the live prefix of each cluster's list, never
	// the tail.

	m_appendCount = 0;

#ifdef RTS_DEBUG
	// NOT m_verifyLights / m_verifyLightCount: those are set in Update(), because on the
	// GPU path this function does not run on the render path at all and the oracle still
	// has to know what light set the frame was binned from.
	m_censusLights = lightCount;
	m_censusLightsBinned = 0;
	m_censusDrops = 0;
	m_censusWholeScreen = 0;
#endif

	if (lightCount == 0)
		return;

	// World space to the forward-positive frame, once per light. Get_View_Matrix is the
	// camera's inverse transform -- the same matrix CameraClass::Apply hands the device as
	// D3DTS_VIEW -- so this puts the light exactly where the rasteriser will put the
	// geometry it lights.
	for (unsigned i = 0; i < lightCount; ++i)
	{
		const GpuLight & light = lights[i];
		const Vector3 world(light.posRange.X, light.posRange.Y, light.posRange.Z);
		Vector3 view;
		Matrix3D::Transform_Vector(m_viewMatrix, world, &view);
		// Negate z: the engine's view space is right-handed with forward along -Z (see the
		// derivation at the top of this file), and everything below wants the positive
		// distance.
		view.Z = -view.Z;
		Scatter_Light(light, view, i);
	}
}

void ClusterGridClass::Update(const GpuLight * lights, unsigned lightCount,
	GfxBuffer * lightBuffer, CameraClass & camera)
{
	Compute_Params(camera, m_params);
	if (!m_params.valid)
		return;

	// The shader first, because a failure here flips m_useCpuBuilder and the buffers below
	// are created with usage flags chosen from it. See the note on Ensure_Compute_Shader.
	Ensure_Compute_Shader();
	Ensure_Buffers_Created();
	if (m_counts == nullptr || m_indices == nullptr)
		return;

	// The same matrix Write_Frame_Constants published into b1's three ClusterView rows a
	// moment ago, from the same camera in the same frame. Kept here as well as sent there
	// because the oracle needs it on the CPU, and taking it from the camera rather than
	// reading back what was sent is what makes the two independent enough to be worth
	// comparing.
	m_viewMatrix = camera.Get_View_Matrix();

#ifdef RTS_DEBUG
	// What the oracle re-derives its own answer from. Recorded on BOTH paths and in
	// Update() rather than in Build(), because on the GPU path Build() does not run here --
	// Verify() runs it later, off the render path, and by then the light array has to
	// already be known.
	m_verifyLights = lights;
	m_verifyLightCount = lightCount;
	m_censusLights = lightCount;	// the one census figure that is free on both paths
	m_censusOccupancyValid = false;
#endif

	if (m_useCpuBuilder)
	{
		Build(lights, lightCount);
		Upload();
#ifdef RTS_DEBUG
		Collect_Census();
		m_censusOccupancyValid = true;
#endif
	}
	else
	{
		Dispatch_Build(lightBuffer, lightCount);
#ifdef RTS_DEBUG
		// The census figures live in device memory now and reading them is a full pipeline
		// stall. Only when someone asked for it; see Collect_Census_From_Gpu.
		if (m_censusReadbackArmed)
			Collect_Census_From_Gpu();
#endif
	}

#ifdef RTS_DEBUG
	Report_Census();
	Maybe_Run_Verify(lightCount);
#endif
}

//-------------------------------------------------------------------------------------------------
// C6's producer: the dispatch.
//-------------------------------------------------------------------------------------------------

bool ClusterGridClass::Dispatch_Build(GfxBuffer * lightBuffer, unsigned lightCount)
{
	GfxDeviceClass * const gfx = DX8Wrapper::Gfx;
	if (gfx == nullptr || m_computeShader == 0 || m_gridBuffer == nullptr
		|| m_indexBuffer == nullptr)
		return false;

	// THE HAZARD, AND THE REASON THE ORDER IN THIS FUNCTION IS WHAT IT IS. Binding either
	// buffer as an unordered-access view nulls any pixel-stage SRV binding of the same
	// buffer -- C1's rule, enforced in the backend, and exactly what it is for. Two
	// consequences, and both are load-bearing:
	//
	//  - The UAV binds come FIRST, before the clear. ClearUnorderedAccessViewUint against a
	//    resource that is simultaneously bound for reading is not a defined thing to do,
	//    and the two binds below are what guarantee it is not (they take the buffers out of
	//    the pixel stage on the way in). W3DView::draw does unbind them at the end of every
	//    frame, but "the caller unbinds" is not a property this function should depend on.
	//  - The pixel stage must be re-bound AFTERWARDS, by the caller. W3DView::draw calls
	//    bindClusteredLightBuffers() immediately after this returns; if it ever stops doing
	//    so, the grid reads all-zero in every shader, which is indistinguishable from "no
	//    lights near this pixel" and has no other symptom at all.
	//
	// t8 (LightBuffer) is not affected: it goes in below as a compute-stage SRV, and a read
	// and a read do not collide.
	gfx->Set_Compute_RW_Buffer(CLUSTER_CS_GRID_SLOT, m_gridBuffer);
	gfx->Set_Compute_RW_Buffer(CLUSTER_CS_INDEX_SLOT, m_indexBuffer);

	// The counts are cleared EVERY FRAME and the index list is not. That asymmetry is the
	// same one Build() spells out on the CPU side: no correct reader looks past its
	// cluster's count, so the tails do not have to be right -- and a clear the shader does
	// not perform would make the two grids differ in the one place the bit-exact comparison
	// is supposed to ignore, as well as costing 3 MB of bandwidth a frame.
	//
	// Unconditional, rather than only when there are lights: with no lights the correct
	// grid is an all-zero one, and skipping the clear would leave the previous frame's
	// counts standing, which draws lights that are no longer there.
	gfx->Clear_RW_Buffer_UInt(m_gridBuffer, 0);

	if (lightCount > 0 && lightBuffer != nullptr)
	{
		gfx->Set_Compute_Shader((GfxShaderHandle)m_computeShader);
		gfx->Set_Compute_Buffer(CLUSTER_CS_LIGHT_BUFFER_SLOT, lightBuffer);

		// One thread per light, rounded up to whole groups. The tail group runs with
		// threads past the end of the light list and the shader's own bounds test is what
		// makes that safe -- see its comment; the two agree about CLUSTER_ASSIGN_GROUP_SIZE
		// and nothing checks that they do.
		const unsigned groups = (lightCount + CLUSTER_ASSIGN_GROUP_SIZE - 1)
			/ CLUSTER_ASSIGN_GROUP_SIZE;
		gfx->Dispatch(groups, 1, 1);

		gfx->Set_Compute_Buffer(CLUSTER_CS_LIGHT_BUFFER_SLOT, nullptr);
		gfx->Set_Compute_Shader(0);
	}
	// ...otherwise there was nothing to bin, and the cleared grid IS this frame's answer.

	// Off the compute stage either way. A buffer left bound for writing cannot be read from
	// anywhere else -- Verify()'s read-back would fail, and the caller's pixel-stage bind
	// would silently null this binding instead of the other way round, which is a hazard
	// resolution that happens to be correct today and would stop being obvious tomorrow.
	gfx->Set_Compute_RW_Buffer(CLUSTER_CS_GRID_SLOT, nullptr);
	gfx->Set_Compute_RW_Buffer(CLUSTER_CS_INDEX_SLOT, nullptr);
	return true;
}

// THE CPU PATH ONLY (W3D_CLUSTER_CPU=1). The GPU producer writes both buffers in place and
// has nothing to upload; Dispatch_Build is its counterpart.
void ClusterGridClass::Upload()
{
	if (m_gridBuffer == nullptr || m_indexBuffer == nullptr || DX8Wrapper::Gfx == nullptr)
		return;

	const unsigned clusters = m_params.Cluster_Count();

	void * data = nullptr;
	if (DX8Wrapper::Gfx->Map_Buffer(m_gridBuffer, GFX_MAP_WRITE_DISCARD, &data))
	{
		memcpy(data, m_counts, clusters * sizeof(unsigned));
		DX8Wrapper::Gfx->Unmap_Buffer(m_gridBuffer);
	}

	// Skipped entirely when nothing was appended -- which is every frame on every map that
	// ships today, because no map has a point or spot light in it yet. That is correct and
	// not just cheap: with no appends every count above is zero, and no reader ever looks
	// past a count.
	//
	// When it does run it copies the WHOLE list, not the live prefix of each cluster. A
	// GFX_MAP_WRITE_DISCARD hands back fresh driver memory whose contents are undefined, so
	// a partial write would leave the gaps holding whatever was there -- and while no
	// correct reader looks past its cluster's count, Verify()'s comparison would then be
	// comparing garbage in the tails.
	if (m_appendCount == 0)
		return;
	if (DX8Wrapper::Gfx->Map_Buffer(m_indexBuffer, GFX_MAP_WRITE_DISCARD, &data))
	{
		memcpy(data, m_indices, (size_t)clusters * (size_t)CLUSTER_MAX_LIGHTS * sizeof(unsigned));
		DX8Wrapper::Gfx->Unmap_Buffer(m_indexBuffer);
	}
}

//-------------------------------------------------------------------------------------------------
// The census and the positive control.
//-------------------------------------------------------------------------------------------------

#ifdef RTS_DEBUG

void ClusterGridClass::Collect_Census()
{
	const unsigned clusters = m_params.Cluster_Count();
	m_censusTouched = 0;
	m_censusMaxOccupancy = 0;
	m_censusOverflowed = 0;
	for (unsigned i = 0; i < clusters; ++i)
	{
		const unsigned c = m_counts[i];
		if (c == 0)
			continue;
		++m_censusTouched;
		if (c > m_censusMaxOccupancy)
			m_censusMaxOccupancy = c;
		if (c > (unsigned)CLUSTER_MAX_LIGHTS)
			++m_censusOverflowed;
	}
}

// The same figures on the GPU path, recovered from the count buffer.
//
// NOT FREE, unlike Collect_Census: this maps a GPU-written buffer for reading, which the
// backend services with a CopyResource and a Map that stall until everything queued ahead
// of them has finished (gfxdevice.h's Map_Buffer comment says so, and says nothing on the
// render path may call it). This IS on the render path, which is why it only runs when
// W3D_CLUSTER_CENSUS=1 armed it, and why every run with that set has a poisoned
// PHASE_LIGHTCLUSTER bucket.
//
// Three figures are derivable from the counts and three are not. Derivable: clusters
// touched, max occupancy, overflow count -- and, because the stride is a constant, the
// accepted/dropped split too (a cluster holding n contributes min(n, stride) accepted and
// max(n - stride, 0) dropped, which is exactly what the CPU scatter counts one append at a
// time). Not derivable: how many lights landed in at least one cluster, and how many had
// to take the whole viewport. Both of those are facts about the scatter's control flow,
// not about its output, and there is nothing in either buffer that implies them -- so they
// are reported as unavailable rather than invented.
void ClusterGridClass::Collect_Census_From_Gpu()
{
	m_censusTouched = 0;
	m_censusMaxOccupancy = 0;
	m_censusOverflowed = 0;
	m_appendCount = 0;
	m_censusDrops = 0;
	m_censusLightsBinned = 0;	// not derivable from the grid; see above
	m_censusWholeScreen = 0;	// likewise

	if (m_gridBuffer == nullptr || DX8Wrapper::Gfx == nullptr)
		return;

	void * data = nullptr;
	if (!DX8Wrapper::Gfx->Map_Buffer(m_gridBuffer, GFX_MAP_READ, &data) || data == nullptr)
		return;

	const unsigned * const words = (const unsigned *)data;
	const unsigned clusters = m_params.Cluster_Count();
	for (unsigned i = 0; i < clusters; ++i)
	{
		const unsigned c = words[i];
		if (c == 0)
			continue;
		++m_censusTouched;
		if (c > m_censusMaxOccupancy)
			m_censusMaxOccupancy = c;
		if (c > (unsigned)CLUSTER_MAX_LIGHTS)
		{
			++m_censusOverflowed;
			m_appendCount += (unsigned)CLUSTER_MAX_LIGHTS;
			m_censusDrops += c - (unsigned)CLUSTER_MAX_LIGHTS;
		}
		else
		{
			m_appendCount += c;
		}
	}
	DX8Wrapper::Gfx->Unmap_Buffer(m_gridBuffer);
	m_censusOccupancyValid = true;
}

void ClusterGridClass::Report_Census() const
{
	static unsigned frames = 0;
	const unsigned REPORT_INTERVAL = 100;	// the same cadence as GpuLightListClass's and the D3D11 backend's own censuses
	if (++frames < REPORT_INTERVAL)
		return;
	frames = 0;

	const unsigned clusters = m_params.Cluster_Count();
	// Mean over TOUCHED clusters, not over all of them. The grid is mostly empty by
	// construction -- that is what makes clustering worth doing -- so a mean over 12240
	// clusters would read as a fraction of a light no matter how crowded the busy ones
	// get, and it is the busy ones the 64-light stride has to survive.
	const float meanTouched = (m_censusTouched > 0)
		? ((float)(m_appendCount + m_censusDrops) / (float)m_censusTouched) : 0.0f;

	// The occupancy half of this line is free on the CPU path and costs a pipeline stall on
	// the GPU one, so it is only printed when it actually describes this frame. Saying
	// "unavailable" is the whole point: a census that quietly prints zeroes for figures it
	// did not measure is worse than no census, because the number a reader takes away is
	// "no cluster overflowed" and that is a claim nobody made.
	if (!m_censusOccupancyValid)
	{
		WWDEBUG_SAY(("ClusterGridClass CENSUS, most recent frame: grid %u x %u x %u = %u "
			"clusters (%u x %u px tiles over %.0f x %.0f, near %.1f far %.1f). %u lights "
			"offered. Occupancy, overflow and the accepted/dropped split are UNAVAILABLE: "
			"the grid was built by clusterassign_cs and those figures exist only in device "
			"memory. Set W3D_CLUSTER_CENSUS=1 to read them back every %u frames -- which "
			"stalls the pipeline each time and makes the PHASE_LIGHTCLUSTER bucket in the "
			"F10 overlay meaningless for that run -- or W3D_CLUSTER_CPU=1 to build on the "
			"CPU, where they are free.",
			m_params.gridX, m_params.gridY, m_params.sliceCount, clusters,
			m_params.tileWidth, m_params.tileHeight,
			m_params.viewportWidth, m_params.viewportHeight, m_params.zNear, m_params.zFar,
			m_censusLights, REPORT_INTERVAL));
		return;
	}

	WWDEBUG_SAY(("ClusterGridClass CENSUS, most recent frame (%s): grid %u x %u x %u = %u "
		"clusters (%u x %u px tiles over %.0f x %.0f, near %.1f far %.1f). %u lights "
		"offered, %u binned somewhere, %u whole-screen rectangles. %u clusters touched "
		"(%.1f%%), max occupancy %u, mean over touched %.2f. %u (light, cluster) pairs "
		"accepted, %u dropped for exceeding the %u-light stride, %u clusters overflowed. "
		"A nonzero overflow count is the number that says the stride is too small -- it is "
		"a one-line change, but change it on this figure and not on a guess.%s",
		m_useCpuBuilder ? "CPU scatter" : "clusterassign_cs, read back",
		m_params.gridX, m_params.gridY, m_params.sliceCount, clusters,
		m_params.tileWidth, m_params.tileHeight,
		m_params.viewportWidth, m_params.viewportHeight, m_params.zNear, m_params.zFar,
		m_censusLights, m_censusLightsBinned, m_censusWholeScreen,
		m_censusTouched, (clusters > 0) ? (100.0f * (float)m_censusTouched / (float)clusters) : 0.0f,
		m_censusMaxOccupancy, meanTouched,
		m_appendCount, m_censusDrops, (unsigned)CLUSTER_MAX_LIGHTS, m_censusOverflowed,
		m_useCpuBuilder ? ""
			: " ...except \"binned somewhere\" and \"whole-screen rectangles\", which are"
			  " facts about the scatter's control flow rather than about its output and"
			  " read 0 here because nothing on the GPU path can produce them."));
}

void ClusterGridClass::Maybe_Run_Verify(unsigned lightCount)
{
	// W3D_CLUSTER_VERIFY=1 runs the control once, unattended, on the first frame with
	// something to bin. Same env-var-gated shape as W3D_SYNTHETIC_LIGHTS and
	// W3D_FORCE_RESET_FRAME: read once, cached, and armed only for a shell that
	// deliberately exported it -- a build left in the game directory cannot stall somebody
	// else's frame with a quadratic gather and a pipeline read-back.
	static bool parsed = false;
	static bool armed = false;
	static bool done = false;
	if (!parsed)
	{
		parsed = true;
		const char * spec = ::getenv("W3D_CLUSTER_VERIFY");
		armed = (spec != nullptr && ::atoi(spec) != 0);
	}
	if (!armed || done || lightCount == 0)
		return;
	done = true;
	Verify();
}

// Insertion sort, ascending, over at most CLUSTER_MAX_LIGHTS entries. Written out rather
// than pulled from <algorithm> because the two arrays it sorts are stack buffers of at most
// 64 words and this is the whole of what a comparison of two unordered lists needs. See the
// ORDERING note in Verify() for why sorting is involved at all.
static void Sort_Light_Indices(unsigned * values, unsigned count)
{
	for (unsigned i = 1; i < count; ++i)
	{
		const unsigned key = values[i];
		unsigned j = i;
		while (j > 0 && values[j - 1] > key)
		{
			values[j] = values[j - 1];
			--j;
		}
		values[j] = key;
	}
}

bool ClusterGridClass::Verify()
{
	if (!m_params.valid || m_counts == nullptr)
	{
		WWDEBUG_SAY(("ClusterGridClass::Verify: nothing to check -- the grid has not been "
			"built this run (camera or device not ready)."));
		return false;
	}

	const unsigned clusters = m_params.Cluster_Count();

	// THE ORACLE. On the CPU path m_counts and m_indices already hold this frame's answer,
	// because Update() built them. On the GPU path they hold nothing -- Build() does not
	// run on the render path there -- so it is run HERE, off the render path, against the
	// light set Update() recorded and the view matrix it took from the same camera in the
	// same frame. That is what makes the comparison below a comparison of two producers
	// rather than of a producer against itself.
	if (!m_useCpuBuilder)
		Build(m_verifyLights, m_verifyLightCount);

	// The lights, in the forward-positive frame, derived here from C3's world-space array
	// rather than taken from anything the scatter left behind -- so a mistake in the
	// scatter's own transform cannot hide inside the check that is supposed to catch it.
	Vector3 * viewPos = (m_verifyLightCount > 0) ? new Vector3[m_verifyLightCount] : nullptr;
	float * radius = (m_verifyLightCount > 0) ? new float[m_verifyLightCount] : nullptr;
	for (unsigned i = 0; i < m_verifyLightCount; ++i)
	{
		const Vector3 world(m_verifyLights[i].posRange.X, m_verifyLights[i].posRange.Y,
			m_verifyLights[i].posRange.Z);
		Matrix3D::Transform_Vector(m_viewMatrix, world, &viewPos[i]);
		viewPos[i].Z = -viewPos[i].Z;
		radius[i] = m_verifyLights[i].posRange.W;
	}

	// ---- 1. THE BRUTE FORCE ------------------------------------------------------------
	//
	// One loop over every cluster, one over every light, and the exact sphere/box test.
	// No tile rectangle, no slice range, no projection algebra: if the candidate rectangle
	// is derived incorrectly -- as it was, against the froxel rather than against the box
	// the predicate actually uses, until the note above Scatter_Light -- or if the
	// ndc-to-pixel mapping is off by a flip, nothing in this loop shares the mistake. That is the
	// whole reason it exists; the plan's own gate for this stage compares the grid against
	// the CPU builder that produced it, which cannot fail.
	//
	// Deliberately O(clusters x lights): 12240 x 200 is 2.4M box tests, which takes a
	// visible moment and is meant to. This is not a per-frame check.
	unsigned firstMismatch = (unsigned)-1;
	unsigned mismatchScatter = 0, mismatchBrute = 0;
	unsigned mismatches = 0;

	for (unsigned slice = 0; slice < m_params.sliceCount; ++slice)
	{
		for (unsigned ty = 0; ty < m_params.gridY; ++ty)
		{
			for (unsigned tx = 0; tx < m_params.gridX; ++tx)
			{
				Vector3 boxMin, boxMax;
				m_params.Cluster_Bounds(tx, ty, slice, boxMin, boxMax);

				unsigned brute = 0;
				for (unsigned i = 0; i < m_verifyLightCount; ++i)
				{
					if (Sphere_Overlaps_Box(viewPos[i], radius[i], boxMin, boxMax))
						++brute;
				}

				const unsigned cluster = m_params.Cluster_Index(tx, ty, slice);
				if (m_counts[cluster] != brute)
				{
					++mismatches;
					if (firstMismatch == (unsigned)-1)
					{
						firstMismatch = cluster;
						mismatchScatter = m_counts[cluster];
						mismatchBrute = brute;
					}
				}
			}
		}
	}

	// ---- 2. THE COUNTS, CLUSTER BY CLUSTER ----------------------------------------------
	//
	// **THIS IS C6'S ACCEPTANCE TEST**, and on the CPU path it is C4's "was what we built
	// what we uploaded" -- one comparison answering a different question on each path,
	// which is exactly why the CPU builder was kept.
	//
	// Exact, and entitled to be: a count is an integer, and InterlockedAdd produces the
	// same total whatever order the threads arrive in. Nothing about the GPU's
	// nondeterminism can reach this number. Every cluster is compared rather than stopping
	// at the first, because "one cluster differs" and "half the grid differs" are different
	// diagnoses and the count is what tells them apart.
	unsigned * gpuCounts = new unsigned[(clusters > 0) ? clusters : 1];
	memset(gpuCounts, 0, ((clusters > 0) ? clusters : 1) * sizeof(unsigned));
	bool countsRead = false;
	unsigned countMismatches = 0;
	unsigned firstCountWrong = (unsigned)-1;
	unsigned firstCountCpu = 0, firstCountGpu = 0;
	if (m_gridBuffer != nullptr && DX8Wrapper::Gfx != nullptr)
	{
		void * data = nullptr;
		if (DX8Wrapper::Gfx->Map_Buffer(m_gridBuffer, GFX_MAP_READ, &data) && data != nullptr)
		{
			countsRead = true;
			memcpy(gpuCounts, data, clusters * sizeof(unsigned));
			DX8Wrapper::Gfx->Unmap_Buffer(m_gridBuffer);
			for (unsigned i = 0; i < clusters; ++i)
			{
				if (gpuCounts[i] == m_counts[i])
					continue;
				++countMismatches;
				if (firstCountWrong == (unsigned)-1)
				{
					firstCountWrong = i;
					firstCountCpu = m_counts[i];
					firstCountGpu = gpuCounts[i];
				}
			}
		}
	}

	// ---- 3. THE LIGHT INDICES, AS SORTED MULTISETS --------------------------------------
	//
	// ORDERING, AND WHY THIS COMPARISON IS WEAKER THAN THE ONE ABOVE. The CPU builder walks
	// the light array from 0 upwards and appends in that order, so every cluster's list
	// comes out ascending. The compute shader's threads append with InterlockedAdd and
	// arrive in whatever order the hardware schedules them, so its lists are a permutation
	// of the same indices. An ORDERED comparison would therefore fail on a perfectly
	// correct shader, and the two ways to avoid that are to sort before comparing or to
	// make the shader order-deterministic.
	//
	// Making the shader deterministic is possible -- a gather (one thread per cluster
	// looping the lights in index order) produces the CPU's order exactly -- but a gather
	// is the algorithm the plan rejected in section 1.3 for costing 90M sphere/AABB tests
	// against a scatter's handful, and rewriting the producer to be checkable rather than
	// to be right is the wrong trade. So: SORT, and say plainly that this half of the
	// comparison establishes that both producers put the same SET of lights in each
	// cluster, not the same sequence. The counts above are the exact half.
	//
	// AND ONE EXCLUSION. When a cluster's count exceeds the stride, *which* of the lights
	// keep their slots is decided by arrival order and genuinely differs between the two
	// producers. Those clusters are counted and skipped here rather than reported as
	// failures -- their counts are still compared exactly above, which is the figure that
	// matters, and the plan says overflow is a measurement (Report_Census) rather than a
	// bug.
	unsigned indexMismatches = 0;
	unsigned indexCompared = 0;
	unsigned indexOverflowSkipped = 0;
	unsigned firstIndexWrong = (unsigned)-1;
	bool indicesRead = false;
	if (countsRead && m_indexBuffer != nullptr && DX8Wrapper::Gfx != nullptr)
	{
		void * data = nullptr;
		if (DX8Wrapper::Gfx->Map_Buffer(m_indexBuffer, GFX_MAP_READ, &data) && data != nullptr)
		{
			indicesRead = true;
			const unsigned * const gpuIndices = (const unsigned *)data;
			const unsigned stride = (unsigned)CLUSTER_MAX_LIGHTS;
			for (unsigned i = 0; i < clusters; ++i)
			{
				const unsigned count = m_counts[i];
				if (count == 0 || gpuCounts[i] != count)
					continue;	// nothing to compare, or already reported above
				if (count > stride)
				{
					++indexOverflowSkipped;
					continue;
				}
				// The LIVE PREFIX only. The tails past a cluster's count are stale by
				// design on both sides -- zeroed once at allocation and never written since
				// -- and comparing them would be comparing two piles of leftovers.
				unsigned cpuList[CLUSTER_MAX_LIGHTS];
				unsigned gpuList[CLUSTER_MAX_LIGHTS];
				const size_t base = (size_t)i * (size_t)stride;
				memcpy(cpuList, &m_indices[base], count * sizeof(unsigned));
				memcpy(gpuList, &gpuIndices[base], count * sizeof(unsigned));
				Sort_Light_Indices(cpuList, count);
				Sort_Light_Indices(gpuList, count);
				++indexCompared;
				if (memcmp(cpuList, gpuList, count * sizeof(unsigned)) != 0)
				{
					++indexMismatches;
					if (firstIndexWrong == (unsigned)-1)
						firstIndexWrong = i;
				}
			}
			DX8Wrapper::Gfx->Unmap_Buffer(m_indexBuffer);
		}
	}

	const bool passed = (mismatches == 0) && countsRead && (countMismatches == 0)
		&& indicesRead && (indexMismatches == 0);
	WWDEBUG_SAY(("ClusterGridClass::Verify: %s (%s producer). (1) brute force over %u "
		"clusters x %u lights: %u disagreeing cluster(s). (2) per-cluster counts read back "
		"off the GPU: %s, %u disagreeing. (3) light-index prefixes, sorted: %s, %u compared, "
		"%u disagreeing, %u skipped for overflowing the %u-light stride.",
		passed ? "PASS" : "FAIL",
		m_useCpuBuilder ? "CPU (W3D_CLUSTER_CPU)" : "clusterassign_cs",
		clusters, m_verifyLightCount, mismatches,
		countsRead ? "read" : "COULD NOT BE READ", countMismatches,
		indicesRead ? "read" : "COULD NOT BE READ", indexCompared, indexMismatches,
		indexOverflowSkipped, (unsigned)CLUSTER_MAX_LIGHTS));

	if (mismatches != 0)
	{
		WWDEBUG_SAY(("ClusterGridClass::Verify: (1) first disagreement at cluster %u -- the "
			"CPU scatter says %u lights, the exhaustive gather says %u. %s Both use the "
			"identical sphere/box predicate, so the difference is in the SET OF CLUSTERS "
			"the scatter offered: Slab_Ratio_Bounds, the ndc-to-pixel mapping, or the "
			"slice range. It is not the predicate. NOTE this check does not involve the GPU "
			"at all -- it says the ORACLE is wrong, which makes checks (2) and (3) "
			"meaningless until it is fixed.",
			firstMismatch, mismatchScatter, mismatchBrute,
			(mismatchScatter < mismatchBrute)
				? "The scatter found FEWER, so its rectangle or slice range is too small -- "
				  "the failure that shows as an unlit rectangular patch beside a lit one."
				: "The scatter found MORE, which means it appended without the exact test "
				  "or appended the same light twice."));
	}
	// ---- Decode the disagreeing clusters ----------------------------------------------
	//
	// Kept, not temporary. It runs only when the comparison has already failed, so it
	// costs a pass nothing, and a bare "13 clusters disagree" is not actionable: what
	// identified the one real defect this check has caught was a decoded line naming the
	// tile, the slice, the box extents and the light that should have been in it. Without
	// that the froxel-versus-its-AABB cause above would have been a guess.
	if (mismatches != 0)
	{
		for (unsigned i = 0; i < m_verifyLightCount; ++i)
		{
			WWDEBUG_SAY(("ClusterGridClass::Verify DIAG: light %u view (%.3f %.3f %.3f) r %.3f",
				i, viewPos[i].X, viewPos[i].Y, viewPos[i].Z, radius[i]));
		}
		unsigned shown = 0;
		for (unsigned slice = 0; slice < m_params.sliceCount && shown < 14; ++slice)
		{
			for (unsigned ty = 0; ty < m_params.gridY && shown < 14; ++ty)
			{
				for (unsigned tx = 0; tx < m_params.gridX && shown < 14; ++tx)
				{
					Vector3 boxMin, boxMax;
					m_params.Cluster_Bounds(tx, ty, slice, boxMin, boxMax);
					unsigned brute = 0;
					unsigned who = (unsigned)-1;
					for (unsigned i = 0; i < m_verifyLightCount; ++i)
					{
						if (Sphere_Overlaps_Box(viewPos[i], radius[i], boxMin, boxMax))
						{
							++brute;
							if (who == (unsigned)-1)
								who = i;
						}
					}
					const unsigned cluster = m_params.Cluster_Index(tx, ty, slice);
					if (m_counts[cluster] == brute)
						continue;
					++shown;
					WWDEBUG_SAY(("ClusterGridClass::Verify DIAG: cluster %u = tile (%u,%u) "
						"slice %u, box x[%.3f..%.3f] y[%.3f..%.3f] z[%.3f..%.3f]; scatter %u "
						"gather %u; first gathered light %u",
						cluster, tx, ty, slice, boxMin.X, boxMax.X, boxMin.Y, boxMax.Y,
						boxMin.Z, boxMax.Z, m_counts[cluster], brute, who));
				}
			}
		}
	}
	// ---- end cluster decode ------------------------------------------------------------

	if (countsRead && countMismatches != 0)
	{
		// The two shapes this takes are worth separating, because they have different
		// causes and only one of them is a binning bug.
		WWDEBUG_SAY(("ClusterGridClass::Verify: (2) first count disagreement at cluster %u "
			"-- CPU %u, GPU %u, out of %u clusters differing. %s",
			firstCountWrong, firstCountCpu, firstCountGpu, countMismatches,
			(countMismatches == 1)
				? "ONE cluster. That is the shape a floating-point tangency takes: log() and "
				  "exp() are specified to 2^-21 in D3D11 and are not the CPU's logf/expf, "
				  "and sqrt is 1 ULP against the CPU's 0.5, so a light whose sphere touches "
				  "a cluster face to within ~1e-6 relative can land on either side. See the "
				  "note at the top of clusterassign_cs.hlsl. Re-run with a different "
				  "synthetic light seed before treating it as a bug."
				: "MANY clusters. That is not a rounding difference -- it is the shader and "
				  "the C++ disagreeing about something structural. Check in this order: b1's "
				  "ClusterProj and ClusterView rows (is the dispatch reading THIS frame's "
				  "constants, or the previous frame's?), then the slice range, then the tile "
				  "rectangle. All-zero GPU counts with nonzero CPU ones means the dispatch "
				  "never ran or its unordered-access slots were nulled by the SRV/UAV "
				  "hazard."));
	}
	if (!countsRead)
	{
		WWDEBUG_SAY(("ClusterGridClass::Verify: (2) the grid buffer could not be mapped for "
			"reading. Nothing about the grid is being claimed by this run."));
	}
	if (indicesRead && indexMismatches != 0)
	{
		WWDEBUG_SAY(("ClusterGridClass::Verify: (3) first index-list disagreement at cluster "
			"%u, in %u of %u compared. The counts for these clusters AGREE, so both "
			"producers found the same number of lights and disagree about which -- the "
			"comparison is order-insensitive, so this is not the append order. A light index "
			"that is present on one side and absent on the other means the two are indexing "
			"LightBuffer differently: the shader stores SV_DispatchThreadID.x and the C++ "
			"stores the loop counter, and those are the same number only while the light "
			"array and the uploaded buffer hold the same records in the same order.",
			firstIndexWrong, indexMismatches, indexCompared));
	}

	delete [] gpuCounts;
	delete [] viewPos;
	delete [] radius;
	return passed;
}

#endif	// RTS_DEBUG

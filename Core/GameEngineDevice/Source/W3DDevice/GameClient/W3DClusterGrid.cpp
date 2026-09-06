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
// for what this is and the clustered lighting plan's C4 section for why.
//
// VERIFICATION RECIPE (the plan's gate for this stage, plus the independent control the
// plan's own gate is missing):
//   1. Build a debug configuration and run with a light population and the cluster
//      inspector on:
//        set W3D_SYNTHETIC_LIGHTS=200
//        set W3D_CLUSTER_VERIFY=1
//      W3D_DEBUG_VIS=9 selects the occupancy heat map at startup for an unattended run;
//      in a live game F11 cycles onto it (it is the 9th mode, "Cluster occupancy", with
//      "Cluster overflow" behind it).
//   2. W3D_CLUSTER_VERIFY runs Verify() once, on the first frame that has lights to bin,
//      and logs PASS or FAIL through WWDEBUG_SAY. It does two things:
//        - a brute-force O(clusters x lights) gather with no tile rectangle and no slice
//          range anywhere in it, compared cluster for cluster against the scatter;
//        - a GFX_MAP_READ of the uploaded grid buffer, compared against the CPU array.
//      A FAIL names the first disagreeing cluster and both counts.
//   3. The per-100-frame census line (Report_Census) gives clusters touched, max and mean
//      occupancy, overflow count and the grid dimensions in force.
// Never call Verify() from the render path: GFX_MAP_READ stalls the pipeline, and the
// brute force is quadratic by design.
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
#include "WW3D2/camera.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/ww3d.h"
#include "WWMath/matrix3d.h"
#include "WWMath/matrix4.h"

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

ClusterGridClass::ClusterGridClass()
	: m_bufferCreateFailed(false)
	, m_allocatedClusters(0)
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
#endif
{
	memset(&m_params, 0, sizeof(m_params));
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
	}
	m_gridBuffer = nullptr;
	m_indexBuffer = nullptr;
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
	// only defined against a typed or raw view, so the buffers C6 will want to clear on
	// the GPU have to be typed R32_UINT from the start -- otherwise C6 changes the view as
	// well as the producer and the "same buffers, different filler" comparison stops being
	// one. GFX_BUFFER_DYNAMIC because the CPU writes them here; that is the bit C6 swaps
	// for GFX_BUFFER_UAV, and it is a swap and not an addition because D3D11 has no usage
	// that is both (see GfxBufferUsage in gfxdevice.h).
	m_gridBuffer = DX8Wrapper::Gfx->Create_Structured_Buffer(sizeof(unsigned), clusters,
		GFX_BUFFER_DYNAMIC | GFX_BUFFER_UINT);
	m_indexBuffer = DX8Wrapper::Gfx->Create_Structured_Buffer(sizeof(unsigned),
		clusters * (unsigned)CLUSTER_MAX_LIGHTS, GFX_BUFFER_DYNAMIC | GFX_BUFFER_UINT);
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

	WWDEBUG_SAY(("ClusterGridClass: grid sized to %u x %u x %u = %u clusters "
		"(%u x %u px tiles over a %.0f x %.0f viewport, near %.1f far %.1f). Index list "
		"stride %u, %u KB.",
		m_params.gridX, m_params.gridY, m_params.sliceCount, clusters,
		m_params.tileWidth, m_params.tileHeight,
		m_params.viewportWidth, m_params.viewportHeight, m_params.zNear, m_params.zFar,
		(unsigned)CLUSTER_MAX_LIGHTS,
		(clusters * (unsigned)CLUSTER_MAX_LIGHTS * 4u) / 1024u));
}

//-------------------------------------------------------------------------------------------------
// The scatter.
//-------------------------------------------------------------------------------------------------

bool ClusterGridClass::Sphere_Axis_Bounds(float c, float cz, float r, float & outLo, float & outHi)
{
	// The silhouette of a sphere, one lateral axis at a time, in the plane spanned by that
	// axis and the view direction. The sphere's centre is at (c, cz) with cz the positive
	// distance in front of the camera; the two tangent lines from the eye touch it at
	//     T+- = (t/d^2) * R(+-A) * (c, cz),   d^2 = c^2 + cz^2,  t = sqrt(d^2 - r^2),
	//     sin A = r/d,  cos A = t/d
	// and the bound wanted is the ratio (axis / z) at each tangent point, where the common
	// t/d^2 factor cancels:
	//     ratio+ = (c*t - cz*r) / (cz*t + c*r)
	//     ratio- = (c*t + cz*r) / (cz*t - c*r)
	//
	// This is the analytic rectangle, not the projected centre plus a projected radius.
	// The cheap version is wrong for exactly the lights this feature is for: a light close
	// to the camera and well off-axis projects to an ellipse whose extent is nothing like
	// symmetric about its centre, and under-covering the rectangle drops clusters the
	// light really does reach -- which shows up as a rectangular patch of a wall that is
	// unlit while the wall beside it is lit, and reads as a shader bug.
	const float d2 = c * c + cz * cz;
	const float t2 = d2 - r * r;
	if (t2 <= 1.0e-6f)
		return false;	// the eye is inside the sphere on this axis: no finite bound

	const float t = sqrtf(t2);
	const float zPlus = cz * t + c * r;
	const float zMinus = cz * t - c * r;
	// A tangent point at or behind the eye means the silhouette wraps past the view
	// direction on that side and the projection runs to infinity. Refuse the whole
	// rectangle rather than return the half that is finite: half a rectangle silently
	// dropped is the failure this function is written to avoid.
	if (zPlus <= 1.0e-6f || zMinus <= 1.0e-6f)
		return false;

	const float a = (c * t - cz * r) / zPlus;
	const float b = (c * t + cz * r) / zMinus;
	outLo = WWMath::Min(a, b);
	outHi = WWMath::Max(a, b);
	return true;
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

void ClusterGridClass::Scatter_Light(const GpuLight & light, const Vector3 & viewPos,
	unsigned lightIndex)
{
	// viewPos is already in the forward-positive frame: (x right, y up, z = viewDist).
	const float radius = light.posRange.W;
	const float dist = viewPos.Z;

	// Entirely behind the near plane, or entirely past the far plane. Both are exact tests
	// on the sphere, not on its centre.
	if (dist + radius <= m_params.zNear)
		return;
	if (dist - radius >= m_params.zFar)
		return;

	// Slice range. Clamped to the grid rather than rejected: a light straddling the near
	// plane still lights slice 0.
	const float dLo = WWMath::Max(dist - radius, m_params.zNear);
	const float dHi = WWMath::Min(dist + radius, m_params.zFar);
	const unsigned sliceLo = (unsigned)m_params.Slice_Of(dLo);
	const unsigned sliceHi = (unsigned)m_params.Slice_Of(dHi);

	// Tile rectangle. An unbounded axis means the sphere wraps around the eye on that
	// axis, in which case the whole viewport is the honest rectangle -- and the exact
	// per-cluster test below throws away the clusters it does not really touch, so the
	// only cost is the loop.
	float ratioXLo, ratioXHi, ratioYLo, ratioYHi;
	const bool boundedX = Sphere_Axis_Bounds(viewPos.X, dist, radius, ratioXLo, ratioXHi);
	const bool boundedY = Sphere_Axis_Bounds(viewPos.Y, dist, radius, ratioYLo, ratioYHi);
#ifdef RTS_DEBUG
	if (!boundedX || !boundedY)
		++m_censusWholeScreen;
#endif

	unsigned tileXLo = 0, tileXHi = m_params.gridX - 1;
	if (boundedX)
	{
		// ratio -> ndc -> viewport-local pixel -> tile. The inverse of Cluster_Bounds's
		// first half, and deliberately written as its inverse rather than as an
		// independent derivation.
		const float pxLo = (m_params.projXScale * ratioXLo - m_params.projXOffset) * 0.5f
			* m_params.viewportWidth + m_params.viewportWidth * 0.5f;
		const float pxHi = (m_params.projXScale * ratioXHi - m_params.projXOffset) * 0.5f
			* m_params.viewportWidth + m_params.viewportWidth * 0.5f;
		// Written as negated comparisons so that a NaN -- which compares false against
		// everything, and would otherwise sail through both tests -- rejects the light
		// instead of reaching the cast below.
		if (!(pxHi >= 0.0f) || !(pxLo <= m_params.viewportWidth))
			return;	// entirely off the left or right of the viewport
		// Clamped to the viewport BEFORE the cast, not after. A light a long way off to
		// one side but still in front of the camera projects to a pixel coordinate in the
		// millions, and float-to-int conversion of a value outside int's range is
		// undefined -- on x86 it produces INT_MIN, which then passes a "is this past the
		// right edge" test and lands as tile 0 rather than as the last tile.
		const float clampedLo = WWMath::Max(pxLo, 0.0f);
		const float clampedHi = WWMath::Min(pxHi, m_params.viewportWidth);
		const int lo = (int)floorf(clampedLo / (float)m_params.tileWidth);
		const int hi = (int)floorf(clampedHi / (float)m_params.tileWidth);
		tileXLo = (lo < 0) ? 0u : (unsigned)lo;
		tileXHi = (hi >= (int)m_params.gridX) ? m_params.gridX - 1 : (unsigned)hi;
		if (tileXLo > tileXHi)
			return;
	}

	unsigned tileYLo = 0, tileYHi = m_params.gridY - 1;
	if (boundedY)
	{
		// Screen y runs down, ndc y runs up: the larger ratio is the smaller pixel row, so
		// the two come out swapped relative to x.
		const float pyForHi = m_params.viewportHeight * 0.5f
			- (m_params.projYScale * ratioYHi - m_params.projYOffset) * 0.5f * m_params.viewportHeight;
		const float pyForLo = m_params.viewportHeight * 0.5f
			- (m_params.projYScale * ratioYLo - m_params.projYOffset) * 0.5f * m_params.viewportHeight;
		// Same negated form and the same clamp-before-cast as the x axis above.
		if (!(pyForLo >= 0.0f) || !(pyForHi <= m_params.viewportHeight))
			return;	// entirely above or below the viewport
		const float clampedTop = WWMath::Max(pyForHi, 0.0f);
		const float clampedBottom = WWMath::Min(pyForLo, m_params.viewportHeight);
		const int lo = (int)floorf(clampedTop / (float)m_params.tileHeight);
		const int hi = (int)floorf(clampedBottom / (float)m_params.tileHeight);
		tileYLo = (lo < 0) ? 0u : (unsigned)lo;
		tileYHi = (hi >= (int)m_params.gridY) ? m_params.gridY - 1 : (unsigned)hi;
		if (tileYLo > tileYHi)
			return;
	}

#ifdef RTS_DEBUG
	bool binnedAnywhere = false;
#endif

	// The append. Refined per cluster with the exact sphere/box test rather than filling
	// the whole box: the box is the candidate set, not the answer, and the refinement is
	// what lets the brute-force control below compare for EQUALITY. It is also what C6's
	// shader will do inside its own loop -- a compute thread that skipped the refinement
	// would over-bin the corners and the two grids would never match.
	for (unsigned slice = sliceLo; slice <= sliceHi; ++slice)
	{
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
				// Counted here and not inside the append below: a light that reached a
				// cluster but lost the capacity contest still reached it, and calling that
				// "not binned anywhere" would hide the very case the overflow census is
				// there to expose.
				binnedAnywhere = true;
#endif
				// The count is incremented whether or not the index fits, so it records
				// how many lights REACHED the cluster. Clamping it here would make an
				// overflowing cluster indistinguishable from one holding exactly the
				// stride, and the overflow census -- the number the plan says decides
				// whether 64 is right -- would always read zero.
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
	// cost for no observable difference -- and C6's shader will not clear it either, so
	// clearing here would make the two grids differ in the one place a bit-exact
	// comparison must ignore. Verify() compares counts and the live prefix of each
	// cluster's list, never the tail.

	m_appendCount = 0;

#ifdef RTS_DEBUG
	m_verifyLights = lights;
	m_verifyLightCount = lightCount;
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

void ClusterGridClass::Update(const GpuLight * lights, unsigned lightCount, CameraClass & camera)
{
	Compute_Params(camera, m_params);
	if (!m_params.valid)
		return;

	Ensure_Buffers_Created();
	if (m_counts == nullptr || m_indices == nullptr)
		return;

	m_viewMatrix = camera.Get_View_Matrix();

	Build(lights, lightCount);
	Upload();

#ifdef RTS_DEBUG
	Collect_Census();
	Report_Census();
	Maybe_Run_Verify(lightCount);
#endif
}

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
	// correct reader looks past its cluster's count, C6's bit-exact comparison would then
	// be comparing garbage in the tails. C6 deletes this path entirely.
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

	WWDEBUG_SAY(("ClusterGridClass CENSUS, most recent frame: grid %u x %u x %u = %u "
		"clusters (%u x %u px tiles over %.0f x %.0f, near %.1f far %.1f). %u lights "
		"offered, %u binned somewhere, %u whole-screen rectangles. %u clusters touched "
		"(%.1f%%), max occupancy %u, mean over touched %.2f. %u (light, cluster) pairs "
		"accepted, %u dropped for exceeding the %u-light stride, %u clusters overflowed. "
		"A nonzero overflow count is the number that says the stride is too small -- it is "
		"a one-line change, but change it on this figure and not on a guess.",
		m_params.gridX, m_params.gridY, m_params.sliceCount, clusters,
		m_params.tileWidth, m_params.tileHeight,
		m_params.viewportWidth, m_params.viewportHeight, m_params.zNear, m_params.zFar,
		m_censusLights, m_censusLightsBinned, m_censusWholeScreen,
		m_censusTouched, (clusters > 0) ? (100.0f * (float)m_censusTouched / (float)clusters) : 0.0f,
		m_censusMaxOccupancy, meanTouched,
		m_appendCount, m_censusDrops, (unsigned)CLUSTER_MAX_LIGHTS, m_censusOverflowed));
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

bool ClusterGridClass::Verify() const
{
	if (!m_params.valid || m_counts == nullptr)
	{
		WWDEBUG_SAY(("ClusterGridClass::Verify: nothing to check -- the grid has not been "
			"built this run (camera or device not ready)."));
		return false;
	}

	const unsigned clusters = m_params.Cluster_Count();

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
	// No tile rectangle, no slice range, no projection algebra: if Sphere_Axis_Bounds is
	// wrong -- if the analytic silhouette is derived incorrectly, or the ndc-to-pixel
	// mapping is off by a flip -- nothing in this loop shares the mistake. That is the
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

	// ---- 2. THE UPLOAD -----------------------------------------------------------------
	//
	// The other half of the question. A grid that is built correctly and uploaded wrongly
	// looks, from the shader's side, exactly like one that was built wrongly.
	bool uploadOk = false;
	unsigned uploadFirstWrong = (unsigned)-1;
	if (m_gridBuffer != nullptr && DX8Wrapper::Gfx != nullptr)
	{
		void * data = nullptr;
		if (DX8Wrapper::Gfx->Map_Buffer(m_gridBuffer, GFX_MAP_READ, &data) && data != nullptr)
		{
			const unsigned * words = (const unsigned *)data;
			uploadOk = true;
			for (unsigned i = 0; i < clusters; ++i)
			{
				if (words[i] != m_counts[i]) { uploadOk = false; uploadFirstWrong = i; break; }
			}
			DX8Wrapper::Gfx->Unmap_Buffer(m_gridBuffer);
		}
	}

	const bool passed = (mismatches == 0) && uploadOk;
	WWDEBUG_SAY(("ClusterGridClass::Verify: %s -- brute force over %u clusters x %u lights: "
		"%u disagreeing cluster(s). GPU read-back of the grid: %s.",
		passed ? "PASS" : "FAIL", clusters, m_verifyLightCount, mismatches,
		uploadOk ? "matches the CPU array"
			: ((uploadFirstWrong == (unsigned)-1) ? "COULD NOT BE READ" : "DISAGREES")));

	if (mismatches != 0)
	{
		WWDEBUG_SAY(("ClusterGridClass::Verify: first disagreement at cluster %u -- the "
			"scatter says %u lights, the exhaustive gather says %u. %s Both use the "
			"identical sphere/box predicate, so the difference is in the SET OF CLUSTERS "
			"the scatter offered: Sphere_Axis_Bounds, the ndc-to-pixel mapping, or the "
			"slice range. It is not the predicate.",
			firstMismatch, mismatchScatter, mismatchBrute,
			(mismatchScatter < mismatchBrute)
				? "The scatter found FEWER, so its rectangle or slice range is too small -- "
				  "the failure that shows as an unlit rectangular patch beside a lit one."
				: "The scatter found MORE, which means it appended without the exact test "
				  "or appended the same light twice."));
	}
	if (!uploadOk && uploadFirstWrong != (unsigned)-1)
	{
		WWDEBUG_SAY(("ClusterGridClass::Verify: grid buffer differs from the CPU array at "
			"cluster %u (CPU %u). The build is not the thing being questioned here -- "
			"Upload's memcpy, the buffer's element count, or the map mode is.",
			uploadFirstWrong, m_counts[uploadFirstWrong]));
	}

	delete [] viewPos;
	delete [] radius;
	return passed;
}

#endif	// RTS_DEBUG

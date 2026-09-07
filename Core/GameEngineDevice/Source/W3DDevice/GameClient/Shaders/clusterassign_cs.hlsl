// The cluster-grid assignment shader (compute, Shader Model 5).
//
// C6 of the clustered lighting plan, and the *producer* half of the clustered light
// grid. One thread per light: work out the clusters that light's bounding sphere reaches
// and append its index into each of them with InterlockedAdd. Scatter, not gather -- see
// the plan's section 1.3 for why (a gather is 22080 clusters x 4096 lights of sphere/box
// tests; a scatter is one thread per light touching the handful of clusters it covers).
//
// The CONSUMER contract is untouched by this file: the two buffers, their layout, the b1
// addressing and clustergrid.hlsli's readers are exactly what C4 established. That is the
// property that lets C6 be *checked against* C4 instead of merely replacing it.
//
// ---------------------------------------------------------------------------------------
// THIS FILE IS ONE HALF OF A PAIR, AND THE OTHER HALF IS THE ORACLE
// ---------------------------------------------------------------------------------------
//
// The twin is
//     Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DClusterGrid.cpp
// whose Scatter_Light, Sphere_Axis_Bounds, Sphere_Overlaps_Box and
// ClusterGridParams::Cluster_Bounds are this same algorithm on the CPU. It is kept behind
// W3D_CLUSTER_CPU=1, and the acceptance test for this stage is that the two produce the
// SAME GRID for one frame's light set, compared buffer to buffer (ClusterGridClass::Verify,
// W3D_CLUSTER_VERIFY=1).
//
// So the arithmetic below is not merely *equivalent* to the C++ -- it is TRANSCRIBED from
// it, expression for expression, in the same association, with the same constants and the
// same comparison directions. That is deliberate and it is the whole reason the comparison
// is worth running: a differently-associated floating-point expression agrees with its
// source almost everywhere and disagrees in the last bit on exactly the boundary cases a
// binning algorithm spends its time at. If you change one side of this pair, change the
// other in the same commit.
//
// `precise` on the decisive locals is part of that transcription and not decoration. fxc
// is otherwise free to contract a*b + c into a single mad, and a mad on this hardware may
// round once where the CPU's separate multiply and add round twice. Marking the values
// that decide a discrete outcome -- a tile index, a slice index, whether a sphere reaches a
// box -- keeps the shader's rounding where the C++'s is.
//
// WHAT STILL CANNOT BE MADE BIT-EXACT, stated because the acceptance test has to be read
// with it in hand: log() and exp() are specified to 2^-21 relative accuracy in D3D11 and
// the CPU's logf/expf are not the same implementation, and sqrt is 1 ULP here against the
// CPU's correctly-rounded 0.5. Those feed the slice boundaries and the tile rectangle. A
// disagreement therefore needs a light whose sphere is tangent to a cluster face to within
// about 1e-6 relative -- measure-zero, but not impossible. Verify() reports the first
// disagreeing cluster with both counts so that a single-cluster difference at a tangency
// can be recognised for what it is rather than mistaken for a binning bug.
//
// ---------------------------------------------------------------------------------------
// WHAT THIS SHADER DOES NOT DO
// ---------------------------------------------------------------------------------------
//
// It does not clear the light-index list, and must not. Those tails are stale by design:
// no correct reader looks past its cluster's count (clustergrid.hlsli says so where the
// readers are), C4's CPU builder zeroes them once at allocation and never per frame, and a
// per-frame clear here would make the two grids differ in the one place the comparison is
// supposed to ignore -- as well as costing 3 MB of bandwidth a frame for nothing. The COUNT
// buffer is a different matter and the CPU side clears it every frame, with
// Clear_RW_Buffer_UInt, before this runs.
//
// It does not clamp the stored count either. InterlockedAdd returns the pre-add value, so
// adding first and writing the index only when that value is below the stride gives the
// unclamped "how many lights reached this cluster" for free -- which is what makes an
// overflow visible at all. A count clamped at 64 is indistinguishable from a cluster that
// legitimately holds exactly 64.

#include "shadermodel.hlsli"
#include "clustergrid.hlsli"
#include "gpulight.hlsli"

// ---------------------------------------------------------------------------------------
// The bindings.
//
// t0 on the compute stage, not t8: the pixel stage's buffer slots start above the eight
// texture stages (GFX_FIRST_PIXEL_BUFFER_SLOT) because Set_Texture owns those, and the
// compute stage has no texture stages competing for anything. See the note on
// Set_Compute_Buffer in WW3D2/gfxdevice.h.
//
// RWBuffer<uint> and not RWStructuredBuffer<uint> for both, and that is load-bearing:
// ClearUnorderedAccessViewUint is only defined against a typed or raw view, so the count
// buffer has to be a typed R32_UINT one for its per-frame clear to happen on the GPU at
// all. C4 created both buffers with GFX_BUFFER_UINT from the start precisely so that C6
// would swap the *producer* and not the view. (C6 does swap GFX_BUFFER_DYNAMIC for
// GFX_BUFFER_UAV -- D3D11 has no usage that is both CPU-written every frame and
// GPU-written, so that one is a swap and not an addition.)
// ---------------------------------------------------------------------------------------
StructuredBuffer<GpuLight> LightBuffer       : register(t0);
RWBuffer<uint>             ClusterCountsRW   : register(u0);
RWBuffer<uint>             LightIndexListRW  : register(u1);

// The b1 fields only the builder needs, named. ClusterProj and the three view rows were
// added for this stage -- every other reader of b1 turns a pixel position into a cluster
// and needs neither. See frameconstants.hlsli.
#define CLUSTER_PROJ_X_SCALE    (ClusterProj.x)
#define CLUSTER_PROJ_X_OFFSET   (ClusterProj.y)
#define CLUSTER_PROJ_Y_SCALE    (ClusterProj.z)
#define CLUSTER_PROJ_Y_OFFSET   (ClusterProj.w)
#define CLUSTER_Z_NEAR          (ClusterDepth.z)
#define CLUSTER_Z_FAR           (ClusterDepth.w)
#define CLUSTER_LIGHT_COUNT     (CameraForward.w)

// ---------------------------------------------------------------------------------------
// Slice arithmetic. ClusterGridParams::Slice_Of and ::Slice_Near_Distance.
//
// Spelled out here rather than reusing clustergrid.hlsli's ClusterSliceOf, even though the
// two agree for every finite input: the reader's clamp() form is the right shape for a
// pixel shader, and this one is the C++ line for line, which is what the bit-exact
// comparison is entitled to. The two are checked against each other by inspection and
// nothing else, so having the builder read the *builder's* form keeps that one comparison
// out of the acceptance test.
//
// viewDist is the POSITIVE distance in front of the camera, equal to clip.w. The long
// derivation of that is at the top of W3DClusterGrid.cpp and it is not an assumption:
// this engine's view space is right-handed with forward along -Z, and everything below
// works in the forward-positive frame (x right, y up, z = viewDist).
// ---------------------------------------------------------------------------------------
int AssignSliceOf(float viewDist)
{
    if (viewDist < 1.0e-4)
        viewDist = 1.0e-4;
    precise float s = log(viewDist) * ClusterDepth.x + ClusterDepth.y;
    if (s <= 0.0)
        return 0;
    int slice = (int)s;
    return (slice >= (int)CLUSTER_SLICE_COUNT) ? (int)CLUSTER_SLICE_COUNT - 1 : slice;
}

float AssignSliceNearDistance(int slice)
{
    return exp(((float)slice - ClusterDepth.y) / ClusterDepth.x);
}

// ---------------------------------------------------------------------------------------
// One cluster's view-space axis-aligned box. ClusterGridParams::Cluster_Bounds.
//
// The froxel is a truncated pyramid whose four side planes pass through the eye, so its
// lateral extent grows linearly with distance and its bounding box is spanned by the eight
// corners at the two slice faces. A ratio may be negative (left of / below the axis), so
// the extreme is not always at the far face -- hence the min/max over both.
// ---------------------------------------------------------------------------------------
void AssignClusterBounds(int tileX, int tileY, int slice, out precise float3 boxMin,
                         out precise float3 boxMax)
{
    precise float zLo = AssignSliceNearDistance(slice);
    precise float zHi = AssignSliceNearDistance(slice + 1);

    // The right and bottom tiles are allowed to run past the viewport when its size is not
    // a whole number of tiles; the froxel is then slightly larger than the pixels it owns,
    // which is conservative in the safe direction.
    precise float pxLo = (float)(tileX * (int)CLUSTER_TILE_SIZE.x);
    precise float pxHi = (float)((tileX + 1) * (int)CLUSTER_TILE_SIZE.x);
    precise float pyLo = (float)(tileY * (int)CLUSTER_TILE_SIZE.y);
    precise float pyHi = (float)((tileY + 1) * (int)CLUSTER_TILE_SIZE.y);

    precise float ndcXLo = 2.0 * pxLo / CLUSTER_VIEWPORT_SIZE.x - 1.0;
    precise float ndcXHi = 2.0 * pxHi / CLUSTER_VIEWPORT_SIZE.x - 1.0;
    // Screen y runs down and NDC y runs up, so the tile's top edge is the larger ndcY.
    precise float ndcYHi = 1.0 - 2.0 * pyLo / CLUSTER_VIEWPORT_SIZE.y;
    precise float ndcYLo = 1.0 - 2.0 * pyHi / CLUSTER_VIEWPORT_SIZE.y;

    precise float ratioXLo = (ndcXLo + CLUSTER_PROJ_X_OFFSET) / CLUSTER_PROJ_X_SCALE;
    precise float ratioXHi = (ndcXHi + CLUSTER_PROJ_X_OFFSET) / CLUSTER_PROJ_X_SCALE;
    precise float ratioYLo = (ndcYLo + CLUSTER_PROJ_Y_OFFSET) / CLUSTER_PROJ_Y_SCALE;
    precise float ratioYHi = (ndcYHi + CLUSTER_PROJ_Y_OFFSET) / CLUSTER_PROJ_Y_SCALE;

    precise float x0 = ratioXLo * zLo;
    precise float x1 = ratioXLo * zHi;
    precise float x2 = ratioXHi * zLo;
    precise float x3 = ratioXHi * zHi;
    precise float y0 = ratioYLo * zLo;
    precise float y1 = ratioYLo * zHi;
    precise float y2 = ratioYHi * zLo;
    precise float y3 = ratioYHi * zHi;

    boxMin.x = min(min(x0, x1), min(x2, x3));
    boxMax.x = max(max(x0, x1), max(x2, x3));
    boxMin.y = min(min(y0, y1), min(y2, y3));
    boxMax.y = max(max(y0, y1), max(y2, y3));
    boxMin.z = zLo;
    boxMax.z = zHi;
}

// ---------------------------------------------------------------------------------------
// The one predicate. ClusterGridClass::Sphere_Overlaps_Box.
//
// Closest point on the box to the centre, squared distance to it, against r^2. Written as
// three separate axis terms accumulated in order rather than as a vectorised
// length2(max(0, ...)) -- the C++ starts d2 at zero and does d2 += d*d for axis 0, then 1,
// then 2, and a horizontal add in a different order is a different sum in the last bit.
// ---------------------------------------------------------------------------------------
float AssignAxisDistanceSq(float v, float lo, float hi)
{
    if (v < lo)
    {
        precise float d = lo - v;
        return d * d;
    }
    if (v > hi)
    {
        precise float d = v - hi;
        return d * d;
    }
    return 0.0;
}

bool AssignSphereOverlapsBox(float3 center, float radius, float3 boxMin, float3 boxMax)
{
    precise float d2 = 0.0;
    d2 = d2 + AssignAxisDistanceSq(center.x, boxMin.x, boxMax.x);
    d2 = d2 + AssignAxisDistanceSq(center.y, boxMin.y, boxMax.y);
    d2 = d2 + AssignAxisDistanceSq(center.z, boxMin.z, boxMax.z);
    return d2 <= radius * radius;
}

// ---------------------------------------------------------------------------------------
// The projected silhouette of a sphere on one lateral axis.
// ClusterGridClass::Sphere_Axis_Bounds.
//
// THE ANALYTIC RECTANGLE, not the projected centre plus a projected radius. The cheap
// version under-covers for exactly the lights this feature exists for: a light close to
// the camera and well off-axis projects to an ellipse whose extent is nothing like
// symmetric about its centre, and a rectangle that is too small drops clusters the light
// really does reach -- which shows as an unlit rectangular patch of wall beside a lit one
// and reads as a shader bug. See the derivation in the C++ twin.
//
// Returns false for "unbounded on this axis": the eye is inside the sphere's slab, or a
// tangent point falls at or behind the eye, and the projection runs to infinity. The
// caller then takes the whole viewport -- refusing half a rectangle is the failure this
// function is written to avoid.
// ---------------------------------------------------------------------------------------
bool AssignSphereAxisBounds(float c, float cz, float r, out precise float outLo,
                            out precise float outHi)
{
    outLo = 0.0;
    outHi = 0.0;

    precise float d2 = c * c + cz * cz;
    precise float t2 = d2 - r * r;
    if (t2 <= 1.0e-6)
        return false;

    precise float t = sqrt(t2);
    precise float zPlus = cz * t + c * r;
    precise float zMinus = cz * t - c * r;
    if (zPlus <= 1.0e-6 || zMinus <= 1.0e-6)
        return false;

    precise float a = (c * t - cz * r) / zPlus;
    precise float b = (c * t + cz * r) / zMinus;
    outLo = min(a, b);
    outHi = max(a, b);
    return true;
}

// ---------------------------------------------------------------------------------------
// One thread per light.
//
// 64 is one wavefront on AMD and two warps on NVIDIA, the same group size selftest_cs uses
// and the same number ClusterGridClass::Dispatch_Build divides the light count by. The two
// have to agree; the CPU side rounds up, so the tail group runs with threads past the end
// and the bounds test below is what makes that safe.
//
// A thread's whole cost is proportional to the clusters its own light covers, which for the
// unit-scale lights this plan targets (radius 20-40 world units at RTS camera distance) is
// single-digit tiles across one or two slices. Divergence within a group is therefore real
// but bounded, and a light that wraps around the eye -- the whole-viewport case -- is the
// pathological one. It is left in rather than clamped: the exact per-cluster test below
// throws away the clusters it does not really touch, so the only cost is the loop, and
// dropping it would silently unlight everything near the camera.
// ---------------------------------------------------------------------------------------
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    // Zero before the b1 block has ever been written, which is every frame before the
    // first Update() and every frame with a degenerate camera. A slice count of zero would
    // make the slice arithmetic divide the range into nothing.
    if (!ClusterGridValid())
        return;

    const uint lightIndex = id.x;
    if (lightIndex >= (uint)CLUSTER_LIGHT_COUNT)
        return;

    const GpuLight light = LightBuffer[lightIndex];

    // World space to the forward-positive frame. The three rows are the camera's inverse
    // transform -- CameraClass::Get_View_Matrix, the same Matrix3D the device is handed as
    // the view transform -- published in b1 by GpuLightListClass::Write_Frame_Constants.
    //
    // Written out term by term in the C++'s own order rather than as a dot product or a
    // mul(): Matrix3D::Transform_Vector evaluates (((a*x + b*y) + c*z) + d) and a dp4 is
    // free to sum those four products in any order it likes.
    const float3 world = light.posRange.xyz;
    precise float viewX = ClusterView0.x * world.x + ClusterView0.y * world.y
                        + ClusterView0.z * world.z + ClusterView0.w;
    precise float viewY = ClusterView1.x * world.x + ClusterView1.y * world.y
                        + ClusterView1.z * world.z + ClusterView1.w;
    precise float viewZSigned = ClusterView2.x * world.x + ClusterView2.y * world.y
                              + ClusterView2.z * world.z + ClusterView2.w;
    // Negate z, and nothing else: the engine's view space is right-handed with forward
    // along -Z, so the signed view z of anything visible is negative and its negation is
    // the positive distance every expression below wants. The lateral axes keep their
    // signs, which is why the projection terms need no adjustment.
    const float3 viewPos = float3(viewX, viewY, -viewZSigned);

    const float radius = light.posRange.w;
    const float dist = viewPos.z;

    // Entirely behind the near plane, or entirely past the far plane. Both are exact tests
    // on the sphere, not on its centre.
    if (dist + radius <= CLUSTER_Z_NEAR)
        return;
    if (dist - radius >= CLUSTER_Z_FAR)
        return;

    // Slice range. Clamped to the grid rather than rejected: a light straddling the near
    // plane still lights slice 0.
    precise float dLo = max(dist - radius, CLUSTER_Z_NEAR);
    precise float dHi = min(dist + radius, CLUSTER_Z_FAR);
    const int sliceLo = AssignSliceOf(dLo);
    const int sliceHi = AssignSliceOf(dHi);

    const int gridX = (int)CLUSTER_GRID_X;
    const int gridY = (int)CLUSTER_GRID_Y;

    // Tile rectangle. An unbounded axis means the sphere wraps around the eye on that
    // axis, in which case the whole viewport is the honest rectangle.
    precise float ratioXLo, ratioXHi, ratioYLo, ratioYHi;
    const bool boundedX = AssignSphereAxisBounds(viewPos.x, dist, radius, ratioXLo, ratioXHi);
    const bool boundedY = AssignSphereAxisBounds(viewPos.y, dist, radius, ratioYLo, ratioYHi);

    int tileXLo = 0;
    int tileXHi = gridX - 1;
    if (boundedX)
    {
        // ratio -> ndc -> viewport-local pixel -> tile. The inverse of AssignClusterBounds's
        // first half, and deliberately written as its inverse rather than as an independent
        // derivation.
        precise float pxLo = (CLUSTER_PROJ_X_SCALE * ratioXLo - CLUSTER_PROJ_X_OFFSET) * 0.5
            * CLUSTER_VIEWPORT_SIZE.x + CLUSTER_VIEWPORT_SIZE.x * 0.5;
        precise float pxHi = (CLUSTER_PROJ_X_SCALE * ratioXHi - CLUSTER_PROJ_X_OFFSET) * 0.5
            * CLUSTER_VIEWPORT_SIZE.x + CLUSTER_VIEWPORT_SIZE.x * 0.5;
        // Negated comparisons, exactly as the C++ has them, so that a NaN -- which compares
        // false against everything and would otherwise sail through both tests -- rejects
        // the light instead of reaching the cast below.
        if (!(pxHi >= 0.0) || !(pxLo <= CLUSTER_VIEWPORT_SIZE.x))
            return;
        // Clamped to the viewport BEFORE the cast. A light far off to one side but still in
        // front of the camera projects to a pixel coordinate in the millions, and the
        // float-to-int conversion of a value outside int's range is not defined.
        precise float clampedLo = max(pxLo, 0.0);
        precise float clampedHi = min(pxHi, CLUSTER_VIEWPORT_SIZE.x);
        const int lo = (int)floor(clampedLo / CLUSTER_TILE_SIZE.x);
        const int hi = (int)floor(clampedHi / CLUSTER_TILE_SIZE.x);
        tileXLo = (lo < 0) ? 0 : lo;
        tileXHi = (hi >= gridX) ? gridX - 1 : hi;
        if (tileXLo > tileXHi)
            return;
    }

    int tileYLo = 0;
    int tileYHi = gridY - 1;
    if (boundedY)
    {
        // Screen y runs down, ndc y runs up: the larger ratio is the smaller pixel row, so
        // the two come out swapped relative to x.
        precise float pyForHi = CLUSTER_VIEWPORT_SIZE.y * 0.5
            - (CLUSTER_PROJ_Y_SCALE * ratioYHi - CLUSTER_PROJ_Y_OFFSET) * 0.5 * CLUSTER_VIEWPORT_SIZE.y;
        precise float pyForLo = CLUSTER_VIEWPORT_SIZE.y * 0.5
            - (CLUSTER_PROJ_Y_SCALE * ratioYLo - CLUSTER_PROJ_Y_OFFSET) * 0.5 * CLUSTER_VIEWPORT_SIZE.y;
        if (!(pyForLo >= 0.0) || !(pyForHi <= CLUSTER_VIEWPORT_SIZE.y))
            return;
        precise float clampedTop = max(pyForHi, 0.0);
        precise float clampedBottom = min(pyForLo, CLUSTER_VIEWPORT_SIZE.y);
        const int lo = (int)floor(clampedTop / CLUSTER_TILE_SIZE.y);
        const int hi = (int)floor(clampedBottom / CLUSTER_TILE_SIZE.y);
        tileYLo = (lo < 0) ? 0 : lo;
        tileYHi = (hi >= gridY) ? gridY - 1 : hi;
        if (tileYLo > tileYHi)
            return;
    }

    // The append. Refined per cluster with the exact sphere/box test rather than filling
    // the whole box: the box is the CANDIDATE SET, not the answer. Without this refinement
    // the corners of every light's box would be over-binned, and the bit-exact comparison
    // against the CPU builder -- which does refine -- could never pass. The plan's own
    // wording is "the exact sphere-vs-cluster-AABB test per candidate cluster".
    const uint stride = (uint)CLUSTER_STRIDE;
    for (int slice = sliceLo; slice <= sliceHi; ++slice)
    {
        for (int ty = tileYLo; ty <= tileYHi; ++ty)
        {
            for (int tx = tileXLo; tx <= tileXHi; ++tx)
            {
                precise float3 boxMin;
                precise float3 boxMax;
                AssignClusterBounds(tx, ty, slice, boxMin, boxMax);
                if (!AssignSphereOverlapsBox(viewPos, radius, boxMin, boxMax))
                    continue;

                const uint cluster = ClusterIndex(int2(tx, ty), slice);
                // Add first, write second. InterlockedAdd hands back the value the slot
                // held BEFORE the add, which is exactly the slot this light would take --
                // so the count comes out unclamped (how many lights reached the cluster)
                // and the index list keeps the first `stride` of them, with no second
                // atomic and no clamp anywhere. WHICH `stride` of them, when a cluster
                // overflows, is decided by the order the threads happen to arrive in and is
                // therefore not reproducible against the CPU builder; see Verify().
                uint slot;
                InterlockedAdd(ClusterCountsRW[cluster], 1u, slot);
                if (slot < stride)
                    LightIndexListRW[ClusterLightListBase(cluster) + slot] = lightIndex;
            }
        }
    }
}

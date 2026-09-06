// Cluster inspector (pixel, Shader Model 5).
//
// C8 of the clustered lighting plan, written during C4 rather than after it, because
// it is the tool C4 is verified with. Two modes, selected by DebugClusterCtl.x:
//
//   0  DEBUG_VIS_CLUSTERS         per-cluster light count, as a heat map
//   1  DEBUG_VIS_CLUSTER_OVERFLOW which clusters exceeded the 64-light index stride
//
// THIS IS THE FIRST THING IN THE PROJECT TO READ A SHADER BUFFER IN A PIXEL SHADER, and
// that is most of its value: C5 has to do exactly this from every lit material shader, and
// having the binding path, the register numbers and the addressing already working -- and
// already checked against a CPU reference -- takes one whole class of failure off C5's
// plate. The three buffers bind at absolute t8, t9 and t10 through Set_Pixel_Buffer
// (GFX_FIRST_PIXEL_BUFFER_SLOT, WW3D2/gfxdevice.h), above the eight texture stages, and
// take NO sampler: they are declared plainly below, not through DECLARE_SAMPLER, which
// pairs a Texture2D with a SamplerState at one slot and would not compile against a
// Buffer<uint> anyway.
//
// NO PER-PIXEL DEPTH. Picking one cluster per pixel would need that pixel's view distance,
// and the only screen-sized depth this engine keeps is the SSR prepass, which exists only
// when screen-space reflections are switched on -- an inspector that goes blank with an
// unrelated feature is not an inspector. So each pixel reduces over all 24 slices of its
// own tile column instead: the heat map shows the BUSIEST slice in the column and the
// overflow mask shows whether ANY slice in it overflowed. That is a weaker picture than a
// per-fragment one and it is the honest one; it still tests every slice index, every tile
// index and the whole addressing chain, which is what it is here to do.

#include "shadermodel.hlsli"
#include "gpulight.hlsli"
#include "clustergrid.hlsli"

// The three clustered buffers, at the slots the plan assigns them. LightBuffer and
// LightIndexList are not needed to draw a count -- they are read by the consistency probe
// below, which is the part that catches the failure C1's hazard note warns about.
StructuredBuffer<GpuLight> LightBuffer    : register(t8);
Buffer<uint>               ClusterGrid    : register(t9);
Buffer<uint>               LightIndexList : register(t10);

// x = mode (0 occupancy, 1 overflow). y, z, w reserved.
float4 DebugClusterCtl : register(c0);

// A colour ramp over the count, on a log scale with the 64-light index stride as its
// ceiling. Linear over 0..64 was the first attempt and it is useless in practice: the
// interesting range for a scene with a few hundred lights is one to five lights per
// cluster, all of which land in the bottom twelfth of a linear ramp and read as the same
// near-black. The ceiling is the stride and not the frame's own maximum on purpose -- a
// ramp that renormalises per frame makes two frames incomparable, which is the one thing
// this project's verification discipline will not have.
float3 OccupancyColor(uint count)
{
    float t = log2((float)count + 1.0) / log2(CLUSTER_STRIDE + 1.0);
    t = saturate(t);
    if (t < 0.25)
        return lerp(float3(0.05, 0.10, 0.65), float3(0.10, 0.85, 0.95), t / 0.25);
    if (t < 0.50)
        return lerp(float3(0.10, 0.85, 0.95), float3(0.20, 0.90, 0.25), (t - 0.25) / 0.25);
    if (t < 0.75)
        return lerp(float3(0.20, 0.90, 0.25), float3(1.00, 0.85, 0.10), (t - 0.50) / 0.25);
    return lerp(float3(1.00, 0.85, 0.10), float3(1.00, 1.00, 1.00), (t - 0.75) / 0.25);
}

float4 main(PS_INPUT_POSITION_PARAM PS_INPUT_UNUSED_COLOR_PARAM float2 uv : TEXCOORD0) : PS_TARGET
{
    // The grid has never been built. Flagged rather than drawn: an all-transparent overlay
    // is indistinguishable from the mode not being reached at all, and this whole file is
    // an argument from what is and is not on screen.
    if (!ClusterGridValid())
        return float4(0.35, 0.0, 0.45, 0.85);

    float2 pixel = psInputPosition.xy;
    if (!ClusterPixelInGrid(pixel))
        return float4(0.0, 0.0, 0.0, 0.0);

    int2 tile = ClusterTileOf(pixel);

    // The tile lattice, drawn always and in every mode.
    //
    // It is the tell that says the inspector is running. Without it, "no lights anywhere"
    // and "this shader never reached the screen" produce the same empty viewport, and
    // every other mode in this family has already had to learn that lesson the expensive
    // way (see the "said once" notes in W3DShaderManager::drawDebugVisOverlay). It also
    // makes the tile size and the viewport origin visible directly, which is exactly what
    // an addressing bug moves.
    float2 inTile = frac((pixel - CLUSTER_VIEWPORT_MIN) / CLUSTER_TILE_SIZE);
    float2 edge = min(inTile, 1.0 - inTile) * CLUSTER_TILE_SIZE;
    bool onLattice = (min(edge.x, edge.y) < 1.0);

    // Reduce over the tile's whole depth column. See the header note: there is no
    // per-pixel view distance to pick a single slice with.
    uint maxCount = 0;
    uint busiestCluster = 0;
    bool anyOverflow = false;
    int slices = (int)CLUSTER_SLICE_COUNT;
    for (int s = 0; s < slices; ++s)
    {
        uint cluster = ClusterIndex(tile, s);
        uint count = ClusterGrid.Load((int)cluster);
        if (count > maxCount) { maxCount = count; busiestCluster = cluster; }
        if (count > (uint)CLUSTER_STRIDE) anyOverflow = true;
    }

    // THE CONSISTENCY PROBE. The count says lights reached this cluster; this asks whether
    // the index list and the light buffer agree, and paints magenta when they do not.
    //
    // It exists because of the failure mode C1's hazard note names: a buffer SRV that got
    // silently unbound reads as ZERO, and a zeroed index list still hands out light index
    // 0 -- a perfectly valid light -- so a plain "did we read something" test cannot see
    // it. Two things can:
    //   - an index at or past the frame's light count is not a light at all;
    //   - the first two entries of a cluster holding two or more lights are never equal,
    //     because the builder appends each light to a cluster exactly once. An all-zero
    //     list fails this immediately.
    // A record beyond the uploaded prefix has a zero range, so the third test catches a
    // light buffer that never got its contents.
    bool inconsistent = false;
    if (maxCount > 0)
    {
        uint base = ClusterLightListBase(busiestCluster);
        uint i0 = LightIndexList.Load((int)base);
        inconsistent = (i0 >= (uint)CameraForward.w);
        if (!inconsistent)
            inconsistent = (LightBuffer[i0].posRange.w <= 0.0);
        if (!inconsistent && maxCount >= 2)
            inconsistent = (LightIndexList.Load((int)(base + 1)) == i0);
    }

    float3 lattice = float3(0.30, 0.30, 0.35);

    if (DebugClusterCtl.x >= 0.5)
    {
        // ---- overflow mask ----------------------------------------------------------
        // Three states, not two. "At the stride" is drawn as well as "past it", because a
        // cluster sitting exactly at 64 is the one that will overflow on the next light,
        // and a mask that only lights up after the damage is done cannot be used to
        // decide whether the stride is big enough -- which is the question it exists to
        // answer.
        if (inconsistent)
            return float4(1.0, 0.0, 1.0, 0.90);
        if (anyOverflow)
            return float4(1.00, 0.10, 0.10, 0.85);
        if (maxCount == (uint)CLUSTER_STRIDE)
            return float4(1.00, 0.60, 0.10, 0.80);
        if (onLattice)
            return float4(lattice, 0.35);
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    // ---- occupancy heat map ---------------------------------------------------------
    if (inconsistent)
        return float4(1.0, 0.0, 1.0, 0.90);

    if (maxCount == 0)
    {
        // Empty tiles keep a faint wash rather than nothing, so the extent of the grid --
        // and therefore the viewport it was built over -- is visible even on a scene with
        // no local lights in it at all.
        if (onLattice)
            return float4(lattice, 0.35);
        return float4(0.02, 0.02, 0.06, 0.15);
    }

    float3 col = OccupancyColor(maxCount);
    if (onLattice)
        col = lerp(col, lattice, 0.5);
    // Opaque enough to read as a field, transparent enough to keep the scene underneath
    // recognisable -- the tile a light is over is only meaningful next to the thing that
    // is emitting it.
    return float4(col, 0.72);
}

// The cluster grid's addressing -- the one place a pixel position and a view distance
// become a cluster index.
//
// C4 of the clustered lighting plan builds the grid on the CPU; C6 replaces the
// builder with a compute shader; C5 reads it from every lit material shader. All three
// have to agree about which cluster a pixel is in, and the C++ side of this file is
//     Core/GameEngineDevice/Include/W3DDevice/GameClient/W3DClusterGrid.h
// whose ClusterGridParams carries the same fields and whose inline Slice_Of /
// Cluster_Index are the same arithmetic. THE TWO MUST AGREE. They are not checkable
// against each other at compile time, and a disagreement does not crash: it shades a
// pixel with the light list belonging to some other part of the screen, which reads as
// "the lighting is a bit wrong here" rather than as an index bug.
//
// THE THREE BUFFERS. C1 put the pixel stage's shader-buffer slots above the eight texture
// stages (GFX_FIRST_PIXEL_BUFFER_SLOT == 8, WW3D2/gfxdevice.h), because a buffer inside
// the stage range would be replaced by the next Set_Texture with nothing to say so. The
// plan assigns:
//
//     StructuredBuffer<GpuLight> LightBuffer    : register(t8);   // gpulight.hlsli
//     Buffer<uint>               ClusterGrid    : register(t9);   // one count per cluster
//     Buffer<uint>               LightIndexList : register(t10);  // CLUSTER_MAX_LIGHTS per cluster
//
// They are declared by the shader that wants them, not here, exactly as shadow.hlsli
// leaves its map's register to the caller. They take NO sampler: DECLARE_SAMPLER pairs a
// Texture2D with a SamplerState at one slot number, and a Buffer<uint> read by Load()
// wants only the t slot. Declaring one through DECLARE_SAMPLER would put a sampler in an
// s register nothing binds and would make the buffer a Texture2D, which will not compile.
//
// THE COUNT IN ClusterGrid IS NOT CLAMPED. It is how many lights *reached* the cluster;
// LightIndexList only holds the first CLUSTER_MAX_LIGHTS of them. A reader must iterate
//     min(ClusterGrid.Load(cluster), CLUSTER_MAX_LIGHTS)
// and never the raw count, or it walks off the end of the cluster's slice of the list and
// into the next cluster's. Storing the raw count is what makes an overflow visible at all
// -- a count clamped at 64 is indistinguishable from a cluster that happens to hold
// exactly 64 -- and it is also what C6's InterlockedAdd produces for free.

#include "shadermodel.hlsli"
#include "frameconstants.hlsli"

#ifndef RTS_SHADER_CLUSTERGRID_HLSLI
#define RTS_SHADER_CLUSTERGRID_HLSLI

// The fixed stride of LightIndexList, in uints per cluster. Also carried in
// ClusterLimits.y so the CPU builder and the shader cannot drift apart; this constant is
// the compile-time spelling for a loop bound, and CLUSTER_STRIDE below is the runtime
// one. See the clustered lighting plan section 1.2 for why a fixed stride and not a
// prefix-summed compacted list.
#define CLUSTER_MAX_LIGHTS 64

// ---------------------------------------------------------------------------------------
// The b1 fields, named. Reading ClusterParams.w at three call sites and remembering what
// .w was is exactly how the packing drifts.
// ---------------------------------------------------------------------------------------

#define CLUSTER_TILE_SIZE   (ClusterParams.xy)
#define CLUSTER_SLICE_COUNT (ClusterParams.z)
#define CLUSTER_GRID_X      (ClusterParams.w)
#define CLUSTER_GRID_Y      (ClusterLimits.x)
#define CLUSTER_STRIDE      (ClusterLimits.y)
#define CLUSTER_VIEWPORT_MIN (ClusterScreen.xy)
#define CLUSTER_VIEWPORT_SIZE (ClusterScreen.zw)

// True once the grid has been built at least once this run. Every field above is zero
// before that (C3's Write_Frame_Constants zeroes the whole block), and a slice count of
// zero would make ClusterSliceOf divide the range into nothing.
bool ClusterGridValid()
{
    return CLUSTER_SLICE_COUNT >= 1.0 && CLUSTER_GRID_X >= 1.0 && CLUSTER_GRID_Y >= 1.0;
}

// ---------------------------------------------------------------------------------------
// Pixel -> tile.
//
// pixelPos is SV_Position.xy, which is the pixel *centre* in render-target pixels -- so
// the top-left pixel is (0.5, 0.5), not (0, 0). The grid is laid over the camera's
// viewport rather than the whole target, because the tactical view does not cover the
// screen: the control bar sits under it, and a grid measured from the target's top-left
// would put every tile boundary a fraction of a tile away from where the CPU builder put
// it. CameraClass::Apply is what sets that viewport, and W3DClusterGrid.cpp takes the
// same numbers from the same place.
// ---------------------------------------------------------------------------------------
int2 ClusterTileOf(float2 pixelPos)
{
    return (int2)floor((pixelPos - CLUSTER_VIEWPORT_MIN) / CLUSTER_TILE_SIZE);
}

// Whether a render-target pixel is inside the viewport the grid was built over. Tested on
// the pixel and not on the tile: the right and bottom tiles are partial whenever the
// viewport is not a whole number of tiles across, so a pixel just outside the viewport
// still lands on a valid tile index and would be shaded from a cluster covering nothing
// it can see.
bool ClusterPixelInGrid(float2 pixelPos)
{
    float2 local = pixelPos - CLUSTER_VIEWPORT_MIN;
    return all(local >= 0.0) && all(local < CLUSTER_VIEWPORT_SIZE);
}

// ---------------------------------------------------------------------------------------
// View distance -> slice.
//
// viewDist is the POSITIVE distance in front of the camera, not the view-space z. See the
// long derivation in W3DClusterGrid.cpp: this engine's view space is right-handed with
// forward along -Z, so the signed view z of anything visible is negative and viewDist is
// its negation. The clip-space w a vertex shader produces is already this number.
//
//     slice = floor(log(viewDist) * scale + bias),  scale = ClusterDepth.x, bias = .y
//
// which is floor(scale * log(viewDist / zNear)) written so the shader does one multiply
// and one add. Clamped rather than rejected at both ends: a fragment slightly in front of
// the near plane or past the far plane still has to be shaded by something, and the end
// slices are the honest answer for it.
// ---------------------------------------------------------------------------------------
int ClusterSliceOf(float viewDist)
{
    float s = log(max(viewDist, 1e-4)) * ClusterDepth.x + ClusterDepth.y;
    return (int)clamp(s, 0.0, CLUSTER_SLICE_COUNT - 1.0);
}

// The inverse: the near face of slice s, in view distance. Used by the debug view's
// legend and by anything that wants to know what a slice covers.
float ClusterSliceNearDistance(int slice)
{
    return exp(((float)slice - ClusterDepth.y) / ClusterDepth.x);
}

// ---------------------------------------------------------------------------------------
// (tile, slice) -> the index into ClusterGrid, and the base index into LightIndexList.
//
// Slice-major, then row, then column: consecutive tiles across the screen are consecutive
// in memory, which is the order a pixel-shader wavefront reads them in.
// ---------------------------------------------------------------------------------------
uint ClusterIndex(int2 tile, int slice)
{
    return (uint)((slice * (int)CLUSTER_GRID_Y + tile.y) * (int)CLUSTER_GRID_X + tile.x);
}

uint ClusterLightListBase(uint cluster)
{
    return cluster * (uint)CLUSTER_STRIDE;
}

#endif  // RTS_SHADER_CLUSTERGRID_HLSLI

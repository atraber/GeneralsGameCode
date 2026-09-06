// The clustered light lookup, in one place -- C5 of the clustered lighting plan.
//
// Everything a lit material shader needs in order to turn "this pixel, this far in front
// of the camera" into "these local lights, this much radiance from each", and nothing
// else. It follows the pattern shadow.hlsli set: the shared file owns the maths every
// receiver has to agree on and takes its resources as parameters, so that six shaders
// cannot end up with six subtly different copies of one falloff curve. That is not a
// hypothetical here -- shadow.hlsli exists because the shadow filter *had* been copied six
// times, and a unit and the ground it stands on have to agree tap for tap.
//
// WHAT IS SHARED AND WHAT IS NOT. Shared: the cluster lookup, the iteration (including the
// unclamped-count trap below), the inverse-square window and the spot cone -- i.e. every
// part of "how much light arrives here". NOT shared: the BRDF. unit_pbr_ps runs GGX +
// Smith + Schlick and carries the engine's PI irradiance convention inside its own
// DirectLight(); unit_ps and terrain_ps are Lambert in the engine's *other* convention
// (see ClusteredLightingDiffuse below, and the note on the two conventions there). Putting
// one BRDF in here would mean either duplicating unit_pbr_ps's -- which is the failure
// this project has been bitten by twice already, one constant living in two spaces -- or
// forcing a terrain pixel through a metallic-roughness evaluation it has no inputs for. So
// the split is: this file produces (L, radiance); the material shader shades with it.
//
// THE THREE BUFFERS are declared by the shader that wants them, at the absolute slots
// clustergrid.hlsli documents, and passed in through CLUSTER_BUFFERS_ARG:
//
//     StructuredBuffer<GpuLight> LightBuffer    : register(t8);
//     Buffer<uint>               ClusterGrid    : register(t9);
//     Buffer<uint>               LightIndexList : register(t10);
//
// No sampler, for any of them -- see clustergrid.hlsli. They are bound once per frame and
// not per draw (W3DShaderManager::bindClusteredLightBuffers, called from W3DView::draw);
// nothing in a material shader has to arrange for that.

#include "shadermodel.hlsli"
#include "gpulight.hlsli"
#include "clustergrid.hlsli"

#ifndef RTS_SHADER_CLUSTERED_HLSLI
#define RTS_SHADER_CLUSTERED_HLSLI

// The resource triple, as a parameter list and as an argument list. Object-like macros and
// not function-like ones: fxc rejects a zero-argument function-like macro outright (error
// X1500, reported on the *definition* line, which is a confusing place to be sent).
#define CLUSTER_BUFFERS_PARAM  StructuredBuffer<GpuLight> lightBuffer, \
                               Buffer<uint> clusterGrid, \
                               Buffer<uint> lightIndexList
#define CLUSTER_BUFFERS_ARG    LightBuffer, ClusterGrid, LightIndexList

// "This pixel is not in any cluster." A sentinel rather than a second out-parameter,
// because every caller's next move is a loop bound and ClusterLightCount returns 0 for it
// -- so a caller that forgets to test it still shades correctly rather than reading
// cluster 0xFFFFFFFF's count off the end of the grid.
#define CLUSTER_NONE 0xFFFFFFFF

// ---------------------------------------------------------------------------------------
// The gate.
// ---------------------------------------------------------------------------------------

// b1's ClusterLimits.z, published once a frame by GpuLightListClass::Write_Frame_Constants
// from TheGlobalData->m_useClusteredLighting (options.ini UseClusteredLighting). The
// polarity is deliberate and is the whole reason the feature is testable: ZERO IS OFF, and
// an unwritten b1 block reads zero. Every path that could leave the block unwritten -- a
// frame before the first Update(), a degenerate camera, a menu, a device that could not
// create the buffers -- therefore lands on "off" and produces exactly the frame this stage
// started from. The plan's gate for C5.1 is a replay run with this off showing 0 differing
// pixels, and that only means anything because there is no state in which "off" is a guess.
//
// ClusterGridValid() is ANDed in for the reason it exists: before the grid has been sized,
// every addressing field in b1 is zero and a slice count of zero divides the depth range
// into nothing.
bool ClusteredLightingEnabled()
{
    return ClusterLimits.z >= 0.5 && ClusterGridValid();
}

// b1's ClusterLimits.w. 1 = suppress the shader's OWN directional term for this frame, so
// that a clustered stand-in for the sun can be compared against the directional path it is
// meant to reproduce. Debug-driven in practice (W3D_CLUSTER_SUN_CHECK=1 -- the recipe is in
// W3DGpuLightList.cpp), but it is a plain frame constant and not a #define so that the two
// paths can be swapped without a rebuild: a rebuild is not a re-run in this tree, and a
// control that needs one cannot be compared against the frame it is checking.
// Zero in every ordinary frame, and zero is what an unwritten b1 says.
#define CLUSTER_SUPPRESS_DIRECTIONAL (ClusterLimits.w)

// ---------------------------------------------------------------------------------------
// Pixel -> cluster.
// ---------------------------------------------------------------------------------------

// pixelPos is SV_Position.xy: RENDER TARGET pixels, centre-sampled, with the viewport
// transform already applied -- which is what ClusterTileOf wants, and it is why nothing
// here has to know which of the game's two viewports is current.
//
// viewDist is the POSITIVE distance in front of the camera. A vertex shader already has it
// and does not know it: this engine's projection is right-handed (Init_Perspective sets
// Row[3][2] = -1), so clip.w == -z_view == exactly the distance wanted, and in a pixel
// shader SV_Position.w is its reciprocal. Nothing needs reconstructing out of the
// projection's _33/_43 the way unit_pbr_ps's viewDepth() has to for the depth *texture* --
// those two numbers describe what is stored in a buffer, not where this fragment is -- and
// a caller reaching for SsrParams here has taken a wrong turn.
uint ClusterIndexAt(float2 pixelPos, float viewDist)
{
    // Tested on the pixel and not on the tile: the right and bottom tiles are partial
    // whenever the viewport is not a whole number of tiles across, so a pixel just outside
    // the viewport still lands on a valid tile index and would be lit from a cluster
    // covering ground it cannot see.
    if (!ClusterPixelInGrid(pixelPos))
        return CLUSTER_NONE;
    return ClusterIndex(ClusterTileOf(pixelPos), ClusterSliceOf(viewDist));
}

// ---------------------------------------------------------------------------------------
// Iteration.
// ---------------------------------------------------------------------------------------

// How many of a cluster's lights may actually be read.
//
// THE STORED COUNT IS NOT CLAMPED and must never be used raw. It records how many lights
// *reached* the cluster, which is the only way an overflow can be seen at all (a count
// clamped at 64 is indistinguishable from a cluster legitimately holding 64) -- but
// LightIndexList only kept the first stride of them, so iterating the raw count walks off
// the end of this cluster's slice of the list and into the next cluster's lights.
// clustergrid.hlsli says so where the grid is described; this is where it is enforced,
// once, for every reader.
//
// Clamped against BOTH spellings of the stride: CLUSTER_MAX_LIGHTS, the compile-time one,
// and ClusterLimits.y, the runtime one the CPU builder publishes. They are the same number
// by construction and nothing checks the pair, so taking the smaller costs one instruction
// and removes the case where the loop bound comes from one of them and
// ClusterLightListBase's multiply from the other.
uint ClusterLightCount(Buffer<uint> clusterGrid, uint cluster)
{
    if (cluster == CLUSTER_NONE)
        return 0;
    uint stride = min((uint)CLUSTER_STRIDE, (uint)CLUSTER_MAX_LIGHTS);
    return min(clusterGrid.Load((int)cluster), stride);
}

// The i'th light of a cluster, for 0 <= i < ClusterLightCount(). Two dependent loads: the
// index list, then the light record it names.
GpuLight ClusterLightAt(StructuredBuffer<GpuLight> lightBuffer, Buffer<uint> lightIndexList,
                        uint cluster, uint i)
{
    uint index = lightIndexList.Load((int)(ClusterLightListBase(cluster) + i));
    return lightBuffer[index];
}

// ---------------------------------------------------------------------------------------
// One light's contribution at a point: direction and radiance.
// ---------------------------------------------------------------------------------------

// How close a fragment may get to a light before the inverse square stops growing, as a
// squared distance. 0.01 is a tenth of a world unit -- far inside whatever geometry a light
// is attached to -- and it exists only so that a fragment sitting exactly on the light
// gives a large number rather than an infinity. An infinity here does not stay local: it
// becomes a NaN in the specular divide, and NaN survives every operation after it, so the
// pixel is simply gone.
#define CLUSTER_MIN_DIST2 0.01

// Direction toward the light, and the radiance arriving from it. Returns false when the
// light does not reach this point at all, so the caller can skip the BRDF entirely.
//
// ATTENUATION. Inverse square, with a smooth window that reaches exactly zero at the
// light's authored range (posRange.w):
//
//     d2    = dot(toLight, toLight)
//     win   = saturate(1 - (d2 / range^2)^2)^2        // = (1 - (d/range)^4)^2
//     atten = win / max(d2, CLUSTER_MIN_DIST2)
//
// A window and not a hard cut at range, because the grid bins a light into a froxel when
// its sphere touches that froxel at all: a light still contributing something at exactly
// its range would step to zero along a froxel boundary, which is a straight edge across the
// ground that moves with the camera and reads as an addressing bug rather than as a
// falloff. The fourth power keeps the window near 1 over most of the range so that it
// shapes the tail rather than dimming the whole light, and squaring it makes the derivative
// zero at the boundary as well as the value. Squared distances throughout: neither term
// needs a sqrt, and the rsqrt below already pays for the one length this function wants.
//
// **THE INTENSITY CONVENTION IS NOT SETTLED, AND THIS IS WHERE IT WILL BITE.** The engine's
// existing lights are authored against LightEnvironmentClass's *linear* ramp
// (atten = 1 - (d - start)/(end - start), lightenvironment.cpp), so a diffuse of 1.0 means
// "full brightness at the light" and stays worth something out to the range. Under an
// inverse square that same light is worth 1/400 at 20 world units, which is black. That is
// not a bug in this function -- it is the physically sensible falloff the plan asks for --
// and the mismatch is a *content* question: either the packer scales colour by a reference
// distance squared, or lights get re-authored in radiometric units. C7 deletes the old path
// and is the stage where the two conventions stop having to coexist, so C7 is where it has
// to be answered. Until then this is the honest curve, and note that C5.1's sun-equivalence
// control works precisely because it scales its stand-in light's colour by d^2 itself
// rather than pretending the question is already settled.
bool ClusterLightRadiance(GpuLight light, float3 worldPos, out float3 L, out float3 radiance)
{
    // Written before any early return: HLSL requires every out parameter to be assigned on
    // every path, and a caller that ignores the bool then reads a defined value.
    L = float3(0.0, 0.0, 1.0);
    radiance = float3(0.0, 0.0, 0.0);

    float3 toLight = light.posRange.xyz - worldPos;
    float  d2      = dot(toLight, toLight);
    float  range2  = light.posRange.w * light.posRange.w;
    if (d2 >= range2)
        return false;                       // past the authored range, where the window is 0 anyway

    L = toLight * rsqrt(max(d2, CLUSTER_MIN_DIST2));

    float ratio = d2 / max(range2, 1e-6);   // (d / range)^2
    float win   = saturate(1.0 - ratio * ratio);
    win *= win;
    float atten = win / max(d2, CLUSTER_MIN_DIST2);

    // Spot cone. Branched on colorType.w rather than left to degrade: gpulight.h stores -1
    // in spotDirCos.w and 0 in spotInner.y for a point light *so that* a shader forgetting
    // this branch fails soft instead of producing a NaN -- but "fails soft" there means
    // "silently runs a cone test on an omnidirectional light", which is a bug nobody would
    // go looking for.
    if (light.colorType.w > 0.5)
    {
        // spotDirCos.xyz points along the light's travel and L points back toward the
        // light, so the cone angle is measured against -L. Same expression as
        // LightEnvironmentClass::Init_From_Point_Or_Spot_Light's Dot(-spot_dir, Direction).
        float cosTheta = dot(-L, light.spotDirCos.xyz);
        if (cosTheta < light.spotDirCos.w)
            return false;                   // outside the outer cone
        // pow(cosTheta, SpotExponent) -- the authored Phong falloff LightClass actually
        // carries, from spotInner.y. NOT a smoothstep between spotInner.x and spotDirCos.w:
        // that inner cosine is *synthesized* by the packer (a fixed 0.75 of the outer angle,
        // see Pack_Light), so shading with it draws a cone no map author ever specified.
        // An exponent of 0 -- which is what a point light carries, and what an unauthored
        // spot carries -- gives pow(x, 0) == 1, i.e. a hard-edged cone, which is what those
        // lights mean.
        atten *= pow(saturate(cosTheta), light.spotInner.y);
    }

    radiance = light.colorType.rgb * atten;
    return true;
}

// ---------------------------------------------------------------------------------------
// The ready-made non-PBR term, for C5.2 and C5.3.
// ---------------------------------------------------------------------------------------

// Sum of N.L * radiance over the cluster's lights. NOT multiplied by an albedo and NOT
// divided by PI: the caller multiplies by whatever it calls its surface colour.
//
// **This is a different irradiance convention from the PBR path, on purpose, and the
// difference is exactly PI.** The engine's light colours are not radiometric. Every
// non-PBR shader here inherits a pipeline whose convention is "a white surface fully facing
// a light of colour C renders as C" -- unit_vs computes MatDiffuse * saturate(dot(N, L))
// with no constants in it, and the terrain's CPU bake computes the same -- so a local light
// dropped into unit_ps or terrain_ps has to land in that convention or local lights will
// read PI times brighter than the sun beside them. unit_pbr_ps reaches the same place by
// the other route: a Lambert BRDF is albedo/PI, so it multiplies the light colour by PI
// first (LIGHT_IRRADIANCE, and the long note above it) and the PI cancels. Two conventions,
// one result; the bug is only ever mixing them, which is exactly what the missing
// `radiance *= PI` in unit_pbr_ps was.
//
// Deliberately not wired into any shader yet: unit_ps, unit_detail_ps, terrain_ps and
// road_ps are C5.2 and C5.3, and those stages also have to get a world position and a world
// normal to the pixel first (the terrain has no vertex normal at all -- see the plan).
float3 ClusteredLightingDiffuse(CLUSTER_BUFFERS_PARAM, uint cluster,
                                float3 worldPos, float3 N)
{
    float3 sum = float3(0.0, 0.0, 0.0);
    uint count = ClusterLightCount(clusterGrid, cluster);
    [loop] for (uint i = 0; i < count; ++i)
    {
        GpuLight light = ClusterLightAt(lightBuffer, lightIndexList, cluster, i);
        float3 L, radiance;
        if (!ClusterLightRadiance(light, worldPos, L, radiance))
            continue;
        sum += radiance * saturate(dot(N, L));
    }
    return sum;
}

#endif  // RTS_SHADER_CLUSTERED_HLSLI

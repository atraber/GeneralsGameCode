// Volumetric fog composite pixel shader.
//
// Two jobs in one pass:
//  1. The froxel volume -- ambient haze, low frequency -- read at the
//     scene depth with trilinear filtering.
//  2. Punctual lights (headlights, spot lights, light pulses), integrated PER PIXEL along
//     the camera ray, clipped exactly to the light's sphere, its spot cone and the scene
//     depth.
//
// WHY THE LIGHTS LEFT THE FROXEL VOLUME. A headlight in the installed HD vehicle mods is a
// 55 degree spot with a 26 unit range. At the RTS camera's usual 300-500 units one of the
// 32 exponential froxel slices is ~70 units deep, so the whole cone sat inside one or two
// slices: its in-scatter was averaged over air that was mostly unlit and then smeared by the
// trilinear read, and nothing stopped it at the ground, so the lit sphere below the terrain
// glowed through. A cone that short needs the ray segment, not the froxel.
//
// Rendered via a fullscreen quad with blend state:
//   SrcBlend = ONE, DestBlend = SRC_ALPHA
// Result: FinalColor = Inscatter.rgb + SceneColor.rgb * Transmittance.a

#include "shadermodel.hlsli"
#include "frameconstants.hlsli"
#include "clustered.hlsli"

Texture3D<float4> VolumeIntegrated : register(t0);
SamplerState      VolumeSampler    : register(s0);
Texture2D<float4> SceneDepth       : register(t1);
SamplerState      DepthSampler     : register(s1);

// The clustered light buffers, bound once a frame for the pixel stage by
// W3DShaderManager::bindClusteredLightBuffers -- see clustergrid.hlsli for the slots.
StructuredBuffer<GpuLight> LightBuffer    : register(t8);
Buffer<uint>               ClusterGrid    : register(t9);
Buffer<uint>               LightIndexList : register(t10);

struct PS_INPUT
{
    float4 position  : SV_Position;
    float4 color     : COLOR0;
    float2 texcoord0 : TEXCOORD0;
    float2 texcoord1 : TEXCOORD1;
};

#define FOG_LIGHT_SCATTER   (FogLightParams.x)
#define FOG_LIGHTS_PER_PIXEL (FogLightParams.y >= 0.5)
#define FOG_DEBUG_MODE      (FogLightParams.z)
#define FOG_POINT_WEIGHT    (FogLightParams.w)

static const float FOG_PI = 3.14159265;
static const float FOG_ISOTROPIC = 1.0 / (4.0 * 3.14159265);
static const float3 FOG_ZERO3 = float3(0.0, 0.0, 0.0);

// Samples per clipped light segment. Equiangular sampling (below) takes the inverse square
// out of the estimator, so what is left to sample is smooth -- window, cone falloff, phase --
// and six dithered samples show no visible noise on a 26 unit cone.
#define FOG_LIGHT_SAMPLES 6

// Share of the phase function that is isotropic. A pure Henyey-Greenstein lobe lights a
// beam only when the camera looks along it, and an RTS camera looks down on nearly every
// beam from the side.
#define FOG_LIGHT_ISOTROPIC_MIX 0.5

// Closest a ray may pass a light, for the equiangular transform. Below this the estimator's
// 1/perp grows without bound for a ray that goes straight through the lamp; half a world unit
// is well inside the vehicle the lamp is mounted on.
#define FOG_MIN_PERP 0.5

// Probe gain for W3D_FOG_DEBUG=1, where the in-scatter is shown on its own.
#define FOG_DEBUG_GAIN 4.0

float FogHenyeyGreenstein(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4);
    return (1.0 - g2) / (4.0 * FOG_PI * denom * sqrt(denom));
}

float InterleavedGradientNoise(float2 pixelPos)
{
    return frac(52.9829189 * frac(dot(pixelPos, float2(0.06711056, 0.00583715))));
}

// The camera-view depth (m_ssrDepthTexture) holds z/w in red, whether it is the snapshot of
// the hardware depth buffer or the prepass's R32F target. Right-handed projection, so the
// view distance is abs(_43 / (_33 + z)) -- see the SSR note in unit_pbr_ps.hlsl.
float SceneViewDistance(float ndcZ)
{
    if (ndcZ >= 0.99989)
        return ClusterDepth.w;   // far plane / sky
    float denom = FogCameraRight.w + ndcZ;
    if (abs(denom) < 1.0e-6)
        return ClusterDepth.w;
    return abs(FogCameraUp.w / denom);
}

// The un-normalised ray through a render-target pixel: the point at VIEW DISTANCE d is
// FogCameraPos + d * ray. The same reconstruction volumetric_scatter_cs uses for a froxel.
float3 ViewRayThroughPixel(float2 pixelPos)
{
    float2 uv = (pixelPos - CLUSTER_VIEWPORT_MIN) / CLUSTER_VIEWPORT_SIZE;
    float ndcX = 2.0 * uv.x - 1.0;
    float ndcY = 1.0 - 2.0 * uv.y;
    float ratioX = (ndcX + ClusterProj.y) / ClusterProj.x;
    float ratioY = (ndcY + ClusterProj.w) / ClusterProj.z;
    return ratioX * FogCameraRight.xyz + ratioY * FogCameraUp.xyz + CameraForward.xyz;
}

// In-scattered radiance from one light over the ray segment [tA, tB] (world distance along
// the unit ray V from O), which the caller has already clipped to where the light reaches.
//
// Equiangular sampling (Kulla & Fajardo 2012): samples are placed with density proportional
// to 1/d^2 about the ray's closest approach to the light, so the inverse square cancels out
// of the estimator exactly and only the smooth factors are averaged.
float3 IntegrateLightSegment(GpuLight light, float3 O, float3 V, float tA, float tB, float dither)
{
    float3 toLight = light.posRange.xyz - O;
    float along = dot(toLight, V);
    float perp = max(sqrt(max(dot(toLight, toLight) - along * along, 0.0)), FOG_MIN_PERP);
    float thA = atan((tA - along) / perp);
    float thB = atan((tB - along) / perp);

    float range2 = max(light.posRange.w * light.posRange.w, 1e-6);
    bool isSpot = light.colorType.w > 0.5;
    float g = FogParams0.w;

    float sum = 0.0;
    [loop] for (int s = 0; s < FOG_LIGHT_SAMPLES; ++s)
    {
        float xi = ((float)s + dither) / (float)FOG_LIGHT_SAMPLES;
        float t = along + perp * tan(lerp(thA, thB, xi));
        float3 toL = light.posRange.xyz - (O + t * V);
        float d2 = max(dot(toL, toL), CLUSTER_MIN_DIST2);
        float3 L = toL * rsqrt(d2);

        // The same window clustered.hlsli lights surfaces with, so a beam fades out exactly
        // where the pool it throws on the ground does.
        float ratio = d2 / range2;
        float win = saturate(1.0 - ratio * ratio);
        win *= win;

        float cone = 1.0;
        if (isSpot)
            cone = pow(max(dot(-L, light.spotDirCos.xyz), 1e-4), light.spotInner.y);

        // dot(L, V): light travels along -L, the scattered photon towards the camera along
        // -V, so this is the cosine between the two travel directions (1 = looking into it).
        float phase = lerp(FogHenyeyGreenstein(dot(L, V), g), FOG_ISOTROPIC, FOG_LIGHT_ISOTROPIC_MIX);
        sum += win * cone * phase;
    }
    return light.colorType.rgb * (sum / (float)FOG_LIGHT_SAMPLES) * ((thB - thA) / perp);
}

// Clip [t0, t1] to the light's sphere and, for a spot, to its forward cone; integrate what is
// left. Exact clipping is what gives a beam a hard edge and stops it at the ground.
float3 LightInscatterOverSegment(GpuLight light, float3 O, float3 V, float t0, float t1, float dither)
{
    float3 W = O - light.posRange.xyz;
    float wv = dot(W, V);
    float ww = dot(W, W);
    float r = light.posRange.w;
    float disc = wv * wv - (ww - r * r);
    if (disc <= 0.0)
        return FOG_ZERO3;
    float sq = sqrt(disc);
    float lo = max(t0, -wv - sq);
    float hi = min(t1, -wv + sq);
    if (hi <= lo)
        return FOG_ZERO3;

    // Point lights, and spots wider than a hemisphere (whose cone the squared test below
    // cannot express): the sphere is the clip, and the cone falloff is left to the samples.
    if (light.colorType.w < 0.5)
        return IntegrateLightSegment(light, O, V, lo, hi, dither) * FOG_POINT_WEIGHT;
    if (light.spotDirCos.w <= 0.0)
        return IntegrateLightSegment(light, O, V, lo, hi, dither);

    float3 D = light.spotDirCos.xyz;
    float vd = dot(V, D);
    float wd = dot(W, D);

    // The forward half-space, dot(P - apex, D) >= 0. It removes the backward nappe of the
    // double cone the quadratic describes, so what survives below is the real cone only.
    if (abs(vd) > 1e-6)
    {
        float tp = -wd / vd;
        if (vd > 0.0) lo = max(lo, tp); else hi = min(hi, tp);
    }
    else if (wd < 0.0)
        return FOG_ZERO3;
    if (hi <= lo)
        return FOG_ZERO3;

    // Inside the double cone: f(t) = (wd + t*vd)^2 - cos^2 * |W + t*V|^2 >= 0
    //                             = qa*t^2 + 2*qb*t + qc
    float c2 = light.spotDirCos.w * light.spotDirCos.w;
    float qa = vd * vd - c2;
    float qb = wd * vd - c2 * wv;
    float qc = wd * wd - c2 * ww;

    if (abs(qa) < 1e-6)
    {
        // Ray parallel to the cone's surface: f is linear.
        if (abs(qb) > 1e-9)
        {
            float tr = -qc / (2.0 * qb);
            if (qb > 0.0) lo = max(lo, tr); else hi = min(hi, tr);
        }
        else if (qc < 0.0)
            return FOG_ZERO3;
        return (hi > lo) ? IntegrateLightSegment(light, O, V, lo, hi, dither) : FOG_ZERO3;
    }

    float dq = qb * qb - qa * qc;
    if (qa < 0.0)
    {
        // The ray crosses the cone: inside between the roots.
        if (dq <= 0.0)
            return FOG_ZERO3;
        float s = sqrt(dq);
        lo = max(lo, (-qb + s) / qa);
        hi = min(hi, (-qb - s) / qa);
        return (hi > lo) ? IntegrateLightSegment(light, O, V, lo, hi, dither) : FOG_ZERO3;
    }

    // qa > 0: the ray runs along the cone, inside outside the roots. After the half-space clip
    // at most one of the two pieces is non-empty; both are evaluated rather than chosen.
    if (dq <= 0.0)
        return IntegrateLightSegment(light, O, V, lo, hi, dither);
    float s = sqrt(dq);
    float rMin = (-qb - s) / qa;
    float rMax = (-qb + s) / qa;
    float3 result = FOG_ZERO3;
    if (min(hi, rMin) > lo)
        result += IntegrateLightSegment(light, O, V, lo, min(hi, rMin), dither);
    if (hi > max(lo, rMax))
        result += IntegrateLightSegment(light, O, V, max(lo, rMax), hi, dither);
    return result;
}

// Every punctual light's in-scatter between the camera and the scene depth for this pixel.
//
// The ray is walked through its tile's cluster column one slice at a time, and each light
// binned into slice k is integrated ONLY over the part of the ray inside slice k's depth
// range. The slices partition the ray, so a light binned into several slices is counted
// once per piece and never twice; and the grid bins a light into every froxel its sphere
// touches, so no piece of the ray inside a light can miss it.
float3 PunctualInscatter(float2 pixelPos, float sceneViewDist)
{
    // CameraForward.w is this frame's uploaded light count (GpuLightListClass::Write_Frame_Constants).
    // Zero on every frame with no local light -- all of daytime -- and then the cluster column
    // walk below would be 24 empty reads per pixel for nothing.
    if (FOG_LIGHT_SCATTER <= 0.0 || CameraForward.w < 0.5
        || !ClusteredLightingEnabled() || !ClusterPixelInGrid(pixelPos))
        return FOG_ZERO3;

    float3 ray = ViewRayThroughPixel(pixelPos);
    float rayLen = length(ray);
    float3 V = ray / rayLen;
    float3 O = FogCameraPos.xyz;
    float dither = InterleavedGradientNoise(pixelPos);

    int2 tile = ClusterTileOf(pixelPos);
    int lastSlice = ClusterSliceOf(sceneViewDist);

    float3 sum = FOG_ZERO3;
    [loop] for (int slice = 0; slice <= lastSlice; ++slice)
    {
        uint cluster = ClusterIndex(tile, slice);
        uint count = ClusterLightCount(ClusterGrid, cluster);
        if (count == 0)
            continue;

        float z0 = (slice == 0) ? 0.0 : ClusterSliceNearDistance(slice);
        float z1 = (slice == lastSlice) ? sceneViewDist : ClusterSliceNearDistance(slice + 1);

        [loop] for (uint i = 0; i < count; ++i)
        {
            GpuLight light = ClusterLightAt(LightBuffer, LightIndexList, cluster, i);
            sum += LightInscatterOverSegment(light, O, V, z0 * rayLen, z1 * rayLen, dither);
        }
    }
    return sum * FOG_LIGHT_SCATTER;
}

// W3D_FOG_DEBUG=2: where the lights are, against the depth this pass reads.
//   magenta ring  -- a light's range sphere meets the depth buffer here
//   green         -- inside a spot light's range AND cone: the footprint its beam should fill
//   blue          -- this pixel's cluster holds at least one light (the positive control:
//                    it shows the buffers are bound and the grid is read at all)
// The ring has to enclose the pool terrain_ps lights on the ground exactly; if it is offset,
// the ray reconstruction or the depth conversion is wrong, and nothing else can be judged.
float4 DebugLightProbe(float2 pixelPos, float sceneViewDist)
{
    if (!ClusteredLightingEnabled() || sceneViewDist >= ClusterDepth.w)
        return float4(0.0, 0.0, 0.0, 1.0);

    float3 P = FogCameraPos.xyz + sceneViewDist * ViewRayThroughPixel(pixelPos);
    uint cluster = ClusterIndexAt(pixelPos, sceneViewDist);
    uint count = ClusterLightCount(ClusterGrid, cluster);

    float3 tint = (count > 0) ? float3(0.0, 0.0, 0.25) : FOG_ZERO3;
    [loop] for (uint i = 0; i < count; ++i)
    {
        GpuLight light = ClusterLightAt(LightBuffer, LightIndexList, cluster, i);
        float3 fromLight = P - light.posRange.xyz;
        float d = length(fromLight);
        if (abs(d - light.posRange.w) < 0.6)
            return float4(1.0, 0.0, 1.0, 0.0);
        if (light.colorType.w > 0.5 && d < light.posRange.w
            && dot(fromLight / max(d, 1e-4), light.spotDirCos.xyz) >= light.spotDirCos.w)
            tint += float3(0.0, 0.3, 0.0);
    }
    return float4(tint, 1.0);
}

float4 main(PS_INPUT input) : SV_Target
{
    // If volumetric fog is inactive, output zero inscatter and 100% transmittance
    if (FogSunColor.w < 0.5 || !ClusterGridValid())
        return float4(0.0, 0.0, 0.0, 1.0);

    // SV_Position is the exact render-target pixel, matching m_ssrDepthTexture.
    float2 pixelPos = input.position.xy;
    float ndcZ = SceneDepth.Load(int3((int2)pixelPos, 0)).r;
    float viewDist = SceneViewDistance(ndcZ);

    if (FOG_DEBUG_MODE > 1.5)
        return DebugLightProbe(pixelPos, viewDist);

    // Continuous slice coordinate: s = k is the NEAR face of slice k.
    float sliceScale = FogParams1.z / CLUSTER_SLICE_COUNT;
    float s = log(max(viewDist, 1e-4)) * (sliceScale * ClusterDepth.x) + sliceScale * ClusterDepth.y;
    s = clamp(s, 0.0, FogParams1.z);

    // volumetric_integrate_cs stores in texel k the accumulation through the FAR face of
    // slice k, i.e. at s = k + 1. So the value at s lives at texel s - 1, whose centre is
    // (s - 0.5) / Z. This used to read (s + 0.5) / Z -- one whole slice beyond the surface,
    // ~70 units behind the ground at RTS distances. Below s = 1 the accumulation runs from
    // zero at the camera, which the lerp restores.
    float w = clamp(s - 0.5, 0.5, FogParams1.z - 0.5) / FogParams1.z;
    float2 uv = (pixelPos - CLUSTER_VIEWPORT_MIN) / CLUSTER_VIEWPORT_SIZE;
    float4 fog = VolumeIntegrated.Sample(VolumeSampler, float3(uv, w));
    fog = lerp(float4(0.0, 0.0, 0.0, 1.0), fog, saturate(s));

    float3 inscatter = fog.rgb;
    if (FOG_LIGHTS_PER_PIXEL)
        inscatter += PunctualInscatter(pixelPos, viewDist);

    // W3D_FOG_DEBUG=1: the in-scatter alone, scene removed (alpha 0 zeroes the destination).
    if (FOG_DEBUG_MODE > 0.5)
        return float4(inscatter * FOG_DEBUG_GAIN, 0.0);

    // rgb = accumulated in-scattering, a = transmittance
    return float4(inscatter, fog.a);
}

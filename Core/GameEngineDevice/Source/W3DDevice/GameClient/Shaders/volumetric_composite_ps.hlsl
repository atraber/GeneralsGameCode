// Volumetric fog composite pixel shader.
//
// Reads camera depth, converts to continuous froxel slice coordinate, and samples
// the integrated 3D fog volume with trilinear filtering.
// Rendered via a fullscreen quad with blend state:
//   SrcBlend = ONE, DestBlend = SRC_ALPHA
// Result: FinalColor = Inscatter.rgb + SceneColor.rgb * Transmittance.a

#include "shadermodel.hlsli"
#include "frameconstants.hlsli"
#include "clustergrid.hlsli"

Texture3D<float4> VolumeIntegrated : register(t0);
SamplerState      VolumeSampler    : register(s0);
Texture2D<float4> SceneDepth       : register(t1);
SamplerState      DepthSampler     : register(s1);

struct PS_INPUT
{
    float4 position  : SV_Position;
    float4 color     : COLOR0;
    float2 texcoord0 : TEXCOORD0;
    float2 texcoord1 : TEXCOORD1;
};

// The camera-view depth target (m_ssrDepthTexture) is R32F: the depth prepass writes
// z/w straight into the red channel, so reading it back is a swizzle. It used to be an
// RGB8 split with 255-based weights, unpacked here; the prepass shares its pixel shader
// with the shadow map, so when that map moved to R32F the packing went and this target
// had to follow.
float unpackDepth(float4 rgba)
{
    return rgba.r;
}

float4 main(PS_INPUT input) : SV_Target
{
    // If volumetric fog is inactive, output zero inscatter and 100% transmittance
    if (FogSunColor.w < 0.5 || !ClusterGridValid())
        return float4(0.0, 0.0, 0.0, 1.0);

    // Read camera-view packed depth from the prepass:
    float4 depthSample = SceneDepth.Sample(DepthSampler, input.texcoord0);
    float ndcZ = unpackDepth(depthSample);

    float viewDist;
    if (ndcZ >= 0.99989)
    {
        // Far plane / sky: fog accumulates up to the camera far plane
        viewDist = ClusterDepth.w;
    }
    else
    {
        // Invert depth: FogCameraRight.w = proj._33, FogCameraUp.w = proj._43
        float denom = FogCameraRight.w + ndcZ;
        if (abs(denom) < 1.0e-6)
            viewDist = ClusterDepth.w;
        else
            viewDist = abs(FogCameraUp.w / denom);
    }

    // Convert viewDist to continuous slice index:
    float sliceScale = FogParams1.z / CLUSTER_SLICE_COUNT;
    float fogDepthScale = sliceScale * ClusterDepth.x;
    float fogDepthBias  = sliceScale * ClusterDepth.y;
    float s = log(max(viewDist, 1e-4)) * fogDepthScale + fogDepthBias;
    s = clamp(s, 0.0, FogParams1.z - 1.0);

    // Continuous 3D texture W coordinate [0, 1]:
    float w = (s + 0.5) / FogParams1.z;

    // Sample integrated in-scattering and transmittance:
    float4 fog = VolumeIntegrated.Sample(VolumeSampler, float3(input.texcoord0.x, input.texcoord0.y, w));

    // rgb = accumulated in-scattering, a = transmittance
    return float4(fog.rgb, fog.a);
}

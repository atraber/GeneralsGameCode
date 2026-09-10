// Volumetric fog integration compute shader.
//
// Raymarches along the depth slices (Z = 0 .. 31) for each screen column (X, Y),
// integrating in-scattered light and computing Beer-Lambert optical transmittance.
// Output: rgb = accumulated in-scattering, a = optical transmittance.

#include "shadermodel.hlsli"
#include "frameconstants.hlsli"
#include "clustergrid.hlsli"

Texture3D<float4>   VolumeScatter      : register(t0);
RWTexture3D<float4> VolumeIntegratedRW : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint gridX = (uint)FogParams1.x;
    uint gridY = (uint)FogParams1.y;
    uint gridZ = (uint)FogParams1.z;

    if (id.x >= gridX || id.y >= gridY)
        return;

    // If volumetric fog is inactive, fill all slices with zero inscatter and full transmittance (1.0)
    if (FogSunColor.w < 0.5)
    {
        for (uint z = 0; z < gridZ; ++z)
        {
            VolumeIntegratedRW[int3(id.xy, z)] = float4(0.0, 0.0, 0.0, 1.0);
        }
        return;
    }

    float3 accumInscatter = float3(0.0, 0.0, 0.0);
    float transmittance = 1.0;

    float sliceScale = (float)gridZ / CLUSTER_SLICE_COUNT;
    float fogDepthScale = sliceScale * ClusterDepth.x;
    float fogDepthBias  = sliceScale * ClusterDepth.y;

    for (uint z = 0; z < gridZ; ++z)
    {
        float4 scatterExt = VolumeScatter.Load(int4(id.xy, z, 0));
        float3 S = scatterExt.rgb; // in-scattering
        float ext = scatterExt.a;  // extinction coefficient

        // Slice thickness: step length dl in world units
        float zNear = exp(((float)z - fogDepthBias) / fogDepthScale);
        float zFar  = exp(((float)(z + 1) - fogDepthBias) / fogDepthScale);
        float dl = max(zFar - zNear, 0.0);

        // Beer-Lambert transmittance over slice dl:
        float sliceOptDepth = ext * dl;
        float sliceTransmittance = exp(-sliceOptDepth);

        // Analytical integration of in-scattering over slice assuming constant S and ext:
        // Integral_{0}^{dl} S * exp(-ext * t) dt = S * (1 - exp(-ext * dl)) / ext
        float3 sliceInscatter;
        if (ext > 1e-5)
            sliceInscatter = S * ((1.0 - sliceTransmittance) / ext);
        else
            sliceInscatter = S * dl;

        accumInscatter += transmittance * sliceInscatter;
        transmittance  *= sliceTransmittance;

        VolumeIntegratedRW[int3(id.xy, z)] = float4(accumInscatter, transmittance);
    }
}

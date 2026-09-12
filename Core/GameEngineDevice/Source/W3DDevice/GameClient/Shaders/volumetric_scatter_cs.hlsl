// Volumetric fog scattering compute shader.
//
// Evaluates in-scattered radiance and extinction at each froxel (120 x 68 x 32) in camera frustum.
// Punctual lights (headlights, spot lights, point lights) are queried from the clustered light grid.
// Forward Mie scattering (Henyey-Greenstein phase function) makes vehicle headlights visible in the air
// as realistic volumetric beams and flares when facing towards the camera.

#include "shadermodel.hlsli"
#include "gpulight.hlsli"
#include "clustergrid.hlsli"
#include "clustered.hlsli"

// COMPILE-TIME OPTION: Set to 1 to enable volumetric sun shafts, 0 to disable.
#define VOLUMETRIC_SUN_SHAFTS 1

StructuredBuffer<GpuLight> LightBuffer       : register(t0);
Buffer<uint>               ClusterGrid       : register(t1);
Buffer<uint>               LightIndexList    : register(t2);

#if VOLUMETRIC_SUN_SHAFTS
Texture2D<float4>          ShadowMap         : register(t3);   // R32F: sun depth in .r
#endif

RWTexture3D<float4>        VolumeScatterRW   : register(u0);

#define CLUSTER_PROJ_X_SCALE    (ClusterProj.x)
#define CLUSTER_PROJ_X_OFFSET   (ClusterProj.y)
#define CLUSTER_PROJ_Y_SCALE    (ClusterProj.z)
#define CLUSTER_PROJ_Y_OFFSET   (ClusterProj.w)

// Henyey-Greenstein phase function for aerosol / water droplet scattering
float HenyeyGreenstein(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = max(1.0 + g2 - 2.0 * g * cosTheta, 0.0);
    return (1.0 / (4.0 * 3.14159265)) * ((1.0 - g2) / max(pow(denom, 1.5), 1e-5));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint gridX = (uint)FogParams1.x;
    uint gridY = (uint)FogParams1.y;
    uint gridZ = (uint)FogParams1.z;

    if (id.x >= gridX || id.y >= gridY || id.z >= gridZ)
        return;

    // Gate: Volumetric fog active flag in FogSunColor.w
    if (FogSunColor.w < 0.5 || !ClusterGridValid())
    {
        VolumeScatterRW[id] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // Voxel normalized coordinates [0, 1] within viewport:
    float u = ((float)id.x + 0.5) / (float)gridX;
    float v = ((float)id.y + 0.5) / (float)gridY;

    // Depth slice distance (exponential distribution matching cluster depth range):
    float zSlice = (float)id.z + 0.5;
    float sliceScale = (float)gridZ / CLUSTER_SLICE_COUNT;
    float fogDepthScale = sliceScale * ClusterDepth.x;
    float fogDepthBias  = sliceScale * ClusterDepth.y;
    float viewDist = exp((zSlice - fogDepthBias) / fogDepthScale);

    // Compute ray direction from camera projection:
    float ndcX = 2.0 * u - 1.0;
    float ndcY = 1.0 - 2.0 * v;

    float ratioX = (ndcX + CLUSTER_PROJ_X_OFFSET) / CLUSTER_PROJ_X_SCALE;
    float ratioY = (ndcY + CLUSTER_PROJ_Y_OFFSET) / CLUSTER_PROJ_Y_SCALE;

    // Reconstruct world position of the voxel:
    float3 worldPos = FogCameraPos.xyz
                    + (ratioX * viewDist) * FogCameraRight.xyz
                    + (ratioY * viewDist) * FogCameraUp.xyz
                    + (viewDist) * CameraForward.xyz;

    // Ray direction from camera to voxel:
    float3 toVoxel = worldPos - FogCameraPos.xyz;
    float voxelDist = length(toVoxel);
    float3 V = toVoxel / max(voxelDist, 1e-4);

    // Height fog density: rho(z) = rho0 * exp(-k * max(z - z0, 0))
    float heightAboveGround = max(worldPos.z - FogParams0.z, 0.0);
    float density = FogParams0.x * exp(-heightAboveGround * FogParams0.y);

    if (density < 1e-6)
    {
        VolumeScatterRW[id] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float mieG = FogParams0.w; // Forward scattering anisotropy (typically 0.55-0.65)
    static const float isotropic = 1.0 / (4.0 * 3.14159265);
    float3 inscatterRadiance = float3(0.0, 0.0, 0.0);

    // 1. Ambient atmospheric in-scattering:
    inscatterRadiance += FogSunColor.rgb * FogCameraPos.w;

    // 2. Directional Sun/Moon shafts (compile-time toggle):
#if VOLUMETRIC_SUN_SHAFTS
    if (FogSunDir.w > 0.0)
    {
        bool inSunLight = true;

        // Evaluate shadows if shadow map is active and shadow strength > 0:
        if (FogShadowParams.y > 0.0 && FogShadowParams.w > 0.0)
        {
            // Reconstruct SunVP row-major matrix:
            float4x4 sunVP = float4x4(FogSunVP0, FogSunVP1, FogSunVP2, FogSunVP3);
            float4 sunClip = mul(float4(worldPos, 1.0), sunVP);
            float3 sunNdc = sunClip.xyz / sunClip.w;
            float2 sunUv = sunNdc.xy * float2(0.5, -0.5) + 0.5;

            // Only test shadow map when within the shadow frustum:
            // Outside the frustum (e.g. higher in the sky), light travels freely (unshadowed)
            if (all(sunUv >= 0.0) && all(sunUv <= 1.0) && sunNdc.z >= 0.0 && sunNdc.z <= 1.0)
            {
                int2 shadowTexel = (int2)(sunUv * FogShadowParams.w);
                // The shadow map is R32F: the sun-clip depth is the red channel, whole.
                // This used to unpack a three-channel RGB8 split; the map moved to a
                // single float and this read had to move with it.
                float shadowZ = ShadowMap.Load(int3(shadowTexel, 0)).r;

                inSunLight = (sunNdc.z <= shadowZ + FogShadowParams.x);
            }
        }

        if (inSunLight)
        {
            float cosThetaSun = dot(FogSunDir.xyz, V);
            // Blend forward Mie scattering with isotropic baseline (60%) so sunlight illuminates
            // the atmospheric volume when looking down at terrain, while keeping forward God-ray shafts:
            float hgSun = HenyeyGreenstein(cosThetaSun, mieG);
            float phaseSun = lerp(hgSun, isotropic, 0.60);
            inscatterRadiance += FogSunColor.rgb * (FogSunDir.w * phaseSun);
        }
    }
#endif

    // 3. Punctual lights (headlights, spot lights, point lights) from cluster grid:
    if (ClusteredLightingEnabled())
    {
        float2 pixelPos = CLUSTER_VIEWPORT_MIN + float2(u, v) * CLUSTER_VIEWPORT_SIZE;
        int2 tile = ClusterTileOf(pixelPos);
        tile = clamp(tile, int2(0, 0), int2((int)CLUSTER_GRID_X - 1, (int)CLUSTER_GRID_Y - 1));
        int slice = ClusterSliceOf(viewDist);
        uint cluster = ClusterIndex(tile, slice);
        uint count = ClusterLightCount(ClusterGrid, cluster);

        float lightBoost = max(FogParams1.w, 1.0);

        // Sub-slice integration: 4 sample points along the ray across slice depth
        // prevents small spot lights from being skipped when slice thickness > light range:
        float zNearSlice = exp(((float)id.z - fogDepthBias) / fogDepthScale);
        float zFarSlice  = exp(((float)(id.z + 1) - fogDepthBias) / fogDepthScale);
        float dl = zFarSlice - zNearSlice;

        float3 punctualInscatter = float3(0.0, 0.0, 0.0);
        static const float subOffsets[4] = { 0.125, 0.375, 0.625, 0.875 };

        for (uint i = 0; i < count; ++i)
        {
            GpuLight light = ClusterLightAt(LightBuffer, LightIndexList, cluster, i);
            float3 lightAccum = float3(0.0, 0.0, 0.0);

            [unroll]
            for (int s = 0; s < 4; ++s)
            {
                float subDist = zNearSlice + dl * subOffsets[s];
                float3 subWorldPos = FogCameraPos.xyz
                                   + (ratioX * subDist) * FogCameraRight.xyz
                                   + (ratioY * subDist) * FogCameraUp.xyz
                                   + (subDist) * CameraForward.xyz;
                float3 subToVoxel = subWorldPos - FogCameraPos.xyz;
                float3 subV = subToVoxel / max(length(subToVoxel), 1e-4);

                float3 L;
                float3 radiance;
                if (ClusterLightRadiance(light, subWorldPos, L, radiance))
                {
                    float cosTheta = dot(L, subV);
                    // Artistic phase function: blend Henyey-Greenstein forward Mie lobe with
                    // an isotropic baseline (65%) so that headlight beams are clearly visible
                    // cutting through the air from any viewing angle (behind, above, side-on):
                    float hg = HenyeyGreenstein(cosTheta, mieG);
                    float phase = lerp(hg, isotropic, 0.65);
                    lightAccum += radiance * phase;
                }
            }

            punctualInscatter += (lightAccum * 0.25) * lightBoost;
        }

        inscatterRadiance += punctualInscatter;
    }

    // Output: rgb = in-scattered radiance * scattering coefficient, a = extinction coefficient
    VolumeScatterRW[id] = float4(inscatterRadiance * density, density);
}

// The global light the ground receives, per frame -- shared by terrain_ps and road_ps.
//
// TheSuperHackers @feature andytraber 13/09/2026 COLOR0.rgb used to be the finished light, baked per
// vertex on the CPU -- and baked only when something asked: at the four nominal time-of-day
// boundaries, and for whatever strip the camera scrolled in or a building deformed, each with the
// light of that moment. Under the day-night cycle that is a pop four times a cycle plus seams
// wherever the camera had been. COLOR0.rgb now carries the smoothed heightfield NORMAL the bake used
// (xy biased into [0,1], z as is -- a heightfield normal never points down), and the lighting the
// bake did is done here, with the light of this frame.
//
// Roads take the same normal of the cell under them and the same function, for the reason every
// other road/terrain pair in these shaders is shared: a road is a decal on the ground, and two
// surfaces that meet along a kerb have to be lit by one expression or the kerb shows. Roads baked
// their own colour until this was shared, and the re-bake never reached the GPU (drawRoads only
// re-uploads when a segment's visibility changes), so they kept the light of map load all cycle.
//
// Bound at c7..c15 by DX8Wrapper for both passes (m_terrainLighting, published per frame in
// HeightMapRenderObjClass::Render).

#ifndef TERRAIN_LIGHTING_HLSLI
#define TERRAIN_LIGHTING_HLSLI

float4 TerrainAmbient     : register(c7);   // rgb = ambient of global light 0 (the only one the bake took ambient from)
float4 TerrainLightDir0   : register(c8);   // xyz = the negated light ray, NOT normalised -- the bake did not normalise it either
float4 TerrainLightDir1   : register(c9);
float4 TerrainLightDir2   : register(c10);
float4 TerrainLightColor0 : register(c11);  // rgb = diffuse; zero for a light past m_numGlobalLights
float4 TerrainLightColor1 : register(c12);
float4 TerrainLightColor2 : register(c13);
float4 TerrainDepthFade   : register(c14);  // rgb = per-channel fade under water, w = 1 when the map uses it
float4 TerrainWater       : register(c15);  // x = water plane height

// BaseHeightMapRenderObjClass::doTheLight, moved here. Same terms in the same order: ambient, plus
// each global light's diffuse times a clamped N.L, the sum clamped per channel, then the underwater
// fade. Interpolating the normal rather than the finished colour is the one difference, and it is
// smoother, not brighter or darker.
float3 terrainLight(float3 encodedNormal, float3 worldPos)
{
    float3 N = normalize(float3(encodedNormal.rg * 2.0 - 1.0, encodedNormal.b));

    float3 shade = TerrainAmbient.rgb;
    shade += TerrainLightColor0.rgb * saturate(dot(N, TerrainLightDir0.xyz));
    shade += TerrainLightColor1.rgb * saturate(dot(N, TerrainLightDir1.xyz));
    shade += TerrainLightColor2.rgb * saturate(dot(N, TerrainLightDir2.xyz));
    shade = saturate(shade);

    // The bake's own formula, oddity and all: (1.4 - z) / waterZ.
    if (TerrainDepthFade.w > 0.5 && worldPos.z <= TerrainWater.x)
    {
        float depthScale = (1.4 - worldPos.z) / max(TerrainWater.x, 1e-3);
        shade *= 1.0 - depthScale * (1.0 - TerrainDepthFade.rgb);
    }
    return shade;
}

#endif // TERRAIN_LIGHTING_HLSLI

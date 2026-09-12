// Unit pixel shader (single texture).
//
// Samples the base texture and modulates it by the per-vertex lit colour from
// the vertex shader. This reproduces the fixed-function "texture * diffuse"
// output. Multi-texture passes use unit_detail_ps instead, so this shader never
// samples a stage it has no texture for.
//
// The one light this shader computes for itself is the clustered local-light term (C5.2 of
// the clustered lighting plan), and it is here rather than in unit_vs for the reason
// the feature exists: a point light beside a tank has to light the near side harder than
// the far side, which a per-vertex sum cannot express. The sun stays in the vertex shader
// and arrives in the interpolated colour exactly as before. Switched off by default, and
// with it off not one instruction below touches the result.

#include "shadermodel.hlsli"

#include "constants.hlsli"
#include "shadow.hlsli"
#include "alphatest.hlsli"
#include "clustered.hlsli"

DECLARE_SAMPLER(BaseSampler, 0);

// x = 1 when a base texture is bound, 0 for an untextured (diffuse-only) pass, which
//     the fixed-function pipeline draws as the lit colour alone (stage 0 SELECTARG2).
// y = material diffuse alpha (stealth / translucency opacity).
// z = 1 for lit meshes, which take their opacity from the material as the
//     fixed-function pipeline did; 0 for pre-lit meshes, which keep the genuine
//     per-vertex alpha. y=z=1 forces the diffuse alpha to 1, for a stage 0 that
//     does not source the diffuse alpha at all.
// w = 1 when stage 0's alpha combine uses the texture alpha, 0 when it does not.
float4 TexCtl : register(c1);

DECLARE_SAMPLER_2D(ShadowMap, 5);      // directional shadow map (R32F depth)
float4 ShadowParams : register(c8);    // x = ground depth bias, y = shadow strength (0 = off)
// y = the depth bias for this mesh. The vertex shader has already lifted the lookup off
// the surface along its normal (see unit_vs), so what is left here is numerical slack
// rather than the whole depth a surface gains across a texel -- a small fraction of
// ShadowParams.x, which is what the terrain, having no normal to offset along, still has
// to use. Meshes that carry no normal are fed ShadowParams.x here instead, by the wrapper.
float4 ShadowMeshParams : register(c9);

// Cloud shadow, matching terrain_ps exactly -- deliberately, and this is the whole point
// of the change: a shadow that sweeps the ground has to sweep whatever is standing on it.
// Until now it stopped at the terrain, so a tank sat fully lit inside a shadow crossing
// the field around it.
//
// The projection is straight down from the pixel's ground-plane position, the same as the
// terrain's, which is what makes a unit and the ground under it agree. Tracing back along
// the sun instead would be more correct for a tall wall and would put the unit out of step
// with the ground it stands on, which reads far worse than the error it fixes.
DECLARE_SAMPLER(CloudSampler, 2);
float4 CloudScroll : register(c10);   // xy = layer A drift, zw = layer B (world units)
float4 CloudCtl    : register(c11);   // x = cloud layer on, y = shade strength

float3 cloudShade(float3 cloudPos)
{
    float2 uvA = (cloudPos.xy + CloudScroll.xy) / CLOUD_PERIOD_A;
    float2 uvB = (cloudPos.xy + CloudScroll.zw) / CLOUD_PERIOD_B;
    // The field stores brightness so the fixed-function path can multiply by it directly;
    // coverage is its complement.
    float a = 1.0 - SAMPLE_2D(CloudSampler, uvA).r;
    float b = 1.0 - SAMPLE_2D(CloudSampler, uvB).r;
    float coverage = 1.0 - (1.0 - a) * (1.0 - b);
    float lit = 1.0 - coverage * CloudCtl.y * CloudCtl.x * cloudPos.z;
    return lerp(CLOUD_SHADE_TINT, float3(1.0, 1.0, 1.0), lit);
}


DECLARE_SAMPLER(SceneDepth, 7);    // camera-view depth from the SSR prepass (R32F)
// x = 1 when this draw is an airborne sprite that may fade against the scene (see
//     DX8Wrapper::m_softParticles -- ground decals reach this shader too and must not),
// y = the view-space distance over which a sprite fades out as it approaches what is
//     behind it, z/w = the two projection terms that turn a clip depth into a distance.
float4 SoftCtl : register(c12);

// The clustered light path's three buffers (C5.2), at the absolute slots clustergrid.hlsli
// assigns them -- above the eight texture stages, bound once per frame rather than per
// draw by W3DShaderManager::bindClusteredLightBuffers. No sampler for any of them: they
// are read with Load()/operator[], which wants the t slot and nothing else.
//
// They cost no c register. Everything the lookup is parameterised by lives in b1, which is
// what made this stage affordable at all -- this shader was already at c1, c8-c12 and c28
// out of GFX_PS_CONSTANTS' 32, and unit_detail_ps, which shares the interpolant signature,
// additionally holds c2-c7 for its stage-1 combine.
StructuredBuffer<GpuLight> LightBuffer    : register(t8);
Buffer<uint>               ClusterGrid    : register(t9);
Buffer<uint>               LightIndexList : register(t10);

// The prepass writes camera-view z/w straight into the red channel of an R32F target, so
// this is a swizzle rather than arithmetic. It used to unpack a 255-weighted RGB8 split;
// the depth pass stopped packing when the shadow map moved to R32F and this target
// followed it there -- the same helper water_ps carries.
float unpackSceneDepth(float4 rgba)
{
    return rgba.r;
}

// Clip depth back to a view distance. Right-handed projection here, so clip.w = -viewZ;
// the abs() keeps it valid under a left-handed one too. Same expression as water_ps.
float softViewDepth(float ndcZ)
{
    return abs(SoftCtl.w / (SoftCtl.z + ndcZ));
}

// How much of this sprite survives where it meets whatever is behind it. A camera-facing
// quad cuts a hard straight line into the ground it intersects; fading it out over the
// last few world units before that contact is the whole difference between a sprite and
// something that looks like it has volume.
float softParticleFade(float4 screenPos)
{
    if (SoftCtl.x < 0.5)
        return 1.0;

    // Clip -> screen UV, Y flipped for the texture. Same expression unit_pbr_ps uses to
    // read this very map, deliberately: two shaders sampling one target must agree.
    float2 uv = (screenPos.xy / screenPos.w) * float2(0.5, -0.5) + 0.5;

    float sceneZ  = softViewDepth(unpackSceneDepth(SAMPLE_2D(SceneDepth, uv)));
    float spriteZ = softViewDepth(screenPos.z / screenPos.w);

    // The prepass excludes water and anything else that did not write depth, which comes
    // back as the far plane -- a huge sceneZ, so the fade is simply 1 and the sprite is
    // left alone. That is the right answer rather than a special case.
    return saturate((sceneZ - spriteZ) / max(SoftCtl.y, 0.001));
}

struct PS_INPUT
{
    float4 position  : VS_POSITION;
    // TEXCOORD4, not COLOR0: a ps_3_0 COLOR interpolator clamps to [0,1] and the lit colour
    // may exceed it now that the scene target is floating point. See unit_vs.
    float4 color     : TEXCOORD4;
    float2 texcoord  : TEXCOORD0;
    float2 texcoord1 : TEXCOORD1;   // unused here; keeps the signature matching the VS
    float4 lightPos  : TEXCOORD2;   // position in the sun's clip space
// The last two members are in the vertex shader's declaration order under model 4 and in
// this shader's own under model 3, which is the same set either way and a different
// register assignment. See PS_INPUT_POSITION in shadermodel.hlsli for why that matters:
// model 4 links the two stages by register as well as by semantic, and unit_vs writes
// TEXCOORD3 before TEXCOORD5. The model 3 branch is character for character what this
// struct has always been, so the .pso does not move.
    float3 cloudPos  : TEXCOORD3;  // xy = ground-plane position, z = receives sun
    float4 screenPos : TEXCOORD5;
    // C5.2. Appended, in the vertex shaders' declaration order, because model 5 links the
    // stages by register as well as by semantic -- inserting either of these anywhere
    // earlier renumbers screenPos and stops this shader linking to any of the three unit
    // vertex shaders, silently and without a compile error on either half.
    float3 worldPos  : TEXCOORD6;
    float4 worldNrm  : TEXCOORD7;  // xyz = world normal, w = takes clustered local light
};

// The clustered local lights reaching this pixel, as a light colour to be multiplied by
// the surface's own -- exactly what input.color.rgb already is.
//
// **NO PI, AND THAT IS THE WHOLE POINT OF THE SPLIT.** ClusteredLightingDiffuse is
// deliberately in the engine's non-radiometric convention: unit_vs computes
// MatDiffuse * saturate(dot(N, L)) with no constants in it, so "a white surface fully
// facing a light of colour C renders as C", and a local light dropped in here has to land
// in the same convention or it reads PI times brighter than the sun next to it.
// unit_pbr_ps reaches the same place by the other route -- a Lambert BRDF is albedo/PI, so
// it multiplies its light colour by PI first (LIGHT_IRRADIANCE) and the PI cancels. Two
// conventions, one result. This project has twice been bitten by a constant living in two
// spaces at once; harmonising these two would be the third time.
//
// **THE GATE IS THE CALLER'S, NOT THIS FUNCTION'S, AND THAT IS DELIBERATE.** Returning
// zero from here and letting the caller add it unconditionally would leave an add of zero
// in every frame the feature is switched off, and "0 differing pixels with the toggle off"
// has to mean there is no arithmetic left behind to be rounded differently -- not that the
// arithmetic happens to be a no-op. unit_pbr_ps structures its own block the same way.
//
// Both halves of that gate are the caller's: the frame constant AND worldNrm.w, the
// per-mesh "this geometry is lit at all" flag the vertex shaders write. Taking the normal
// as a float3 here is what forces that -- a float4 parameter whose .w this function
// ignored would read as though the flag were being honoured somewhere inside.
float3 clusteredLight(float4 clipPos, float3 worldPos, float3 worldNrm)
{
    // rsqrt(max(...)) and not normalize(): a zero normal reaches here whenever the gate is
    // ever loosened, and normalize() would answer NaN, which survives every operation after
    // it and takes the pixel with it. Same guard as Safe_Normalize in unit_vs.
    float3 n = worldNrm;
    float3 N = n * rsqrt(max(dot(n, n), 1e-12));

    // The view distance is SV_Position.w's reciprocal and nothing else. This engine's
    // projection is right-handed, so clip.w is the positive distance in front of the
    // camera and a pixel shader receives 1/w -- no interpolant, and no reconstruction out
    // of the projection's _33/_43. max() only guards the division; ClusterSliceOf clamps
    // the result to the grid at both ends anyway.
    uint cluster = ClusterIndexAt(clipPos.xy, ClusterViewDist(clipPos));
    return ClusteredLightingDiffuse(CLUSTER_BUFFERS_ARG, cluster, worldPos, N);
}

// Cast-shadow term. The filter is the shared one in shadow.hlsli, so a unit and the ground
// it stands on agree tap for tap about where a shadow falls and how far it softens -- the
// same shadow must not soften differently depending on which shader the mesh routed to.
//
// The projection and the receiver-plane fit are *not* done here. They need ddx/ddy, and
// main calls this inside dynamic flow control where the screen-space neighbours are
// unavailable, so the caller works them out first and passes them in. The filter itself is
// branch-safe (it samples with an explicit level throughout).
float shadowTerm(float3 ndc, float2 uv, float2 dzduv)
{
    // ShadowMeshParams.y, not ShadowParams.x: the vertex shader has already lifted the
    // lookup off the surface along its normal (see unit_vs), so what is left here is
    // numerical slack rather than a slope allowance -- and with the plane fit covering the
    // slope across the kernel, it stays slack however wide the kernel gets.
    float lit = shadowFilter16(SAMPLER_2D_ARG(ShadowMap), uv, ndc.z, ShadowParams.z,
                               ShadowParams.w, dzduv, ShadowMeshParams.y);
    return saturate(lerp(1.0, lit, ShadowParams.y));
}

float4 main(PS_INPUT input) : PS_TARGET
{
    float4 baseColor = SAMPLE_2D(BaseSampler, input.texcoord);
    // Untextured pass: fold the texture out (white) so only the lit colour remains.
    baseColor = lerp(float4(1.0, 1.0, 1.0, 1.0), baseColor, TexCtl.x);

    // Alpha mirrors stage 0's fixed-function alpha combine. Lit meshes source the
    // diffuse alpha from the material (that is where stealth translucency lives);
    // pre-lit meshes keep their vertex alpha. Either factor folds to 1 when the
    // stage does not use it, so a texture-only alpha stays exactly the texture's.
    float diffAlpha = lerp(input.color.a, TexCtl.y, TexCtl.z);
    float texAlpha  = lerp(1.0, baseColor.a, TexCtl.w);

    // The lit colour arriving from the vertex shader, plus whatever local lights reach
    // this pixel (C5.2).
    //
    // **THE LOCAL LIGHT IS ADDED TO THE LIGHT, NOT TO THE PIXEL.** input.color.rgb is the
    // light this surface receives -- emissive + material ambient * scene ambient + the four
    // directionals -- and baseColor is its albedo, so a term added here is multiplied by
    // the texture exactly as the sun is. Adding it to the product instead would light a
    // black tank tread as brightly as white paint, which is not a lighting model, it is a
    // fog. This is the same place unit_pbr_ps adds it (into Lo, before anything encodes)
    // reached through a shader that has no BRDF.
    //
    // TWO CONDITIONS, AND BOTH ARE PER-DRAW CONSTANTS, so the branch is coherent across
    // the whole draw call and predicts perfectly -- the same argument the cast-shadow
    // branch below is written on.
    //   * ClusteredLightingEnabled() is b1's ClusterLimits.z, which since C7 says "the
    //     cluster buffers exist". With it zero -- which is also what an unwritten b1 block
    //     reads -- not one instruction here touches the colour: no multiply by 1, no add
    //     of 0, nothing left behind to round differently. That is what made every stage
    //     before C7 measurable at 0 differing pixels, and it is now what makes a device
    //     that could not create the buffers degrade to sun-only rather than to garbage.
    //   * worldNrm.w is the vertex shaders' "this mesh is lit at all" flag. It is 1 only
    //     where the fixed-function lighting equation actually ran, so texture-only overlay
    //     passes (which composite over an already-lit base and would be lit twice) and
    //     effect geometry (which emits rather than reflects, and carries a zero normal
    //     besides) are excluded -- and unit_prelit_vs writes 0 outright, having no normal
    //     to take an N.L against. Testing it here rather than relying on the zero normal
    //     multiplying the sum out is not fussiness: the zero normal makes the *result*
    //     zero only after the loop has already run over every light in the cluster, and
    //     effect geometry is the most overdrawn thing in the frame.
    float3 litColor = input.color.rgb;
    [branch] if (ClusteredLightingEnabled() && input.worldNrm.w > 0.5)
    {
        litColor += clusteredLight(input.position, input.worldPos, input.worldNrm.xyz);
    }

    // Darken toward a floor rather than to black: the lit colour from the vertex shader
    // is ambient and direct light already summed, so there is no direct term left to
    // remove on its own. SHADOW_MIN comes from constants.hlsli, the same value the terrain
    // reads, so a unit and its own cast shadow on the ground sit at the same brightness.
    //
    // The clustered term is inside that darkening, and inside the cloud shade below, which
    // is deliberate and is a choice against physics: a muzzle flash is not switched off by
    // the sun's shadow map. unit_pbr_ps carries the full argument for accepting it (the
    // error is bounded -- SHADOW_MIN = 0.51, so 51% rather than 100% for a local light
    // standing in a cast shadow -- and exempting one term means adding it in a second
    // space, in the shader whose two known bugs were both a value used in the wrong one); the
    // two shaders must reach the same answer or a tank half in shadow would be lit
    // differently depending on whether it happened to have an ORM map.
    float3 rgb = baseColor.rgb * litColor;

    // Skipped outright for geometry the sun does not light: effect geometry, and
    // texture-only overlay passes. Both already fed cloudPos.z = 0, so both terms
    // already resolved to "no change" -- but resolving to no change still costs the
    // eighteen texture fetches it takes to get there (sixteen for the PCF kernel, two
    // for the cloud layers), and effects are the most overdrawn thing in the frame.
    //
    // Measured on chinooks.rep when effect geometry first moved onto this shader:
    // median frame time went from 33.3 ms -- the replay's 30 Hz cap, i.e. the renderer
    // finishing early and waiting -- to 40.3, which is the renderer no longer keeping
    // up. Smoke and beam sprites cover the screen many times over, and they were paying
    // a shadow lookup per pixel per layer for a result fixed at 1.
    //
    // [branch] rather than letting the compiler flatten it: the condition is constant
    // across a whole draw call, so the branch is coherent and predicts perfectly, which
    // is the case dynamic flow control in ps_3_0 exists for.
    //
    // Only the cast shadow is branched around. The cloud is two taps against the sixteen
    // here, and it samples a mipped texture -- inside dynamic flow control it would need
    // an explicit level too, and unlike the shadow map, forcing one on the cloud would be
    // a real change to how it filters. Not worth it for an eighth of the saving; it
    // already multiplies out to 1 through cloudPos.z.
    // The projection and the receiver-plane fit sit outside the branch and are therefore
    // paid by every pixel, shadowed or not: the fit needs ddx/ddy, which dynamic flow
    // control makes unavailable. That is about a dozen ALU against the sixteen texture
    // fetches the branch still skips, so it costs a small fraction of what the branch was
    // put here to save.
    float3 shNdc  = shadowNdc(input.lightPos);
    float2 shUv   = shadowUv(shNdc);
    float2 shGrad = shadowReceiverGradient(shUv, shNdc.z);

    [branch] if (input.cloudPos.z > 0.5)
    {
        rgb *= lerp(SHADOW_MIN, 1.0, shadowTerm(shNdc, shUv, shGrad));
    }
    rgb *= cloudShade(input.cloudPos);
    // Soft-particle fade multiplies the alpha, so it works for the alpha-blended case
    // directly and for additive draws too -- an additive sprite carries its coverage in
    // the colour, but the wrapper feeds additive effect draws a diffuse alpha of 1, so
    // scaling alpha alone would not dim them. Scale both; for a normal blend the colour
    // scale is harmless because the alpha already governs the result.
    float soft = softParticleFade(input.screenPos);

    // The alpha test, against the alpha this shader is about to write rather than
    // against the sampled texture alpha -- the hardware stage tests what the shader
    // returned, so anything else is a different test, and the two would disagree
    // exactly at the cutout edge where it shows. See alphatest.hlsli.
    float outAlpha = texAlpha * diffAlpha * soft;
    AlphaTest(outAlpha);
    return float4(rgb * soft, outAlpha);
}

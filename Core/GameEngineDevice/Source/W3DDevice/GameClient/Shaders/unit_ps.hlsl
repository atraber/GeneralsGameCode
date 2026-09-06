// Unit pixel shader (single texture).
//
// Samples the base texture and modulates it by the per-vertex lit colour from
// the vertex shader. This reproduces the fixed-function "texture * diffuse"
// output. Multi-texture passes use unit_detail_ps instead, so this shader never
// samples a stage it has no texture for.

#include "shadermodel.hlsli"

#include "constants.hlsli"
#include "shadow.hlsli"
#include "alphatest.hlsli"

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

DECLARE_SAMPLER_2D(ShadowMap, 5);      // directional shadow map (packed depth)
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


DECLARE_SAMPLER(SceneDepth, 7);    // camera-view packed depth from the SSR prepass
// x = 1 when this draw is an airborne sprite that may fade against the scene (see
//     DX8Wrapper::m_softParticles -- ground decals reach this shader too and must not),
// y = the view-space distance over which a sprite fades out as it approaches what is
//     behind it, z/w = the two projection terms that turn a clip depth into a distance.
float4 SoftCtl : register(c12);

// Unpack the RGB-packed depth the prepass writes. Weights are 255, matching
// shadowdepth_ps's pack -- the same helper water_ps carries.
float unpackSceneDepth(float4 rgba)
{
    return dot(rgba.xyz, float3(1.0, 1.0 / 255.0, 1.0 / (255.0 * 255.0)));
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
};

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

    // Darken toward a floor rather than to black: the lit colour from the vertex shader
    // is ambient and direct light already summed, so there is no direct term left to
    // remove on its own. SHADOW_MIN comes from constants.hlsli, the same value the terrain
    // reads, so a unit and its own cast shadow on the ground sit at the same brightness.
    float3 rgb = baseColor.rgb * input.color.rgb;

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

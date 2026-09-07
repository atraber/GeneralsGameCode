// Unit pixel shader (base + detail texture).
//
// Reproduces the fixed-function two-stage texture pipeline for a mesh pass:
//   stage 0 : base texture modulated by the per-vertex lit colour
//   stage 1 : detail texture combined with the stage 0 result
// so that a mesh whose passes are partly multi-textured can be drawn entirely by
// the shader. Splitting a mesh across the shader and the fixed-function pipeline
// gives its passes slightly different depth, making coincident passes z-fight.
//
// The stage 1 expression is driven by constants rather than being hard-coded: each
// argument carries a source selector and the operation is a one-hot weight, so the
// shader evaluates what the fixed-function stage would. Assuming an operand (for
// instance treating every MODULATE as "stage 0 result times detail" when the stage
// actually modulates the texture by the diffuse) double-darkens the pass.
//
// Bound only when a detail texture is present -- the single-texture shader (unit_ps)
// never samples stage 1, since sampling a stage with no texture bound is undefined.
//
// It shares unit_vs's interpolant signature with unit_ps, so it also carries the clustered
// local-light term (C5.2 of the clustered lighting plan) -- and it has to, or a mesh
// would be lit by a nearby lamp on its single-texture passes and not on its two-texture
// ones. Those are usually different passes of the SAME mesh: the routing decision is
// per-pass (a detail texture is present or it is not), so a difference here is a difference
// within one tank. See the long note in unit_ps; the arithmetic is deliberately identical.

#include "shadermodel.hlsli"

#include "constants.hlsli"
#include "shadow.hlsli"
#include "alphatest.hlsli"
#include "clustered.hlsli"

DECLARE_SAMPLER(BaseSampler, 0);
DECLARE_SAMPLER(DetailSampler, 1);

// x = 1 when a base texture is bound, 0 for an untextured (diffuse-only) pass.
// y = material diffuse alpha (stealth / translucency opacity), z = 1 for lit meshes,
// which take opacity from the material as the fixed-function pipeline did. y=z=1
// forces the diffuse alpha to 1 when stage 0 does not source it at all.
// w = 1 when stage 0's alpha combine uses the texture alpha, 0 when it does not.
float4 TexCtl : register(c1);

// Source selectors: xyz = (texture, current, diffuse) -- exactly one component is 1.
// Stage1CArg1.w carries the colour scale: 1 (MODULATE), 2 (2X), 4 (4X). It rides here
// because the wrapper only has pixel constant registers 0..7 to give out.
float4 Stage1CArg1 : register(c2);
float4 Stage1CArg2 : register(c3);
// Operation weights: (modulate, add, select arg1, select arg2) -- one-hot.
float4 Stage1COp   : register(c4);
float4 Stage1AArg1 : register(c5);
float4 Stage1AArg2 : register(c6);
float4 Stage1AOp   : register(c7);

DECLARE_SAMPLER_2D(ShadowMap, 5);      // directional shadow map (packed depth)
float4 ShadowParams : register(c8);    // x = ground depth bias, y = shadow strength (0 = off)
// y = this mesh's depth bias, small because unit_vs has already lifted the lookup off the
// surface along its normal. See the note in unit_ps.
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


// Cast-shadow term, matching unit_ps / terrain_ps through the shared filter in
// shadow.hlsli. As in unit_ps, the projection and the receiver-plane fit are the caller's
// job: they need ddx/ddy, and main calls this inside dynamic flow control.
float shadowTerm(float3 ndc, float2 uv, float2 dzduv)
{
    float lit = shadowFilter16(SAMPLER_2D_ARG(ShadowMap), uv, ndc.z, ShadowParams.z,
                               ShadowParams.w, dzduv, ShadowMeshParams.y);
    return saturate(lerp(1.0, lit, ShadowParams.y));
}

DECLARE_SAMPLER(SceneDepth, 7);    // camera-view packed depth from the SSR prepass
// x = 1 when this draw is an airborne sprite that may fade against the scene (see
//     DX8Wrapper::m_softParticles -- ground decals reach this shader too and must not),
// y = the view-space distance over which a sprite fades out as it approaches what is
//     behind it, z/w = the two projection terms that turn a clip depth into a distance.
float4 SoftCtl : register(c12);

// The clustered light path's three buffers (C5.2), at the absolute slots clustergrid.hlsli
// assigns them -- above the eight texture stages, bound once per frame rather than per draw
// by W3DShaderManager::bindClusteredLightBuffers. No sampler for any of them: they are read
// with Load()/operator[], which wants the t slot and nothing else.
//
// They cost no c register, which is what makes this shader able to take the feature at all:
// it holds c1 through c12 and c28 out of GFX_PS_CONSTANTS' 32, six of them (c2-c7) being
// the stage-1 combine that unit_ps does not have. Everything the cluster lookup is
// parameterised by lives in b1.
StructuredBuffer<GpuLight> LightBuffer    : register(t8);
Buffer<uint>               ClusterGrid    : register(t9);
Buffer<uint>               LightIndexList : register(t10);

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
    // DIAGNOSTIC: force off, to separate the fade maths from the signature change.
    return 1.0;
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
    float2 texcoord  : TEXCOORD0;   // stage 0 coordinates
    float2 texcoord1 : TEXCOORD1;   // stage 1 coordinates (may be generated)
    float4 lightPos  : TEXCOORD2;   // position in the sun's clip space
// The last two members are in the vertex shader's declaration order under model 4 and in
// this shader's own under model 3, which is the same set either way and a different
// register assignment. See PS_INPUT_POSITION in shadermodel.hlsli for why that matters:
// model 4 links the two stages by register as well as by semantic, and unit_vs writes
// TEXCOORD3 before TEXCOORD5. The model 3 branch is character for character what this
// struct has always been, so the .pso does not move.
    float3 cloudPos  : TEXCOORD3;  // xy = ground-plane position, z = receives sun
    float4 screenPos : TEXCOORD5;
    // C5.2. Appended, in the vertex shaders' declaration order, and character for character
    // what unit_ps declares -- the two share all three unit vertex shaders, so a difference
    // here is a difference in which register each interpolant lands in.
    //
    // MEASURED, not assumed. Model 5 links the two stages by semantic AND by register, and
    // fxc numbers a signature by packing declarations in order (which is why TEXCOORD0 and
    // TEXCOORD1 share register 2 as .xy and .zw). Before this shader declared them, its
    // input signature stopped at register 5 while all three vertex shaders wrote through
    // register 7: that links -- the engine's own Signatures_Link, and D3D11, require the
    // pixel shader's inputs to be *found* in the vertex shader's outputs, not to exhaust
    // them, and registers 0-5 matched exactly. So this was never the silent-black-frame
    // case the SM4 migration hit; it was simply a shader not being given the lights.
    float3 worldPos  : TEXCOORD6;
    float4 worldNrm  : TEXCOORD7;  // xyz = world normal, w = takes clustered local light
};

// The clustered local lights reaching this pixel, as a light colour to be multiplied by the
// surface's own. Character for character unit_ps's clusteredLight, and it has to be: two
// passes of one mesh route to the two shaders by whether a detail texture is bound, so any
// difference between these two functions is a seam down the middle of a tank.
//
// **NO PI, AND THAT IS THE WHOLE POINT OF THE SPLIT.** ClusteredLightingDiffuse is
// deliberately in the engine's non-radiometric convention: unit_vs computes
// MatDiffuse * saturate(dot(N, L)) with no constants in it, so "a white surface fully
// facing a light of colour C renders as C", and a local light dropped in here has to land
// in the same convention or it reads PI times brighter than the sun next to it. unit_pbr_ps
// reaches the same place by the other route -- a Lambert BRDF is albedo/PI, so it
// multiplies its light colour by PI first (LIGHT_IRRADIANCE) and the PI cancels. Two
// conventions, one result. This project has twice been bitten by a constant living in two
// spaces at once; harmonising these two would be the third time.
float3 clusteredLight(float4 clipPos, float3 worldPos, float3 worldNrm)
{
    // rsqrt(max(...)) and not normalize(): a zero normal reaches here whenever the gate is
    // ever loosened, and normalize() would answer NaN, which survives every operation after
    // it and takes the pixel with it. Same guard as Safe_Normalize in unit_vs.
    float3 n = worldNrm;
    float3 N = n * rsqrt(max(dot(n, n), 1e-12));

    // The view distance is SV_Position.w's reciprocal and nothing else. This engine's
    // projection is right-handed, so clip.w is the positive distance in front of the camera
    // and a pixel shader receives 1/w -- no interpolant, and no reconstruction out of the
    // projection's _33/_43. max() only guards the division; ClusterSliceOf clamps the
    // result to the grid at both ends anyway.
    uint cluster = ClusterIndexAt(clipPos.xy, 1.0 / max(clipPos.w, 1e-8));
    return ClusteredLightingDiffuse(CLUSTER_BUFFERS_ARG, cluster, worldPos, N);
}

float3 PickRGB(float4 sel, float3 tex, float3 cur, float3 dif)
{
    return sel.x * tex + sel.y * cur + sel.z * dif;
}

float PickA(float4 sel, float tex, float cur, float dif)
{
    return sel.x * tex + sel.y * cur + sel.z * dif;
}

float4 main(PS_INPUT input) : PS_TARGET
{
    float4 baseColor = SAMPLE_2D(BaseSampler, input.texcoord);
    baseColor = lerp(float4(1.0, 1.0, 1.0, 1.0), baseColor, TexCtl.x);

    // The lit colour arriving from the vertex shader, plus whatever local lights reach this
    // pixel (C5.2). Identical in structure to unit_ps -- see the long note there for why
    // the term is added to the *light* rather than to the finished pixel, why both halves
    // of the gate are tested here, and why with the toggle off not one instruction below
    // touches the result.
    //
    // **IT REPLACES input.color.rgb EVERYWHERE THE COLOUR IS A LIGHT, INCLUDING THE STAGE 1
    // DIFFUSE SOURCE.** That third argument to PickRGB is the fixed-function DIFFUSE
    // register, which is this same lit vertex colour: a stage that combines its texture
    // against the diffuse is asking for the light on this surface, and a stage that has
    // been handed the pre-clustered one would draw the lamp's light on the base pass and
    // not on the detail pass laid over it. Feeding both from one variable is what makes
    // that structural instead of a promise.
    float3 litColor = input.color.rgb;
    [branch] if (ClusteredLightingEnabled() && input.worldNrm.w > 0.5)
    {
        litColor += clusteredLight(input.position, input.worldPos, input.worldNrm.xyz);
    }

    // Stage 0 result: texture * diffuse, matching the fixed-function combine. Its alpha
    // mirrors that stage's alpha combine -- lit meshes source the diffuse alpha from the
    // material (where stealth translucency lives), and either factor folds to 1 when the
    // stage does not use it.
    float diffAlpha = lerp(input.color.a, TexCtl.y, TexCtl.z);
    float texAlpha  = lerp(1.0, baseColor.a, TexCtl.w);
    float4 current = float4(baseColor.rgb * litColor, texAlpha * diffAlpha);
    float4 detail  = SAMPLE_2D(DetailSampler, input.texcoord1);

    float3 c1 = PickRGB(Stage1CArg1, detail.rgb, current.rgb, litColor);
    float3 c2 = PickRGB(Stage1CArg2, detail.rgb, current.rgb, litColor);
    float3 rgb = Stage1COp.x * (c1 * c2)
               + Stage1COp.y * (c1 + c2)
               + Stage1COp.z * c1
               + Stage1COp.w * c2;
    rgb *= Stage1CArg1.w;

    float a1 = PickA(Stage1AArg1, detail.a, current.a, input.color.a);
    float a2 = PickA(Stage1AArg2, detail.a, current.a, input.color.a);
    float a = Stage1AOp.x * (a1 * a2)
            + Stage1AOp.y * (a1 + a2)
            + Stage1AOp.z * a1
            + Stage1AOp.w * a2;

    const float SHADOW_MIN = 0.35;
    // Skipped for geometry the sun does not light -- see the same branch in unit_ps for
    // why the eighteen fetches are worth branching around rather than multiplying by a
    // result that is already 1, and for why the plane fit has to sit outside the branch.
    float3 shNdc  = shadowNdc(input.lightPos);
    float2 shUv   = shadowUv(shNdc);
    float2 shGrad = shadowReceiverGradient(shUv, shNdc.z);

    [branch] if (input.cloudPos.z > 0.5)
    {
        rgb *= lerp(SHADOW_MIN, 1.0, shadowTerm(shNdc, shUv, shGrad));
    }
    rgb *= cloudShade(input.cloudPos);

    // Colour is left unclamped: on a floating-point scene target this saturate was the last
    // thing pinning a two-stage combine to display white, and an additive detail pass over a
    // bright base is exactly the kind of draw that should be allowed past it. Alpha keeps
    // its saturate -- it is coverage, not brightness, and a blend factor above 1 is
    // meaningless whatever the target can hold.
    //
    // See the note in unit_ps: both colour and alpha are scaled so the fade reaches
    // additive draws as well as blended ones.
    float soft = softParticleFade(input.screenPos);

    // See the note in unit_ps: tested against the alpha actually written, which here is
    // the second stage's alpha combine result rather than the base texture's.
    float outAlpha = saturate(a) * soft;
    AlphaTest(outAlpha);
    return float4(max(rgb, 0.0) * soft, outAlpha);
}

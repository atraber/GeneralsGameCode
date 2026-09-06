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

#include "shadermodel.hlsli"

#include "constants.hlsli"
#include "shadow.hlsli"
#include "alphatest.hlsli"

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
};

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

    // Stage 0 result: texture * diffuse, matching the fixed-function combine. Its alpha
    // mirrors that stage's alpha combine -- lit meshes source the diffuse alpha from the
    // material (where stealth translucency lives), and either factor folds to 1 when the
    // stage does not use it.
    float diffAlpha = lerp(input.color.a, TexCtl.y, TexCtl.z);
    float texAlpha  = lerp(1.0, baseColor.a, TexCtl.w);
    float4 current = float4(baseColor.rgb * input.color.rgb, texAlpha * diffAlpha);
    float4 detail  = SAMPLE_2D(DetailSampler, input.texcoord1);

    float3 c1 = PickRGB(Stage1CArg1, detail.rgb, current.rgb, input.color.rgb);
    float3 c2 = PickRGB(Stage1CArg2, detail.rgb, current.rgb, input.color.rgb);
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

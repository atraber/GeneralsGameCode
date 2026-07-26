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

sampler BaseSampler   : register(s0);
sampler DetailSampler : register(s1);

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

struct PS_INPUT
{
    float4 position  : POSITION;
    float4 color     : COLOR0;
    float2 texcoord  : TEXCOORD0;   // stage 0 coordinates
    float2 texcoord1 : TEXCOORD1;   // stage 1 coordinates (may be generated)
};

float3 PickRGB(float4 sel, float3 tex, float3 cur, float3 dif)
{
    return sel.x * tex + sel.y * cur + sel.z * dif;
}

float PickA(float4 sel, float tex, float cur, float dif)
{
    return sel.x * tex + sel.y * cur + sel.z * dif;
}

float4 main(PS_INPUT input) : COLOR
{
    float4 baseColor = tex2D(BaseSampler, input.texcoord);
    baseColor = lerp(float4(1.0, 1.0, 1.0, 1.0), baseColor, TexCtl.x);

    // Stage 0 result: texture * diffuse, matching the fixed-function combine. Its alpha
    // mirrors that stage's alpha combine -- lit meshes source the diffuse alpha from the
    // material (where stealth translucency lives), and either factor folds to 1 when the
    // stage does not use it.
    float diffAlpha = lerp(input.color.a, TexCtl.y, TexCtl.z);
    float texAlpha  = lerp(1.0, baseColor.a, TexCtl.w);
    float4 current = float4(baseColor.rgb * input.color.rgb, texAlpha * diffAlpha);
    float4 detail  = tex2D(DetailSampler, input.texcoord1);

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

    return float4(saturate(rgb), saturate(a));
}

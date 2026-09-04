// Legacy standing water combiner (Shader Model 3).
//
// Port of the second ps_1_1 shader W3DWater assembled at runtime with D3DXAssembleShader,
// used by drawTrapezoidWater on the same terms as waterriver_ps: the programmable water path
// declined, fixed-function vertex processing, four coordinate sets from the texture stages.
//
//   t0  water texture               t1  sparkle highlights
//   t2  the same highlights, tiled tighter
//   t3  the shroud -- black where fogged, and a white 1x1 stand-in when there is no shroud,
//       because setting stage 3 to no texture at all produced garbage on the hardware this
//       was written for
//
// The original:
//
//     mul r0, v0, t0          ; vertex colour and alpha into the base texture
//     mad r0.rgb, t1, t2, r0  ; blend sparkles and noise
//     mul r0.rgb, r0, t3      ; blend in black shroud
//
// saturate() is ps_1_1's per-instruction clamp; see the note in waterriver_ps.hlsl for why
// the last one is no longer free.

#include "shadermodel.hlsli"

DECLARE_SAMPLER_2D(WaterMap, 0);
DECLARE_SAMPLER_2D(SparkleMap, 1);
DECLARE_SAMPLER_2D(SparkleMap2, 2);
DECLARE_SAMPLER_2D(ShroudMap, 3);

struct PS_INPUT
{
    PS_INPUT_POSITION
    float4 color : COLOR0;
    float2 uv0   : TEXCOORD0;
    float2 uv1   : TEXCOORD1;
    float2 uv2   : TEXCOORD2;
    float2 uv3   : TEXCOORD3;
};

float4 main(PS_INPUT input) : PS_TARGET
{
    float4 water    = SAMPLE_2D(WaterMap, input.uv0);
    float4 sparkle  = SAMPLE_2D(SparkleMap, input.uv1);
    float4 sparkle2 = SAMPLE_2D(SparkleMap2, input.uv2);
    float4 shroud   = SAMPLE_2D(ShroudMap, input.uv3);

    float4 result = saturate(input.color * water);
    result.rgb = saturate(sparkle.rgb * sparkle2.rgb + result.rgb);
    result.rgb = saturate(result.rgb * shroud.rgb);
    return result;
}

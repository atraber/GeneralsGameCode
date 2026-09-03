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

sampler2D WaterMap    : register(s0);
sampler2D SparkleMap  : register(s1);
sampler2D SparkleMap2 : register(s2);
sampler2D ShroudMap   : register(s3);

struct PS_INPUT
{
    float4 color : COLOR0;
    float2 uv0   : TEXCOORD0;
    float2 uv1   : TEXCOORD1;
    float2 uv2   : TEXCOORD2;
    float2 uv3   : TEXCOORD3;
};

float4 main(PS_INPUT input) : COLOR
{
    float4 water    = tex2D(WaterMap,    input.uv0);
    float4 sparkle  = tex2D(SparkleMap,  input.uv1);
    float4 sparkle2 = tex2D(SparkleMap2, input.uv2);
    float4 shroud   = tex2D(ShroudMap,   input.uv3);

    float4 result = saturate(input.color * water);
    result.rgb = saturate(sparkle.rgb * sparkle2.rgb + result.rgb);
    result.rgb = saturate(result.rgb * shroud.rgb);
    return result;
}

// Legacy river water combiner (Shader Model 3).
//
// Port of the ps_1_1 shader W3DWater assembled at runtime with D3DXAssembleShader, used by
// drawRiverWater when the programmable water path is not taken -- that is, on additive-blend
// maps, and if water_vs/water_ps failed to load. The vertex side is still fixed function:
// D3D9 pairs fixed-function vertex processing with a Shader Model 3 pixel shader without
// complaint (it only refuses to mix shader model 3 with shader model 2 across a vs/ps pair),
// so the four coordinate sets below still arrive from the texture-generation state that
// drawRiverWater sets, including stage 2's camera-space position and its texture matrix.
//
//   t0  river texture                  t1  sparkle highlights
//   t2  noise, camera-space projected   t3  river bank alpha ramp
//   v0  vertex colour; v0.a is the water's own fade
//
// The original, instruction for instruction:
//
//     mul r0.rgb, v0, t0     ; blend vertex colour into the base water
//     mov r0.a, t0           ; keep vertex alpha from fading the base water
//     mul r1, t1, t2
//     add r1.rgb, r1, t3
//     mul r1.rgb, r1, v0.a
//     +mul r0.a, r0, t3      ; co-issued, so it reads r0.a from before the pair
//     add r0.rgb, r0, r1
//
// The saturates are not decoration. ps_1_1 clamps every instruction's result to [-1,1] and
// its output register to [0,1]; nothing here can go negative, so saturate() is that clamp.
// Two of them matter: t1*t2 + t3 does exceed 1 where a sparkle lands on the bank ramp, and
// the final sum exceeds 1 over bright water. The second used to be clamped for free by an
// 8-bit render target and is not any more -- the scene target is fp16 under HDR.

sampler2D RiverMap   : register(s0);
sampler2D SparkleMap : register(s1);
sampler2D NoiseMap   : register(s2);
sampler2D EdgeMap    : register(s3);

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
    float4 river   = tex2D(RiverMap,   input.uv0);
    float4 sparkle = tex2D(SparkleMap, input.uv1);
    float4 noise   = tex2D(NoiseMap,   input.uv2);
    float4 edge    = tex2D(EdgeMap,    input.uv3);

    float3 base  = saturate(input.color.rgb * river.rgb);
    float  alpha = saturate(river.a * edge.a);

    float3 detail = saturate(sparkle.rgb * noise.rgb);
    detail = saturate(detail + edge.rgb);
    detail = saturate(detail * input.color.a);

    return float4(saturate(base + detail), alpha);
}

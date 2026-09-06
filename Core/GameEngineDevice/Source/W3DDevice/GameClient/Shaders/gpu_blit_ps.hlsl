// Texture copy / blit pixel shader.
//
// Samples the source texture with either point or linear filtering for surface copying,
// format conversions, and resolves.

Texture2D src_tex : register(t0);
SamplerState src_sampler : register(s0);

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    return src_tex.Sample(src_sampler, texcoord);
}

// Terrain pixel shader.
//
// Base tile cross-blend (uv0/uv1) modulated by baked vertex lighting, then the
// cloud-shadow and noise-detail overlays multiplied on top (matching the old
// renderer's DESTCOLOR/ZERO multiplicative overlay passes).

sampler BaseSampler  : register(s0);
sampler CloudSampler : register(s2);
sampler NoiseSampler : register(s3);

float4 OverlayEnable : register(c0); // x = cloud layer on, y = noise layer on

struct PS_INPUT
{
    float4 position : POSITION;
    float4 color    : COLOR0;
    float2 uv0      : TEXCOORD0;
    float2 uv1      : TEXCOORD1;
    float2 cloudUV  : TEXCOORD2;
    float2 noiseUV  : TEXCOORD3;
};

float4 main(PS_INPUT input) : COLOR
{
    // Cross-blend the base tile (uv0) and neighbour tile (uv1).
    float4 tile0 = tex2D(BaseSampler, input.uv0);
    float4 tile1 = tex2D(BaseSampler, input.uv1);
    float  blend = tile1.a * input.color.a;
    float3 col   = lerp(tile0.rgb, tile1.rgb, blend) * input.color.rgb;

    // Multiplicative overlays, matching the fixed-function noise/cloud pass, which
    // blends over the terrain with SRCBLEND=DESTCOLOR / DESTBLEND=ZERO -- i.e. a plain
    // framebuffer * overlayTexture multiply at unit strength. (An earlier build scaled
    // the darkening up by a "strength" factor to make the overlays more visible, but
    // that pushed the terrain noticeably darker and cooler than the original; the
    // faithful match is a straight multiply.)
    float3 cloudTex = tex2D(CloudSampler, input.cloudUV).rgb;
    float3 noiseTex = tex2D(NoiseSampler, input.noiseUV).rgb;

    // Off layers lerp to white (no effect); safe with an unbound sampler and no
    // ps_2_0 dynamic branching.
    float3 cloud = lerp(float3(1.0, 1.0, 1.0), cloudTex, OverlayEnable.x);
    float3 noise = lerp(float3(1.0, 1.0, 1.0), noiseTex, OverlayEnable.y);

    return float4(col * cloud * noise, 1.0);
}

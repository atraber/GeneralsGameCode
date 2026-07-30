// Terrain pixel shader.
//
// Base tile cross-blend (uv0/uv1) modulated by baked vertex lighting, then the
// cloud-shadow and noise-detail overlays multiplied on top (matching the old
// renderer's DESTCOLOR/ZERO multiplicative overlay passes).

sampler BaseSampler  : register(s0);
sampler CloudSampler : register(s2);
sampler NoiseSampler : register(s3);
sampler ShadowMap    : register(s5);   // directional shadow map (packed depth)

float4 OverlayEnable : register(c0); // x = cloud layer on, y = noise layer on
float4 ShadowParams  : register(c1); // x = depth bias, y = shadow strength (0 = off)

struct PS_INPUT
{
    float4 position : POSITION;
    float4 color    : COLOR0;
    float2 uv0      : TEXCOORD0;
    float2 uv1      : TEXCOORD1;
    float2 cloudUV  : TEXCOORD2;
    float2 noiseUV  : TEXCOORD3;
    float4 lightPos : TEXCOORD4;
};

float unpackDepth(float4 rgba)
{
    // Weights are 255, matching shadowdepth_ps's pack -- see the note there.
    return dot(rgba.xyz, float3(1.0, 1.0 / 255.0, 1.0 / (255.0 * 255.0)));
}

// Cast-shadow term for the terrain. Branchless (ps_2_0 has no dynamic flow): the
// out-of-frustum case is folded in with a mask instead of an early-out. 2x2 PCF
// (3x3 overflows the ps_2_0 arithmetic-slot limit alongside the terrain blend).
float terrainShadow(float4 lightPos)
{
    // Guard the divide: a degenerate w yields inf, then NaN, and NaN survives every
    // operation after it -- the pixel is simply gone. Cheaper to be certain here.
    float3 ndc = lightPos.xyz / max(abs(lightPos.w), 1e-6);
    float2 uv  = ndc.xy * float2(0.5, -0.5) + 0.5;
    float inBounds = step(0.0, uv.x) * step(uv.x, 1.0) * step(0.0, uv.y) * step(uv.y, 1.0);

    // Bias arrives per frame (see unit_pbr_ps): the sun frustum is fitted to the camera,
    // so a texel's world size -- and with it the bias needed -- changes with the zoom.
    const float texel = ShadowParams.z;   // 1/SHADOW_MAP_SIZE, fed per frame
    float lit = 0.0;
    [unroll] for (int x = 0; x <= 1; ++x)
        [unroll] for (int y = 0; y <= 1; ++y) {
            float2 o = (float2(x, y) - 0.5) * texel;
            float stored = unpackDepth(tex2D(ShadowMap, uv + o));
            lit += (ndc.z - ShadowParams.x > stored) ? 0.0 : 1.0;
        }
    // Outside the sun frustum, or with shadowing off, everything is lit.
    return saturate(lerp(1.0, lit * 0.25, inBounds * ShadowParams.y));
}

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

    // Cast shadows: the terrain colour has its lighting baked in, so darken toward a
    // floor (ambient) rather than to black where occluded from the sun.
    const float SHADOW_MIN = 0.35;
    float shadow = terrainShadow(input.lightPos);
    col *= lerp(SHADOW_MIN, 1.0, shadow);

    return float4(col * cloud * noise, 1.0);
}

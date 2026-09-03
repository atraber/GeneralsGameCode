// Road pixel shader.
//
// Base road texture modulated by the baked vertex lighting, then the cloud-shadow and
// noise-detail overlays multiplied on top, then cast shadows -- the same composition
// terrain_ps performs, because a road has to darken exactly as the ground beside it does
// or a shadow crossing the verge shows a seam. The one difference is the alpha: a road is
// blended into the terrain with SRCALPHA/INVSRCALPHA, and both the texture alpha (the
// road's soft edges) and the vertex alpha (segment fade) have to reach that blend, where
// the terrain simply writes 1.

#include "shadermodel.hlsli"

#include "constants.hlsli"
#include "shadow.hlsli"

DECLARE_SAMPLER(BaseSampler, 0);
DECLARE_SAMPLER(CloudSampler, 2);
DECLARE_SAMPLER(NoiseSampler, 3);
DECLARE_SAMPLER_2D(ShadowMap, 5);   // directional shadow map (packed depth)

float4 OverlayEnable : register(c0); // x = cloud on, y = noise on, z = cloud shade strength
float4 ShadowParams  : register(c1); // x = depth bias, y = shadow strength (0 = off), z = texel


// Cloud shadow from the two drifting layers.
//
// The texture holds *coverage*, not brightness: 0 under clear sky, 1 under full cloud,
// and mostly 0. Two layers are combined as a union -- the probability that at least one
// deck is overhead -- which is what two real cloud layers do, and which keeps the field
// mostly clear instead of stacking into permanent gloom.
//
// The result darkens toward a tint rather than multiplying toward black. Shade is the
// loss of *direct sun*; the sky still lights the ground, and skylight is blue, so shaded
// ground goes darker and cooler rather than simply dimmer. Multiplying by the texture --
// what this did before -- drives everything toward black and reads as dirt on the lens.
// The tint itself lives in constants.hlsli: terrain, road and units all darken toward the
// same colour, or cloud crossing a tank shades it differently from the ground under it.

float3 cloudShade(float4 cloudUV, float enable, float strength)
{
    // The texture stores brightness so the fixed-function fallback can still multiply by
    // it; coverage is its complement.
    float a = 1.0 - SAMPLE_2D(CloudSampler, cloudUV.xy).r;
    float b = 1.0 - SAMPLE_2D(CloudSampler, cloudUV.zw).r;
    float coverage = 1.0 - (1.0 - a) * (1.0 - b);
    float lit = 1.0 - coverage * strength * enable;
    return lerp(CLOUD_SHADE_TINT, float3(1.0, 1.0, 1.0), lit);
}

// Deliberately identical to terrain_ps's cloudShade, for the same reason the shadow
// filter is: a road is a decal on the ground and must take exactly the ground's shade.
struct PS_INPUT
{
    float4 position : POSITION;
    float4 color    : COLOR0;
    float2 uv0      : TEXCOORD0;
    float4 cloudUV  : TEXCOORD2;   // xy = cloud layer A, zw = layer B
    float2 noiseUV  : TEXCOORD3;
    float4 lightPos : TEXCOORD4;
};

// Cast-shadow term. Deliberately identical to terrainShadow in terrain_ps: a road is a
// decal on the terrain, so the two have to agree tap for tap, or the shadow edge breaks
// where it crosses onto the tarmac. Sharing the filter through shadow.hlsli is what makes
// that structural rather than a promise -- the two agree because there is one filter, not
// because two copies were kept in step by hand.
float roadShadow(float4 lightPos)
{
    float3 ndc = shadowNdc(lightPos);
    float2 uv  = shadowUv(ndc);

    // Fitted here, in unbranched code -- see shadow.hlsli. A road carries no vertex normal
    // for the lookup to be lifted along, so like the terrain it depends on the plane fit
    // rather than on a normal offset to survive a kernel wider than a texel.
    float2 dzduv = shadowReceiverGradient(uv, ndc.z);

    float lit = shadowFilter16(SAMPLER_2D_ARG(ShadowMap), uv, ndc.z, ShadowParams.z,
                               ShadowParams.w, dzduv, ShadowParams.x);
    return saturate(lerp(1.0, lit, ShadowParams.y));
}

float4 main(PS_INPUT input) : PS_TARGET
{
    float4 base = SAMPLE_2D(BaseSampler, input.uv0);
    float3 col  = base.rgb * input.color.rgb;

    // Multiplicative overlays, as the fixed-function road pass applied them (stage 1 and
    // stage 2, both MODULATE against the running colour). Off layers lerp to white.

    float3 noiseTex = SAMPLE_2D(NoiseSampler, input.noiseUV).rgb;
    float3 cloud = cloudShade(input.cloudUV, OverlayEnable.x, OverlayEnable.z);
    float3 noise = lerp(float3(1.0, 1.0, 1.0), noiseTex, OverlayEnable.y);

    // Cast shadows: the road colour has its lighting baked in, so darken toward the same
    // ambient floor the terrain uses rather than to black. SHADOW_MIN is in
    // constants.hlsli, so every receiver darkens to the same place.
    float shadow = roadShadow(input.lightPos);
    col *= lerp(SHADOW_MIN, 1.0, shadow);

    return float4(col * cloud * noise, base.a * input.color.a);
}

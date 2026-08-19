// Road pixel shader.
//
// Base road texture modulated by the baked vertex lighting, then the cloud-shadow and
// noise-detail overlays multiplied on top, then cast shadows -- the same composition
// terrain_ps performs, because a road has to darken exactly as the ground beside it does
// or a shadow crossing the verge shows a seam. The one difference is the alpha: a road is
// blended into the terrain with SRCALPHA/INVSRCALPHA, and both the texture alpha (the
// road's soft edges) and the vertex alpha (segment fade) have to reach that blend, where
// the terrain simply writes 1.

#include "constants.hlsli"

sampler BaseSampler  : register(s0);
sampler CloudSampler : register(s2);
sampler NoiseSampler : register(s3);
sampler ShadowMap    : register(s5);   // directional shadow map (packed depth)

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
    float a = 1.0 - tex2D(CloudSampler, cloudUV.xy).r;
    float b = 1.0 - tex2D(CloudSampler, cloudUV.zw).r;
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

float unpackDepth(float4 rgba)
{
    // Weights are 255, matching shadowdepth_ps's pack -- see the note there.
    return dot(rgba.xyz, float3(1.0, 1.0 / 255.0, 1.0 / (255.0 * 255.0)));
}

// Cast-shadow term. Deliberately identical to terrainShadow in terrain_ps: a road is a
// decal on the terrain, so the two have to agree tap for tap, or the shadow edge breaks
// where it crosses onto the tarmac.
float roadShadow(float4 lightPos)
{
    // Guard the divide: a degenerate w yields inf, then NaN, and NaN survives every
    // operation after it -- the pixel is simply gone.
    float3 ndc = lightPos.xyz / max(abs(lightPos.w), 1e-6);
    float2 uv  = ndc.xy * float2(0.5, -0.5) + 0.5;
    float inBounds = step(0.0, uv.x) * step(uv.x, 1.0) * step(0.0, uv.y) * step(uv.y, 1.0);

    const float texel = ShadowParams.z;   // 1/SHADOW_MAP_SIZE, fed per frame

    // Bilinear-weighted PCF over a 3x3 texel footprint. The taps have to be point-sampled
    // -- depth is packed across RGB and hardware filtering would interpolate the packed
    // bytes -- so the smoothing comes from weighting the comparisons by where the pixel
    // sits inside its texel.
    float2 texelPos = uv / texel;
    float2 frc      = frac(texelPos - 0.5);
    float2 baseUv   = (floor(texelPos - 0.5) + 0.5) * texel;

    float wx[4] = { 1.0 - frc.x, 1.0, 1.0, frc.x };
    float wy[4] = { 1.0 - frc.y, 1.0, 1.0, frc.y };

    float lit = 0.0;
    [unroll] for (int y = 0; y < 4; ++y)
        [unroll] for (int x = 0; x < 4; ++x) {
            float2 tapUv = baseUv + float2(x - 1, y - 1) * texel;
            float stored = unpackDepth(tex2D(ShadowMap, tapUv));
            float tapLit = (ndc.z - ShadowParams.x > stored) ? 0.0 : 1.0;
            lit += tapLit * wx[x] * wy[y];
        }
    // Weights sum to 3 per axis ((1-f) + 1 + 1 + f), so 9 over the kernel.
    return saturate(lerp(1.0, lit / 9.0, inBounds * ShadowParams.y));
}

float4 main(PS_INPUT input) : COLOR
{
    float4 base = tex2D(BaseSampler, input.uv0);
    float3 col  = base.rgb * input.color.rgb;

    // Multiplicative overlays, as the fixed-function road pass applied them (stage 1 and
    // stage 2, both MODULATE against the running colour). Off layers lerp to white.

    float3 noiseTex = tex2D(NoiseSampler, input.noiseUV).rgb;
    float3 cloud = cloudShade(input.cloudUV, OverlayEnable.x, OverlayEnable.z);
    float3 noise = lerp(float3(1.0, 1.0, 1.0), noiseTex, OverlayEnable.y);

    // Cast shadows: the road colour has its lighting baked in, so darken toward the same
    // ambient floor the terrain uses rather than to black. SHADOW_MIN is in
    // constants.hlsli, so every receiver darkens to the same place.
    float shadow = roadShadow(input.lightPos);
    col *= lerp(SHADOW_MIN, 1.0, shadow);

    return float4(col * cloud * noise, base.a * input.color.a);
}

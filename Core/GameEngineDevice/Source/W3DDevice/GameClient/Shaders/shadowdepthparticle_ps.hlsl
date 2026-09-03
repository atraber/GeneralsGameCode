// Shadow-map depth pass for particle sprites (pixel, Shader Model 3).
//
// Packs sun-clip depth exactly as shadowdepth_ps does -- same three-channel split, same
// unpack in the receivers -- and differs only in how it decides whether a texel casts at
// all.
//
// The shadow map holds one depth per texel and the receivers compare against it, so a
// texel is either lit or not. That is the right representation for a wall and the wrong
// one for smoke, which stops some of the sun and passes the rest. Thresholding smoke's
// alpha the way a rotor disc is thresholded gives a black blob with a hard rim, which
// reads worse than no shadow at all.
//
// So coverage is dithered instead: a 4x4 ordered pattern in shadow-map texel space, and
// a texel casts if its opacity clears its own entry. Half-opaque smoke therefore writes
// half its texels. What makes this work rather than merely trade one artefact for
// another is that the receivers already run a 4x4 bilinear-weighted PCF over exactly
// this footprint (see terrainShadow) -- sixteen taps whose average recovers the density
// the pattern encoded, at about sixteen levels of grey. The filter that exists to
// de-stair-step shadow edges resolves the dither for free.
//
// It stays put frame to frame because the sun frustum is snapped to whole shadow texels:
// a patch of ground keeps the same texels as the camera scrolls, so the pattern does not
// crawl beneath a stationary cloud.

#include "shadermodel.hlsli"

DECLARE_SAMPLER(BaseSampler, 0);   // the sprite's own texture, for its alpha

// y = density ceiling: the most of the sun a fully opaque sprite texel may take. Short of
// 1 deliberately -- see the note where it is set. x is the plain shader's hard cutoff and
// z its own dither ceiling; neither is read here, since the pattern below is this
// shader's only arbiter.
float4 ShadowCastParams : register(c0);

struct PS_INPUT { float4 lightPos : TEXCOORD0; float2 texcoord : TEXCOORD1;
                  float  alpha    : TEXCOORD2; PIXEL_POSITION_TYPE vpos     : PS_PIXEL_POSITION; };

// Split the depth across three 8-bit channels, coarse to fine. Identical to
// shadowdepth_ps's -- the receivers unpack both with one function, so the two must agree
// exactly. See the note there for why the weights are 255 and not 256.
float4 packDepth(float depth, float alpha)
{
    float3 enc = frac(depth * float3(1.0, 255.0, 255.0 * 255.0));
    enc.xy -= enc.yz * (1.0 / 255.0);
    return float4(enc, alpha);
}

// Ordered 4x4 dither threshold in (0,1), evaluated arithmetically.
//
// Built the way the Bayer matrix itself is, by recursion: the 2x2 pattern
// [[0,2],[3,1]] closes as 2x + 3y - 4xy over one bit of each coordinate, and the 4x4 is
// the high bits' entry plus four times the low bits'. Cheaper than the alternative,
// which is a lookup table, and a table would need a texture stage the depth pass has no
// material set up to provide.
//
// The half-step offset puts the sixteen thresholds at (0.5..15.5)/16, so that fully
// opaque casts everywhere and fully transparent casts nowhere -- both exactly, with no
// texel of either lost to a boundary.
float bayer4x4(float2 vpos)
{
    float2 f  = fmod(floor(vpos), 4.0);
    float2 hi = floor(f * 0.5);
    float2 lo = f - hi * 2.0;
    float m2hi = 2.0 * hi.x + 3.0 * hi.y - 4.0 * hi.x * hi.y;
    float m2lo = 2.0 * lo.x + 3.0 * lo.y - 4.0 * lo.x * lo.y;
    return (m2hi + 4.0 * m2lo + 0.5) / 16.0;
}

float4 main(PS_INPUT input) : PS_TARGET
{
    // Clamp below 1.0: packDepth(1.0) wraps to (0,0,0), which unpacks to the near plane
    // and would shadow everything under it.
    float depth = min(input.lightPos.z / input.lightPos.w, 0.9999);

    // Both halves of the sprite's opacity, and the ceiling on the pair.
    float coverage = SAMPLE_2D(BaseSampler, input.texcoord).a * input.alpha * ShadowCastParams.y;
    // vpos is the pixel's own coordinate, and it is the one semantic here whose *value*
    // differs between the models: VPOS in ps_3_0 is the integer pixel coordinate, while
    // SV_Position is the pixel centre, half a texel further on in each axis.
    //
    // bayer4x4 happens to be immune -- it floors before it takes the modulus, and
    // floor(i + 0.5) is i -- so the dither pattern would come out identical either way.
    // PIXEL_POSITION is applied at the call anyway, because that immunity is a property of
    // one line of arithmetic inside the function rather than of the value being passed to
    // it, and a later reader tuning the pattern should not have to rediscover it. Under
    // model 3 the macro is the bare argument, so this costs nothing and moves no bytecode.
    clip(coverage - bayer4x4(PIXEL_POSITION(input.vpos)));

    // Alpha out is 1.0, not the coverage. The scene's alpha test is left as the particle
    // shader set it, and for an alpha-tested system it would test whatever went here --
    // cutting a second time, against a reference chosen for compositing, a fragment the
    // pattern has already accepted. The receivers read only RGB, so nothing else is
    // reading this.
    return packDepth(depth, 1.0);
}

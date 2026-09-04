// Camera-depth inspector (pixel, Shader Model 3).
//
// Unpacks the camera-space depth prepass -- written by the same shadowdepth_vs/ps pair as
// the sun's map, just with the camera's projection -- and draws it linearised, for the
// DEBUG_VIS_DEPTH tile.
//
// Linearising is the whole point. The stored value is post-projection z/w, which is not
// distance: a perspective projection spends roughly half its range on the first few
// percent of the view, so everything past the immediate foreground reads as one shade a
// hair under 1.0. Shown raw, a depth buffer that is completely wrong and one that is
// completely right look identical over most of the screen.

#include "shadermodel.hlsli"

DECLARE_SAMPLER_2D(DepthSampler, 0);

// x = projection _33, y = projection _43. Together these invert the projection's depth
// term. Handed over as the raw matrix elements rather than as a recovered near/far pair,
// exactly as the PBR shader takes them -- the recovery is two divisions that each pass
// through zero for projections this code does not anticipate, and it buys nothing.
//
// The inversion is `abs(_43 / (_33 + ndcZ))`, character for character what unit_pbr_ps
// uses, and that is not a coincidence to be tidied away: the projection in use here is
// right-handed, so the left-handed form -- `_43 / (ndcZ - _33)` -- returns a *negative*
// distance for every pixel. Written that way first, this tile came out uniformly white,
// every sample having clamped at zero. abs() is what keeps the expression valid under
// both handednesses.
// z = the view distance the ramp saturates at, so the tile can be read at the scale the
// camera is actually at rather than against a far plane nothing reaches.
float4 DebugDepthCtl : register(c0);

// Exact inverse of shadowdepth_ps's packDepth -- see debugshadow_ps for why the weights
// are 255-based rather than 256-based.
float unpackDepth(float4 packed)
{
    return dot(packed.rgb, float3(1.0, 1.0 / 255.0, 1.0 / (255.0 * 255.0)));
}

float4 main(PS_INPUT_POSITION_PARAM PS_INPUT_UNUSED_COLOR_PARAM float2 uv : TEXCOORD0) : PS_TARGET
{
    float4 packed = SAMPLE_2D(DepthSampler, uv);
    float  ndcZ   = unpackDepth(packed);

    // Untouched texels keep the clear value. Flagged rather than drawn, for the same
    // reason as in the shadow inspector: "nothing was submitted here" and "this is very
    // far away" are the same shade on a ramp and have nothing else in common.
    if (ndcZ >= 0.99989)
        return float4(0.15, 0.0, 0.25, 1.0);

    // Guard the pole. Where the denominator vanishes the view distance is infinite, and
    // near it the division amplifies the quantisation in the packed value into noise;
    // both are outside anything the scene occupies.
    float denom = DebugDepthCtl.x + ndcZ;
    if (abs(denom) < 1.0e-6)
        return float4(0.15, 0.0, 0.25, 1.0);

    float viewZ  = abs(DebugDepthCtl.y / denom);
    float scaled = saturate(viewZ / max(DebugDepthCtl.z, 1.0));

    // Near bright, far dark -- the same convention as the shadow tile, so the two can be
    // compared without re-learning which way round they are.
    float3 grey = 1.0 - scaled;

    // Iso-distance contours every 1/32 of the displayed range, with the flatness guard
    // the shroud inspector documents: where the field is exactly constant the derivative
    // is zero and an unguarded contour test floods the whole region.
    float bands   = scaled * 32.0;
    float w       = fwidth(bands);
    float band    = frac(bands);
    float contour = (w > 1.0e-4)
        ? (1.0 - smoothstep(0.0, w, min(band, 1.0 - band)))
        : 0.0;
    grey = lerp(grey, float3(0.20, 1.00, 0.55), contour * 0.55);

    return float4(grey, 1.0);
}

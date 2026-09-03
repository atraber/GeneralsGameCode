// Bloom bright-pass inspector (pixel, Shader Model 3).
//
// Draws what bloom_bright_ps selected, in false colour, over the finished scene. Fed the
// copy of the bright-pass target that W3DShaderManager takes while this mode is active --
// the live target cannot be used, because the two blur passes immediately ping-pong
// through it and what survives to the end of the frame is the blurred glow, which is the
// one thing this mode must not show. A blurred glow is spread over its neighbours by
// construction, so it cannot answer "did *this* pixel bloom".
//
// Alpha-blended rather than opaque, so the scene stays visible underneath: "which parts
// of this frame bloom" is a question about the frame, and an answer that hides it is
// half an answer.

#include "shadermodel.hlsli"

#include "constants.hlsli"

DECLARE_SAMPLER_2D(BrightSampler, 0);

float4 main(float2 uv : TEXCOORD0) : PS_TARGET
{
    float3 c    = SAMPLE_2D(BrightSampler, uv).rgb;
    float  luma = dot(c, LUMA);

    // bloom_bright_ps multiplies the pixel by a contribution that is exactly zero below
    // the threshold, so this test is the threshold test itself rather than an
    // approximation of it -- no second copy of the threshold to keep in step, and it
    // stays correct when the engine switches thresholds for HDR.
    //
    // The epsilon is for the 8-bit case: against a non-floating-point target a very small
    // contribution quantises to zero anyway, so anything below one code value is not
    // blooming in any sense that would show.
    if (luma <= (1.0 / 512.0))
        return float4(0.0, 0.0, 0.0, 0.0);   // does not bloom: leave the scene alone

    // Ramp across the range that matters. Under HDR luma runs past 1.0 and is meant to;
    // the top stop is white, so anything far over simply saturates rather than wrapping
    // into a colour that reads as "less".
    float3 col;
    if (luma < 0.25)
        col = lerp(float3(0.10, 0.25, 1.00), float3(0.10, 0.90, 0.95), luma / 0.25);
    else if (luma < 0.75)
        col = lerp(float3(0.10, 0.90, 0.95), float3(1.00, 0.90, 0.15), (luma - 0.25) / 0.50);
    else
        col = lerp(float3(1.00, 0.90, 0.15), float3(1.00, 1.00, 1.00), saturate((luma - 0.75) / 1.25));

    // Floor the opacity well above zero. A pixel that only just clears the threshold
    // contributes almost nothing and would be invisible if opacity tracked its
    // contribution -- and those are the interesting ones, because they are what moves in
    // and out of blooming when the threshold is wrong.
    float alpha = saturate(0.45 + luma * 0.55);
    return float4(col, alpha);
}

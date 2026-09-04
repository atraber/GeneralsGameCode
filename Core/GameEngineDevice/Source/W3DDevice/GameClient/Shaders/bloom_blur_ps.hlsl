// Bloom separable Gaussian blur (Shader Model 2).
//
// Second stage of the bloom post-process: a 9-tap Gaussian applied once
// horizontally and once vertically (run this shader twice with BlurStep set to a
// one-texel step along X, then along Y). Operates on the reduced-resolution bright
// texture, so the wide blur is cheap. Drawn as a fullscreen XYZRHW quad.

#include "shadermodel.hlsli"

DECLARE_SAMPLER_2D(BlurSampler, 0);

// xy = one-texel step in the blur direction (e.g. (1/width, 0) then (0, 1/height)).
float4 BlurStep : register(c0);

// Normalised 1D Gaussian weights (sigma ~ 2 texels), centre + 4 each side.
static const float W0 = 0.2270270270;
static const float W1 = 0.1945945946;
static const float W2 = 0.1216216216;
static const float W3 = 0.0540540541;
static const float W4 = 0.0162162162;

float4 main(PS_INPUT_POSITION_PARAM PS_INPUT_UNUSED_COLOR_PARAM float2 uv : TEXCOORD0) : PS_TARGET
{
    float2 d = BlurStep.xy;

    float3 c = SAMPLE_2D(BlurSampler, uv).rgb * W0;
    c += SAMPLE_2D(BlurSampler, uv + d * 1.0).rgb * W1;
    c += SAMPLE_2D(BlurSampler, uv - d * 1.0).rgb * W1;
    c += SAMPLE_2D(BlurSampler, uv + d * 2.0).rgb * W2;
    c += SAMPLE_2D(BlurSampler, uv - d * 2.0).rgb * W2;
    c += SAMPLE_2D(BlurSampler, uv + d * 3.0).rgb * W3;
    c += SAMPLE_2D(BlurSampler, uv - d * 3.0).rgb * W3;
    c += SAMPLE_2D(BlurSampler, uv + d * 4.0).rgb * W4;
    c += SAMPLE_2D(BlurSampler, uv - d * 4.0).rgb * W4;

    return float4(c, 1.0);
}

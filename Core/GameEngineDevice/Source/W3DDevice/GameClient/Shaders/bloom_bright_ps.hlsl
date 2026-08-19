// Bloom bright-pass (Shader Model 2).
//
// First stage of the screen-space bloom post-process: samples the rendered scene
// and keeps only the portion of each pixel above a luminance threshold, so only
// bright areas (explosions, muzzle flashes, lit windows, sky) feed the blur. The
// scene is LDR (A8R8G8B8), so this is a soft threshold on the [0,1] colour rather
// than a true HDR over-1.0 extraction; it still gives a convincing glow.
//
// Drawn as a fullscreen XYZRHW quad, typically into a reduced-resolution target.

#include "constants.hlsli"

sampler2D SceneSampler : register(s0);

// Bloom tuning -- edit and recompile the shader to tweak (no engine rebuild needed).
static const float BLOOM_THRESHOLD = 0.65;  // luminance above which pixels bloom
static const float BLOOM_KNEE      = 0.30;  // soft ramp width above the threshold

float4 main(float2 uv : TEXCOORD0) : COLOR
{
    float3 c    = tex2D(SceneSampler, uv).rgb;
    float  luma = dot(c, LUMA);

    // Smooth ramp from threshold to threshold+knee so the bloom edge isn't hard.
    float contrib = saturate((luma - BLOOM_THRESHOLD) / max(BLOOM_KNEE, 1e-3));

    // Keep the pixel's own colour so coloured highlights bloom in their own hue.
    return float4(c * contrib, 1.0);
}

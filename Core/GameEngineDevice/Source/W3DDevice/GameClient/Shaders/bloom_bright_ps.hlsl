// Bloom bright-pass.
//
// First stage of the screen-space bloom post-process: samples the rendered scene and keeps
// only the portion of each pixel above a luminance threshold, so only bright areas feed the
// blur. Drawn as a fullscreen quad into a reduced-resolution target.
//
// Where the threshold sits is the whole argument, and it depends on what the scene is drawn
// into. Against a floating-point scene, "bright" means what it says: above 1.0 is light the
// display cannot show, which is exactly what should bloom. Against an 8-bit scene nothing
// can exceed 1.0, so a threshold up there would never select anything and the effect would
// vanish; the old fixed 0.65 is kept for that case, with the understanding that it selects
// light-coloured pixels rather than bright ones -- a white building blooms as readily as a
// muzzle flash. That is a fair description of bloom without HDR, and the reason for HDR.
//
// So the engine passes the threshold rather than the shader assuming one.

#include "constants.hlsli"

sampler2D SceneSampler : register(s0);

// x: luminance above which a pixel blooms. y: width of the soft ramp above it.
float4 BloomThreshold : register(c0);

float4 main(float2 uv : TEXCOORD0) : COLOR
{
    float3 c    = tex2D(SceneSampler, uv).rgb;
    float  luma = dot(c, LUMA);

    // Smooth ramp from the threshold upward so the bloom edge is not a hard cut.
    float contrib = saturate((luma - BloomThreshold.x) / max(BloomThreshold.y, 1e-3));

    // Keep the pixel's own colour so coloured highlights bloom in their own hue. Above the
    // threshold this can exceed 1.0 and is meant to: the blur target holds the same format
    // as the scene, so with HDR the range survives into the blur instead of clipping there.
    return float4(c * contrib, 1.0);
}

// Profiler frame-capture swizzle (Shader Model 3).
//
// Port of the ps_1_4 shader W3DProfilerFrameCapture assembled at runtime with
// D3DXAssembleShader. It exists because the back buffer is BGRA and the profiler wants
// RGBA, and it is the whole of what that shader did: seven instructions moving the blue
// channel into red and the red into blue through three mask constants in c0..c2.
//
// The masks are gone. They were three compile-time constants the caller uploaded on every
// capture, and a channel swizzle is what they spelled; writing it as a swizzle is the
// "much simpler" the original's own comment asked for.
//
// Alpha is the source's, untouched -- the ps_1_4 version wrote only .rgb after the texld,
// so r0.a kept the sampled alpha.

#include "shadermodel.hlsli"

DECLARE_SAMPLER_2D(SourceMap, 0);

float4 main(float2 uv : TEXCOORD0) : PS_TARGET
{
    float4 source = SAMPLE_2D(SourceMap, uv);
    return float4(source.b, source.g, source.r, source.a);
}

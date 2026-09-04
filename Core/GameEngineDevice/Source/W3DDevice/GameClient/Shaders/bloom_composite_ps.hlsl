// Bloom composite (Shader Model 3).
//
// Final stage of the bloom post-process: adds the blurred bloom texture back onto the
// scene and writes the result to the back buffer. Drawn as a single fullscreen XYZRHW
// quad. The scene occupies a sub-rect of its texture (the tactical viewport), so it
// carries its own texcoord set (TEXCOORD0); the bloom target is a full 0..1 texture on
// TEXCOORD1.
//
// Under HDR this pass is also the tone map for the visible frame, and the ordering is the
// whole reason it has to be. The bloom is selected and blurred in the scene's own range,
// which runs above 1.0; the scene must therefore still be above 1.0 when the two are added,
// and the curve applied to the sum. Tone mapping first and adding afterwards -- which is
// what this did while the curve was the identity, and which was harmless only because the
// identity commutes with addition -- would let the glow ride on top of an already-compressed
// image and clip flat against the 8-bit target, which is precisely the look HDR exists to
// avoid. So under HDR the scene arrives here straight from the floating-point target, not
// from the tone mapped copy.

#include "shadermodel.hlsli"

#include "tonemap.hlsli"

DECLARE_SAMPLER_2D(SceneSampler, 0);
DECLARE_SAMPLER_2D(BloomSampler, 1);

// x = exposure. y = 1 when the scene sampled above is the floating-point one and this pass
//     owns the tone map; 0 when the scene is already the displayable 8-bit image and the
//     curve must not be applied a second time.
float4 ToneMapCtl : register(c0);

// Bloom tuning -- edit and recompile the shader to tweak (no engine rebuild needed).
static const float BLOOM_INTENSITY = 1.00;  // how strongly the glow is added on top

float4 main(PS_INPUT_POSITION_PARAM PS_INPUT_UNUSED_COLOR_PARAM float2 uvScene : TEXCOORD0, float2 uvBloom : TEXCOORD1) : PS_TARGET
{
    float3 scene = SAMPLE_2D(SceneSampler, uvScene).rgb;
    float3 bloom = SAMPLE_2D(BloomSampler, uvBloom).rgb * BLOOM_INTENSITY;

    [branch] if (ToneMapCtl.y > 0.5)
    {
        // Summed in linear, not in the encoded values the two textures hold. Light adds
        // linearly and this is the one place in the frame where both operands are known to
        // be light rather than an already-composited surface, so it costs one decode to do
        // the physically right thing. The curve then sees a genuine sum.
        float3 lin = SceneToLinear(scene) + SceneToLinear(bloom);
        return float4(LinearToScene(ToneMapLinear(lin * ToneMapCtl.x)), 1.0);
    }

    // No HDR: the scene is the 8-bit displayable image, the bloom was selected out of it in
    // the same range, and the target clamps the sum. Unchanged from before HDR existed.
    return float4(scene + bloom, 1.0);
}

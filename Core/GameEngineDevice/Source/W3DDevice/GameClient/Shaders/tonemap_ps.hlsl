// Tone map: high dynamic range scene -> the 8-bit texture the rest of the frame reads.
//
// With HDR on, the scene is drawn into a floating-point colour target where a value may
// exceed 1.0 -- a muzzle flash, a sun specular, an explosion. Nothing downstream can hold
// that: the back buffer is 8-bit, and so is every consumer of the scene texture (the
// black-and-white filter, the cross-fade, motion blur, the reflection history). This pass
// is the one place the range is brought back down, and it runs at the end of
// render-to-texture so that everything after it sees exactly the 8-bit scene it always saw.
//
// Note what this pass does NOT feed: when the bloom filter is running it is the composite,
// not this, that writes the visible frame -- it has to be, because the bloom has to be
// added while the scene is still in range and the curve applied to the sum. This pass then
// serves the *other* consumers, which want the scene without the glow. Both apply the same
// curve out of tonemap.hlsli, which is why the curve lives there.

#include "tonemap.hlsli"

sampler2D SceneSampler : register(s0);

// x = exposure applied before the curve. y is unused here (the composite uses it to switch
// the curve off when the scene reaching it is already 8-bit).
float4 ToneMapCtl : register(c0);

float4 main(float2 uv : TEXCOORD0) : COLOR
{
    float4 scene = tex2D(SceneSampler, uv);

    // Alpha is carried through untouched. It is not brightness and must not be curved: the
    // soft water edge writes the frame buffer's destination alpha during the scene, and the
    // cross-fade's framebuffer-mask mode reads it back afterwards.
    return float4(ToneMapScene(scene.rgb, ToneMapCtl.x), scene.a);
}

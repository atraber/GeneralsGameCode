// Tone map: high dynamic range scene -> the 8-bit texture the rest of the frame reads.
//
// With HDR on, the scene is drawn into a floating-point colour target where a value may
// exceed 1.0 -- a muzzle flash, a sun specular, an explosion. Nothing downstream can hold
// that: the back buffer is 8-bit, and so is every consumer of the scene texture (the
// black-and-white filter, the cross-fade, motion blur, the reflection history). This pass
// is the one place the range is brought back down, and it runs at the end of
// render-to-texture so that everything after it sees exactly the 8-bit scene it always saw.
//
// Right now the curve is the identity, and deliberately so. This stage exists to move the
// scene onto a floating-point target and prove the plumbing without changing a pixel: every
// shader in the frame still clamps its own output at 1.0, so there is no range here to
// compress yet, and a curve applied to an image that never exceeds 1.0 would darken the
// whole game for no reason. The curve arrives with the sources that can actually exceed 1.0.

sampler2D SceneSampler : register(s0);

// Scene colour -> displayable colour. Identity until something emits above 1.0; see above.
float3 toneMap(float3 c)
{
    return c;
}

float4 main(float2 uv : TEXCOORD0) : COLOR
{
    float4 scene = tex2D(SceneSampler, uv);

    // Alpha is carried through untouched. It is not brightness and must not be curved: the
    // soft water edge writes the frame buffer's destination alpha during the scene, and the
    // cross-fade's framebuffer-mask mode reads it back afterwards.
    return float4(toneMap(scene.rgb), scene.a);
}

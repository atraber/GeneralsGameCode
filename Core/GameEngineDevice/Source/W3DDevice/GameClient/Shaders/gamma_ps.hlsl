// Display gamma, brightness and contrast -- the ramp the hardware used to apply.
//
// The options screen's Gamma slider drove SetDeviceGammaRamp, a 256-entry lookup table
// the display applied at scanout, after everything. D3D11 has no windowed equivalent
// (IDXGIOutput::SetGammaControl is exclusive full-screen only), so the slider has done
// nothing since D3D11 became the default. This is that lookup table, evaluated per pixel.
//
//
// WHERE THIS SITS IN THE HDR CHAIN, WHICH IS THE DESIGN QUESTION AND NOT AN INCIDENTAL
//
// It sits after all of it, on the finished 8-bit frame, and that is the whole point.
//
// A gamma ramp is a transfer curve on *display code values*. It is not an exposure, not a
// tone map and not a scene-referred operation: it takes the number the frame buffer holds
// and says what light the panel should make of it. So the faithful place for it is the
// last thing before the frame is presented -- after the tone map, after the bloom
// composite, and after the interface, because the hardware ramp applied to the control
// bar and the mouse cursor exactly as it applied to the terrain.
//
// Folding it into the tone map instead was the obvious alternative and it is wrong twice.
// It would miss the interface, which is half the screen in this game; and the tone map's
// output is not the frame -- with the bloom filter running it is the composite that writes
// what the player sees, and tonemap_ps feeds the reflection history, the black-and-white
// filter and the cross-fade, none of which wants a display curve baked into it. Applying
// it there would look right in the midtones and be wrong everywhere the two paths differ.
//
// See tonemap.hlsli for why the scene buffer is gamma-encoded rather than linear. That
// fact does not reach this shader: what arrives here is a finished display-referred
// image whatever produced it, which is the property that makes this the one safe place.

#include "shadermodel.hlsli"

DECLARE_SAMPLER_2D(FrameSampler, 0);

// x = 1/gamma, y = brightness, z = contrast. Named the way DX8Wrapper::Set_Gamma names
// them, and evaluated in the same order its ramp did, so the two agree entry for entry.
float4 GammaCtl : register(c0);

float4 main(PS_INPUT_POSITION_PARAM PS_INPUT_UNUSED_COLOR_PARAM float2 uv : TEXCOORD0) : PS_TARGET
{
    float4 frame = SAMPLE_2D(FrameSampler, uv);

    // The ramp, term for term:
    //     x   = Bound(in, 0, 1)
    //     x   = pow(x, 1/gamma)
    //     out = Bound(contrast * x + brightness, 0, 1)
    // The saturate before the pow is not decoration: pow() of a negative is NaN, and a
    // frame buffer that has held a blend result can carry one.
    float3 c = pow(saturate(frame.rgb), GammaCtl.x);
    c = saturate(c * GammaCtl.z + GammaCtl.y);

    // Alpha through untouched. A gamma ramp never had an alpha channel to act on, and the
    // back buffer's destination alpha is read by the cross-fade's framebuffer-mask mode.
    return float4(c, frame.a);
}

// The alpha test, as a shader stage.
//
// D3D9 did this in hardware, *after* the pixel shader had run: D3DRS_ALPHATESTENABLE
// with a compare function and an 8-bit reference, applied to the alpha the shader
// returned. It is what cuts out foliage, fences, ladders, tree billboards and the
// transparent parts of a cast shadow. As of 2026-09-01 that stage is off: the three
// alpha render states are still tracked, and still read here, but DX8Wrapper no
// longer forwards them to the device. This is the alpha test now.
//
// D3D11 has no such stage at all. Every shader that can receive an alpha-tested draw has
// to do it itself with clip(), and the failure mode if one is missed is silent: the
// geometry does not vanish, it goes soft. So it lives here, shared, rather than being
// written out three times and drifting.
//
// AlphaTestCtl is uploaded once per draw by DX8Wrapper::Draw, out of the same tracked
// render states the device is given, so no caller has to know this exists:
//
//   x = the reference, already scaled to [0,1] (the device takes 0..255)
//   y = +1 for a GREATEREQUAL test, -1 for a LESSEQUAL one
//   zw = unused
//
// Disabled is encoded as (0, +1) rather than as a separate permutation: clip() discards
// only on a negative argument and alpha is never negative, so clip(alpha - 0) discards
// nothing. Same convention shadowdepth_ps's ShadowCastParams.x already used, and for the
// same reason.
//
// NOTEQUAL cannot be expressed this way, and with the hardware stage off there is
// nothing behind it: an inexpressible compare discards nothing. The only caller that
// sets one is W3DWater's WATER_TYPE_1_FB_REFLECTION path, and WaterType = 0 is the
// only value present anywhere in shipped content, so no shipped map can select it.
// W3DCustomEdging used to set NOTEQUAL too; it was code that could not be built at
// all and is gone. If the water path is ever wanted, this is the constant that has
// to grow a mode -- the census is what would say so.

#ifndef RTS_SHADER_ALPHATEST_HLSLI
#define RTS_SHADER_ALPHATEST_HLSLI

float4 AlphaTestCtl : register(c28);

// Pass the alpha the shader is about to *return*, not the alpha it sampled. The hardware
// stage tests the value the shader wrote -- texture alpha times material opacity, times
// whatever else the combine did -- so testing anything else is a different test, and the
// two would disagree exactly at the cutout edge where it shows.
void AlphaTest(float outAlpha)
{
    clip(AlphaTestCtl.y * (outAlpha - AlphaTestCtl.x));
}

#endif  // RTS_SHADER_ALPHATEST_HLSLI

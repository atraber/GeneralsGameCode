// The alpha test, as a shader stage.
//
// D3D9 does this in hardware, *after* the pixel shader has run: D3DRS_ALPHATESTENABLE
// with a compare function and an 8-bit reference, applied to the alpha the shader
// returned. It is what cuts out foliage, fences, ladders, tree billboards and the
// transparent parts of a cast shadow.
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
// NOTEQUAL cannot be expressed this way. It is set by W3DCustomEdging and by W3DWater's
// legacy clip-plane path, neither of which reached a shader-routed draw in any scene
// measured (see the Phase 2 alpha test and fog investigation), so those draws are left to the
// hardware stage. If one ever routes, this is the constant that has to grow a mode.

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

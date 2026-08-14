// Black-and-white screen filter (Shader Model 3).
//
// Port of monochrome.pso, which was ps_1_1. Desaturates the tactical view and tints it,
// fading in and out over a few frames. Used by the mission-failure / special-power screen
// effects, and by a debug hotkey.
//
// The three constants keep the registers the ps_1_1 version used, so the engine side that
// uploads them is unchanged:
//   c0  luminance weights (0.30, 0.59, 0.11)
//   c1  filter colour -- white for plain black-and-white, red or green for the tinted modes
//   c2  fade, 0 = untouched scene, 1 = fully filtered
//
// The original was three instructions (dp3 / mul / lrp) and this is the same three
// operations. What it is *not* carrying over is ps_1_1's arithmetic: every intermediate
// there was clamped to [0,1] and the register precision was roughly 8-bit. Nothing in this
// shader relied on that, since all three inputs are already in range and the lerp cannot
// leave it.

sampler2D SceneSampler : register(s0);

float4 LumaWeights : register(c0);
float4 FilterColor : register(c1);
float4 FadeAmount  : register(c2);

float4 main(float2 uv : TEXCOORD0) : COLOR
{
    float4 scene = tex2D(SceneSampler, uv);

    float  luma   = dot(scene.rgb, LumaWeights.rgb);
    float3 tinted = luma * FilterColor.rgb;

    // Alpha is the scene's own, not the filtered result. The ps_1_1 version left its alpha
    // channel to whatever dp3 happened to leave in the register -- dp3 writes colour only --
    // and got away with it because the quad draws opaque. Passing the scene alpha through is
    // what that was meant to be.
    return float4(lerp(scene.rgb, tinted, FadeAmount.x), scene.a);
}

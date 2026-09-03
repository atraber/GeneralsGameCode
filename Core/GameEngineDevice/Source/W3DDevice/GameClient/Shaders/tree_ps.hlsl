// Tree / grass / bush pixel shader (Shader Model 3).
//
// This shader is new rather than a port. Trees.pso existed but was never bound -- the call
// sat behind an #if 0 and the asset's own comment said "Not actually used at this time" --
// so trees were drawn by a vs_1_1 vertex shader feeding the fixed-function pixel pipeline.
// That pairing is what forced the vertex shader to stay at vs_1_1: D3D9 will not run a
// vs_3_0 shader against fixed-function pixel processing, so moving the vertex side up
// requires a pixel shader to exist at all.
//
// What it reproduces is the two texture stages the fixed-function path was set up with:
//   stage 0  tree texture, modulated by the vertex colour (GRADIENT_MODULATE)
//   stage 1  shroud,       modulated over that, alpha passed through unchanged
// The blend state around it is unchanged and still does the work it always did: SRCBLEND_ONE
// / DSTBLEND_ZERO with the alpha test enabled, i.e. opaque with a cutout.

#include "shadermodel.hlsli"

#include "alphatest.hlsli"

DECLARE_SAMPLER_2D(TreeSampler, 0);
DECLARE_SAMPLER_2D(ShroudSampler, 1);

// x = 1 when a shroud texture is bound, 0 when there is none.
//
// It has to be a constant rather than something the shader can detect. With no shroud the
// engine leaves stage 1 empty, and sampling an unbound sampler returns whatever the driver
// feels like -- black on some, the last texture bound on others, which would show up as
// trees mysteriously darkened or textured with someone else's art on exactly the maps that
// have no fog of war.
float4 ShroudCtl : register(c0);

float4 main(float4 color    : COLOR0,
            float2 texcoord : TEXCOORD0,
            float2 shroudUV : TEXCOORD1) : PS_TARGET
{
    float4 tex = SAMPLE_2D(TreeSampler, texcoord);

    float3 shroud = lerp(float3(1.0, 1.0, 1.0),
                         SAMPLE_2D(ShroudSampler, shroudUV).rgb,
                         ShroudCtl.x);

    // Alpha is the texture's cutout times the vertex alpha, and the shroud stage does not
    // touch it -- the fixed-function setup selected CURRENT for stage 1's alpha op. That
    // matters more than it looks: the alpha test runs on this value, so letting the shroud
    // into it would dissolve the leaves of every tree standing in fog.
    float outAlpha = tex.a * color.a;

    // The cutout that makes a tree a tree rather than a rectangle. W3DTreeBuffer draws
    // with ALPHATEST_ENABLE (SC_ALPHA_DETAIL), so this stage is what does that work.
    //
    // Added on the strength of that declared shader state rather than of a measured
    // draw: no tree draw appeared in the alpha census on either map measured, which is
    // unresolved and written up in the Phase 2 alpha test and fog investigation. The clip is a
    // no-op whenever the constant says discard-nothing, so covering a shader that turns
    // out never to be alpha-tested costs one instruction, while missing one that is
    // costs soft-edged foliage that nobody notices.
    AlphaTest(outAlpha);
    return float4(tex.rgb * color.rgb * shroud, outAlpha);
}

// Shadow-map inspector (pixel, Shader Model 3).
//
// Unpacks the sun depth map that shadowdepth_ps wrote and draws it as a legible grey
// ramp, for the DEBUG_VIS_SHADOW_MAP corner tile. Drawn as a screen-space quad through
// screenquad_vs.
//
// Why this needs a shader rather than just blitting the texture: the map holds depth
// split across three 8-bit channels, coarse to fine. Displayed raw it is a red-green
// mess whose brightness has nothing to do with distance -- the green channel wraps
// 255 times across the frustum -- so the one thing you would want to read off it, how
// depth is distributed, is the one thing it does not show.

#include "shadermodel.hlsli"

DECLARE_SAMPLER_2D(ShadowSampler, 0);

// x = start of the displayed depth range, y = 1/(end - start). The interesting depth
// in a fitted sun frustum occupies a narrow band, and stretched over the full [0,1] it
// is a flat mid-grey in which nothing can be told from anything. The engine passes the
// window so the ramp can be spent where the geometry actually is.
// z = 1 when the alpha channel (the caster's texture alpha) should be shown instead.
float4 DebugShadowCtl : register(c0);

// Exact inverse of shadowdepth_ps's packDepth. The weights are the reciprocals of the
// ones used to split, and they are 255-based there for a reason that matters here too:
// an 8-bit channel stores k/255, so unpacking against 256 would reintroduce the very
// rounding error that split was built to avoid.
float unpackDepth(float4 packed)
{
    return dot(packed.rgb, float3(1.0, 1.0 / 255.0, 1.0 / (255.0 * 255.0)));
}

float4 main(float2 uv : TEXCOORD0) : PS_TARGET
{
    float4 packed = SAMPLE_2D(ShadowSampler, uv);

    if (DebugShadowCtl.z > 0.5)
    {
        // Coverage view: what the depth pass let through. A cut-out billboard casting
        // its whole quad rather than its silhouette shows up here and nowhere else.
        return float4(packed.aaa, 1.0);
    }

    float depth  = unpackDepth(packed);
    float scaled = saturate((depth - DebugShadowCtl.x) * DebugShadowCtl.y);

    // Near is bright, far is dark: the reverse of the stored convention, and chosen so
    // the tile reads like the scene rather than like a depth buffer -- what is close to
    // the sun is what casts.
    float3 grey = 1.0 - scaled;

    // Iso-depth contours, every 1/64 of the range.
    //
    // The plain ramp is almost unreadable in practice, and for a reason worth keeping in
    // view rather than tuning away: a sun frustum fitted to the whole map puts the entire
    // visible scene inside a few percent of its depth range, so everything of interest is
    // one shade of grey. Contours restore the structure without rescaling anything -- the
    // ramp still means what it says, and the lines simply mark where it crosses each
    // 1/64. Tightly packed lines are a scene using the range well; a populated area with
    // no line through it at all is a frustum whose far plane is much too far away, which
    // is a thing to fix rather than to compensate for here.
    // The flatness guard matters here as much as it does in the shroud inspector: where
    // the depth is exactly constant the derivative is zero, and a zero-width smoothstep
    // returns 1 wherever the value sits on a multiple of the contour interval -- flooding
    // the flat region with contour colour, which reads as maximum structure where there
    // is none. No contours is the honest rendering of a flat field.
    //
    // ("line" is a reserved word in HLSL -- it names a geometry primitive type.)
    float scaledBands = scaled * 64.0;
    float w    = fwidth(scaledBands);
    float band = frac(scaledBands);
    float contour = (w > 1.0e-4)
        ? (1.0 - smoothstep(0.0, w, min(band, 1.0 - band)))
        : 0.0;
    grey = lerp(grey, float3(0.20, 0.70, 1.00), contour * 0.55);

    // Depth that never got written stays at the clear value. Flagging it rather than
    // showing it as "very far" is the difference between "the frustum is too big" and
    // "nothing was submitted", which look identical on a grey ramp and have completely
    // different causes.
    if (depth >= 0.99989)
        return float4(0.15, 0.0, 0.25, 1.0);   // dark violet: empty

    return float4(grey, 1.0);
}

// Shadow-map inspector (pixel, Shader Model 3).
//
// Reads the sun depth map and draws it as a legible grey ramp with iso-depth contours,
// for the DEBUG_VIS_SHADOW_MAP corner tile. Drawn as a screen-space quad through
// screenquad_vs.
//
// The map is R32F -- a single 32-bit float in the red channel -- so the depth is read
// directly with no unpacking arithmetic.

#include "shadermodel.hlsli"

DECLARE_SAMPLER_2D(ShadowSampler, 0);

// x = start of the displayed depth range, y = 1/(end - start). The interesting depth
// in a fitted sun frustum occupies a narrow band, and stretched over the full [0,1] it
// is a flat mid-grey in which nothing can be told from anything. The engine passes the
// window so the ramp can be spent where the geometry actually is.
// z = 1 when the coverage view is requested: which texels the depth pass wrote at all,
// as opposed to how deep they are.
float4 DebugShadowCtl : register(c0);

float4 main(PS_INPUT_POSITION_PARAM PS_INPUT_UNUSED_COLOR_PARAM float2 uv : TEXCOORD0) : PS_TARGET
{
    float depth = SAMPLE_2D(ShadowSampler, uv).r;

    if (DebugShadowCtl.z > 0.5)
    {
        // Coverage view: what the depth pass let through. This used to read the
        // caster's texture alpha out of the map's alpha channel; R32F has no alpha,
        // and a sample from it returns a constant 1.0 there, so reading .a would
        // show a flat white tile that looks like total coverage.
        //
        // The same question is still answerable from the depth alone: the pass
        // clears to 1.0 and clamps everything it writes to 0.9999, so a texel below
        // 1.0 is one a caster wrote. That is the diagnostic this view existed for --
        // a cut-out billboard casting its whole quad rather than its silhouette
        // shows up as a solid white rectangle here and nowhere else.
        float coverage = (depth < 1.0) ? 1.0 : 0.0;
        return float4(coverage, coverage, coverage, 1.0);
    }

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


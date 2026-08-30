// Shroud field inspector (pixel, Shader Model 3).
//
// Draws the shroud projection texture as a top-down tile. The field is what the terrain
// and every mesh sample to decide how fogged they are, and it is sampled in world space,
// so seeing it laid out flat is seeing it in its own coordinates rather than through
// whatever texgen a given draw happened to set up.
//
// The point of looking at it at all: a shroud sampling bug -- the wrong coordinates
// reaching the lookup -- produces no visible error on a map whose shroud is uniform,
// because reading a constant field in the wrong place still returns the right value. This
// tile says whether the field is uniform, which is what decides whether the frame is
// evidence of anything.

sampler2D ShroudSampler : register(s0);

static const float3 LUMA = float3(0.299, 0.587, 0.114);

float4 main(float2 uv : TEXCOORD0) : COLOR
{
    float3 shroud = tex2D(ShroudSampler, uv).rgb;
    float  level  = dot(shroud, LUMA);

    // The field's own colour, not a remap of it. The shroud modulates scene colour, so it
    // can legitimately be tinted rather than grey, and flattening it to luminance here
    // would hide a channel going wrong on its own.
    float3 col = shroud;

    // Iso-level contours every 1/16, for the same reason the shadow inspector has them:
    // the interesting question is whether the field varies at all, and a gentle gradient
    // and a constant are the same flat grey to the eye. A tile crossed by contours has
    // structure; a tile with none is uniform, and any draw sampling it proves nothing.
    //
    // The flatness guard is what makes that reading true rather than backwards. In a
    // perfectly constant region the derivative is zero, so the contour test degenerates:
    // smoothstep over a zero-width edge returns 1 wherever the level happens to sit on a
    // multiple of 1/16, and the entire flat area floods with contour colour -- which is
    // the exact opposite of what it is supposed to indicate. Fully shrouded ground sits
    // at 0, which is such a multiple, so the whole shrouded half of the map lit up.
    float scaled  = level * 16.0;
    float w       = fwidth(scaled);
    float band    = frac(scaled);
    float contour = (w > 1.0e-4)
        ? (1.0 - smoothstep(0.0, w, min(band, 1.0 - band)))
        : 0.0;
    col = lerp(col, float3(1.00, 0.35, 0.10), contour * 0.6);

    return float4(col, 1.0);
}

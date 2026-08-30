// Shared tone mapping: the one definition of how the HDR scene becomes a displayable image.
//
// Included by tonemap_ps (HDR scene -> the 8-bit scene texture the rest of the frame reads)
// and by bloom_composite_ps (HDR scene + bloom -> the back buffer). Both must apply exactly
// the same curve or the tactical view and everything derived from it drift apart, so the
// curve lives here rather than being written twice.
//
//
// WHAT SPACE THE SCENE IS IN, WHICH IS THE THING TO UNDERSTAND BEFORE EDITING ANY OF THIS
//
// The floating-point scene target does NOT hold linear radiance. It holds ordinary
// display-encoded colour -- roughly sRGB, gamma ~2.2 -- that is simply no longer clamped at
// 1.0. Every shader that writes the scene encodes on the way out (unit_pbr_ps does it
// explicitly with LinearToSrgb; the others were authored against 8-bit targets and their
// textures are sRGB to begin with), and, more importantly, every alpha blend in the frame
// happens in that space. Moving the scene to linear would change the result of every
// blended draw in the game -- smoke, water, glass, every fade -- which is a far larger
// change than this one and is deliberately not being made here.
//
// Every tone mapping operator ever published, including the one below, expects LINEAR input.
// So the sequence has to be: decode to linear, expose, curve, encode back. Applying a
// filmic curve straight to encoded values double-applies a gamma and produces a washed-out,
// low-contrast image -- which looks enough like "a filmic look" to be mistaken for one.
//
// The consequence to keep in mind: this is an honest tone map of a scene whose *blending*
// is still gamma-space. It is not a linear renderer and the curve cannot make it one.

// Encoded scene value <-> linear. Both extend naturally above 1.0, which is the point.
// max() rather than saturate(): pow() of a negative is NaN, and a NaN here would spread
// across the whole frame through the blur, but values above 1.0 must survive untouched.
float3 SceneToLinear(float3 c) { return pow(max(c, 0.0), 2.2); }
float3 LinearToScene(float3 c) { return pow(max(c, 0.0), 1.0 / 2.2); }

// ---------------------------------------------------------------------------------------
// Curves. Exactly one is selected by TONEMAP_CURVE below; the others are kept compiled-out
// rather than deleted because choosing between them is a judgement about the look that is
// meant to be re-made against real content, not a decision that was settled once.
// ---------------------------------------------------------------------------------------

// ACES filmic approximation (Krzysztof Narkowicz's fit to the RRT+ODT).
//
// Takes linear scene radiance, returns linear display. The characteristic shape is a toe
// that lifts shadows slightly and a long shoulder that rolls highlights off instead of
// clipping them -- so a value of 4.0 and a value of 40.0 remain distinguishable rather than
// both landing on white. That rolloff is the entire reason to run an HDR target at all.
//
// It is not the identity below 1.0 and does not pretend to be: it will change the look of
// every pixel in the game, not only the bright ones. That is what makes it a filmic look
// rather than a highlight fix.
float3 ToneMapAces(float3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// Reinhard with a white point: gentler, no character of its own, and W sets the value that
// maps to display white (everything above it clips). Kept as the fallback if ACES reads as
// too strong a restyle.
float3 ToneMapReinhard(float3 x, float w)
{
    return saturate(x * (1.0 + x / max(w * w, 1e-4)) / (1.0 + x));
}

// Exact identity below the knee, smooth rolloff above it. The conservative option: nothing
// that already existed changes at all, and only genuinely-above-display values are touched.
// Kept for A/B against ACES -- it is the curve that makes "did the HDR path change anything
// it should not have" answerable, because the answer is supposed to be no.
float3 ToneMapShoulder(float3 x, float knee)
{
    // Above the knee, compress the remaining range into what is left below 1.0 with a
    // hyperbolic rolloff that is C1-continuous at the knee (value and slope both match).
    float3 over = (x - knee) / max(1.0 - knee, 1e-4);
    float3 rolled = knee + (1.0 - knee) * (over / (1.0 + over));
    return min(x < knee ? x : rolled, 1.0);
}

//   1 = ACES filmic      2 = Reinhard + white point      3 = shoulder only
#define TONEMAP_CURVE       3
#define TONEMAP_WHITE       4.0    // curve 2 only: linear value that maps to display white
#define TONEMAP_KNEE        0.80   // curve 3 only: below this, exactly unchanged

// The selected curve, linear scene in -> linear display out. This is the form the bloom
// composite needs, because it has to sum the scene and the glow in linear before curving
// and does not want an encode round-trip in between.
float3 ToneMapLinear(float3 lin)
{
#if   TONEMAP_CURVE == 1
    return ToneMapAces(lin);
#elif TONEMAP_CURVE == 2
    return ToneMapReinhard(lin, TONEMAP_WHITE);
#else
    // The shoulder curve is defined on *encoded* values, where "1.0 is display white" is the
    // statement it is making -- a knee at 0.80 linear is not the same picture as a knee at
    // 0.80 on screen. So it round-trips rather than being applied to the linear value, which
    // is what keeps it an exact identity below the knee where that is the entire point.
    return SceneToLinear(ToneMapShoulder(LinearToScene(lin), TONEMAP_KNEE));
#endif
}

// Scene-encoded HDR colour -> scene-encoded displayable colour.
//
// exposure scales the linear scene before the curve. It is the dial to reach for first if
// the image comes out darker or brighter overall than the pre-HDR game: the curve fixes the
// shape of the highlights, exposure fixes where the midtones sit under it.
float3 ToneMapScene(float3 sceneEncoded, float exposure)
{
    return LinearToScene(ToneMapLinear(SceneToLinear(sceneEncoded) * exposure));
}

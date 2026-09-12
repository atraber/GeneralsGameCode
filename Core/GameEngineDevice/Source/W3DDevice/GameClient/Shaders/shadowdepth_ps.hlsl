// Shadow-map depth pass (pixel).
//
// Writes the sun-clip-space depth (z/w, in [0,1] for D3D) directly into the R32F
// render target as a plain 32-bit float. The D3D9-era RGBA8 depth-packing scheme
// (spreading depth across three 8-bit channels) has been retired now that we use
// an R32F colour render target, which carries a full 32 bits in the red channel.

#include "shadermodel.hlsli"

#include "alphatest.hlsli"

DECLARE_SAMPLER(BaseSampler, 0);   // the caster's own texture, for its alpha

// x = alpha below which a texel casts nothing.
//
// Zero for ordinary casters, leaving the hardware alpha test as the only arbiter, so
// opaque and cut-out geometry behave exactly as before.
//
// y = the particle variant's density ceiling. Not read here.
//
// z = density ceiling for a dithered caster, and zero to threshold with x instead.
// Dithering is for casters that are genuinely translucent rather than cut out; see below.
float4 ShadowCastParams : register(c0);

// The position is declared first under model 4 and last under model 3, which is the same
// set either way and a different register assignment -- see PS_INPUT_POSITION in
// shadermodel.hlsli. This shader gets an #if of its own rather than that macro because it
// already has a position input to move, and a second one would be a duplicate semantic.
struct PS_INPUT { PIXEL_POSITION_TYPE vpos : PS_PIXEL_POSITION;
                  float4 lightPos : TEXCOORD0; float2 texcoord : TEXCOORD1; };

// Ordered 4x4 dither threshold in (0,1). Character for character the same function
// shadowdepthparticle_ps carries, and it has to stay that way: two translucent casters
// that overlap must share one pattern, or each accepts texels the other rejected and
// the pair comes out denser than either. See the note there for how it is built and
// why the thresholds are offset by half a step. The duplication is deliberate for now
// -- the depth casters have no shared header yet.
float bayer4x4(float2 vpos)
{
    float2 f  = fmod(floor(vpos), 4.0);
    float2 hi = floor(f * 0.5);
    float2 lo = f - hi * 2.0;
    float m2hi = 2.0 * hi.x + 3.0 * hi.y - 4.0 * hi.x * hi.y;
    float m2lo = 2.0 * lo.x + 3.0 * lo.y - 4.0 * lo.x * lo.y;
    return (m2hi + 4.0 * m2lo + 0.5) / 16.0;
}

float4 main(PS_INPUT input) : PS_TARGET
{
    // Clamp below 1.0: a depth of exactly 1.0 would be indistinguishable from the
    // clear value (1.0), making geometry at the far plane appear unwritten.
    float depth = min(input.lightPos.z / input.lightPos.w, 0.9999);
    float texAlpha = SAMPLE_2D(BaseSampler, input.texcoord).a;

    if (ShadowCastParams.z > 0.0)
    {
        // A rotor disc. It is a blended mesh with no alpha test of its own, and its
        // texture is a radial ramp: measured on avcomanche_p, alpha runs 0.94 at the hub
        // down to 0.18 at the blade tips, which is what a spinning blade actually does --
        // the same chord swept round a longer circumference covers less of it.
        //
        // Cutting that at a threshold was the first attempt and it made the shadow the
        // wrong *size*. Alpha crosses the 0.45 the cut used at a little over half the
        // radius, so the disc cast a stubby ragged star reaching nowhere near the blade
        // tips, and the contour was the texture's own noise rather than anything about the
        // rotor. No threshold fixes that: pick a lower one and the whole disc becomes a
        // solid black circle instead, because a threshold can only ever say all or none.
        //
        // So dither, exactly as the particle variant does for smoke, and for the same
        // reason -- partial coverage is the thing being represented, and one depth per
        // texel cannot hold it. Density then follows the disc's own alpha and the extent
        // is the real one.
        //
        // The ceiling is 1 for meshes, unlike the sprites' 0.85, so that the ordinary
        // opaque geometry this path also admits still dithers to solid. See where it is
        // set.
        float coverage = texAlpha * ShadowCastParams.z;
        // vpos is the pixel's own coordinate, and it is the one semantic here whose *value*
        // differs between the models: VPOS in ps_3_0 is the integer pixel coordinate, while
        // SV_Position is the pixel centre, half a texel further on in each axis.
        //
        // bayer4x4 happens to be immune -- it floors before it takes the modulus, and
        // floor(i + 0.5) is i -- so the dither pattern would come out identical either way.
        // PIXEL_POSITION is applied at the call anyway, because that immunity is a property of
        // one line of arithmetic inside the function rather than of the value being passed to
        // it, and a later reader tuning the pattern should not have to rediscover it. Under
        // model 3 the macro is the bare argument, so this costs nothing and moves no bytecode.
        clip(coverage - bayer4x4(PIXEL_POSITION(input.vpos)));

        return depth;
    }

    // clip() discards only on a negative argument, so a cutoff of 0 discards nothing.
    clip(texAlpha - ShadowCastParams.x);

    // ...and the *scene's* alpha test, which this pass has until now left to the
    // hardware -- which is exactly what the note on the return value below describes,
    // and why a cut-out caster works today with ShadowCastParams.x at 0.
    //
    // It is also why this pass would have broken first on D3D11, where nothing performs
    // that stage: a tree billboard would cast its whole rectangle, a wall of shadow
    // instead of a canopy. This shader has had that failure once already. 143060 draws
    // in the largest measured window arrive here with the hardware test enabled.
    //
    // Tested against texAlpha because that is what the return below writes to alpha, and
    // the hardware stage tests what the shader wrote. The dithered branch above returns
    // 1.0 and deliberately does not take part -- see its own note.
    AlphaTest(texAlpha);
    return depth;
}


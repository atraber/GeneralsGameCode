// Shadow-map depth pass (pixel).
//
// Packs the sun-clip-space depth (z/w, in [0,1] for D3D) into RGBA8 so it can live in
// a plain colour render target -- D3D9 depth-stencil textures are not universally
// sampleable, so the lit shaders unpack this instead. Matching unpack lives in the
// unit / terrain pixel shaders.

sampler BaseSampler : register(s0);   // the caster's own texture, for its alpha

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

struct PS_INPUT { float4 lightPos : TEXCOORD0; float2 texcoord : TEXCOORD1;
                  float2 vpos     : VPOS; };

// Split the depth across three 8-bit channels, coarse to fine.
//
// The channel weights are 255, not 256. An 8-bit channel stores k/255, so building the
// split around 256 leaves the coarse channel holding a value it cannot represent
// exactly, and the finer channels are no help -- they carry the true low-order bits,
// not the rounding error the coarse one just made. That cost ~1/510 of depth on every
// texel (measured: a true 0.5000038 came back as 0.501965), which is several times any
// sensible compare bias, so the terrain shadowed itself wherever it was in the map.
//
// Subtracting the next channel down leaves each one an exact multiple of 1/255, so all
// three survive the 8-bit write and the round trip is good to about 1/16M.
float4 packDepth(float depth, float alpha)
{
    float3 enc = frac(depth * float3(1.0, 255.0, 255.0 * 255.0));
    enc.xy -= enc.yz * (1.0 / 255.0);
    return float4(enc, alpha);
}

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

float4 main(PS_INPUT input) : COLOR
{
    // Clamp below 1.0: packDepth(1.0) wraps to (0,0,0) which unpacks to 0 (near),
    // which would make far geometry cast false shadows.
    float depth = min(input.lightPos.z / input.lightPos.w, 0.9999);
    float texAlpha = tex2D(BaseSampler, input.texcoord).a;

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
        clip(coverage - bayer4x4(input.vpos));

        // Alpha out is 1.0, not the coverage: the scene's alpha test is left as the mesh
        // set it, and letting it cut a second time -- against a reference chosen for
        // compositing, on a fragment the pattern has already accepted -- would take texels
        // back out at random. The receivers read only RGB.
        return packDepth(depth, 1.0);
    }

    // clip() discards only on a negative argument, so a cutoff of 0 discards nothing.
    clip(texAlpha - ShadowCastParams.x);
    // Alpha out is the caster's texture alpha, and the depth pass leaves the scene's
    // alpha test alone, so the hardware discards the transparent texels of a cut-out
    // exactly as it does in the visible pass. Writing 1.0 here made a tree billboard
    // cast its whole rectangle -- a wall of shadow instead of a canopy. Meshes with no
    // alpha test ignore alpha entirely, so opaque casters are unaffected.
    return packDepth(depth, texAlpha);
}

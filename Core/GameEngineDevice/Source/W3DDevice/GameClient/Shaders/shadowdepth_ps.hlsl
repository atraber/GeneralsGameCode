// Shadow-map depth pass (pixel, Shader Model 2).
//
// Packs the sun-clip-space depth (z/w, in [0,1] for D3D) into RGBA8 so it can live in
// a plain colour render target -- D3D9 depth-stencil textures are not universally
// sampleable, so the lit shaders unpack this instead. Matching unpack lives in the
// unit / terrain pixel shaders.

sampler BaseSampler : register(s0);   // the caster's own texture, for its alpha

struct PS_INPUT { float4 lightPos : TEXCOORD0; float2 texcoord : TEXCOORD1; };

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

float4 main(PS_INPUT input) : COLOR
{
    // Clamp below 1.0: packDepth(1.0) wraps to (0,0,0) which unpacks to 0 (near),
    // which would make far geometry cast false shadows.
    float depth = min(input.lightPos.z / input.lightPos.w, 0.9999);
    // Alpha out is the caster's texture alpha, and the depth pass leaves the scene's
    // alpha test alone, so the hardware discards the transparent texels of a cut-out
    // exactly as it does in the visible pass. Writing 1.0 here made a tree billboard
    // cast its whole rectangle -- a wall of shadow instead of a canopy. Meshes with no
    // alpha test ignore alpha entirely, so opaque casters are unaffected.
    return packDepth(depth, tex2D(BaseSampler, input.texcoord).a);
}

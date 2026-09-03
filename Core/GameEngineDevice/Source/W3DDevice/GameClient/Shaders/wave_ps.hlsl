// Open-sea water pixel shader (Shader Model 3).
//
// Port of wave.pso (ps_1_1), the partner to wave_vs. Distorts a reflection texture by a
// scrolling bump map, tints it with the vertex colour, and that is the whole surface.
//
// The original was two instructions, one of which was `texbem` -- a fixed-function texture
// addressing mode that D3D9 removed with ps_2_0 and later. It read a signed (du, dv) pair
// from stage 0, ran it through the 2x2 matrix in that stage's BUMPENVMAT registers, and
// added the result to stage 1's coordinates before sampling. There is no instruction for
// that any more, so it is written out below, which is all it ever was.
//
// drawSea sets the matrix to a diagonal (BUMPENVMAT00 = BUMPENVMAT11 = bump scale, the
// off-diagonals zero), so the general 2x2 form is kept here in the constant rather than
// baked to a scalar -- the engine uploads whatever it has set, and a future non-diagonal
// matrix would otherwise be silently dropped.

#include "shadermodel.hlsli"

DECLARE_SAMPLER_2D(BumpSampler, 0);   // U8V8, signed (du, dv)
DECLARE_SAMPLER_2D(ReflectionSampler, 1);   // the mirrored scene

// The stage-1 bump environment matrix, flattened: xy = row 0 (00, 01), zw = row 1 (10, 11).
float4 BumpEnvMat : register(c0);

float4 main(float4 color     : COLOR0,
            float2 bumpUV    : TEXCOORD0,
            float2 reflectUV : TEXCOORD1) : PS_TARGET
{
    // The bump texture is WW3D_FORMAT_U8V8, so these arrive already signed in [-1, 1] --
    // no decode from [0,1] is needed and adding one would double the wave amplitude and
    // shift the whole surface diagonally.
    float2 duv = SAMPLE_2D(BumpSampler, bumpUV).rg;

    // texbem, written out. Note the transposed-looking indexing: D3D defines the offset as
    // u' = u + M00*du + M10*dv and v' = v + M01*du + M11*dv, so the first *column* drives u.
    float2 offset;
    offset.x = BumpEnvMat.x * duv.x + BumpEnvMat.z * duv.y;
    offset.y = BumpEnvMat.y * duv.x + BumpEnvMat.w * duv.y;

    float4 reflection = SAMPLE_2D(ReflectionSampler, reflectUV + offset);

    // Full rgba multiply, as the original's `mul r0, t1, v0`. The alpha matters: drawSea
    // blends SRCALPHA/INVSRCALPHA, so the vertex alpha is what makes the sea fade out at
    // the shoreline rather than ending in a hard edge.
    return reflection * color;
}

// 2D interface pixel shader.
//
// Reproduces the two stage-0 combines the interface actually uses:
//
//   normal    texture * diffuse, which is what ShaderClass leaves set up for a 2D draw.
//   greyscale the desaturating combine Render2DClass built by hand out of two stages, for
//             disabled command buttons and the like.
//
// Everything else about a 2D draw -- the blend, the alpha test, the depth state -- is
// frame-buffer and raster state that the hardware applies after this shader, exactly as it
// did after the fixed-function stages. None of it needs reproducing here.

#include "shadermodel.hlsli"

DECLARE_SAMPLER(BaseSampler, 0);

// x: 1 when stage 0's *colour* combine samples the texture, 0 when it does not.
// y: 1 to desaturate (see below), 0 to pass the colour through.
// z: 1 when stage 0's *alpha* combine samples the texture.
//
// x and z are resolved from the combine rather than from whether a texture is bound: the
// 2D renderer draws lines and solid rectangles by turning texturing off in the ShaderClass
// and leaving whatever texture it last used still attached to the device.
float4 UiCtl : register(c0);

// The weights the fixed-function path arrived at, recovered rather than chosen.
//
// Render2DClass built its greyscale out of two texture stages: stage 0 did MULTIPLYADD with
// D3DTA_TFACTOR|D3DTA_ALPHAREPLICATE for both the addend and the multiplier, which with
// TFACTOR = 0x80A5CA8E (alpha 0x80) computes 0.502 + 0.502*texture -- the standard trick for
// biasing an unsigned texture into the signed [-1,1] range DOT3 expects. Stage 1 then did
// DOTPRODUCT3 of that against the same TFACTOR, and D3D's dot product expands both sides as
// 2*c-1, so the second operand resolves to
//
//     (2*0xA5/255 - 1, 2*0xCA/255 - 1, 2*0x8E/255 - 1) = (0.294, 0.584, 0.114)
//
// which is Rec. 601 luma to within the precision a byte can express. So the artist-facing
// behaviour was always "desaturate", spelled in the only vocabulary two texture stages had.
static const float3 LUMA_WEIGHTS = float3(0.294, 0.584, 0.114);

struct PS_INPUT
{
    float4 color    : COLOR0;
    float2 texcoord : TEXCOORD0;
};

float4 main(PS_INPUT input) : PS_TARGET
{
    float4 texel = SAMPLE_2D(BaseSampler, input.texcoord);

    // Folded out with a select rather than a multiply. The stage can be genuinely unbound,
    // and an unbound sampler reads undefined -- a NaN would survive being multiplied by
    // zero, where a select discards the operand it did not take.
    float3 texRgb = (UiCtl.x > 0.5) ? texel.rgb : float3(1.0, 1.0, 1.0);
    float  texA   = (UiCtl.z > 0.5) ? texel.a   : 1.0;

    // The greyscale path takes the luma of the texture and stops there. That is not a
    // simplification: the fixed-function version ran entirely on the colour stages, which
    // ended at the DOT3, so the vertex diffuse never reached the colour at all. Alpha is
    // untouched by it either way -- the greyscale override left stage 0's alpha combine
    // alone, so a desaturated image keeps exactly the cutout it had.
    float grey = dot(texRgb, LUMA_WEIGHTS);
    float3 rgb = lerp(texRgb * input.color.rgb, grey.xxx, UiCtl.y);

    return float4(rgb, texA * input.color.a);
}

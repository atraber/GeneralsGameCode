// Alpha-mask projection, pixel stage.
//
// Alpha is the entire product of this pass. Both callers disable colour writes
// (D3DRS_COLORWRITEENABLE = ALPHA) around it, so the RGB returned here is discarded by the
// hardware before it reaches the target -- the fixed-function version passed the mask
// texture's colour through for the same non-reason. Returning black states that plainly
// instead of implying the colour matters.

sampler MaskSampler : register(s0);

struct PS_INPUT
{
    float2 texcoord : TEXCOORD0;
};

float4 main(PS_INPUT input) : COLOR
{
    float4 texel = tex2D(MaskSampler, input.texcoord);
    return float4(0.0, 0.0, 0.0, texel.a);
}

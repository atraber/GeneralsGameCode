// Shadow-map depth pass (vertex, Shader Model 2).
//
// Transforms geometry into the sun's clip space so the pixel shader can write its
// depth into the shadow map. Only reads POSITION, so it works for every mesh/terrain
// vertex format (position is always the first element). SunVP = sun view * ortho
// projection; World is the per-draw object transform.

#include "shadermodel.hlsli"

CONSTANTS_BEGIN(ShadowDepthVsConstants)
row_major float4x4 SunVP : CREGISTER(0);
row_major float4x4 World : CREGISTER(4);
CONSTANTS_END

// The texture coordinates ride along so the pixel shader can read the base texture's
// alpha: cut-out foliage has to cast its silhouette rather than its quad.
//
// The input position stays POSITION -- it names an element of the vertex layout, and that
// is the same in both models. Only the position handed on to the rasteriser becomes
// SV_POSITION.
struct VS_INPUT  { float4 position : POSITION; float2 texcoord : TEXCOORD0; };
struct VS_OUTPUT { float4 position : VS_POSITION; float4 lightPos : TEXCOORD0;
                   float2 texcoord : TEXCOORD1; };

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    float4 worldPos = mul(input.position, World);
    output.position = mul(worldPos, SunVP);
    output.lightPos = output.position;   // forward clip pos so the PS can read z/w
    output.texcoord = input.texcoord;
    return output;
}

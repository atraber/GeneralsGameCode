// Shadow-map depth pass (vertex, Shader Model 2).
//
// Transforms geometry into the sun's clip space so the pixel shader can write its
// depth into the shadow map. Only reads POSITION, so it works for every mesh/terrain
// vertex format (position is always the first element). SunVP = sun view * ortho
// projection; World is the per-draw object transform.

row_major float4x4 SunVP : register(c0);
row_major float4x4 World : register(c4);

// The texture coordinates ride along so the pixel shader can read the base texture's
// alpha: cut-out foliage has to cast its silhouette rather than its quad.
struct VS_INPUT  { float4 position : POSITION; float2 texcoord : TEXCOORD0; };
struct VS_OUTPUT { float4 position : POSITION; float4 lightPos : TEXCOORD0;
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

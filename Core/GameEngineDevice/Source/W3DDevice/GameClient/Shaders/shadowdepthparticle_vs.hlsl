// Shadow-map depth pass for particle sprites (vertex, Shader Model 3).
//
// Same transform as shadowdepth_vs -- the quads arrive in world space with an identity
// World, so SunVP does all of it -- and one extra input: the vertex colour.
//
// That colour is why particles cannot share the plain depth shader. A particle's opacity
// is the product of its texture and its per-particle alpha, and the per-particle half is
// the entire life story of the sprite: it fades in as the puff forms and out as it
// dissipates. The shared shader reads only the texture, so every puff would cast at full
// strength right up to the frame it vanished, and a dust cloud would end with a shadow
// snapping off the ground.

#include "shadermodel.hlsli"

row_major float4x4 SunVP : register(c0);
row_major float4x4 World : register(c4);

struct VS_INPUT  { float4 position : POSITION; float4 color : COLOR0;
                   float2 texcoord : TEXCOORD0; };
struct VS_OUTPUT { float4 position : POSITION; float4 lightPos : TEXCOORD0;
                   float2 texcoord : TEXCOORD1; float  alpha    : TEXCOORD2; };

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    float4 worldPos = mul(input.position, World);
    output.position = mul(worldPos, SunVP);
    output.lightPos = output.position;   // forward clip pos so the PS can read z/w
    output.texcoord = input.texcoord;
    output.alpha    = input.color.a;
    return output;
}

// PBR unit vertex shader (Shader Model 3).
//
// Same clip-space transform as the M3 unit shader, but forwards world-space
// position and normal to the pixel shader so it can run a metallic-roughness BRDF
// there.
//
// Note c4 differs from the M3 unit shader, which shades in camera space and so
// receives world*view there. This one shades in world space -- the pixel shader
// reconstructs the view vector from a world-space camera position and indexes a
// world-space environment cubemap -- so the wrapper feeds it the plain world
// matrix and rotates the light directions to match.

#include "shadermodel.hlsli"

row_major float4x4 WorldViewProj : register(c0);
row_major float4x4 World         : register(c4);

struct VS_INPUT
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float4 color    : COLOR0;   // vertex diffuse (pre-lit colour / alpha)
    float2 texcoord : TEXCOORD0;
};

struct VS_OUTPUT
{
    float4 position : POSITION;
    float4 color    : COLOR0;
    float2 texcoord : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float3 worldNrm : TEXCOORD2;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    output.position = mul(float4(input.position, 1.0), WorldViewProj);
    output.worldPos = mul(float4(input.position, 1.0), World).xyz;
    output.worldNrm = mul(input.normal, (float3x3)World);
    output.color    = input.color;
    output.texcoord = input.texcoord;
    return output;
}

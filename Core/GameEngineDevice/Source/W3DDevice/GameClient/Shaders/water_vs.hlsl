// Water vertex shader.
//
// The water surface is built on the CPU as world-space trapezoids (standing water) or
// as a strip down a river polygon, both with a world transform of identity -- see
// drawTrapezoidWater / drawRiverWater. The transform is still applied here rather than
// assumed, so the same pair of shaders can serve the grid-mesh water if that path is
// routed later, and so nothing silently breaks if a caller ever sets a world matrix.
//
// Everything interesting happens in the pixel shader. All this has to do is get the
// world position there: the depth lookup, the shroud projection, the noise projection
// and the wave field are all functions of it, which is what lets one shader replace
// the fixed-function stack of camera-space texture generations the old path used.

#include "shadermodel.hlsli"

row_major float4x4 WorldViewProj : register(c0);
row_major float4x4 World         : register(c4);

struct VS_INPUT
{
    float3 position : POSITION;
    float3 normal   : NORMAL;     // always (0,0,1); the real normal is built per pixel
    float4 color    : COLOR0;     // CPU-lit water tint, alpha = the INI opacity
    float2 uv0      : TEXCOORD0;  // water texture (scrolled/wobbled on the CPU)
    float2 uv1      : TEXCOORD1;  // bump set for standing water, edge ramp for rivers
};

struct VS_OUTPUT
{
    float4 position : POSITION;
    float4 color    : COLOR0;
    float2 uv0      : TEXCOORD0;
    float2 uv1      : TEXCOORD1;
    float3 worldPos : TEXCOORD2;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    output.position = mul(float4(input.position, 1.0), WorldViewProj);
    output.worldPos = mul(float4(input.position, 1.0), World).xyz;
    output.color    = input.color;
    output.uv0      = input.uv0;
    output.uv1      = input.uv1;
    return output;
}

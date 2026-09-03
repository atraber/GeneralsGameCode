// Fullscreen / screen-space quad vertex shader.
//
// The post-process chain -- the bloom passes, the tone map, the black-and-white filter, the
// cross-fade, the motion blur, the smudge and the profiler capture -- all draw the same
// thing: two triangles covering a rectangle given in screen pixels, sampling one or two
// textures. Every one of them did it with D3DFVF_XYZRHW, a position already in screen space
// that the fixed-function pipeline passes through untransformed.
//
// That is a fixed-function draw even when a pixel shader is bound, and D3D9 will pair the
// two happily, which is why it went unnoticed for so long: the direct-draw census asked only
// whether a pixel shader was bound and reported the whole chain as programmable while its
// vertex side was not merely fixed function but skipped entirely. Measured, that was 3000
// draws per 600-frame window, five a frame, every one of them fixed function.
//
// A vertex shader cannot consume a transformed position -- D3D9 reserves the POSITIONT
// semantic to the fixed-function pipeline -- so the callers now supply an ordinary
// untransformed position in the same pixel coordinates they always computed, and the matrix
// below carries the mapping to clip space that XYZRHW used to imply. See
// DX8Wrapper::Bind_Screen_Quad_Shader, which builds it from the viewport, the same rectangle
// XYZRHW was defined against.
//
// Deliberately not ui_vs, though the two are close. ui_vs pairs with ui_ps and owns both
// halves of the draw; these callers bring their own pixel shader and want only the vertex
// side replaced, so this leaves the pixel shader alone. It also carries a second texture
// coordinate set, which ui_vs does not: the bloom passes sample the scene and the bloom
// target with different coordinates in a single draw.

#include "shadermodel.hlsli"

row_major float4x4 PixelsToClip : register(c0);

// The FVF doubles as the vertex declaration, so every input declared here must be present in
// the buffer. All the callers are standardised on position + diffuse + two coordinate sets;
// where a pass has only one meaningful set it writes the same values to both, which costs
// eight bytes on four vertices and saves a second shader and a second FVF to keep in step.
struct VS_INPUT
{
    float3 position : POSITION;
    float4 color    : COLOR0;
    float2 texcoord0 : TEXCOORD0;
    float2 texcoord1 : TEXCOORD1;
};

struct VS_OUTPUT
{
    float4 position : VS_POSITION;
    float4 color    : COLOR0;
    float2 texcoord0 : TEXCOORD0;
    float2 texcoord1 : TEXCOORD1;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    output.position  = mul(float4(input.position, 1.0), PixelsToClip);
    output.color     = input.color;
    output.texcoord0 = input.texcoord0;
    output.texcoord1 = input.texcoord1;
    return output;
}

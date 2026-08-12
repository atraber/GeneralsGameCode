// 2D interface vertex shader.
//
// The interface is the largest body of fixed-function drawing left in the frame -- the
// control bar, the command bar, every button, every glyph of text -- and it is also the
// simplest, because none of the transform & lighting pipeline applies to it. Render2DClass
// builds its vertices in clip space and sets world, view and projection all to identity;
// there is no lighting, no texture generation and no second stage.
//
// So this is a passthrough with a matrix in front of it. The matrix is passed rather than
// assumed: it is identity for everything drawing today, but concatenating it costs one
// vertex-shader instruction, and a 2D drawer that does set up a projection (an orthographic
// one in pixel coordinates, say) then routes here unchanged instead of silently drawing
// nothing.

row_major float4x4 WorldViewProj : register(c0);  // clip space; identity in practice

// The FVF doubles as the vertex declaration, so every input declared here must be present
// in the buffer -- but a subset is fine, and this is one. Render2DClass allocates
// DX8_FVF_XYZNDUV2 (position, normal, diffuse, two texture coordinate sets) and fills the
// position, the diffuse and the first coordinate set. The normal and the second set are
// left out of the declaration below rather than declared and ignored.
struct VS_INPUT
{
    float3 position : POSITION;
    float4 color    : COLOR0;      // vertex diffuse: the tint and the alpha
    float2 texcoord : TEXCOORD0;
};

struct VS_OUTPUT
{
    float4 position : POSITION;
    float4 color    : COLOR0;
    float2 texcoord : TEXCOORD0;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    output.position = mul(float4(input.position, 1.0), WorldViewProj);
    output.color    = input.color;
    output.texcoord = input.texcoord;
    return output;
}

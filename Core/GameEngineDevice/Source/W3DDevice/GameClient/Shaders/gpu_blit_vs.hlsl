// Fullscreen triangle vertex shader for GPU blit / StretchRect.
//
// Draws a single fullscreen triangle covering the viewport without needing a vertex buffer.
// Generates clip-space coordinates from SV_VertexID (0, 1, 2) and maps UV coordinates
// according to the source rect specified in BlitCB (cbuffer b0).

cbuffer BlitCB : register(b0)
{
    float4 uv_rect;
};

struct VS_OUTPUT
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD;
};

VS_OUTPUT main(uint vertex_id : SV_VertexID)
{
    VS_OUTPUT output;
    float2 uv = float2((vertex_id == 1) ? 2.0f : 0.0f, (vertex_id == 2) ? 2.0f : 0.0f);
    output.position = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
    output.texcoord = uv * uv_rect.zw + uv_rect.xy;
    return output;
}

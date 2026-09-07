// Road vertex shader.
//
// Roads are decals laid on the terrain: built in world space, pre-lit (the lighting is
// baked into the vertex colour on the CPU) and alpha-blended into the ground they sit on.
// They want exactly what the ground gets -- the same scrolling cloud layer and static
// noise layer, projected the same way, and the same cast shadows -- so this mirrors
// terrain_vs. The difference is that a road carries a single UV set, having no second
// tile to cross-blend with, and that its alpha survives to the frame-buffer blend.
//
// The overlay coordinate is the world XY scaled by STRETCH_FACTOR: the fixed-function
// road pass generated it with a camera-space projection whose matrix cancels the view,
// which comes to the same thing (and to the same thing the terrain computes, which is
// what keeps a cloud shadow continuous across the edge of a road).

#include "shadermodel.hlsli"

#include "constants.hlsli"

row_major float4x4 WorldViewProj : register(c0);
float4 CloudOffset : register(c4);         // xy = cloud layer A drift, zw = layer B (world units)
row_major float4x4 SunVP : register(c5);   // sun view*projection (road verts are world-space)


struct VS_INPUT
{
    float3 position : POSITION;
    float4 color    : COLOR0;     // pre-baked road lighting; alpha fades the road edges
    float2 uv0      : TEXCOORD0;
};

struct VS_OUTPUT
{
    float4 position : VS_POSITION;
    float4 color    : COLOR0;
    float2 uv0      : TEXCOORD0;
    float4 cloudUV  : TEXCOORD2;   // xy = layer A, zw = layer B
    float2 noiseUV  : TEXCOORD3;
    float4 lightPos : TEXCOORD4;   // position in the sun's clip space (for shadowing)
    // C5.3. The raw world position, at the same semantic terrain_vs carries it on, so that
    // the pixel shader can find its cluster and recover the ground's geometric normal from
    // its own derivatives. This is the ONE new interpolant the clustered path costs the
    // road, and it costs one and not two because a road, like the terrain, has no vertex
    // normal to hand over -- the normal is reconstructed on the other side.
    //
    // Appended, not inserted: model 5 links the stages by register as well as by semantic
    // and fxc numbers a signature by packing declarations in order, so putting this
    // anywhere earlier renumbers every interpolant after it and stops the pair linking --
    // silently, with both halves still compiling clean.
    float3 worldPos : TEXCOORD5;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    // Road vertices are already in world space, as the terrain's are, so they reproject
    // into the sun's clip space directly.
    output.lightPos = mul(float4(input.position, 1.0), SunVP);
    output.position = mul(float4(input.position, 1.0), WorldViewProj);
    output.color    = input.color;
    output.uv0      = input.uv0;

    float2 worldXY  = input.position.xy * STRETCH_FACTOR;
    output.noiseUV  = worldXY;                   // static noise-detail layer
    // Identical to terrain_vs, deliberately: a cloud shadow must cross the edge of a
    // road without changing shape or position.
    output.cloudUV  = float4((input.position.xy + CloudOffset.xy) / CLOUD_PERIOD_A,
                             (input.position.xy + CloudOffset.zw) / CLOUD_PERIOD_B);

    // Unscaled and untransformed, exactly as terrain_vs hands its own over. Road vertices
    // are already in world space -- which is why lightPos above reprojects straight by
    // SunVP with no world matrix -- so there is nothing to do to it, and no constant to
    // add. The STRETCH_FACTOR copy above is for the overlay lattice and is not this.
    output.worldPos = input.position;
    return output;
}

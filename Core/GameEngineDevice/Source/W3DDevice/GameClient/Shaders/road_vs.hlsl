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

row_major float4x4 WorldViewProj : register(c0);
float4 CloudOffset : register(c4);         // xy = cloud layer A drift, zw = layer B (world units)
row_major float4x4 SunVP : register(c5);   // sun view*projection (road verts are world-space)

// 1 / (63 * MAP_XY_FACTOR / 2), MAP_XY_FACTOR = 10  ->  1/315
static const float STRETCH_FACTOR = 1.0 / 315.0;

// Cloud shadow. Two layers, each projected over thousands of world units and drifting
// at its own rate, so the field does not read as one texture sliding rigidly across the
// map. CloudOffset carries both layers' drift in *world* units; dividing here rather
// than scrolling in UV means one wind speed means the same thing at either scale.
static const float CLOUD_PERIOD_A = 1800.0;
static const float CLOUD_PERIOD_B = 2900.0;


struct VS_INPUT
{
    float3 position : POSITION;
    float4 color    : COLOR0;     // pre-baked road lighting; alpha fades the road edges
    float2 uv0      : TEXCOORD0;
};

struct VS_OUTPUT
{
    float4 position : POSITION;
    float4 color    : COLOR0;
    float2 uv0      : TEXCOORD0;
    float4 cloudUV  : TEXCOORD2;   // xy = layer A, zw = layer B
    float2 noiseUV  : TEXCOORD3;
    float4 lightPos : TEXCOORD4;   // position in the sun's clip space (for shadowing)
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
    return output;
}

// Terrain vertex shader.
//
// Transforms terrain tiles (lighting is baked into the vertex colour on the CPU)
// and generates the projected coordinates for the cloud (scrolling) and noise
// (static) overlay layers. The overlay coordinate is simply the world XY scaled
// by STRETCH_FACTOR (the fixed-function camera-space transform cancels the view),
// with a scroll offset for the cloud layer.

row_major float4x4 WorldViewProj : register(c0);
float4 CloudOffset : register(c4);   // xy = cloud scroll offset
row_major float4x4 SunVP : register(c5);   // sun view*projection (terrain verts are world-space)

// 1 / (63 * MAP_XY_FACTOR / 2), MAP_XY_FACTOR = 10  ->  1/315
static const float STRETCH_FACTOR = 1.0 / 315.0;

struct VS_INPUT
{
    float3 position : POSITION;
    float4 color    : COLOR0;     // pre-baked terrain lighting
    float2 uv0      : TEXCOORD0;  // base tile
    float2 uv1      : TEXCOORD1;  // neighbour tile (cross-blend)
};

struct VS_OUTPUT
{
    float4 position : POSITION;
    float4 color    : COLOR0;
    float2 uv0      : TEXCOORD0;
    float2 uv1      : TEXCOORD1;
    float2 cloudUV  : TEXCOORD2;
    float2 noiseUV  : TEXCOORD3;
    float4 lightPos : TEXCOORD4;   // position in the sun's clip space (for shadowing)
    float3 worldPos : TEXCOORD5;   // raw world position; XY drives the tiling lattice and the
                                   // detail projection, Z lets the pixel shader recover the
                                   // geometric normal from its own derivatives
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    // Terrain vertices are already in world space, so reproject straight by SunVP.
    output.lightPos = mul(float4(input.position, 1.0), SunVP);
    // Straight WVP transform (no depth nudge). The fixed-function shroud pass depth-
    // tests co-planar against this with LESSEQUAL; rather than trying (and failing) to
    // bit-match its depth from a shader, the shroud pass itself carries a slope-scaled
    // depth bias so it reliably lands on the surface. The stencil shadow volumes also
    // test against this terrain, so its depth is left un-nudged for them.
    output.position = mul(float4(input.position, 1.0), WorldViewProj);
    output.color    = input.color;
    output.uv0      = input.uv0;
    output.uv1      = input.uv1;

    float2 stretched = input.position.xy * STRETCH_FACTOR;
    output.cloudUV   = stretched + CloudOffset.xy;  // scrolling cloud layer
    output.noiseUV   = stretched;                   // static noise-detail layer

    // Unscaled, so the tiling lattice and the detail layer can be sized in world units
    // rather than in whatever the overlay stretch happens to be.
    output.worldPos = input.position;
    return output;
}

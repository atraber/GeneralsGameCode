// Unit vertex shader for geometry that carries no normals.
//
// Roads, tank tracks and similar ground decals use DX8_FVF_XYZDUV1 -- position, diffuse
// and one texture coordinate set, no NORMAL. They are pre-lit: their lighting is baked
// into the vertex colour, so nothing here needs a normal. unit_vs cannot be used for them
// even so, because the FVF doubles as the vertex declaration and a shader that declares an
// input the FVF does not supply is a validation hazard -- the same reason unit_detail_ps
// exists separately rather than sampling a stage that may have no texture.
//
// Without this these draws stayed on the fixed-function pipeline, which meant they never
// sampled the shadow map: a road ran across shadowed ground at full brightness.
//
// Constant layout matches unit_vs where it overlaps, so the wrapper feeds both the same
// way. The lighting and material registers are simply not read here.

row_major float4x4 WorldViewProj : register(c0);  // object -> clip space
row_major float4x4 WorldView     : register(c4);  // object -> camera (view) space

float4 SceneAmbient  : register(c16);  // D3DRS_AMBIENT equivalent
float4 LightingParams : register(c17); // x: 2 = texture-only, 1 = lit, 0 = pre-lit
                                       // y: 1 when the ambient source is the vertex colour
float4 MatAmbient    : register(c18);
float4 MatEmissive   : register(c19);
float4 TexGenCtl : register(c21);      // see unit_vs; modes 2 and 3 need a normal and are
                                       // never routed here
row_major float4x4 TexMatrix0 : register(c24);
row_major float4x4 TexMatrix1 : register(c28);
row_major float4x4 WorldSunVP : register(c32);  // object -> sun clip space

struct VS_INPUT
{
    float3 position : POSITION;
    float4 color    : COLOR0;      // baked vertex lighting
    float2 texcoord : TEXCOORD0;
};

struct VS_OUTPUT
{
    float4 position  : POSITION;
    float4 color     : COLOR0;
    float2 texcoord  : TEXCOORD0;
    float2 texcoord1 : TEXCOORD1;
    float4 lightPos  : TEXCOORD2;  // position in the sun's clip space (cast shadows)
};

// Passthrough or camera-space position only -- the normal-based sources cannot be
// reproduced without a normal, so the wrapper keeps those draws off this path entirely.
//
// The padding matches unit_vs and, through it, what the fixed function pipeline hands
// the texture matrix: a 2-D mesh coordinate set becomes (u, v, 1, 0), so the translation
// the 2-D mappers write into _31/_32 is actually read. See the longer note there.
float4 Select_TexGen_Source(float mode, float2 meshUV, float3 viewPos)
{
    float w0 = saturate(1.0 - abs(mode - 0.0));
    float w1 = saturate(1.0 - abs(mode - 1.0));
    return w0 * float4(meshUV, 1.0, 0.0)
         + w1 * float4(viewPos, 1.0);
}

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;

    output.position = mul(float4(input.position, 1.0), WorldViewProj);

    // Texture-only overlay passes composite over an already-shaded base, so they neither
    // take the vertex colour nor receive the shadow a second time. Everything else here is
    // pre-lit ground geometry, which does receive.
    float textureOnly = (LightingParams.x > 1.5) ? 1.0 : 0.0;
    output.color = lerp(input.color, float4(1.0, 1.0, 1.0, 1.0), textureOnly);

    float3 viewPos = mul(float4(input.position, 1.0), WorldView).xyz;
    float4 gen0 = Select_TexGen_Source(TexGenCtl.x, input.texcoord, viewPos);
    float4 gen1 = Select_TexGen_Source(TexGenCtl.y, input.texcoord, viewPos);

    output.texcoord  = lerp(gen0.xy, mul(gen0, TexMatrix0).xy, TexGenCtl.z);
    output.texcoord1 = lerp(gen1.xy, mul(gen1, TexMatrix1).xy, TexGenCtl.w);

    // Selected rather than lerped, so a degenerate sun matrix cannot reach a draw that is
    // not receiving -- lerp still multiplies the far operand by zero, and 0 * inf is NaN.
    float4 sunClip = mul(float4(input.position, 1.0), WorldSunVP);
    output.lightPos = (textureOnly > 0.5) ? float4(2.0, 2.0, 2.0, 1.0) : sunClip;
    return output;
}

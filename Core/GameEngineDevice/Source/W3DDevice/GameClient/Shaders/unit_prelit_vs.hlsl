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

#include "shadermodel.hlsli"

row_major float4x4 WorldViewProj : register(c0);  // object -> clip space
row_major float4x4 WorldView     : register(c4);  // object -> camera (view) space

float4 SceneAmbient  : register(c16);  // D3DRS_AMBIENT equivalent
float4 LightingParams : register(c17); // x: 2 = texture-only, 1 = lit, 0 = pre-lit
                                       // y: 1 when the ambient source is the vertex colour
                                       // z: 1 for effect geometry -- see unit_vs
                                       // w: emissive gain for additive effects -- see unit_vs
float4 MatAmbient    : register(c18);
float4 MatEmissive   : register(c19);
float4 TexGenCtl : register(c21);      // see unit_vs; modes 2 and 3 need a normal and are
                                       // never routed here
row_major float4x4 TexMatrix0 : register(c24);
row_major float4x4 TexMatrix1 : register(c28);

// The cloud shadow is projected straight down, so all a vertex shader needs to hand on is
// where this pixel sits on the ground plane. Only two columns of the object->world matrix
// are required for that, which is why they arrive as a pair of vectors rather than a whole
// matrix -- this path otherwise never needs world space.
float4 WorldAxisX : register(c22);   // object -> world X
float4 WorldAxisY : register(c23);   // object -> world Y
// The third column, added by C5.2 alongside unit_vs's. This variant does not take
// clustered local light -- see where worldNrm is written -- but it still has to hand the
// pixel shader a real world position rather than a plausible-looking lie, because the
// signature is shared and a later stage reading it would have no way to tell.
float4 WorldAxisZ : register(c37);   // object -> world Z

row_major float4x4 WorldSunVP : register(c32);  // object -> sun clip space
// c36 (ShadowMeshParams in unit_vs) is deliberately not read here. Its normal offset
// needs a normal, which is the one thing this variant's geometry does not carry; the
// wrapper therefore zeroes the offset for these draws and feeds unit_ps the terrain's
// blanket depth bias instead -- which suits them, being ground decals.

struct VS_INPUT
{
    float3 position : POSITION;
    float4 color    : COLOR0;      // baked vertex lighting
    float2 texcoord : TEXCOORD0;
};

struct VS_OUTPUT
{
    float4 position  : VS_POSITION;
    // TEXCOORD, not COLOR0 -- ps_3_0 clamps COLOR interpolators to [0,1] and this colour
    // may now exceed it. See the note on the same field in unit_vs; both feed the same
    // pixel shaders, so the two signatures have to agree.
    float4 color     : TEXCOORD4;
    float2 texcoord  : TEXCOORD0;
    float2 texcoord1 : TEXCOORD1;
    float4 lightPos  : TEXCOORD2;  // position in the sun's clip space (cast shadows)
    float3 cloudPos  : TEXCOORD3;  // xy = world position on the ground plane, z = receives sun
    // The clip position again, so the pixel shader can find itself on screen and read the
    // depth prepass. POSITION is not readable in a pixel shader, hence the copy.
    float4 screenPos : TEXCOORD5;
    // C5.2, matching unit_vs member for member and in the same declaration order. Model 5
    // links the stages by register as well as by semantic, so a variant that stopped one
    // interpolant short of the pixel shader's input signature would not draw at all --
    // and it would not fail to compile either, because each half is valid on its own.
    float3 worldPos  : TEXCOORD6;
    float4 worldNrm  : TEXCOORD7;   // xyz = world normal, w = takes clustered light
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
    output.screenPos = output.position;

    // Texture-only overlay passes composite over an already-shaded base, so they neither
    // take the vertex colour nor receive the shadow a second time. Everything else here is
    // pre-lit ground geometry, which does receive.
    float textureOnly = (LightingParams.x > 1.5) ? 1.0 : 0.0;
    output.color = lerp(input.color, float4(1.0, 1.0, 1.0, 1.0), textureOnly);

    // Effect geometry receives neither, for the same reason a texture-only overlay does
    // not: it is not lit by the sun, so nothing that blocks the sun may darken it. Tank
    // tracks and scorch marks arrive here -- normal-less, pre-lit, blended -- and a track
    // is a mark *on* ground that is already shadowed, so shading it again doubles it.
    float noSun = max(textureOnly, (LightingParams.z > 0.5) ? 1.0 : 0.0);

    // Emissive gain for additive effect draws -- see the long note in unit_vs. Ground
    // decals reach this shader alongside the effects, and they are alpha-blended rather
    // than additive, so the wrapper leaves them at 1.
    output.color.rgb *= max(LightingParams.w, 1.0);

    float3 viewPos = mul(float4(input.position, 1.0), WorldView).xyz;
    float4 gen0 = Select_TexGen_Source(TexGenCtl.x, input.texcoord, viewPos);
    float4 gen1 = Select_TexGen_Source(TexGenCtl.y, input.texcoord, viewPos);

    output.texcoord  = lerp(gen0.xy, mul(gen0, TexMatrix0).xy, TexGenCtl.z);
    output.texcoord1 = lerp(gen1.xy, mul(gen1, TexMatrix1).xy, TexGenCtl.w);

    // Selected rather than lerped, so a degenerate sun matrix cannot reach a draw that is
    // not receiving -- lerp still multiplies the far operand by zero, and 0 * inf is NaN.
    float4 sunClip = mul(float4(input.position, 1.0), WorldSunVP);
    output.lightPos = (noSun > 0.5) ? float4(2.0, 2.0, 2.0, 1.0) : sunClip;

    float4 objectPos = float4(input.position, 1.0);
    float3 worldPos  = float3(dot(objectPos, WorldAxisX),
                              dot(objectPos, WorldAxisY),
                              dot(objectPos, WorldAxisZ));

    // Same gate as the cast shadow above: whatever the sun does not light, no cloud may
    // take the sun away from.
    output.cloudPos = float3(worldPos.xy, (noSun > 0.5) ? 0.0 : 1.0);

    output.worldPos = worldPos;

    // **PRE-LIT GEOMETRY TAKES NO CLUSTERED LOCAL LIGHT, AND THE ZEROES SAY SO TWICE.**
    //
    // The gate is 0 for two independent reasons, either of which alone would settle it.
    // The first is the one this whole variant exists for: this geometry carries no NORMAL.
    // Roads, tank tracks and scorch marks reach here on DX8_FVF_XYZDUV1, and a diffuse
    // light term is N.L -- there is no N to take it against and nothing sensible to invent
    // (a flat +Z would light a road on a slope as though it were level, and would light
    // the *underside* of anything that is not a ground decal). The second is what "pre-lit"
    // means: the colour is already decided and no lighting equation should touch it, which
    // is the same reason these draws are kept off unit_pbr_ps.
    //
    // Note this is NOT the same decision as the cloud/shadow gate above. An ordinary
    // ground decal does receive the sun's shadow -- that was the whole point of giving
    // these draws a shader at all, since on fixed function a road ran across shadowed
    // ground at full brightness -- so cloudPos.z stays 1 for it while this stays 0. That
    // is why the clustered gate rides in its own component rather than being read back off
    // cloudPos.z.
    //
    // The road under a street lamp is a real gap and it is C7's, not this stage's: C7
    // deletes the CPU dynamic-light path that lights these decals today, and it is the
    // stage that has to decide whether ground decals grow a normal or are lit from the
    // terrain underneath them.
    output.worldNrm = float4(0.0, 0.0, 0.0, 0.0);
    return output;
}

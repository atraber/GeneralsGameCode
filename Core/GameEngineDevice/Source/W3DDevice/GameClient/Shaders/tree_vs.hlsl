// Tree / grass / bush vertex shader (Shader Model 3).
//
// Port of Trees.nvv, the last vs_1_1 shader in the renderer. It draws every tree, bush and
// clump of grass on the map, and its job is the wind: the mesh is authored upright and this
// shader leans it, by an amount that grows with height above the tree's own base so the
// trunk stays planted while the crown moves.
//
// The vertex format is DX8_FVF_XYZNDUV1, and the NORMAL slot does not carry a normal. Trees
// are drawn with baked lighting and never needed one, so the three components were
// repurposed and the name is now simply wrong -- which is worth knowing before reading any
// arithmetic below that appears to be shading:
//   normal.x = sway type, an index into the Sway table (1..MAX_SWAY_TYPES; 0 = no sway)
//   normal.y = colour scale, the per-tree darkening applied when a unit pushes it aside
//   normal.z = world Z of the base of this tree, i.e. where it meets the ground
//
// Constant layout is inherited from the assembly version and from the engine code that
// still uploads it, so the registers are not free to move.

// Composite world*view*projection, uploaded already transposed by W3DTreeBuffer.
//
// Declared row_major and multiplied matrix-first, which together reproduce the assembly's
// `m4x4 oPos, r1, c4` exactly: that instruction dots the position against c4, c5, c6, c7 in
// turn, and mul(M, v) on a row_major matrix is the same four dot products. Writing the more
// familiar mul(v, M) here would silently transpose the transform.
#include "shadermodel.hlsli"

row_major float4x4 WorldViewProj : register(c4);

// Sway offsets, indexed by the vertex's sway type. Entry 0 is a zero vector -- trees are
// assigned types 1..MAX_SWAY_TYPES, so index 0 is the "no sway" slot the engine uploads to
// keep an out-of-range index harmless rather than a garbage lean.
#define MAX_SWAY_TYPES 10
float4 Sway[1 + MAX_SWAY_TYPES] : register(c8);   // c8 .. c18

float4 ShroudOffset : register(c32);   // xy added to the world position
float4 ShroudScale  : register(c33);   // xy scales it into shroud texture space

struct VS_INPUT
{
    float3 position : POSITION;
    float3 sway     : NORMAL;      // NOT a normal -- see the note above
    float4 color    : COLOR0;      // baked vertex lighting
    float2 texcoord : TEXCOORD0;
};

struct VS_OUTPUT
{
    float4 position : VS_POSITION;
    float4 color    : COLOR0;
    float2 texcoord : TEXCOORD0;   // tree texture
    float2 shroudUV : TEXCOORD1;   // shroud lookup
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;

    // How far this vertex sits above the base of its own tree. The lean is proportional to
    // it, which is what pins the trunk to the ground and lets the crown travel: a vertex at
    // the base gets zero displacement no matter how hard the wind is blowing.
    float heightAboveBase = input.position.z - input.sway.z;

    // Relative addressing, as in the original. The index comes from vertex data and is
    // clamped rather than trusted: a stray value would read a constant register belonging to
    // something else (the shroud transform lives at c32) and lean the tree by whatever
    // happened to be there.
    int swayIndex = clamp((int)input.sway.x, 0, MAX_SWAY_TYPES);
    float4 swayVec = Sway[swayIndex];

    // Applied to all three axes, matching the assembly. The Z term is small but it is what
    // makes a swaying tree dip rather than merely shear.
    float3 swayed = input.position + heightAboveBase * swayVec.xyz;
    output.position = mul(WorldViewProj, float4(swayed, 1.0));

    // Per-tree darkening, applied to colour only. The alpha is the texture's cutout and is
    // carried through untouched -- scaling it would eat away the leaves of a tree that a
    // unit merely brushed past.
    output.color = float4(input.color.rgb * input.sway.y, input.color.a);

    output.texcoord = input.texcoord;

    // Shroud coordinates come from the *unswayed* position, deliberately. The shroud is a
    // property of the ground the tree stands on, not of the tree, so a crown leaning several
    // feet downwind must not drag its own fog-of-war lookup along with it.
    output.shroudUV = (input.position.xy + ShroudOffset.xy) * ShroudScale.xy;

    return output;
}

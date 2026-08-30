// Surface-normal inspector (vertex, Shader Model 3).
//
// Transforms a mesh's vertex normal into camera space and hands it to debugnormal_ps, for
// DEBUG_VIS_NORMALS. Bound in place of unit_vs for draws that carry a normal and took the
// mesh path.
//
// Why a vertex shader of its own rather than a pixel shader reading what unit_vs already
// produces: unit_vs does not produce it. It lights per vertex and forwards the *result*,
// so by the time the pixel stage sees anything the normal has been consumed. Only
// unit_pbr_vs carries a normal through, and routing to that shader depends on ORM maps
// and the PBR mask -- a mode built on it would be showing which meshes reached the PBR
// path, which is mode MESH_TECHNIQUE's job and not this one's.
//
// Camera space rather than world space, and again not arbitrarily: what a normal is being
// judged against here is the view. A surface facing the camera is the same colour wherever
// it sits on the map, so two instances of one mesh can be compared directly, and a normal
// pointing away from the viewer reads as dark rather than as some other hue.

// Deliberately the same registers unit_vs declares, so this shader can be substituted for
// it without re-uploading anything: the routing block has already put this draw's matrices
// in c0 and c4 by the time the substitution happens. Keep these in step with unit_vs.
row_major float4x4 WorldViewProj : register(c0);  // object -> clip space
row_major float4x4 WorldView     : register(c4);  // object -> camera space

struct VS_INPUT
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
};

struct VS_OUTPUT
{
    float4 position : POSITION;
    float3 normal   : TEXCOORD0;   // camera space, unnormalised (the PS normalises)
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    output.position = mul(float4(input.position, 1.0), WorldViewProj);

    // Rotation only -- a normal is a direction, so the translation row must not apply.
    // This is the inverse-transpose only for a transform without non-uniform scale, which
    // is what mesh instance transforms are here; the same assumption unit_vs makes when it
    // lights with this matrix.
    output.normal = mul(input.normal, (float3x3)WorldView);
    return output;
}

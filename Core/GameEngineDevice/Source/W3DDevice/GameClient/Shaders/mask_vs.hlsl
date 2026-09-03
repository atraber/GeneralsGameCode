// Alpha-mask projection, vertex stage.
//
// Drives the screen cross-fade wipe and the wireframe preview: the whole scene is drawn
// once with colour writes disabled, so that every pixel deposits the mask texture's alpha
// into the destination alpha channel. What is later composited (the faded-in scene, or the
// wireframe) is decided by that alpha, so this pass writes nothing anyone ever sees
// directly -- only where.
//
// The fixed-function version projected the mask by handing stage 0 the camera-space
// position and a texture matrix of inv(view) * translate(-centre) * scale * translate(0.5),
// which unwinds to nothing more than "take the world-space XY, recentre it on the point of
// the screen the wipe radiates from, and scale it by the current fade radius". Since that
// is an affine map of the world position, it collapses into one multiply-add here, and the
// matrix, the camera-space round trip and the inverse all go away.
//
// Only two rows of the object->world matrix are needed to get world XY, which is why they
// arrive as a pair of vectors rather than a whole matrix -- the same convention unit_vs
// uses to find the ground-plane position for cloud shadows.

#include "shadermodel.hlsli"

row_major float4x4 WorldViewProj : register(c0);  // object -> clip space
float4 WorldAxisX : register(c4);                 // object -> world X
float4 WorldAxisY : register(c5);                 // object -> world Y

// xy = world -> mask uv scale, zw = bias. The degenerate fully-faded case (a scale of
// zero) is folded in by the caller as scale 0 / bias 0, which pins every vertex to uv
// (0,0) exactly as the old zeroed texture matrix did -- no branch needed here.
float4 MaskProj : register(c6);

struct VS_INPUT
{
    // Deliberately only the position. Meshes reach this pass under several vertex formats
    // (0x112, 0x142, 0x152, ...) and the mask cares about none of the rest; declaring a
    // subset lets one shader serve all of them.
    float3 position : POSITION;
};

struct VS_OUTPUT
{
    float4 position : VS_POSITION;
    float2 texcoord : TEXCOORD0;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;

    float4 pos = float4(input.position, 1.0);
    output.position = mul(pos, WorldViewProj);

    float2 worldXY = float2(dot(pos, WorldAxisX), dot(pos, WorldAxisY));
    output.texcoord = worldXY * MaskProj.xy + MaskProj.zw;

    return output;
}

// The per-frame constant buffer, at b1.
//
// Every c-register file a shader can declare is full or close to it: water_ps uses c0-c28
// of the 32 GFX_PS_CONSTANTS holds, unit_pbr_ps c0-c25 plus c28 (AlphaTestCtl, from
// alphatest.hlsli). There is nowhere left to put cluster parameters that works for every
// shader, and the c-register file is an emulated D3D9 artefact besides -- per-frame data
// never belonged in a buffer DX8Wrapper re-uploads on every draw. This is the second
// buffer C2 of the clustered lighting plan opens: written once a frame by
// DX8Wrapper::Set_Frame_Constants, bound on the vertex, pixel AND compute stages alike.
//
// Not shadermodel.hlsli: that file is Shader Model 5 portability macros -- sampler and
// semantic spelling, mostly -- and has no opinion about what any particular buffer holds.
// This one does, so it gets its own header, the way alphatest.hlsli and shadow.hlsli each
// own the constant they declare rather than shadermodel.hlsli growing an unrelated
// register per feature.
//
// **C2 defines the mechanism only.** ClusterParams, ClusterDepth and CameraForward are
// declared here because C4-C6 need the layout settled now -- moving a field after shaders
// reference it means finding every reader -- but nothing in this engine writes anything
// meaningful into them yet. A shader that reads them before C4 lands reads zero.

#include "shadermodel.hlsli"

#ifndef RTS_SHADER_FRAMECONSTANTS_HLSLI
#define RTS_SHADER_FRAMECONSTANTS_HLSLI

cbuffer FrameConstants : register(b1)
{
    float4 ClusterParams;   // xy = tile size px, z = slice count, w = grid X
    float4 ClusterDepth;    // x = scale, y = bias, z = near, w = far
    float4 CameraForward;   // xyz = view direction, w = light count
};

#endif  // RTS_SHADER_FRAMECONSTANTS_HLSLI

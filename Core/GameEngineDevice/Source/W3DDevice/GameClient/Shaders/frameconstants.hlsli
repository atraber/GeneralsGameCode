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
// **C2 defined the mechanism only.** ClusterParams, ClusterDepth and CameraForward were
// declared here because C4-C6 need the layout settled now -- moving a field after shaders
// reference it means finding every reader. C4 fills them; see W3DClusterGrid.cpp, and
// clustergrid.hlsli for the named accessors every reader should use in preference to
// spelling out a component here.
//
// ONE WRITER. GpuLightListClass::Write_Frame_Constants (W3DGpuLightList.cpp) is the only
// place this block is written, and it writes all of it, from offset 0, once a frame. The
// two stages that own fields here do not each call Set_Frame_Constants: whichever ran
// last would silently win. If a later stage needs a field, it extends that function.

#include "shadermodel.hlsli"

#ifndef RTS_SHADER_FRAMECONSTANTS_HLSLI
#define RTS_SHADER_FRAMECONSTANTS_HLSLI

cbuffer FrameConstants : register(b1)
{
    float4 ClusterParams;   // xy = tile size px, z = slice count, w = grid X
    float4 ClusterDepth;    // x = scale, y = bias, z = near, w = far
    float4 CameraForward;   // xyz = view direction, w = light count
    // C4. The grid is laid over the CAMERA'S VIEWPORT, not the render target: the
    // tactical view does not cover the screen (the control bar is under it), so a tile
    // index derived from SV_Position needs the viewport's own origin subtracted first.
    // Without it every tile boundary sits a fraction of a tile away from where the CPU
    // builder put it -- a uniform shift, which is the hardest kind of wrong to see.
    float4 ClusterScreen;   // xy = viewport origin, render-target px; zw = viewport size px
    // Grid Y is here rather than beside grid X because ClusterParams was full and
    // recovering it as ceil(viewport height / tile height) in float is one rounding
    // decision away from being off by one, which shifts every slice above the first.
    // C5.1 claimed .z and .w. Both are gates and BOTH READ ZERO WHEN THIS BLOCK HAS NEVER
    // BEEN WRITTEN, which is the property that makes them safe: zero is "clustered lighting
    // off" and "directional term normal", i.e. exactly the frame that existed before C5.
    // Named and read through clustered.hlsli (ClusteredLightingEnabled,
    // CLUSTER_SUPPRESS_DIRECTIONAL), never spelled out component-wise by a shader.
    float4 ClusterLimits;   // x = grid Y, y = light-index stride (CLUSTER_MAX_LIGHTS),
                            // z = clustered lighting on (the three buffers exist -- see
                            //     W3DShaderManager::isClusteredLightingActive),
                            // w = suppress the shader's own directional term (the C5.1
                            //     sun-equivalence control; 0 in every ordinary frame)
    // C6. THE BUILDER'S FIELDS, and the only ones in this block no *reader* wants: every
    // consumer turns a pixel position and a view distance into a cluster, which needs
    // nothing below. clusterassign_cs.hlsl goes the other way -- a world-space light
    // sphere to a set of clusters -- and that needs the camera's view transform and the
    // projection's lateral terms, neither of which any of the five fields above carries.
    //
    // They are here rather than in a constant buffer of the compute shader's own because
    // there isn't one: Dispatch has no per-draw setup step, and b1 is the single buffer a
    // backend binds on the compute stage (see GfxDeviceClass::Set_Frame_Constants). Four
    // vec4 of the sixteen GFX_FRAME_CONSTANTS holds; nine are now in use and seven remain.
    float4 ClusterProj;     // x = projXScale, y = projXOffset, z = projYScale, w = projYOffset
                            // -- Row[0][0], Row[0][2], Row[1][1], Row[1][2] of the camera's
                            // projection. Read off the matrix and not rebuilt from a stored
                            // FOV and aspect: that matrix is the only place the two have
                            // actually been combined. See ClusterGridClass::Compute_Params.
    // The camera's view matrix (world -> view), as the three rows of a Matrix3D. Its
    // fourth row is implicitly (0,0,0,1) and is not sent. WORLD-SPACE Z IS NOT NEGATED
    // HERE: the engine's view space is right-handed with forward along -Z, and the shader
    // negates the third row's result itself, exactly as the CPU builder does -- see the
    // handedness derivation at the top of W3DClusterGrid.cpp.
    float4 ClusterView0;
    float4 ClusterView1;
    float4 ClusterView2;
};

#endif  // RTS_SHADER_FRAMECONSTANTS_HLSLI

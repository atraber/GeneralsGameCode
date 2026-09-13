// Shader Model 5 definitions for the D3D11 backend.
//
// Every shader in this directory compiles at Shader Model 5 (vs_5_0 and ps_5_0).
// D3D9 and Shader Model 3 were retired in Phase 10; Shader Model 4 was retired in
// favor of Shader Model 5 to align with Direct3D Feature Level 11.0.
//
#ifndef RTS_SHADER_MODEL_HLSLI
#define RTS_SHADER_MODEL_HLSLI

#ifndef RTS_SHADER_MODEL
#define RTS_SHADER_MODEL 5
#endif


// ---------------------------------------------------------------------------------------
// Textures and samplers
// ---------------------------------------------------------------------------------------

// In SM5 a texture and the state used to filter it are two objects in two register files.
// DX8Wrapper::Set_Sampler takes a texture and a SamplerStateClass separately and
// sends them to one slot. `s3` becomes `t3` and `s3`.
//
// The sampler keeps the name the shader used, and the texture gets a suffix.
#define DECLARE_SAMPLER(name, slot)      Texture2D name##_texture : register(t##slot); \
                                         SamplerState name : register(s##slot)
#define DECLARE_SAMPLER_2D(name, slot)   DECLARE_SAMPLER(name, slot)
#define DECLARE_SAMPLER_CUBE(name, slot) TextureCube name##_texture : register(t##slot); \
                                         SamplerState name : register(s##slot)

// A sampler crossing a function boundary. shadow.hlsli's filters take the shadow map as a
// parameter so that every receiver can share one implementation. Declared with
// SAMPLER_2D_PARAM and passed with SAMPLER_2D_ARG.
#define SAMPLER_2D_PARAM(name)  Texture2D name##_texture, SamplerState name
#define SAMPLER_2D_ARG(name)    name##_texture, name

// A comparison sampler (hardware PCF). The CPU side must bind a sampler built with a
// comparison function on the same slot -- see SamplerStateClass::With_Compare -- because
// D3D11 will not filter a SamplerComparisonState with an ordinary sampler, nor the reverse.
// Passed across functions with SAMPLER_2D_ARG like any other.
#define DECLARE_SAMPLER_2D_CMP(name, slot)  Texture2D name##_texture : register(t##slot); \
                                            SamplerComparisonState name : register(s##slot)
#define SAMPLER_2D_CMP_PARAM(name)          Texture2D name##_texture, SamplerComparisonState name

// The reads themselves.
#define SAMPLE_2D(name, uv)                     name##_texture.Sample(name, uv)
#define SAMPLE_2D_LOD(name, uv, lod)            name##_texture.SampleLevel(name, uv, lod)
#define SAMPLE_2D_GRAD(name, uv, ddxUv, ddyUv)  name##_texture.SampleGrad(name, uv, ddxUv, ddyUv)
#define SAMPLE_2D_CMP_LOD0(name, uv, ref)       name##_texture.SampleCmpLevelZero(name, uv, ref)
#define SAMPLE_CUBE(name, dir)                  name##_texture.Sample(name, dir)
#define SAMPLE_CUBE_LOD(name, dir, lod)         name##_texture.SampleLevel(name, dir, lod)


// ---------------------------------------------------------------------------------------
// Semantics
// ---------------------------------------------------------------------------------------

#define VS_POSITION   SV_POSITION
#define PS_TARGET     SV_Target
#define PS_TARGET0    SV_Target

// The pixel shader's clip position, and the register every other input sits after.
//
// Model 5 matches stages by semantic and by register, and fxc numbers a stage's signature
// in declaration order. Every vertex shader here declares its clip position first, so
// SV_Position takes register 0 and everything else starts at r1. Pixel shaders that do not
// read position declare PS_INPUT_POSITION first so their inputs align register-for-register.
#define PS_INPUT_POSITION        float4 psInputPosition : SV_Position;
#define PS_INPUT_POSITION_PARAM  float4 psInputPosition : SV_Position,

// The vertex colour a full-screen filter is handed and does not read.
// screenquad_vs writes a COLOR0, so a filter declaring only TEXCOORD0 uses this macro
// to preserve register alignment.
#define PS_INPUT_UNUSED_COLOR        float4 psUnusedColor : COLOR0;
#define PS_INPUT_UNUSED_COLOR_PARAM  float4 psUnusedColor : COLOR0,

// SV_Position in SM5 is the pixel centre ((x+0.5, y+0.5)).
// PIXEL_POSITION subtracts 0.5 to restore integer coordinates for Bayer dither calculations.
#define PS_PIXEL_POSITION     SV_Position
#define PIXEL_POSITION_TYPE   float4
#define PIXEL_POSITION(v)     ((v).xy - 0.5)


// ---------------------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------------------

// Constant packing in $Globals: fxc honours `: register(cN)` on globals and places them
// into the automatic $Globals constant buffer at byte offset N*16.
// `row_major` is required on all matrices because SM5 defaults to column-major.


#endif  // RTS_SHADER_MODEL_HLSLI

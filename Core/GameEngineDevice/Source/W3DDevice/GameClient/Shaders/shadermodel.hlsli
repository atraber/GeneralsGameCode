// One source, two shader models.
//
// Every shader in this directory compiles at Shader Model 3 and at Shader Model 4. D3D9
// runs the model 3 bytecode and is the only thing that runs at all today; the model 4
// build is compiled on every edit and loaded by nothing. It exists so that "the shaders
// are ready for a second backend" is something the build checks rather than something a
// future session has to find out.
//
// The alternative was a fork, and it was considered and rejected twice. The shaders *are*
// the look here -- the irradiance convention, the shadow floor, the PCF disk, the tone
// curve -- so a fork means every tuning decision after it is either made twice or made
// once and silently diverges. That is exactly what makes the `linux` branch's brightness
// tuning untrustworthy today, and it is not a mistake worth making again deliberately.
//
// RTS_SHADER_MODEL is 3 unless the build says otherwise, and the model 3 invocation passes
// no definitions at all. That is deliberate, and it is the thing that makes this safe: the
// model 3 token stream is character for character what it was before any of these macros
// existed, so the bytecode can be compared byte for byte against the previous build. If a
// .pso or .vso changes, a macro changed the tokens, and that is a bug in the macro rather
// than a cost of the port.
//
// Which gives every macro below one rule: **its model 3 expansion must be exactly the text
// it replaced.** No added parentheses, no normalised spelling, no tidying on the way past.
// Where the old source said `sampler` rather than `sampler2D`, there is a separate macro
// so that it still says `sampler`.
//
#ifndef RTS_SHADER_MODEL_HLSLI
#define RTS_SHADER_MODEL_HLSLI

#ifndef RTS_SHADER_MODEL
#define RTS_SHADER_MODEL 3
#endif


// ---------------------------------------------------------------------------------------
// Textures and samplers
// ---------------------------------------------------------------------------------------

// In model 3 a texture and the state used to filter it are one object at one register. In
// model 4 they are two objects in two register files -- and this engine already binds them
// that way. DX8Wrapper::Set_Sampler takes a texture and a SamplerStateClass separately and
// sends them to one slot. That was the point of doing the binding model first: the split
// costs nothing here because the engine had already paid for it, and no slot has to be
// renumbered. `s3` becomes `t3` and `s3`, not `t0` and `s0`.
//
// The sampler keeps the name the shader already used, so every SAMPLE_2D call site reads
// the way the tex2D call it replaced did. The texture is the one that gets a suffix.
#if RTS_SHADER_MODEL >= 4
#define DECLARE_SAMPLER(name, slot)      Texture2D name##_texture : register(t##slot); \
                                         SamplerState name : register(s##slot)
#define DECLARE_SAMPLER_2D(name, slot)   DECLARE_SAMPLER(name, slot)
#define DECLARE_SAMPLER_CUBE(name, slot) TextureCube name##_texture : register(t##slot); \
                                         SamplerState name : register(s##slot)
#else
#define DECLARE_SAMPLER(name, slot)      sampler name : register(s##slot)
#define DECLARE_SAMPLER_2D(name, slot)   sampler2D name : register(s##slot)
#define DECLARE_SAMPLER_CUBE(name, slot) samplerCUBE name : register(s##slot)
#endif

// A sampler crossing a function boundary. shadow.hlsli's filters take the shadow map as a
// parameter so that every receiver can share one implementation, and in model 4 that is
// two parameters rather than one. Declared with SAMPLER_2D_PARAM and passed with
// SAMPLER_2D_ARG, the call sites do not have to know which model they are compiled for.
#if RTS_SHADER_MODEL >= 4
#define SAMPLER_2D_PARAM(name)  Texture2D name##_texture, SamplerState name
#define SAMPLER_2D_ARG(name)    name##_texture, name
#else
#define SAMPLER_2D_PARAM(name)  sampler2D name
#define SAMPLER_2D_ARG(name)    name
#endif

// The reads themselves.
//
// SAMPLE_2D_LOD is the one to be careful with. tex2Dlod hides the level of detail in the
// *fourth* component of a float4 whose z is unused, while SampleLevel takes it as its own
// argument -- so a macro that took a float4 and forwarded it would compile at both models
// and sample the wrong mip at one of them. This one takes the coordinate and the level
// apart, and only the model 3 side ever assembles the float4.
#if RTS_SHADER_MODEL >= 4
#define SAMPLE_2D(name, uv)                     name##_texture.Sample(name, uv)
#define SAMPLE_2D_LOD(name, uv, lod)            name##_texture.SampleLevel(name, uv, lod)
#define SAMPLE_2D_GRAD(name, uv, ddxUv, ddyUv)  name##_texture.SampleGrad(name, uv, ddxUv, ddyUv)
#define SAMPLE_CUBE(name, dir)                  name##_texture.Sample(name, dir)
#define SAMPLE_CUBE_LOD(name, dir, lod)         name##_texture.SampleLevel(name, dir, lod)
#else
#define SAMPLE_2D(name, uv)                     tex2D(name, uv)
#define SAMPLE_2D_LOD(name, uv, lod)            tex2Dlod(name, float4(uv, 0, lod))
#define SAMPLE_2D_GRAD(name, uv, ddxUv, ddyUv)  tex2Dgrad(name, uv, ddxUv, ddyUv)
#define SAMPLE_CUBE(name, dir)                  texCUBE(name, dir)
#define SAMPLE_CUBE_LOD(name, dir, lod)         texCUBElod(name, float4(dir, lod))
#endif


// ---------------------------------------------------------------------------------------
// Semantics
// ---------------------------------------------------------------------------------------

// Only the ones whose meaning changes. A vertex shader's *input* POSITION is a name in the
// input layout and stays POSITION in both models; it is the value a vertex shader hands on,
// and that the pixel shader receives, which has to become SV_POSITION. Likewise COLOR0 on a
// pixel shader's input struct is an interpolated vertex colour and stays exactly what it is
// -- it is only a pixel shader's *return* that becomes a render target.
//
// Both distinctions are invisible at model 3, where the two spellings mean the same thing,
// and a blanket substitution gets them wrong in a way that still compiles.
#if RTS_SHADER_MODEL >= 4
#define VS_POSITION   SV_POSITION
#define PS_TARGET     SV_Target
#define PS_TARGET0    SV_Target
#else
#define VS_POSITION   POSITION
#define PS_TARGET     COLOR
#define PS_TARGET0    COLOR0
#endif

// The pixel's own coordinate, and half a pixel of difference.
//
// VPOS in ps_3_0 is the integer pixel coordinate: the top-left pixel is (0, 0). SV_Position
// in model 4 is the pixel *centre*, so the same pixel is (0.5, 0.5). Both uses in this tree
// feed the Bayer dither that gives translucent casters partial shadow, computed with fmod
// over that coordinate -- so under model 4 every pixel would land one bucket over, the
// pattern would shift and the translucency would change. With no error, on a render target
// nobody looks at directly.
//
// PIXEL_POSITION takes the coordinate back to the integer one the dither was written for.
// Its model 3 expansion is the bare parameter and not `(v)`, because parentheses are tokens
// and the bytecode has to come out identical.
#if RTS_SHADER_MODEL >= 4
#define PS_PIXEL_POSITION   SV_Position
#define PIXEL_POSITION(v)   ((v) - 0.5)
#else
#define PS_PIXEL_POSITION   VPOS
#define PIXEL_POSITION(v)   v
#endif


// ---------------------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------------------

// Model 3 has a flat file of float4 constant registers; model 4 has constant buffers. The
// move is free here, and that is worth stating because it is the thing that could most
// easily have been expensive. Every one of the constants these shaders declare is a float4,
// a row_major float4x4, or an array of float4 -- so every one occupies whole 16-byte slots
// in a buffer exactly as it occupies whole registers, and a cbuffer whose members carry
// their own register numbers is byte-identical to the register file the engine already
// maintains in Vertex_Shader_Constants[96] and Pixel_Shader_Constants[32]. Nothing repacks,
// and not one offset had to be recomputed.
//
// packoffset rather than declaration order, because the registers are neither contiguous
// nor always ascending -- unit_vs_body declares c21, then c24, then c28, then c22 -- and
// reordering declarations to make them so would change the model 3 token stream for no gain
// whatsoever.
//
// row_major is kept on every matrix and has to be. Model 4 defaults to column-major where
// model 3 defaults to row-major, so dropping the keyword transposes 33 matrices at once,
// silently, in the model that nothing runs yet.
#if RTS_SHADER_MODEL >= 4
#define CONSTANTS_BEGIN(name)   cbuffer name : register(b0) {
#define CONSTANTS_END           };
#define CREGISTER(n)            packoffset(c##n)
#else
#define CONSTANTS_BEGIN(name)
#define CONSTANTS_END
#define CREGISTER(n)            register(c##n)
#endif


#endif  // RTS_SHADER_MODEL_HLSLI

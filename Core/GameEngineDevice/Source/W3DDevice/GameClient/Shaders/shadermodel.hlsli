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

// The pixel shader's clip position, and the register every other input sits after.
//
// This is the model 3 / model 4 difference that Phase 3.8's gate could not see, because
// it is a property of a vertex shader and a pixel shader *together* and that gate compiles
// one shader at a time.
//
// ps_3_0 matches a vertex shader's outputs to a pixel shader's inputs by semantic alone.
// Model 4 matches by semantic *and by register*, and fxc numbers a stage's signature in
// declaration order. Every vertex shader here declares its clip position first, so
// SV_Position takes register 0 and everything else starts at r1 -- while a pixel shader
// that does not declare a position input at all packs its own inputs from r0. Semantic for
// semantic the two agree; register for register they are off by one, and D3D11 refuses the
// pair and draws nothing. Measured: 703332 of 876705 draws in a 600-frame window.
//
// So a pixel shader has to declare the position it does not read. PS_INPUT_POSITION is
// that declaration, first in the struct or first in the parameter list, and it expands to
// *nothing* at model 3 -- which is what keeps every .pso in this directory byte for byte
// what it was. PS_INPUT_POSITION_PARAM is the same thing with the trailing comma a
// parameter list needs; it too is empty at model 3.
//
// Two shaders already declare a position input for the dither (PS_PIXEL_POSITION below)
// and must not get a second one. They put the existing declaration first under an #if of
// their own instead.
#if RTS_SHADER_MODEL >= 4
#define PS_INPUT_POSITION        float4 psInputPosition : SV_Position;
#define PS_INPUT_POSITION_PARAM  float4 psInputPosition : SV_Position,
#else
#define PS_INPUT_POSITION
#define PS_INPUT_POSITION_PARAM
#endif

// The vertex colour a full-screen filter is handed and does not read.
//
// The same register-numbering rule as PS_INPUT_POSITION, one step further on.
// screenquad_vs writes a COLOR0 -- it is shared with the interface quads, which do read
// one -- so a filter that declares only TEXCOORD0 puts it one register below where
// screenquad_vs wrote it. Declaring the colour it ignores costs an interpolator that was
// already being written and nothing else.
//
// Empty at model 3, like everything else here, so the .pso does not move.
#if RTS_SHADER_MODEL >= 4
#define PS_INPUT_UNUSED_COLOR        float4 psUnusedColor : COLOR0;
#define PS_INPUT_UNUSED_COLOR_PARAM  float4 psUnusedColor : COLOR0,
#else
#define PS_INPUT_UNUSED_COLOR
#define PS_INPUT_UNUSED_COLOR_PARAM
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
//
// The type has to move with the semantic. VPOS is a float2; SV_Position must be declared
// float4 with all four components, and fxc refuses the shader outright rather than warning
// -- "type must be float32 and mask must be xyzw". That refusal is the model 4 build
// earning its place: it is a compile error in the model nothing runs, on a line that was
// correct in the model everything runs.
#if RTS_SHADER_MODEL >= 4
#define PS_PIXEL_POSITION     SV_Position
#define PIXEL_POSITION_TYPE   float4
#define PIXEL_POSITION(v)     ((v).xy - 0.5)
#else
#define PS_PIXEL_POSITION     VPOS
#define PIXEL_POSITION_TYPE   float2
#define PIXEL_POSITION(v)     v
#endif


// ---------------------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------------------

// There is nothing here, and that is the finding.
//
// Model 3 has a flat file of float4 constant registers and model 4 has constant buffers,
// which looked like the one part of this that would need real work: a macro per declaration
// to turn `register(cN)` into `packoffset(cN)`, a cbuffer opened and closed around each
// shader's constants, and three files whose declarations are interleaved with samplers and
// a function reordered to make one contiguous block possible.
//
// None of it is needed. fxc honours `: register(cN)` on a global at model 4 and places it
// in the automatic $Globals buffer at exactly that register's byte offset. Read back out of
// the compiled reflection (dxbc_cbuffer.py, which parses the RDEF chunk):
//
//   unit_prelit_vs   $Globals is 576 bytes = 36 slots. c0, c4, then a gap to c16 -- the
//                    c5..c15 hole is padding that survives. WorldAxisX is *listed after*
//                    TexMatrix1 and placed *before* it, at c22 against c28, so this is not
//                    declaration-order packing that happens to agree.
//   tree_vs          Sway[1 + MAX_SWAY_TYPES] is 176 bytes at c8: eleven whole slots.
//   unit_ps          AlphaTestCtl, declared in alphatest.hlsli, sits at c28 in the same
//                    buffer as the shader's own c1..c12. Constants from an included header
//                    need no separate buffer and no second register slot.
//
// So the buffer is a straight re-declaration of the register file, and the engine's
// Vertex_Shader_Constants[96] and Pixel_Shader_Constants[32] map onto it as an offset and
// a count with nothing recomputed. That holds because every constant these shaders declare
// is a float4, a row_major float4x4, or an array of float4, and each occupies whole 16-byte
// slots in a buffer exactly as it occupies whole registers. Had one been a float3 or a
// float2x2, this paragraph would say something much worse.
//
// The one thing that does have to be kept is `row_major` on every matrix. Model 4 defaults
// to column-major where model 3 defaults to row-major, so dropping the keyword transposes
// 33 matrices at once, silently, in the model that nothing runs yet.


#endif  // RTS_SHADER_MODEL_HLSLI

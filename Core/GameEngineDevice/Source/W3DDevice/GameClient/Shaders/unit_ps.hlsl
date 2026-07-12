// Unit pixel shader.
//
// Samples the base texture and modulates it by the per-vertex lit colour from
// the vertex shader. This reproduces the fixed-function "texture * diffuse"
// output; richer (PBR) shading is layered on in later milestones.

sampler BaseSampler : register(s0);

// x = 1 when a base texture is bound, 0 for an untextured (diffuse-only) pass, which
//     the fixed-function pipeline draws as the lit colour alone (stage 0 SELECTARG2).
// y = material diffuse alpha (stealth / translucency opacity).
// z = 1 for lit meshes, which take their opacity from the material as the
//     fixed-function pipeline did; 0 for pre-lit meshes, which keep the genuine
//     per-vertex alpha. y=z=1 forces the diffuse alpha to 1, for a stage 0 that
//     does not source the diffuse alpha at all.
// w = 1 when stage 0's alpha combine uses the texture alpha, 0 when it does not.
float4 TexCtl : register(c1);

struct PS_INPUT
{
    float4 position : POSITION;
    float4 color    : COLOR0;
    float2 texcoord : TEXCOORD0;
};

float4 main(PS_INPUT input) : COLOR
{
    float4 baseColor = tex2D(BaseSampler, input.texcoord);
    // Untextured pass: fold the texture out (white) so only the lit colour remains.
    baseColor = lerp(float4(1.0, 1.0, 1.0, 1.0), baseColor, TexCtl.x);

    // Alpha mirrors stage 0's fixed-function alpha combine. Lit meshes source the
    // diffuse alpha from the material (that is where stealth translucency lives);
    // pre-lit meshes keep their vertex alpha. Either factor folds to 1 when the
    // stage does not use it, so a texture-only alpha stays exactly the texture's.
    float diffAlpha = lerp(input.color.a, TexCtl.y, TexCtl.z);
    float texAlpha  = lerp(1.0, baseColor.a, TexCtl.w);
    return float4(baseColor.rgb * input.color.rgb, texAlpha * diffAlpha);
}

// Unit pixel shader (single texture).
//
// Samples the base texture and modulates it by the per-vertex lit colour from
// the vertex shader. This reproduces the fixed-function "texture * diffuse"
// output. Multi-texture passes use unit_detail_ps instead, so this shader never
// samples a stage it has no texture for.

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

sampler ShadowMap : register(s5);      // directional shadow map (packed depth)
float4 ShadowParams : register(c8);    // x = depth bias, y = shadow strength (0 = off)

struct PS_INPUT
{
    float4 position  : POSITION;
    float4 color     : COLOR0;
    float2 texcoord  : TEXCOORD0;
    float2 texcoord1 : TEXCOORD1;   // unused here; keeps the signature matching the VS
    float4 lightPos  : TEXCOORD2;   // position in the sun's clip space
};

// Cast-shadow term. Same pack/compare and 2x2 PCF as the terrain, so a unit and the
// ground it stands on agree about where a shadow falls. Branchless for ps_2_0: the
// out-of-frustum case folds in as a mask rather than an early-out.
float unpackDepth(float4 rgba)
{
    // Weights are 255, matching shadowdepth_ps's pack -- see the note there.
    return dot(rgba.xyz, float3(1.0, 1.0 / 255.0, 1.0 / (255.0 * 255.0)));
}

float shadowTerm(float4 lightPos)
{
    // Guard the divide: a degenerate w yields inf, then NaN, and NaN survives every
    // operation after it -- the pixel is simply gone. Cheaper to be certain here.
    float3 ndc = lightPos.xyz / max(abs(lightPos.w), 1e-6);
    float2 uv  = ndc.xy * float2(0.5, -0.5) + 0.5;
    float inBounds = step(0.0, uv.x) * step(uv.x, 1.0) * step(0.0, uv.y) * step(uv.y, 1.0);

    const float texel = ShadowParams.z;   // 1/SHADOW_MAP_SIZE, fed per frame
    float lit = 0.0;
    [unroll] for (int x = 0; x <= 1; ++x)
        [unroll] for (int y = 0; y <= 1; ++y) {
            float2 o = (float2(x, y) - 0.5) * texel;
            float stored = unpackDepth(tex2D(ShadowMap, uv + o));
            lit += (ndc.z - ShadowParams.x > stored) ? 0.0 : 1.0;
        }
    return saturate(lerp(1.0, lit * 0.25, inBounds * ShadowParams.y));
}

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

    // Darken toward a floor rather than to black: the lit colour from the vertex shader
    // is ambient and direct light already summed, so there is no direct term left to
    // remove on its own. SHADOW_MIN matches the terrain's, so a unit and its own cast
    // shadow on the ground sit at the same brightness.
    const float SHADOW_MIN = 0.35;
    float3 rgb = baseColor.rgb * input.color.rgb;
    rgb *= lerp(SHADOW_MIN, 1.0, shadowTerm(input.lightPos));
    return float4(rgb, texAlpha * diffAlpha);
}

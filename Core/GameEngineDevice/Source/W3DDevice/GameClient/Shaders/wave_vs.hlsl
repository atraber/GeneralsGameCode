// Open-sea water vertex shader (Shader Model 3).
//
// Port of wave.vso (vs_1_1). Drives WATER_TYPE_2_PVSHADER -- the tiled "sea" surface with a
// bump-mapped reflection, drawn patch by patch by WaterRenderObjClass::drawSea. It is not
// the water most maps use: the default is WATER_TYPE_0 on the programmable water path
// (water_vs / water_ps), and this one only runs when GameData.ini asks for WaterType = 2.
//
// The only real work here is the reflection coordinate. The reflection is rendered to its
// own texture from the mirrored camera, so a sea vertex has to look up that texture at its
// own position *on screen* -- which means taking its clip-space position, dividing through
// by w to get normalised device coordinates, and mapping those into texture space. That is
// the projective transform arriving in TexProj.
//
// Constants keep the register numbers the assembly used, since drawSea still uploads them
// by number (CV_WORLDVIEWPROJ_0 = 2, CV_TEXPROJ_0 = 6).

// c0 and c1 were CV_ZERO and CV_ONE, uploaded by drawSea and never read by the shader even
// in the original. Left unclaimed rather than declared.

// Composite world*view*projection for this patch, uploaded already transposed. Declared
// row_major and multiplied matrix-first so this reproduces the assembly's four dp4s against
// c2..c5 exactly; mul(v, M) would silently transpose it. Same convention as tree_vs.
row_major float4x4 WorldViewProj : register(c2);

// Clip space -> reflection texture space, as a scale and bias pair rather than a matrix:
// the assembly only ever used the first row (mad oT1.xy, r1.xy, c6.xy, c6.zw), so of the
// four registers drawSea uploads only c6 is read.
float4 TexProj : register(c6);

struct VS_INPUT
{
    float3 position : POSITION;
    float4 color    : COLOR0;
    float2 texcoord : TEXCOORD0;   // bump map coordinates
};

struct VS_OUTPUT
{
    float4 position : POSITION;
    float4 color    : COLOR0;
    float2 texcoord : TEXCOORD0;   // bump map
    float2 reflectUV: TEXCOORD1;   // projective reflection lookup
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;

    float4 clip = mul(WorldViewProj, float4(input.position, 1.0));
    output.position = clip;

    // Perspective divide done here rather than relying on the interpolator, matching the
    // original. It is not projectively correct across a triangle -- the right way is to pass
    // clip.xyw and divide in the pixel shader -- but the sea is a dense grid of small
    // patches, so the error never gets large enough to see, and reproducing the old look
    // exactly matters more here than fixing something nobody reported.
    float2 ndc = clip.xy / clip.w;
    output.reflectUV = ndc * TexProj.xy + TexProj.zw;

    output.texcoord = input.texcoord;
    output.color    = input.color;
    return output;
}

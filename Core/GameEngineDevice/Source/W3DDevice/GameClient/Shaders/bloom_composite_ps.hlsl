// Bloom composite (Shader Model 2).
//
// Final stage of the bloom post-process: adds the blurred bloom texture back onto
// the original scene and writes the result to the back buffer. Drawn as a single
// fullscreen XYZRHW quad. The scene occupies a sub-rect of its texture (the tactical
// viewport), so it carries its own texcoord set (TEXCOORD0); the bloom target is a
// full 0..1 texture on TEXCOORD1.

sampler2D SceneSampler : register(s0);
sampler2D BloomSampler : register(s1);

// Bloom tuning -- edit and recompile the shader to tweak (no engine rebuild needed).
static const float BLOOM_INTENSITY = 1.00;  // how strongly the glow is added on top

float4 main(float2 uvScene : TEXCOORD0, float2 uvBloom : TEXCOORD1) : COLOR
{
    float3 scene = tex2D(SceneSampler, uvScene).rgb;
    float3 bloom = tex2D(BloomSampler, uvBloom).rgb;
    return float4(scene + bloom * BLOOM_INTENSITY, 1.0);
}

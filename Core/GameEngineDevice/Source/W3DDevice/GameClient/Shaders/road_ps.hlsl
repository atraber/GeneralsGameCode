// Road pixel shader.
//
// Base road texture modulated by the baked vertex lighting, then the cloud-shadow and
// noise-detail overlays multiplied on top, then cast shadows -- the same composition
// terrain_ps performs, because a road has to darken exactly as the ground beside it does
// or a shadow crossing the verge shows a seam. The one difference is the alpha: a road is
// blended into the terrain with SRCALPHA/INVSRCALPHA, and both the texture alpha (the
// road's soft edges) and the vertex alpha (segment fade) have to reach that blend, where
// the terrain simply writes 1.

// The clustered local lights (C5.3 of the clustered lighting plan) reach a road for the
// same reason the cloud shade and the shadow filter do, and by exactly the same arithmetic:
// a road is a decal on the terrain, so a lamp at the verge must not stop at the tarmac. Off
// by default, and with it off not one instruction below touches the colour.

#include "shadermodel.hlsli"

#include "constants.hlsli"
#include "shadow.hlsli"
#include "clustered.hlsli"

DECLARE_SAMPLER(BaseSampler, 0);
DECLARE_SAMPLER(CloudSampler, 2);
DECLARE_SAMPLER(NoiseSampler, 3);
DECLARE_SAMPLER_2D_CMP(ShadowMap, 5);   // directional shadow map (R32F depth, hardware PCF)

float4 OverlayEnable : register(c0); // x = cloud on, y = noise on, z = cloud shade strength
float4 ShadowParams  : register(c1); // x = depth bias, y = shadow strength (0 = off), z = texel

// The clustered light path's three buffers (C5.3), at the absolute slots clustergrid.hlsli
// assigns them -- above the eight texture stages, bound once per frame rather than per draw
// by W3DShaderManager::bindClusteredLightBuffers. No sampler for any of them: they are read
// with Load()/operator[], which wants the t slot and nothing else.
StructuredBuffer<GpuLight> LightBuffer    : register(t8);
Buffer<uint>               ClusterGrid    : register(t9);
Buffer<uint>               LightIndexList : register(t10);


// Cloud shadow from the two drifting layers.
//
// The texture holds *coverage*, not brightness: 0 under clear sky, 1 under full cloud,
// and mostly 0. Two layers are combined as a union -- the probability that at least one
// deck is overhead -- which is what two real cloud layers do, and which keeps the field
// mostly clear instead of stacking into permanent gloom.
//
// The result darkens toward a tint rather than multiplying toward black. Shade is the
// loss of *direct sun*; the sky still lights the ground, and skylight is blue, so shaded
// ground goes darker and cooler rather than simply dimmer. Multiplying by the texture --
// what this did before -- drives everything toward black and reads as dirt on the lens.
// The tint itself lives in constants.hlsli: terrain, road and units all darken toward the
// same colour, or cloud crossing a tank shades it differently from the ground under it.

float3 cloudShade(float4 cloudUV, float enable, float strength)
{
    // The texture stores brightness so the fixed-function fallback can still multiply by
    // it; coverage is its complement.
    float a = 1.0 - SAMPLE_2D(CloudSampler, cloudUV.xy).r;
    float b = 1.0 - SAMPLE_2D(CloudSampler, cloudUV.zw).r;
    float coverage = 1.0 - (1.0 - a) * (1.0 - b);
    float lit = 1.0 - coverage * strength * enable;
    return lerp(CLOUD_SHADE_TINT, float3(1.0, 1.0, 1.0), lit);
}

// Deliberately identical to terrain_ps's cloudShade, for the same reason the shadow
// filter is: a road is a decal on the ground and must take exactly the ground's shade.
struct PS_INPUT
{
    float4 position : VS_POSITION;
    float4 color    : COLOR0;
    float2 uv0      : TEXCOORD0;
    float4 cloudUV  : TEXCOORD2;   // xy = cloud layer A, zw = layer B
    float2 noiseUV  : TEXCOORD3;
    float4 lightPos : TEXCOORD4;
    float3 worldPos : TEXCOORD5;   // C5.3; see road_vs, and the note on the normal below
};

// The clustered local lights reaching this pixel, as a light colour to be multiplied by the
// road's albedo -- exactly what input.color.rgb already is. Character for character
// terrain_ps's clusteredLight, for the same reason road_ps's cloudShade is character for
// character terrain_ps's: two surfaces that meet along a line have to be lit by one
// expression, or the line shows.
//
// **NO PI, AND THAT IS THE WHOLE POINT OF THE SPLIT.** ClusteredLightingDiffuse is
// deliberately in the engine's non-radiometric convention -- "a white surface fully facing a
// light of colour C renders as C", which is what the CPU bake in COLOR0 and unit_vs both
// compute. unit_pbr_ps reaches the same place by the other route (a Lambert BRDF is
// albedo/PI, so it multiplies its light colour by PI first and the PI cancels). Two
// conventions, one result; the bug is only ever mixing them, and this project has been
// bitten by exactly that twice.
float3 clusteredLight(float4 clipPos, float3 worldPos, float3 N)
{
    // The view distance is SV_Position.w's reciprocal and nothing else -- right-handed
    // projection, so clip.w is the positive distance in front of the camera and a pixel
    // shader receives 1/w. max() only guards the division; ClusterSliceOf clamps the result
    // to the grid at both ends anyway.
    uint cluster = ClusterIndexAt(clipPos.xy, ClusterViewDist(clipPos));
    return ClusteredLightingDiffuse(CLUSTER_BUFFERS_ARG, cluster, worldPos, N);
}

// Cast-shadow term. Deliberately identical to terrainShadow in terrain_ps: a road is a
// decal on the terrain, so the two have to agree tap for tap, or the shadow edge breaks
// where it crosses onto the tarmac. Sharing the filter through shadow.hlsli is what makes
// that structural rather than a promise -- the two agree because there is one filter, not
// because two copies were kept in step by hand.
float roadShadow(float4 lightPos)
{
    float3 ndc = shadowNdc(lightPos);
    float2 uv  = shadowUv(ndc);

    // Fitted here, in unbranched code -- see shadow.hlsli. A road carries no vertex normal
    // for the lookup to be lifted along, so like the terrain it depends on the plane fit
    // rather than on a normal offset to survive a kernel wider than a texel.
    float2 dzduv = shadowReceiverGradient(uv, ndc.z);

    float lit = shadowFilter16(SAMPLER_2D_ARG(ShadowMap), uv, ndc.z, ShadowParams.z,
                               ShadowParams.w, dzduv, ShadowParams.x);
    return saturate(lerp(1.0, lit, ShadowParams.y));
}

float4 main(PS_INPUT input) : PS_TARGET
{
    float4 base = SAMPLE_2D(BaseSampler, input.uv0);

    // Geometric normal off the world position's derivatives, by the same three lines
    // terrain_ps uses and for the same reason: a road carries no vertex normal either, and
    // whatever the terrain does the road must do, or a lamp at the verge would light the
    // grass and the tarmac at two different angles and draw its own edge along the kerb.
    // That is the same rule CLOUD_PERIOD_A and STRETCH_FACTOR are shared constants for.
    //
    // The sign correction is the handedness argument -- see the long note in terrain_ps. For
    // flat ground worldPos.z is constant, so dpx and dpy both lie in the world XY plane and
    // their cross product is exactly (0, 0, k): +/-Z whatever the camera yaw and whatever the
    // winding, and this line pins it to exactly (0, 0, 1). The cross-product order is
    // therefore not load-bearing; swapping the operands negates k, which this undoes.
    //
    // The one thing it does NOT guarantee is that a road facet and the terrain facet beneath
    // it reconstruct the SAME normal -- they are separate geometry with separate
    // triangulations, so on a slope the two can differ by a degree or so and a local light
    // will land fractionally differently either side of the kerb. That is a much smaller
    // step than the two-expressions version would give, both surfaces are near horizontal
    // where roads are laid, and closing it properly means sampling the terrain's normal
    // rather than reconstructing a second one -- which is C7's question, alongside whether
    // ground decals grow a normal at all.
    float3 dpx  = ddx(input.worldPos);
    float3 dpy  = ddy(input.worldPos);
    float3 Ngeo = normalize(cross(dpx, dpy));
    Ngeo *= (Ngeo.z < 0.0) ? -1.0 : 1.0;

    // The baked vertex light, plus whatever clustered local lights reach this pixel (C5.3).
    //
    // **ADDED TO THE LIGHT, NOT TO THE PIXEL.** input.color.rgb is the light this stretch of
    // road receives and base.rgb is its albedo, so a term added here is multiplied by the
    // road texture exactly as the baked sun is. Adding it after the multiply would wash the
    // white lane markings and the dark tarmac to the same colour under a lamp.
    //
    // **THE DOUBLE-COUNT WITH THE CPU PATH IS EXPECTED AND IS C7'S.** input.color.rgb still
    // contains the CPU-computed dynamic light baked into the road's vertices, so with this
    // toggle on a lamp is counted twice -- once per vertex, once per pixel. C7 deletes the
    // CPU path; until then the toggle is off by default.
    //
    // **CLUSTER_SUPPRESS_DIRECTIONAL IS NOT HONOURED HERE, AND CANNOT BE** -- see the same
    // note in terrain_ps. The sun arrives already summed with the ambient inside COLOR0,
    // with no way to recover the parts, so there is no directional term to suppress and
    // scaling the lot would take the ambient down with it.
    float3 litColor = input.color.rgb;
    [branch] if (ClusteredLightingEnabled())
    {
        litColor += clusteredLight(input.position, input.worldPos, Ngeo);
    }

    float3 col  = base.rgb * litColor;

    // Multiplicative overlays, as the fixed-function road pass applied them (stage 1 and
    // stage 2, both MODULATE against the running colour). Off layers lerp to white.

    float3 noiseTex = SAMPLE_2D(NoiseSampler, input.noiseUV).rgb;
    float3 cloud = cloudShade(input.cloudUV, OverlayEnable.x, OverlayEnable.z);
    float3 noise = lerp(float3(1.0, 1.0, 1.0), noiseTex, OverlayEnable.y);

    // Cast shadows: the road colour has its lighting baked in, so darken toward the same
    // ambient floor the terrain uses rather than to black. SHADOW_MIN is in
    // constants.hlsli, so every receiver darkens to the same place.
    float shadow = roadShadow(input.lightPos);
    col *= lerp(SHADOW_MIN, 1.0, shadow);

    return float4(col * cloud * noise, base.a * input.color.a);
}

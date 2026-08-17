// PBR unit pixel shader (Shader Model 3) -- metallic-roughness workflow.
//
// Textures:
//   s0 Albedo   (<name>.dds, sRGB)
//   s1 ORM      (<name>_orm.dds, linear: R=AO, G=Roughness, B=Metallic, A=Height)
// The ORM map is optional per unit. Where one is not authored the engine binds a
// neutral 1x1 default instead (unoccluded, dielectric, mostly rough -- see
// initDefaultOrmMap), so every eligible mesh reaches this shader and nothing here
// needs to know which kind of map it is sampling.
//
// The ORM alpha channel is a height map: it drives derivative-based bump mapping
// (a tangent frame reconstructed from screen-space derivatives, so no per-vertex
// tangents are needed) to perturb the shading normal before lighting.
//
// Lighting matches the engine's LightEnvironment (up to four directional lights +
// scene ambient) so PBR units sit under the same light as everything else, with
// physically-based specular and AO added on top. A shared environment cubemap
// (s4), re-baked from the scene's dominant light/ambient as time-of-day drifts,
// supplies the Fresnel-weighted reflection term.

sampler2D   AlbedoSampler : register(s0);
sampler2D   OrmSampler    : register(s1);
samplerCUBE EnvSampler    : register(s4);   // shared environment cubemap (reflections)

float4 LightDir0     : register(c0);   // xyz = direction toward the light
float4 LightDir1     : register(c1);
float4 LightDir2     : register(c2);
float4 LightDir3     : register(c3);
float4 LightDiffuse0 : register(c4);   // 0 when the light is disabled
float4 LightDiffuse1 : register(c5);
float4 LightDiffuse2 : register(c6);
float4 LightDiffuse3 : register(c7);
float4 SceneAmbient  : register(c8);   // D3DRS_AMBIENT equivalent
float4 CameraPos     : register(c9);   // world-space camera position
float4 MatAmbient    : register(c10);  // house-colour tint (white when none)
float4 AlphaCtl      : register(c11);  // x = material opacity, y = 1 when lit (use it)

struct PS_INPUT
{
    float4 position : POSITION;
    float4 color    : COLOR0;
    float2 texcoord : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float3 worldNrm : TEXCOORD2;
};

static const float PI = 3.14159265;

float3 SrgbToLinear(float3 c) { return pow(saturate(c), 2.2); }
float3 LinearToSrgb(float3 c) { return pow(saturate(c), 1.0 / 2.2); }

// GGX / Trowbridge-Reitz normal distribution.
float D_GGX(float NdotH, float rough)
{
    float a  = rough * rough;
    float a2 = a * a;
    float d  = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-4);
}

// Smith geometry with Schlick-GGX, combined for view + light.
float G_Smith(float NdotV, float NdotL, float rough)
{
    float k = (rough + 1.0);
    k = (k * k) / 8.0;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

float3 F_Schlick(float VdotH, float3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
}

// Perturb a geometric normal by the gradient of a height field, using a tangent
// basis reconstructed from screen-space derivatives (Schueler/Mikkelsen surface-
// gradient method). Needs no per-vertex tangents and no texel size -- purely a
// function of the interpolated world position and the sampled height. A flat
// height map has zero gradient, so units without real height detail are untouched.
float3 PerturbNormalFromHeight(float3 N, float3 worldPos, float height, float strength)
{
    float3 dpdx = ddx(worldPos);
    float3 dpdy = ddy(worldPos);
    float  dhdx = ddx(height);
    float  dhdy = ddy(height);

    float3 r1  = cross(dpdy, N);
    float3 r2  = cross(N, dpdx);
    float  det = dot(dpdx, r1);
    float3 surfaceGrad = sign(det) * (dhdx * r1 + dhdy * r2) / max(abs(det), 1e-8);

    return normalize(N - strength * surfaceGrad);
}

// How much irradiance an engine light colour of 1.0 stands for.
//
// The engine's LightDiffuse is not a radiometric quantity. It comes from a pipeline whose
// whole convention is "a white surface fully facing a light of colour C renders as C" --
// that is what unit_vs computes (`MatDiffuse * saturate(dot(N, L))`, no constants), and it
// is what the terrain's CPU bake computes. A Lambert BRDF is albedo/PI, so reproducing
// that convention takes an irradiance of PI: albedo/PI * PI * N.L = albedo * N.L.
//
// Without this the direct term was the only part of this shader still reading the light
// colour as if it were radiance, while the two terms either side of it -- the ambient
// (SceneAmbient, straight off D3DRS_AMBIENT) and the cubemap (baked from that same sun and
// ambient) -- were already in the engine's convention. So the sun arrived a factor of PI
// weaker than the sky it was being added to, and inverted the balance of an outdoor scene:
// measured on a daylight map the ambient was contributing more to a sunlit tank than the
// sun was. That reads as dark and, more tellingly, flat -- the lit side never separates
// from the shadowed side.
//
// It also put this shader out of step with the one drawing half the same building. A mesh
// whose base pass routes here and whose detail pass routes to unit_detail_ps was being lit
// by two equations that disagreed by exactly this factor.
//
// The specular gets it too, and must: the same light is what drives the highlight. At the
// default ORM's roughness of 0.8 the GGX lobe is broad enough that this is not visible
// (~0.004 -> ~0.009 against a frame that peaks at 1.0); it is only on the low-roughness
// authored maps, and on genuine metals, that it becomes a highlight -- which is the point
// of having authored them.
static const float LIGHT_IRRADIANCE = PI;

float3 DirectLight(float3 N, float3 V, float3 L, float3 radiance,
                   float3 diffuseColor, float3 F0, float rough)
{
    float NdotL = saturate(dot(N, L));
    if (NdotL <= 0.0)
        return 0.0;
    radiance *= LIGHT_IRRADIANCE;
    float3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float  D = D_GGX(NdotH, rough);
    float  G = G_Smith(NdotV, NdotL, rough);
    float3 F = F_Schlick(VdotH, F0);
    float3 spec = (D * G) * F / max(4.0 * NdotV * NdotL, 1e-4);

    float3 kd = (1.0 - F) * (1.0 - 0.0); // metallic already removed from diffuseColor
    return (kd * diffuseColor / PI + spec) * radiance * NdotL;
}

float4 main(PS_INPUT input) : COLOR
{
    float4 albedoTex = tex2D(AlbedoSampler, input.texcoord);
    // Both factors are authored in sRGB, so both are converted. Modulating a linear albedo by
    // a raw sRGB tint applies the tint at the wrong strength -- and always the wrong way for a
    // team colour, which is the only thing that sets MatAmbient to anything but white here.
    float3 albedo    = SrgbToLinear(albedoTex.rgb) * SrgbToLinear(MatAmbient.rgb);

    float4 orm       = tex2D(OrmSampler, input.texcoord);
    float  ao        = orm.r;
    // The map decides. saturate() only keeps out-of-range texels in [0,1]; there is no
    // artistic scaling here. A map that reads wrong gets fixed in the map, not hidden
    // behind a constant in the shader.
    // Note roughness 0 is a perfect mirror, which a GGX lobe cannot represent: the
    // analytic highlight from the four directional lights goes to zero, while the
    // environment reflection below stays at full strength. That is the intended
    // behaviour for a mirror, not a bug.
    float  roughness = saturate(orm.g);
    float  metallic  = saturate(orm.b);

    float3 N = normalize(input.worldNrm);
    // Bump the shading normal from the ORM height channel (orm.a). Derivatives must be
    // taken in uniform flow, so this stays above any branching.
    //
    // The surface gradient here is scale-sensitive and was far too strong at 1.0. The
    // position derivatives give det ~= 0.09 for a unit covering ~100 pixels, while a
    // procedurally generated height channel changes by 0.1-0.5 across a single screen
    // pixel -- so surfaceGrad comes out the same magnitude as N itself. The shading
    // normal is then driven by height noise rather than by the geometry, the reflection
    // vector samples effectively random cubemap directions per pixel, and the result
    // averages to a flat colour that never responds to the unit turning or the camera
    // moving. Off by default until the height data is worth trusting; raise it slowly
    // (0.02-0.1 is a sane range) and only with a real height map.
    const float BUMP_STRENGTH = 0.0;
    N = PerturbNormalFromHeight(N, input.worldPos, orm.a, BUMP_STRENGTH);
    float3 V = normalize(CameraPos.xyz - input.worldPos);

    float3 diffuseColor = albedo * (1.0 - metallic);
    float3 F0           = lerp(0.04, albedo, metallic);

    float3 Lo = 0.0;
    Lo += DirectLight(N, V, LightDir0.xyz, LightDiffuse0.rgb, diffuseColor, F0, roughness);
    Lo += DirectLight(N, V, LightDir1.xyz, LightDiffuse1.rgb, diffuseColor, F0, roughness);
    Lo += DirectLight(N, V, LightDir2.xyz, LightDiffuse2.rgb, diffuseColor, F0, roughness);
    Lo += DirectLight(N, V, LightDir3.xyz, LightDiffuse3.rgb, diffuseColor, F0, roughness);

    // Ambient diffuse under the scene ambient, attenuated by AO.
    float3 ambient   = diffuseColor * SceneAmbient.rgb * ao;

    // Reflection of the shared environment cubemap, Fresnel-weighted and faded on
    // rough surfaces. World-space reflection vector indexes the cubemap directly.
    float3 R      = reflect(-V, N);
    float3 envCol = texCUBE(EnvSampler, R).rgb;
    float  NdotV  = saturate(dot(N, V));
    float3 Fenv   = F_Schlick(NdotV, F0);
    float3 envSpec = envCol * Fenv * (1.0 - roughness * 0.6);

    float3 color = Lo + ambient + envSpec;

    // Diagnostics. Set to a non-zero mode and rebuild the rts_shaders target only (no
    // engine rebuild); every PBR mesh then renders that quantity instead of the shading.
    //   1 = environment sample    -- black means the cubemap never reaches sampler s4
    //   2 = shading normal N      -- should vary smoothly over the mesh and turn with it;
    //                                per-pixel noise means BUMP_STRENGTH is too high
    //   3 = reflection vector R   -- must change as the unit turns AND as the camera moves
    //   4 = ORM as authored       -- red=AO, green=roughness, blue=metallic
    //   5 = Fresnel weight Fenv   -- how much of the reflection actually survives
#define PBR_DEBUG_MODE 0
#if   PBR_DEBUG_MODE == 1
    return float4(envCol, 1.0);
#elif PBR_DEBUG_MODE == 2
    return float4(N * 0.5 + 0.5, 1.0);
#elif PBR_DEBUG_MODE == 3
    return float4(R * 0.5 + 0.5, 1.0);
#elif PBR_DEBUG_MODE == 4
    return float4(ao, roughness, metallic, 1.0);
#elif PBR_DEBUG_MODE == 5
    return float4(Fenv, 1.0);
#endif

    float diffAlpha = lerp(input.color.a, AlphaCtl.x, AlphaCtl.y);
    return float4(LinearToSrgb(color), albedoTex.a * diffAlpha);
}

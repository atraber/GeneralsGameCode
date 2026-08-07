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
sampler2D   ShadowMap     : register(s5);   // directional shadow map (packed depth)
sampler2D   SceneColor    : register(s6);   // previous frame's resolved scene
sampler2D   SceneDepth    : register(s7);   // this frame's camera-view packed depth

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
row_major float4x4 SunVP : register(c12);  // sun view*projection (world -> shadow clip)
float4 ShadowParams  : register(c16);  // x = ground depth bias, y = shadow strength (0 = off)
float4 SsrParams     : register(c17);  // x = strength (0 = off), y = max ray length, zw = proj _33/_43
row_major float4x4 CameraVP : register(c18); // camera view*projection (world -> screen clip)
float4 EnvAverage    : register(c22);  // mean colour of the baked env cubemap
// x = how far to lift the shadow lookup off the surface along its normal, in world units;
// y = the depth bias left over once it is. Together they replace ShadowParams.x for
// meshes -- see the note where they are computed in W3DView.
float4 ShadowMeshParams : register(c23);

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

// Directional shadow map. Unpack the RGB-packed depth and 3x3-PCF compare this
// pixel's sun-clip-space depth against it. Returns 1 = lit, 0 = fully shadowed.
float unpackDepth(float4 rgba)
{
    // Weights are 255, matching shadowdepth_ps's pack -- see the note there.
    return dot(rgba.xyz, float3(1.0, 1.0 / 255.0, 1.0 / (255.0 * 255.0)));
}

float computeShadow(float3 worldPos, float3 worldNormal)
{
    // Normal offset: look up not at this surface but a little way off it along its own
    // normal, which is what keeps a surface out of its own shadow for a fraction of the
    // depth licence a compare bias needs. The M3 path does the same thing in its vertex
    // shader; here the world normal has already reached the pixel, so it is done per
    // pixel and follows the shading normal exactly.
    worldPos += worldNormal * ShadowMeshParams.x;

    float4 clip = mul(float4(worldPos, 1.0), SunVP);
    float3 ndc  = clip.xyz / clip.w;
    float2 uv   = ndc.xy * float2(0.5, -0.5) + 0.5;   // clip -> UV, flip Y for the texture
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
        return 1.0;   // outside the sun frustum -> lit

    // The bias comes in per frame rather than being baked: it has to counter the
    // world-space size of a shadow texel, and the sun frustum is fitted to the camera,
    // so that size changes with the zoom. A fixed value large enough for the widest
    // frustum erases small casters' shadows entirely once zoomed in.
    const float texel = ShadowParams.z;   // 1/SHADOW_MAP_SIZE, fed per frame

    // Bilinear-weighted PCF over a 3x3 texel footprint.
    //
    // The taps have to be point-sampled -- the map holds depth packed across RGB, and
    // hardware filtering would interpolate the packed bytes, which is meaningless. So the
    // smoothing has to come from weighting the *comparisons* instead of the depths.
    //
    // Weighting them by where the pixel falls inside its texel is the part that removes
    // stair-stepping. A plain box of point comparisons (what this was) still snaps every
    // tap to the texel grid: it only turns a hard edge into ten grey levels that are all
    // still aligned to that grid, which reads as chunky steps rather than a soft edge.
    // Blending across the texel makes the transition continuous as the edge crosses it.
    // Note the map is already 4096 -- this was never a resolution problem.
    //
    // Sixteen taps: a 3-texel-wide box needs a 4-tap span once it is offset by the
    // fractional position, with the two end taps carrying the partial weights.
    float2 texelPos = uv / texel;
    float2 frc      = frac(texelPos - 0.5);
    float2 baseUv   = (floor(texelPos - 0.5) + 0.5) * texel;

    float wx[4] = { 1.0 - frc.x, 1.0, 1.0, frc.x };
    float wy[4] = { 1.0 - frc.y, 1.0, 1.0, frc.y };

    float lit = 0.0;
    [unroll] for (int y = 0; y < 4; ++y)
        [unroll] for (int x = 0; x < 4; ++x) {
            float2 tapUv = baseUv + float2(x - 1, y - 1) * texel;
            float stored = unpackDepth(tex2D(ShadowMap, tapUv));
            float tapLit = (ndc.z - ShadowMeshParams.y > stored) ? 0.0 : 1.0;
            lit += tapLit * wx[x] * wy[y];
        }
    // Weights sum to 3 per axis ((1-f) + 1 + 1 + f), so 9 over the kernel.
    return lerp(1.0, lit / 9.0, ShadowParams.y);
}

// Clip-space depth back to a view-space distance. The prepass stores z/w under the
// camera's perspective projection, which is heavily non-linear -- almost the whole
// scene lands in the last fraction of the range -- so a difference in it is not a
// distance and the thickness test cannot be written in one.
//
// Straight from the two projection elements that produced the value, rather than
// recovering the near and far planes from those same two numbers and rebuilding the
// transform out of them -- algebraically the same thing, with two extra divisions in
// the middle that go through zero for projections this code does not anticipate.
float viewDepth(float ndcZ)
{
    // This engine's projection is right-handed -- measured _33 = -1.005808,
    // _43 = -10.058081, which is near 10, far 1734. Right-handed means clip.w = -viewZ,
    // so ndcZ = -_33 - _43/viewZ and therefore viewZ = -_43 / (_33 + ndcZ). The sign of
    // _33 in the denominator is the whole difference from the left-handed form.
    //
    // Getting it wrong was not a subtle error: the left-handed version returns 10 at the
    // near plane, correctly, and 5 at the far plane -- squeezing all 1734 units of scene
    // into a 5-unit band, where every surface reads as the same distance and no ray can
    // ever fall inside the thickness window. abs() keeps it valid for a left-handed
    // projection too, where the same expression comes out negative.
    return abs(SsrParams.w / (SsrParams.z + ndcZ));
}

// Screen-space reflection. Walks the reflection ray forward looking for the first step
// that ends up behind the surface the depth prepass recorded, and returns the scene
// colour there.
//
// The march is in world space, reprojected per step, rather than interpolated along a
// screen-space line. The depth being read was rendered with this very matrix, so
// reprojection is exact; and it keeps the step in world units, which is the only frame
// in which the thickness test below means anything.
//
// Returns rgb = reflected colour (still sRGB, as the frame buffer had it), a = how much
// to trust it. Zero means the ray found nothing and the caller should keep the cubemap.
// hitMask comes back 1 when a surface was actually found, separately from the alpha,
// which is the edge fade. They are not the same thing: a genuine hit right at the frame
// border fades to zero alpha, and conflating the two makes a real reflection read as a
// miss -- which is exactly what the triage mode was mis-reporting.
float4 traceSsr(float3 worldPos, float3 R, out float hitMask)
{
    const int STEPS  = 32;
    const int REFINE = 5;
    hitMask = 0.0;

    float stepLen = SsrParams.y / STEPS;
    // How deep the recorded surface is assumed to be. A hit test can only ever ask "is
    // this step behind the nearest surface", which is equally true of the entire void
    // behind that surface -- so without a thickness, every ray passing behind anything
    // reports a hit against it.
    //
    // It must also be at least a step wide. The test only ever looks at the sample
    // points, so a window narrower than the spacing between them is one the ray steps
    // clean over: the surface is in front at sample i and already too far behind at
    // i+1. That is what made hits flicker on and off under small camera movements --
    // identical geometry, sampled a fraction of a step differently each frame.
    float thickness = max(8.0, stepLen * 2.0);

    float3 prev = worldPos;
    float3 p    = worldPos;

    [loop] for (int i = 0; i < STEPS; ++i)
    {
        prev = p;
        p   += R * stepLen;

        float4 clip = mul(float4(p, 1.0), CameraVP);
        if (clip.w <= 1e-4)
            break;                                  // stepped behind the camera
        float3 ndc = clip.xyz / clip.w;
        float2 uv  = ndc.xy * float2(0.5, -0.5) + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
            break;                                  // left the screen; nothing to read

        // tex2Dlod, not tex2D: these reads sit inside varying flow control, where the
        // implicit derivatives an ordinary sample needs are undefined (and fxc refuses
        // to compile them). The depth is point-sampled anyway and the colour wants no
        // mip selection here, so an explicit LOD 0 costs nothing.
        float sceneZ = viewDepth(unpackDepth(tex2Dlod(SceneDepth, float4(uv, 0, 0))));
        // The ray's own distance needs no conversion at all: under this projection the
        // clip w *is* the view-space z. Only the stored side was ever non-linear.
        float rayZ   = clip.w;

        if (rayZ - sceneZ > 0.0 && rayZ - sceneZ < thickness)
        {
            // The coarse step only established that the crossing lies somewhere in this
            // interval. Bisect it down so the hit lands on the surface rather than
            // wherever the march happened to stop -- without this the reported position
            // moves by up to a whole step as the camera shifts, which is visible as the
            // reflection swimming across the surface.
            float3 lo = prev, hi = p;
            [unroll] for (int r = 0; r < REFINE; ++r)
            {
                float3 mid = 0.5 * (lo + hi);
                float4 mc  = mul(float4(mid, 1.0), CameraVP);
                float2 muv = (mc.xy / mc.w) * float2(0.5, -0.5) + 0.5;
                float  msz = viewDepth(unpackDepth(tex2Dlod(SceneDepth, float4(muv, 0, 0))));
                if (mc.w > msz) hi = mid; else lo = mid;
            }
            float4 hc  = mul(float4(hi, 1.0), CameraVP);
            float2 huv = (hc.xy / hc.w) * float2(0.5, -0.5) + 0.5;

            hitMask = 1.0;
            // Fade towards the frame edge. A ray landing near the border is reading
            // something about to leave the screen, and the reflection popping as it goes
            // is more noticeable than the cubemap it would have replaced.
            float2 edge = min(huv, 1.0 - huv);
            float  fade = saturate(min(edge.x, edge.y) * 10.0);
            return float4(tex2Dlod(SceneColor, float4(huv, 0, 0)).rgb, fade);
        }
    }
    return 0.0;
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

float3 DirectLight(float3 N, float3 V, float3 L, float3 radiance,
                   float3 diffuseColor, float3 F0, float rough)
{
    float NdotL = saturate(dot(N, L));
    if (NdotL <= 0.0)
        return 0.0;
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
    float3 albedo    = SrgbToLinear(albedoTex.rgb) * MatAmbient.rgb;

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

    // Cast shadows.
    //
    // This is what was missing, and it is why PBR meshes read as not receiving shadows.
    // The term itself was always fine -- measured on this map, 255 distinct values with
    // 64% of PBR pixels genuinely shadowed -- but it only ever reached Lo. The ambient
    // below was left at full strength, and in this engine ambient is the larger half of a
    // surface's brightness (SceneAmbient ~0.48 against a cubemap averaging ~0.16), so a
    // fully shadowed mesh stayed nearly as bright as a lit one.
    //
    // The renderer has one convention for this and PBR now joins it: terrain_ps and
    // unit_ps both do `col *= lerp(0.35, 1.0, shadow)` over their whole colour, and
    // unit_ps notes that the constant is matched to the terrain's on purpose, so that a
    // mesh and its own cast shadow on the ground sit at the same brightness. Applying the
    // same factor to everything is what keeps a PBR mesh at the brightness of the ground
    // it is standing on, whatever the balance of direct and ambient happens to be on it.
    //
    // Driving the direct term to zero instead (Lo *= shadow) is the more physical
    // reading -- an occluded surface receives no sunlight and its highlight should go with
    // it -- and it was tried first. Measured, it put 30% of PBR pixels below 0.35 and the
    // darkest at 0.004: sun-dominated pixels lose nearly everything, so units went black
    // in shadows the ground beside them merely dimmed. Wrong in the other direction, and
    // more noticeable than the highlight this preserves, because it is the comparison
    // against the neighbouring surface that the eye actually makes.
    //
    // The cost is that a GGX highlight survives at 35% inside a shadow. That is the same
    // thing the M3 and terrain shaders do with their own baked specular, so it is at
    // least consistent; splitting DirectLight's diffuse from its specular to kill only
    // the latter is the improvement if it ever looks wrong.
    const float SHADOW_MIN = 0.35;
    float shadow = computeShadow(input.worldPos, N);
    float shadowFill = lerp(SHADOW_MIN, 1.0, shadow);
    float3 LoUnshadowed = Lo;   // debug mode 16 only
    Lo *= shadowFill;

    // Ambient diffuse, attenuated by AO.
    //
    // The engine's SceneAmbient is a single flat colour applied to every surface no
    // matter which way it faces, which is what makes unlit sides read as dead grey. The
    // environment cubemap can supply a directional ambient instead: sky colour from
    // above, ground colour from below, warmed on the side the sun is on.
    //
    // This is the *diffuse* half of image-based lighting and it is what the reflection
    // term cannot do on its own -- envSpec below is specular, Fresnel-weighted, and on a
    // dielectric (F0 = 0.04) keeps about 4% head-on. Dropping SceneAmbient and hoping
    // the reflection covers it just makes everything not facing a light go black.
    //
    // Irradiance is approximated with one extra tap: the cubemap sampled along N at a
    // high mip, which the mip chain already makes an aggressive blur. A cosine-convolved
    // probe would be more correct; at 8x8 the difference is not worth a second bake.
    // Exposure is preserved by construction rather than by a hand-tuned gain. The tap is
    // divided by the cubemap's own mean colour (EnvAverage, computed during the bake), so
    // the result averages to 1.0 over all normals: it can only redistribute the engine's
    // ambient by direction, never raise or lower it overall.
    //
    // A fixed gain was tried first and was wrong. This map's cubemap averages ~0.16
    // against a SceneAmbient of ~0.48, so the 1.5 gain landed at roughly half the old
    // ambient and darkened every surface -- worst on downward-facing ones, which sample
    // the darkest part of the cube.
    //   ENV_DIFFUSE_IBL  0 = engine ambient only (previous behaviour), 1 = fully directional
    #define ENV_DIFFUSE_IBL     0.60
    #define ENV_IRRADIANCE_LOD  5.0
    float3 envIrradiance = texCUBElod(EnvSampler, float4(N, ENV_IRRADIANCE_LOD)).rgb;
    float3 envRelative   = envIrradiance / max(EnvAverage.rgb, 0.0001);
    float3 ambientLight  = SceneAmbient.rgb * lerp(1.0, envRelative, ENV_DIFFUSE_IBL);
    float3 ambient   = diffuseColor * ambientLight * ao * shadowFill;

    // Reflection of the shared environment cubemap, Fresnel-weighted and faded on
    // rough surfaces. World-space reflection vector indexes the cubemap directly.
    float3 R       = reflect(-V, N);
    // Kept separate from envCol below: SSR overwrites envCol where it finds a hit, so
    // this is the only place the cubemap's own answer survives for the debug modes.
    float3 cubeCol = texCUBE(EnvSampler, R).rgb;
    float3 envCol  = cubeCol;
    float  NdotV   = saturate(dot(N, V));
    float3 Fenv    = F_Schlick(NdotV, F0);

    // Where a screen-space ray finds a real surface, its colour replaces the cubemap's
    // guess. The cubemap is a coarse stand-in baked from the sun and ambient; an actual
    // pixel of the scene beats it whenever one can be found. It stays the fallback --
    // and in a camera this far above the battlefield most rays leave the screen without
    // hitting anything, so it earns its keep.
    //
    // Weighted down as the surface roughens, because a single sharp tap is a mirror and
    // a mirror-sharp reflection on a visibly rough surface reads as a bug rather than as
    // detail. It must be a falloff and not a cutoff: this was a hard gate at roughness
    // 0.5, and every ORM map in the HD set has its roughness floor just above that
    // (measured: 0.51 to 0.53 minimum, 0.78 to 0.83 mean), so the branch below was false
    // for every texel of every unit and the march had never once run.
    // SSR_INTENSITY is a comparison dial, not physics: 1.0 is the honest reflection, above
    // that exaggerates it to make the contribution legible against everything else in the
    // frame. The captured history also reads darker than the displayed frame, so some lift
    // here may be compensating for that rather than for the reflection being wrong -- worth
    // settling separately before treating any value but 1.0 as correct.
    #define SSR_INTENSITY 2.0
    float ssrWeight = SsrParams.x * saturate(1.0 - roughness);
    if (ssrWeight > 0.0)
    {
        float ssrHit;
        float4 ssr = traceSsr(input.worldPos, R, ssrHit);
        // The scene texture is the frame buffer as displayed, so sRGB; everything here
        // is linear until the final encode.
        envCol = lerp(envCol, SrgbToLinear(ssr.rgb) * SSR_INTENSITY, ssr.a * ssrWeight);
    }

    // Reflections are occluded by the same geometry that casts the shadow, so they are
    // attenuated by it too. Without this a shadowed surface picks up the full reflection
    // of sunlit ground and its shadow washes out -- which stayed hidden for as long as
    // SSR was returning black, and appeared the moment it started returning real pixels.
    //
    // Not driven all the way to zero: a surface in shadow is occluded from the sun but
    // still sees most of the sky, so killing the reflection outright would be as wrong in
    // the other direction. 1.0 = reflection fully follows the shadow, 0.0 = ignores it.
    #define ENV_SPEC_SHADOW 0.75
    float3 envSpec = envCol * Fenv * (1.0 - roughness * 0.6) * lerp(1.0, shadow, ENV_SPEC_SHADOW);

    float3 color = Lo + ambient + envSpec;

    // Diagnostics. Set to a non-zero mode and rebuild the rts_shaders target only (no
    // engine rebuild); every PBR mesh then renders that quantity instead of the shading.
    //   1 = cubemap sample        -- the cubemap's own answer at this pixel, with SSR and
    //                                Fresnel both excluded. Black means it never reaches
    //                                sampler s4. It should read as a sky/ground gradient
    //                                that sweeps as the surface turns and as the camera
    //                                orbits, with the sun disc appearing on faces angled
    //                                towards it -- and the sun should show up on the same
    //                                side the cast shadows say it is on.
    //   2 = shading normal N      -- should vary smoothly over the mesh and turn with it;
    //                                per-pixel noise means BUMP_STRENGTH is too high
    //   3 = reflection vector R   -- must change as the unit turns AND as the camera moves
    //   4 = ORM as authored       -- red=AO, green=roughness, blue=metallic
    //   5 = Fresnel weight Fenv   -- how much of the reflection actually survives
    //   6 = SSR triage           -- see below; one run says which part is missing
    //   7 = mirror               -- every PBR unit becomes a perfect mirror, no albedo,
    //                               no Fresnel, no roughness fade. If SSR does anything
    //                               at all, it is unmissable here.
    //   8 = camera depth          -- the prepass read back at this pixel, near white to
    //                               far black. Should be a smooth relief of the scene.
    //   9 = scene history         -- last frame's colour read back at this pixel. Should
    //                               look like the frame itself, painted onto the units.
    //  11 = cubemap contribution  -- what the cubemap actually adds to the frame: the
    //                               sample above, Fresnel-weighted and roughness-faded,
    //                               with SSR excluded. Legitimately dark on dielectrics
    //                               (F0 is 0.04, so a head-on face keeps ~4% of it) and
    //                               brightest at grazing angles. Use 12 if it is too dark
    //                               to judge; use 1 to see the cubemap itself.
    //  12 = cubemap contribution, exposed up 8x so the shape of it is legible. Absolute
    //                               brightness is meaningless here -- only the pattern is.
    //  13 = cast-shadow term      -- computeShadow's raw output on PBR meshes only, white
    //                               = lit, black = fully shadowed. Everything else in the
    //                               frame (terrain, M3 meshes) still shades normally, so the
    //                               frame carries its own control: if the ground shows a
    //                               shadow and the unit standing in it stays white, the term
    //                               is broken; if the unit greys where the ground darkens,
    //                               the term works and the question is how it is combined.
    //  14 = shadow lookup bisect -- takes computeShadow apart on screen, one colour per
    //                               failure. RED = the reprojected UV landed outside the
    //                               sun frustum, so the early-out fires and nothing is ever
    //                               sampled (SunVP wrong, or worldPos not in the space it
    //                               expects). Otherwise green = this pixel's own sun-clip
    //                               depth, blue = the depth the map holds there: blue
    //                               flat-saturated means the sampler is not reading the map
    //                               at all, and green ~= blue means both agree and the
    //                               compare is the thing to look at.
    //  15 = shadow constants     -- what the shader actually received, as colour, to be read
    //                               back numerically. R = ShadowParams.y (strength; 255 means
    //                               1.0), G = ShadowMeshParams.y x10, B = ShadowMeshParams.y
    //                               x1000 (the leftover depth bias, bracketed across two
    //                               scales so whichever magnitude it is, one channel is
    //                               legible). Compare against the CPU-side values logged by
    //                               the routing census -- if they disagree the upload is
    //                               wrong, if they agree the values themselves are.
    //  16 = shadow keep-fraction -- what fraction of its fully-lit brightness a PBR pixel
    //                               retains once the shadow is applied. This is the quantity
    //                               that was actually broken: before the ambient fix it sat
    //                               near 1.0 even where the term said fully shadowed. The
    //                               terrain and the M3 unit shader both bottom out at 0.35,
    //                               so that is the number to land near -- well below it means
    //                               PBR meshes are now darker in shadow than the ground they
    //                               stand on, which is the over-correction to watch for.
#define PBR_DEBUG_MODE 0
#if   PBR_DEBUG_MODE == 1
    // sRGB-encoded like the real output, so what you see is what the bake looks like
    // rather than a linear buffer shown raw (which reads much darker than it is).
    return float4(LinearToSrgb(cubeCol), 1.0);
#elif PBR_DEBUG_MODE == 2
    return float4(N * 0.5 + 0.5, 1.0);
#elif PBR_DEBUG_MODE == 3
    return float4(R * 0.5 + 0.5, 1.0);
#elif PBR_DEBUG_MODE == 4
    return float4(ao, roughness, metallic, 1.0);
#elif PBR_DEBUG_MODE == 5
    return float4(Fenv, 1.0);
#elif PBR_DEBUG_MODE == 6
    {
        // SSR triage. "Nothing changed" has four possible causes and they are
        // indistinguishable on screen, so this separates them in a single run by
        // checking each input where the answer is known: at this pixel's own position,
        // the depth buffer must hold roughly this pixel's depth and the scene history
        // must hold roughly this pixel's colour.
        //
        //   reflected scene = working. Rays are hitting and reading real pixels.
        //   red   = the depth prepass is empty here. It never ran, never cleared, or is
        //           not the target the shader is sampling.
        //   blue  = the scene history is black. endRenderToTexture is not capturing --
        //           likely no filter is active, so the scene never goes through it.
        //   green = every input is live and the ray simply found nothing. Expected for
        //           most pixels at this camera height; if it is *every* pixel, the ray
        //           length or the thickness is wrong, not the plumbing.
        float4 selfClip = mul(float4(input.worldPos, 1.0), CameraVP);
        float2 selfUv   = (selfClip.xy / selfClip.w) * float2(0.5, -0.5) + 0.5;
        float  selfDep  = unpackDepth(tex2Dlod(SceneDepth, float4(selfUv, 0, 0)));
        float3 selfCol  = tex2Dlod(SceneColor, float4(selfUv, 0, 0)).rgb;

        float ssrHit;
        float4 ssr = traceSsr(input.worldPos, R, ssrHit);
        if (ssrHit > 0.0)             return float4(ssr.rgb, 1.0);
        if (selfDep > 0.999)          return float4(1.0, 0.0, 0.0, 1.0);
        if (dot(selfCol, 1.0) < 0.01) return float4(0.0, 0.0, 1.0, 1.0);
        return float4(0.0, 1.0, 0.0, 1.0);
    }
#elif PBR_DEBUG_MODE == 7
    {
        // Pure mirror: the reflection and nothing else. SSR at full strength wherever
        // it hits, the cubemap everywhere else -- so a unit that picks up any of the
        // ground or a neighbouring building is SSR working, and a unit that stays a
        // flat sky-and-ground gradient is the cubemap alone.
        float ssrHit;
        float4 ssr = traceSsr(input.worldPos, R, ssrHit);
        float3 mirror = lerp(texCUBE(EnvSampler, R).rgb, SrgbToLinear(ssr.rgb), ssr.a);
        return float4(LinearToSrgb(mirror), 1.0);
    }
#elif PBR_DEBUG_MODE == 10
    {
        // SSR alone, with the cubemap taken out of the picture entirely. Mode 7 blends
        // the two, so a blown-out environment bake washes the march out no matter what
        // it found. Here a miss is black and nothing hides a hit.
        //   recognisable scene content -- the march works; the cubemap was drowning it
        //   pure black                 -- the march finds nothing, and the cubemap has
        //                                 been carrying the whole reflection all along
        float ssrHit;
        float4 ssr = traceSsr(input.worldPos, R, ssrHit);
        return float4(ssr.rgb * ssrHit, 1.0);
    }
#elif PBR_DEBUG_MODE == 8
    {
        // The depth prepass, read back at this pixel's own position and shown as a
        // greyscale distance -- near white, far black. It should look like a smooth
        // shaded relief of the scene from the camera. Flat black means the prepass is
        // writing nothing; banding or a hard edge partway across means its viewport or
        // its matrix disagrees with the main pass, which is the failure that would make
        // every hit test wrong without ever looking obviously broken.
        float4 selfClip = mul(float4(input.worldPos, 1.0), CameraVP);
        float2 selfUv   = (selfClip.xy / selfClip.w) * float2(0.5, -0.5) + 0.5;
        // The raw texel, not a derived depth. Anything computed from it collapses three
        // very different failures into the same grey mush; the bytes themselves tell
        // them apart, because the target is cleared to pure red (depth = far) and the
        // pack puts coarse depth in R, finer in G, finest in B:
        //   flat pure red        -- bound, but the prepass drew nothing into it
        //   red darkening with distance, G/B banding finely across surfaces -- working
        //   flickering noise     -- not bound at all, and every reading so far has been
        //                           whatever happened to be in that sampler
        return float4(tex2Dlod(SceneDepth, float4(selfUv, 0, 0)).rgb, 1.0);
    }
#elif PBR_DEBUG_MODE == 9
    {
        // The scene history, read back at this pixel's own position. Should look like
        // the frame itself (one frame stale), painted onto the units. A frozen image
        // means the capture ran once and stopped; black means it never ran.
        float4 selfClip = mul(float4(input.worldPos, 1.0), CameraVP);
        float2 selfUv   = (selfClip.xy / selfClip.w) * float2(0.5, -0.5) + 0.5;
        return float4(tex2Dlod(SceneColor, float4(selfUv, 0, 0)).rgb, 1.0);
    }
#elif PBR_DEBUG_MODE == 11 || PBR_DEBUG_MODE == 12
    {
        // The cubemap's contribution to the final image and nothing else: no albedo, no
        // direct light, no ambient, no SSR. This is literally the envSpec term with the
        // SSR blend left out, so whatever shows here is exactly what the cubemap is
        // worth in the shipping shader.
        float3 cubeSpec = cubeCol * Fenv * (1.0 - roughness * 0.6);
#if PBR_DEBUG_MODE == 12
        cubeSpec *= 8.0;   // exposure only, to make a dim-but-correct result legible
#endif
        return float4(LinearToSrgb(cubeSpec), 1.0);
    }
#elif PBR_DEBUG_MODE == 13
    return float4(shadow.xxx, 1.0);
#elif PBR_DEBUG_MODE == 14
    {
        float3 wp   = input.worldPos + N * ShadowMeshParams.x;
        float4 clip = mul(float4(wp, 1.0), SunVP);
        float3 ndc  = clip.xyz / clip.w;
        float2 uv   = ndc.xy * float2(0.5, -0.5) + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
            return float4(1.0, 0.0, 0.0, 1.0);
        float stored = unpackDepth(tex2D(ShadowMap, uv));
        return float4(0.0, saturate(ndc.z), saturate(stored), 1.0);
    }
#elif PBR_DEBUG_MODE == 15
    return float4(ShadowParams.y,
                  saturate(ShadowMeshParams.y * 10.0),
                  saturate(ShadowMeshParams.y * 1000.0), 1.0);
#elif PBR_DEBUG_MODE == 16
    {
        // The ratio is only meaningful where the lit value is large enough to divide by.
        // On a near-black pixel it is noise, and reading that noise as over-darkening is
        // exactly the mistake this mode exists to avoid -- so those are painted pure blue
        // and excluded on the way out, rather than left to sink the distribution.
        float3 envFull = envCol * Fenv * (1.0 - roughness * 0.6);
        float3 lit     = LoUnshadowed + (ambient / max(shadowFill, 1e-4)) + envFull;
        float  litSum  = dot(lit, 1.0);
        if (litSum < 0.05)
            return float4(0.0, 0.0, 1.0, 1.0);
        // Written into red alone, with green and blue forced to zero. Greyscale was
        // unreadable: the filter that picks the result back out of the frame cannot tell
        // a grey debug pixel from dark terrain or grey UI, and counting those as
        // over-darkened PBR is what made the first two measurements look alarming. A pure
        // red ramp is a channel combination the rest of the frame never produces.
        return float4(saturate(dot(color, 1.0) / litSum), 0.0, 0.0, 1.0);
    }
#endif

    float diffAlpha = lerp(input.color.a, AlphaCtl.x, AlphaCtl.y);
    return float4(LinearToSrgb(color), albedoTex.a * diffAlpha);
}

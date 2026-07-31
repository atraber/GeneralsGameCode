// PBR unit pixel shader (Shader Model 3) -- metallic-roughness workflow.
//
// Textures:
//   s0 Albedo   (<name>.dds, sRGB)
//   s1 ORM      (<name>_orm.dds, linear: R=AO, G=Roughness, B=Metallic, A=Height)
// The ORM map is optional per unit; the engine binds a neutral 1x1 default when a
// unit ships no _orm, so this shader degrades to plain lit albedo for those.
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
float4 ShadowParams  : register(c16);  // x = depth bias, y = shadow strength (0 = off)
float4 SsrParams     : register(c17);  // x = strength (0 = off), y = max ray length, zw = proj _33/_43
row_major float4x4 CameraVP : register(c18); // camera view*projection (world -> screen clip)

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

float computeShadow(float3 worldPos)
{
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
    float lit = 0.0;
    [unroll] for (int x = -1; x <= 1; ++x)
        [unroll] for (int y = -1; y <= 1; ++y) {
            float stored = unpackDepth(tex2D(ShadowMap, uv + float2(x, y) * texel));
            lit += (ndc.z - ShadowParams.x > stored) ? 0.0 : 1.0;
        }
    return lerp(1.0, lit / 9.0, ShadowParams.y);
}

// Clip-space depth back to a view-space distance. The prepass stores z/w under the
// camera's perspective projection, which is heavily non-linear -- almost the whole
// scene lands in the last fraction of the range -- so a difference in it is not a
// distance and the thickness test cannot be written in one.
//
// Straight from the two projection elements that produced the value: the projection
// gives ndcZ = _33 + _43/viewZ, so viewZ = _43 / (ndcZ - _33). The previous version
// recovered the near and far planes from those same two numbers and then rebuilt the
// transform out of them -- algebraically identical, but with two extra divisions that
// go through zero for projections this one does not expect, and it is sitting right
// where the stored depth is closest to 1.0 and least forgiving.
float viewDepth(float ndcZ)
{
    return SsrParams.w / (ndcZ - SsrParams.z);
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

    // Cast shadows darken the direct sunlight only (ambient + reflections remain).
    Lo *= computeShadow(input.worldPos);

    // Ambient diffuse under the scene ambient, attenuated by AO.
    float3 ambient   = diffuseColor * SceneAmbient.rgb * ao;

    // Reflection of the shared environment cubemap, Fresnel-weighted and faded on
    // rough surfaces. World-space reflection vector indexes the cubemap directly.
    float3 R      = reflect(-V, N);
    float3 envCol = texCUBE(EnvSampler, R).rgb;
    float  NdotV  = saturate(dot(N, V));
    float3 Fenv   = F_Schlick(NdotV, F0);

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
    float ssrWeight = SsrParams.x * saturate(1.0 - roughness);
    if (ssrWeight > 0.0)
    {
        float ssrHit;
        float4 ssr = traceSsr(input.worldPos, R, ssrHit);
        // The scene texture is the frame buffer as displayed, so sRGB; everything here
        // is linear until the final encode.
        envCol = lerp(envCol, SrgbToLinear(ssr.rgb), ssr.a * ssrWeight);
    }

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
    //   6 = SSR triage           -- see below; one run says which part is missing
    //   7 = mirror               -- every PBR unit becomes a perfect mirror, no albedo,
    //                               no Fresnel, no roughness fade. If SSR does anything
    //                               at all, it is unmissable here.
    //   8 = camera depth          -- the prepass read back at this pixel, near white to
    //                               far black. Should be a smooth relief of the scene.
    //   9 = scene history         -- last frame's colour read back at this pixel. Should
    //                               look like the frame itself, painted onto the units.
#define PBR_DEBUG_MODE 10
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
#endif

    float diffAlpha = lerp(input.color.a, AlphaCtl.x, AlphaCtl.y);
    return float4(LinearToSrgb(color), albedoTex.a * diffAlpha);
}

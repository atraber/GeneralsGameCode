// Water pixel shader (Shader Model 3).
//
// Replaces the two runtime-assembled ps.1.1 shaders the water used (see the strings in
// WaterRenderObjClass::ReAcquireResources) and the fixed-function stack of camera-space
// texture generations around them. Both of those combines are reproduced here exactly --
// they are the "base" layer below -- and the new terms are added on top of that base, so
// a map that dials everything new to zero renders what it always did.
//
// Textures:
//   s0 water base      the scrolling water texture (uv0)
//   s1 refraction      the scene as it stood immediately before the water drew
//   s2 noise           highlight/foam field, projected from world XY
//   s3 edge ramp       river bank alpha (uv1); a 1x1 white texture for standing water
//   s4 env cubemap     the shared procedural sky -- what the surface reflects
//   s5 shadow map      sun-view packed depth
//   s6 shroud          the fog-of-war projection, from world XY
//   s7 scene depth     camera-view packed depth from the SSR prepass
//
// The one input that makes the rest possible is s7. The depth prepass excludes water
// (it is soft-blended with no alpha test, which the routing in Apply_Render_State_Changes
// rejects), so what it holds under a water pixel is the river bed -- not the surface. The
// water can therefore ask how much water is between it and the ground, per pixel, and that
// single number drives the opacity ramp, the shallow-to-deep colour shift and how calm the
// surface is. The old code answered the same question on the CPU, per terrain tile, ahead
// of time (BaseHeightMapRenderObjClass::updateShorelineTile) and could only express the
// answer as one destination-alpha value per vertex of a shoreline tile.
//
// Everything here works in the frame buffer's own (gamma) space rather than in linear.
// That is deliberate: the result is composited by a fixed-function alpha blend into a
// gamma frame buffer, and the terrain and roads underneath it were shaded the same way, so
// a physically-linear result here would be the only thing in the scene that did not match.

sampler2D   WaterTex   : register(s0);
sampler2D   Refraction : register(s1);
sampler2D   NoiseTex   : register(s2);
sampler2D   EdgeTex    : register(s3);
samplerCUBE EnvSampler : register(s4);
sampler2D   ShadowMap  : register(s5);
sampler2D   ShroudTex  : register(s6);
sampler2D   SceneDepth : register(s7);

// x = 1 for river water, 0 for standing; y = sparkle strength; z = 1 when the depth
// prepass ran (0 makes the depth terms stand down rather than read an unbound stage);
// w = the water animation phase, in the same units the CPU scrolls the texture with.
float4 WaterCtl   : register(c0);
// x = opacity extinction rate (1/world units), 0 to disable the ramp entirely;
// y = the opacity of fully deep water, which is the legacy MinWaterOpacity;
// z = colour extinction rate, w = the depth over which the surface calms towards flat.
float4 DepthCtl   : register(c1);
float4 ShallowTint : register(c2);   // rgb multiplier where the bottom is just below
float4 DeepTint    : register(c3);   // rgb multiplier where the column is fully absorbing
// x = reflection strength, y = F0 (the reflectance straight down), z = how much the sun
// shadow darkens the body colour, w = the Fresnel falloff exponent.
//
// The last two are the dials that have to exist because this is a low-dynamic-range
// pipeline. Water's real F0 is about 0.02, and at this game's fixed camera pitch (37.5
// degrees, so 52 degrees off the normal) Schlick's fifth power puts the reflectance at
// roughly 3%. That is correct, and on real water it still reads as reflective -- because
// the sky is orders of magnitude brighter than the bottom, which is a contrast an 8-bit
// gamma frame buffer cannot hold. Reflecting 3% of a sky whose value is 0.6 adds 0.018 to
// the pixel, which is invisible. So F0 is lifted and the exponent softened until the
// reflection is worth the arithmetic; the shape of the curve -- weak looking down, total
// at grazing -- is preserved, only its scale is not physical.
float4 ReflCtl    : register(c4);
float4 SunDir     : register(c5);    // xyz = direction toward the sun, world space
float4 SunCol     : register(c6);    // rgb = sun colour, w = specular exponent
// x = specular strength, y = wave steepness, z = base spatial frequency (radians per
// world unit), w = wave speed.
float4 WaveCtl    : register(c7);
float4 ShroudUV   : register(c8);    // xy = world->UV scale, zw = offset
float4 NoiseUV    : register(c9);    // x = world->UV scale, y = scroll offset
float4 CameraPos  : register(c10);   // world-space camera position
// x = how much of the body opacity comes from the legacy source alpha (vertex alpha times
// texture alpha) rather than from the depth ramp.
//
// It is 0 on every map that ships, and that is not a detail. With the soft water edge on,
// the fixed-function path set SRCBLEND to DESTALPHA -- so the blend factor was the alpha
// the frame clear and the shoreline tiles had left in the buffer, and the source alpha the
// water computed never reached the blend at all. Multiplying by it here as well made the
// surface roughly half as opaque as it has ever been, which read as the new shader being
// washed out rather than as a blend-mode mismatch. Only maps with the soft edge switched
// off, or a back buffer with no destination alpha, ever blended by source alpha.
float4 BlendCtl   : register(c11);

// From here on the registers match unit_pbr_ps deliberately: the shadow lookup and the
// depth reconstruction are the same problem with the same inputs, and giving them the same
// homes means the code below can be read against that shader line for line.
row_major float4x4 SunVP : register(c12);
float4 ShadowParams  : register(c16);  // x = depth bias, y = strength (0 = off), z = 1/size
float4 SsrParams     : register(c17);  // zw = projection _33/_43; xy unused here
row_major float4x4 CameraVP : register(c18);

// x = the vertical depth foam reaches out to, y = strength, z = the foam field's world
// scale, w = its drift rate.
float4 FoamCtl    : register(c24);
float4 FoamCol    : register(c25);
// x = the largest sideways offset refraction may ask for, in world units; y = 1 when the
// grab texture actually exists; z = the vertical depth at which the offset reaches full.
float4 RefractCtl : register(c26);
// rgb = per-channel extinction of the *bottom*, per world unit of path.
//
// This is the one term that needs the grab texture rather than the hardware blend. An
// alpha blend attenuates the background by a single scalar, so on its own it can only
// make deep water more opaque, never more blue -- the colour has to come from the water's
// own body colour showing through more. With the grab in hand the background can be
// multiplied per channel before it is substituted back in, which is what actually makes
// a sandy bottom go green with depth instead of merely fading out.
float4 AbsorbCtl  : register(c27);

struct PS_INPUT
{
    float4 position : POSITION;
    float4 color    : COLOR0;
    float2 uv0      : TEXCOORD0;
    float2 uv1      : TEXCOORD1;
    float3 worldPos : TEXCOORD2;
};

// Unpack the RGB-packed depth the shadow/depth shaders write. Weights are 255, matching
// shadowdepth_ps's pack.
float unpackDepth(float4 rgba)
{
    return dot(rgba.xyz, float3(1.0, 1.0 / 255.0, 1.0 / (255.0 * 255.0)));
}

// Clip-space depth back to a view-space distance. Straight from the two projection
// elements that produced the value. This engine's projection is right-handed, so
// clip.w = -viewZ and ndcZ = -_33 - _43/viewZ; the abs() keeps the expression valid under
// a left-handed projection too, where it comes out negative. (The same note in
// unit_pbr_ps has the full derivation and the story of getting the handedness wrong.)
float viewDepth(float ndcZ)
{
    return abs(SsrParams.w / (SsrParams.z + ndcZ));
}

// Cast shadow, four taps weighted by where the pixel falls inside its texel. The map holds
// depth packed across RGB, so the taps have to be point-sampled and the smoothing has to
// come from weighting the comparisons rather than the depths.
//
// Four rather than the sixteen the mesh shader uses: this term only gates a specular
// highlight and applies a partial darkening to a large smooth surface, where the extra
// taps buy an edge quality nothing on the water is sharp enough to show.
float computeShadow(float3 worldPos)
{
    if (ShadowParams.y <= 0.0)
        return 1.0;

    float4 clip = mul(float4(worldPos, 1.0), SunVP);
    float3 ndc  = clip.xyz / clip.w;
    float2 uv   = ndc.xy * float2(0.5, -0.5) + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
        return 1.0;   // outside the sun frustum -> lit

    const float texel = ShadowParams.z;
    float2 texelPos = uv / texel;
    float2 frc      = frac(texelPos - 0.5);
    float2 baseUv   = (floor(texelPos - 0.5) + 0.5) * texel;

    float lit = 0.0;
    [unroll] for (int y = 0; y < 2; ++y)
        [unroll] for (int x = 0; x < 2; ++x) {
            float2 tapUv  = baseUv + float2(x, y) * texel;
            float  stored = unpackDepth(tex2D(ShadowMap, tapUv));
            float  tapLit = (ndc.z - ShadowParams.x > stored) ? 0.0 : 1.0;
            float  w      = (x == 0 ? 1.0 - frc.x : frc.x) * (y == 0 ? 1.0 - frc.y : frc.y);
            lit += tapLit * w;
        }
    return lerp(1.0, lit, ShadowParams.y);
}

// The surface normal, built analytically rather than sampled.
//
// Four directional waves; each contributes the gradient of its own height field and the
// normal falls out of the sum. Only the gradient is ever needed -- the height itself is
// never used, because the geometry stays flat and all of this is a shading normal.
//
// Analytic rather than a scrolling normal map because there is no normal map to scroll:
// nothing in the shipped art set is one, and deriving a usable tangent-space map from the
// existing water textures would be guesswork about art that was authored to be looked at
// flat. Four waves is also enough to avoid the thing a single normal map does badly here,
// which is reading as one sheet sliding rigidly across the map -- the frequency ratios
// below are deliberately not integers, so the pattern does not close on itself at any
// scale the camera can see.
// Six octaves rather than four. Four covered a range of about 4.7x, which is a single
// swell and no chop -- at this camera's height that reads as a tilted sheet rather than as
// water, because everything the eye uses to judge a water surface (the break-up of the sun
// path, the fine structure in the reflection) lives in the octaves below the swell.
//
// Frequencies rise by 1.75 and gradient weights fall by 0.72, so the run spans about 17x:
// with the default base that is a 140-unit swell down to an 8-unit ripple. The phase rate
// goes as sqrt(k), which is the deep-water dispersion relation -- get this wrong (rate
// linear in k, which is the easy mistake) and the small ripples race across the surface
// faster than the swell instead of shivering in place on it.
//
// footprint is how many world units one screen pixel covers. Octaves finer than a couple
// of pixels cannot be represented and are faded out rather than sampled: left in, they do
// not read as detail, they read as the whole surface boiling when the camera zooms out.
float3 waveNormal(float2 p, float t, float steepness, float freq, float speed, float footprint)
{
    const float2 dirs[6] = {
        float2( 0.8944,  0.4472),
        float2(-0.5145,  0.8575),
        float2( 0.3162, -0.9487),
        float2(-0.9701, -0.2425),
        float2( 0.7071, -0.7071),
        float2(-0.2588,  0.9659)
    };
    const float fmul[6] = { 1.0,  1.75, 3.06, 5.36, 9.38, 16.41 };
    const float wmul[6] = { 1.0,  0.72, 0.52, 0.37, 0.27, 0.19 };
    const float smul[6] = { 1.0,  1.32, 1.75, 2.31, 3.06, 4.05 };   // sqrt(fmul)

    // Sum of wmul, so steepness stays one dial that means the same thing however many
    // octaves are in the sum.
    const float wsum = 3.07;

    float2 grad = 0.0;
    [unroll] for (int i = 0; i < 6; ++i)
    {
        float k     = freq * fmul[i];
        float phase = dot(dirs[i], p) * k + t * speed * smul[i];
        // Nyquist: a wave needs its wavelength to span more than two pixels. This reaches
        // zero at k*footprint ~ pi, which is exactly that limit.
        float band  = saturate(1.0 - k * footprint * 0.35);
        grad += dirs[i] * (cos(phase) * wmul[i] * band);
    }
    return normalize(float3(-grad * (steepness / wsum) * 6.0, 1.0));
}

float4 main(PS_INPUT input) : COLOR
{
    // ---------------------------------------------------------------------------
    // The legacy base. Reproduces the two ps.1.1 combines exactly.
    // ---------------------------------------------------------------------------
    float2 noiseUv  = input.worldPos.xy * NoiseUV.x + NoiseUV.y;
    float2 shroudUv = input.worldPos.xy * ShroudUV.xy + ShroudUV.zw;

    float4 tex    = tex2D(WaterTex, input.uv0);
    float3 noise  = tex2D(NoiseTex, noiseUv).rgb;
    float4 edge   = tex2D(EdgeTex, input.uv1);
    float3 shroud = tex2D(ShroudTex, shroudUv).rgb;

    // The legacy shimmer, rebuilt from the noise field at two scales drifting against each
    // other. It used to be a dedicated sparkle texture on stage 1; that stage now carries
    // the refraction grab, which is worth more than the overlay was. The product of two
    // decorrelated scales gives the same sparse bright speckle the pair used to.
    float3 noise2  = tex2D(NoiseTex, noiseUv * 2.7 - NoiseUV.y * 1.6).rgb;
    float3 sparkle = noise * noise2 * WaterCtl.y;

    float3 baseCol;
    float  legacyAlpha;
    if (WaterCtl.x > 0.5)
    {
        // River. The shroud is already folded into the vertex colour per vertex (that is
        // a deliberate fix for double-darkening at the banks), so the base must not sample
        // it again; the sampled value is still used further down, where the new terms need
        // to be shrouded and have no vertex colour of their own to carry it.
        baseCol     = input.color.rgb * tex.rgb + (sparkle + edge.rgb) * input.color.a;
        legacyAlpha = tex.a * edge.a;
    }
    else
    {
        baseCol     = (input.color.rgb * tex.rgb + sparkle) * shroud;
        legacyAlpha = input.color.a * tex.a;
    }

    // ---------------------------------------------------------------------------
    // How much water is between this pixel and the ground.
    // ---------------------------------------------------------------------------
    // Reprojected with the very matrix the depth prepass rendered with, so the lookup
    // cannot drift out of step with the depth it is reading.
    float4 clip       = mul(float4(input.worldPos, 1.0), CameraVP);
    float2 screenUv   = (clip.xy / clip.w) * float2(0.5, -0.5) + 0.5;
    // Under this projection the clip w is the view-space distance directly; only the
    // stored side is non-linear and needs converting.
    float  surfaceZ   = clip.w;

    // Along-the-view-ray distance through the water, which is the path light actually
    // travels through it -- not the vertical depth. At this camera's pitch the two differ
    // by about a third, and it is the path length that absorption is a function of.
    float pathLen = 400.0;   // no depth pass: behave as if the column were deep
    float bedZ    = surfaceZ + pathLen;
    if (WaterCtl.z > 0.5)
    {
        bedZ    = viewDepth(unpackDepth(tex2D(SceneDepth, screenUv)));
        // Clamped at both ends. Below zero is the prepass and the z-buffer disagreeing by
        // a hair at a silhouette; the ceiling is for water over a texel the prepass never
        // rasterised, which reads as the far plane and would otherwise ask for absorption
        // over a thousand world units.
        pathLen = clamp(bedZ - surfaceZ, 0.0, 400.0);
    }

    // The vertical depth as well, recovered by walking the same view ray to where the bed
    // is. Cheap, because the ray is already implied by two points we have. Absorption
    // wants the path length above; how choppy the surface should be wants this one -- a
    // hand's breadth of water over a sandbank is flat however obliquely it is being seen.
    float3 bedWorld  = CameraPos.xyz + (input.worldPos - CameraPos.xyz) * (bedZ / surfaceZ);
    float  vertDepth = max(input.worldPos.z - bedWorld.z, 0.0);

    // ---------------------------------------------------------------------------
    // Surface, light and sky.
    // ---------------------------------------------------------------------------
    float3 V = normalize(CameraPos.xyz - input.worldPos);
    // Waves flatten out as the water shallows, so the surface goes calm into the shore
    // instead of chopping right up to the waterline.
    float calm = (DepthCtl.w > 0.0) ? saturate(vertDepth / DepthCtl.w) : 1.0;
    // World units per screen pixel, from the interpolated world position's own
    // derivatives. Feeds the octave band-limit in waveNormal.
    float footprint = 0.5 * (length(ddx(input.worldPos.xy)) + length(ddy(input.worldPos.xy)));
    float3 N = waveNormal(input.worldPos.xy, WaterCtl.w, WaveCtl.y * calm,
                          WaveCtl.z, WaveCtl.w, footprint);

    float shadow = computeShadow(input.worldPos);

    // Schlick, against the wave normal rather than the plane -- which is the whole point
    // of having one: a flat plane gives every pixel of a water area the same reflectance,
    // and it is the variation across the waves that reads as a surface. Straight down it
    // is F0 and the body colour carries the pixel; toward the horizon it climbs to 1 and
    // the sky does. Both constants are tuned rather than physical -- see ReflCtl.
    float NdotV = saturate(dot(N, V));
    float fres  = ReflCtl.y + (1.0 - ReflCtl.y) * pow(1.0 - NdotV, ReflCtl.w);
    fres        = saturate(fres * ReflCtl.x);

    // The shared environment cubemap, which is re-baked as time of day drifts and already
    // holds this map's sky gradient, cloud masses and sun disc. Sampled in world space:
    // the bake is built with world +Z as up, the same convention the PBR shader reflects
    // in. This is what makes the surface a surface -- a flat tint cannot tell the viewer
    // which way it is facing, and the horizon brightening comes out of the Fresnel above
    // for free once there is something on the other end of it.
    float3 R       = reflect(-V, N);
    float3 reflCol = texCUBE(EnvSampler, R).rgb * shroud;

    // Sun glint. Blinn-Phong rather than a microfacet lobe: the normal here is analytic
    // and already smooth, so the extra terms of a GGX would be describing a roughness
    // distribution the surface does not have. Gated by the shadow so water under a bridge
    // or a cliff does not sparkle.
    //
    // Deliberately not multiplied by the Fresnel term above. The physical answer is that
    // the sun's reflection is also only a few percent -- and it is still the brightest
    // thing on a lake, because the sun is five orders of magnitude brighter than the sky
    // it sits in. With both compressed into the same 0..1 range that ratio is gone, so
    // scaling this by 3% would delete the highlight rather than dim it. The strength dial
    // stands in for the exposure that is not being modelled.
    float3 H    = normalize(SunDir.xyz + V);
    float  spec = pow(saturate(dot(N, H)), SunCol.w) * WaveCtl.x;
    float3 specCol = SunCol.rgb * (spec * shadow) * shroud;

    // ---------------------------------------------------------------------------
    // Composite.
    // ---------------------------------------------------------------------------
    // Deep water is its own colour; shallow water is mostly whatever is under it. The
    // colour shift and the opacity ramp are separate rates on purpose: how fast the bottom
    // disappears and how fast the water goes green are properties of different things
    // (turbidity and absorption), and maps want to set them apart.
    float colourFade = 1.0 - exp(-pathLen * DepthCtl.z);
    float3 bodyCol   = baseCol * lerp(ShallowTint.rgb, DeepTint.rgb, colourFade);
    bodyCol *= lerp(1.0, shadow, ReflCtl.z);

    // The ramp runs from nothing at the waterline to DepthCtl.y once the column is deep.
    // A rate of zero means the map has the soft edge switched off, and every pixel takes
    // the deep value -- which is what the fixed-function path did with the alpha the frame
    // clear left in the buffer, so switching the feature off lands back on the old look
    // rather than on a different one.
    float cover = (DepthCtl.x > 0.0) ? (1.0 - exp(-pathLen * DepthCtl.x)) : 1.0;
    float bodyAlpha = lerp(DepthCtl.y, legacyAlpha, BlendCtl.x) * cover;

    // Premultiplied, then divided back out, because the hardware blend is a single
    // SRCALPHA/INVSRCALPHA and every term has to reach it through that one alpha.
    //
    // The body covers what is behind it in proportion to its own opacity, and the
    // reflection covers it in proportion to Fresnel -- so the two weights sum to the alpha
    // the blend needs, and dividing the accumulated colour by it recovers the straight
    // colour that multiplies back to the same thing. Getting this wrong is what makes
    // hand-rolled water either wash out at grazing angles (alpha too low to carry the
    // reflection) or go opaque in the shallows (alpha driven by the reflection everywhere).
    //
    // The specular is added to the numerator but not to the alpha, which is exactly right:
    // a highlight is light arriving on top of the scene, not coverage of it. That is what
    // lets the glint sit over shallow water without turning the bottom opaque underneath.
    // -----------------------------------------------------------------------
    // Shoreline foam.
    // -----------------------------------------------------------------------
    // Keyed off the vertical depth, not the path length: surf is a property of how close
    // the bottom is, and a band measured along the view ray would widen and narrow as the
    // camera turned.
    float foam = 0.0;
    if (FoamCtl.y > 0.0 && FoamCtl.x > 0.0)
    {
        float band = 1.0 - saturate(vertDepth / FoamCtl.x);   // 1 at the waterline
        float2 fuv = input.worldPos.xy * FoamCtl.z + WaterCtl.w * FoamCtl.w;
        float  fa  = tex2D(NoiseTex, fuv).r;
        float  fb  = tex2D(NoiseTex, fuv * 1.9 - WaterCtl.w * FoamCtl.w * 0.6).r;
        // Surf runs up the beach and drains back rather than sitting as a fixed ring, so
        // the band edge is pushed in and out by one of the swell's own phases. Sharing the
        // wave field's frequency is what keeps the two looking like one body of water --
        // an independent rate reads as a separate effect laid over the top.
        float surge = 0.5 + 0.5 * sin(dot(float2(0.8944, 0.4472), input.worldPos.xy) * WaveCtl.z
                                      + WaterCtl.w * WaveCtl.w * 1.3);
        float edgeV = band * (0.55 + 0.75 * surge) + (fa * fb - 0.25);
        // The trailing band multiply is what guarantees foam cannot appear out in open
        // water however the noise happens to land -- it still reaches zero at the outer
        // edge, so the guarantee holds.
        //
        // Square-rooted rather than linear. A linear fade over a wide band spends most of
        // its range at low values, which turns the outer half into an even grey veil: the
        // noise still breaks the *shape* up out there, but every streak it leaves is too
        // faint to read as foam. The root keeps the outer streaks at a strength worth
        // seeing while still dying out at the edge, so widening the band adds surf rather
        // than adding haze.
        foam = smoothstep(0.35, 0.80, edgeV) * FoamCtl.y * sqrt(band);
    }

    // -----------------------------------------------------------------------
    // Refraction, and the absorption that rides on it.
    // -----------------------------------------------------------------------
    // The hardware blend will contribute dst * (1 - alpha), where dst is the background at
    // *this* pixel, untinted. What is wanted is the background at a displaced pixel, tinted
    // by the water column. Both are available from the grab -- which was taken immediately
    // before this draw and therefore holds exactly what dst holds -- so the correction is
    // their difference, added to the premultiplied colour with the same (1 - alpha) weight
    // the blend is about to apply. The unwanted term cancels and the wanted one takes its
    // place, with no change to the blend mode and no loss of the multisampled silhouette
    // that outputting an opaque composite here would have cost.
    float3 refractDelta = 0.0;
    if (RefractCtl.y > 0.5)
    {
        // The offset has to vanish at the waterline. There is nothing under a millimetre
        // of water to bend into, and an offset applied there samples the dry beach and
        // smears it out over the surface.
        float  depthScale  = saturate(vertDepth / max(RefractCtl.z, 1e-3));
        float2 offsetWorld = N.xy * (RefractCtl.x * depthScale);
        float4 oclip = mul(float4(input.worldPos + float3(offsetWorld, 0.0), 1.0), CameraVP);
        float2 ouv   = (oclip.xy / oclip.w) * float2(0.5, -0.5) + 0.5;

        // Reject a displaced sample that turns out to be in front of the water. Without
        // this, a unit standing between the camera and this pixel gets dragged across the
        // surface by the offset -- the classic smear that gives hand-rolled refraction
        // away, and the reason the test is on depth rather than on the offset's length.
        float ozRaw = viewDepth(unpackDepth(tex2Dlod(SceneDepth, float4(ouv, 0, 0))));
        if (WaterCtl.z < 0.5 || ozRaw < surfaceZ || ouv.x < 0.0 || ouv.x > 1.0 ||
            ouv.y < 0.0 || ouv.y > 1.0)
            ouv = screenUv;

        float3 bent   = tex2D(Refraction, ouv).rgb;
        float3 plain  = tex2D(Refraction, screenUv).rgb;
        float3 absorb = exp(-pathLen * AbsorbCtl.rgb);
        refractDelta  = bent * absorb - plain;
    }

    float bodyWeight = bodyAlpha * (1.0 - fres);
    float reflWeight = fres;
    // Foam is coverage, not light: it hides the bottom, so it adds to the alpha as well as
    // to the colour. Saturating here rather than after the divide keeps the premultiplied
    // arithmetic below consistent with the alpha the hardware will actually use.
    float foamAlpha  = saturate(foam);
    float outAlpha   = saturate(bodyWeight + reflWeight + foamAlpha);

    float3 premul = bodyCol * bodyWeight + reflCol * reflWeight + specCol
                  + FoamCol.rgb * (foamAlpha * shroud);
    premul += refractDelta * (1.0 - outAlpha);
    float3 outCol = premul / max(outAlpha, 1e-4);

// Diagnostic. Set to 1 and rebuild the shader alone (fxc is a second; no link) to replace
// the surface with three of its own inputs at once, opaque, so the numbers can be read off
// a frame dump instead of reasoned about: R = path length through the water over 100 world
// units, G = the opacity ramp, B = Fresnel. Kept in the file because every one of these is
// invisible in the composited result -- a surface that is too faint looks identical
// whether the depth came back as zero, the ramp never rose, or the reflection is missing.
#define WATER_DEBUG 0
#if WATER_DEBUG == 1
    return float4(pathLen / 100.0, cover, fres, 1.0);
#elif WATER_DEBUG == 2
    // R = foam, G = the refraction offset's magnitude over its maximum, B = wave slope.
    return float4(foam, length(N.xy) * 4.0, saturate(length(refractDelta) * 4.0), 1.0);
#elif WATER_DEBUG == 3
    return float4(N * 0.5 + 0.5, 1.0);          // the wave normal itself
#endif

    return float4(outCol, saturate(outAlpha));
}

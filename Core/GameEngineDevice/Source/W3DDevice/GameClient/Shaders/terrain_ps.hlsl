// Terrain pixel shader.
//
// Base tile cross-blend (uv0/uv1) modulated by baked vertex lighting, then the
// cloud-shadow and noise-detail overlays multiplied on top (matching the old
// renderer's DESTCOLOR/ZERO multiplicative overlay passes).
//
// The two base taps go through stochasticSample, which breaks up the ground's
// repetition by re-sampling each terrain type at a position that varies across the
// map. That has to happen here rather than in the vertex data because the artwork
// tiles seamlessly from cell to cell -- see the comment on stochasticSample.

sampler BaseSampler  : register(s0);
sampler ClassMap     : register(s1);   // per-slot class table (point sampled)
sampler CloudSampler : register(s2);
sampler NoiseSampler : register(s3);
sampler DetailMap    : register(s4);   // procedural detail: RG = gradient, B = height
sampler ShadowMap    : register(s5);   // directional shadow map (packed depth)

float4 OverlayEnable : register(c0); // x = cloud layer on, y = noise layer on
float4 ShadowParams  : register(c1); // x = depth bias, y = shadow strength (0 = off)
float4 AtlasParams   : register(c2); // xy = atlas size in texels, zw = 1/atlas size
float4 TilingParams  : register(c3); // x = stochastic tiling on, y = lattice size (world units)
float4 DetailParams  : register(c4); // x = detail on, y = albedo strength, z = relief strength, w = scale
float4 SunDir        : register(c5); // xyz = direction toward the sun, world space
float4 ColourParams  : register(c6); // x = macro colour variation strength

// Two periods for the detail layer rather than one. A single projection would reintroduce
// exactly the regular repeat the stochastic tiling just removed, only at a different
// pitch; two periods that share no small factor beat together and the combined pattern
// does not close inside a map.
//
// These are deliberately long. At 31 and 71 the layer sat close enough to the base
// texture's own frequency that it added to the clutter instead of shaping it; the point
// of the layer is variation the base artwork cannot express, which means variation
// spanning many cells rather than sitting inside one. DetailParams.w scales both.
static const float DETAIL_PERIOD_A = 120.0;  // world units, 12 terrain cells
static const float DETAIL_PERIOD_B = 290.0;  // world units, 29 terrain cells

// A third, far longer projection, used for colour rather than for light and shade. At
// 800 units it changes only two or three times across a whole map, which is the point:
// this is meant to read as one region being drier or damper than another, not as
// patchiness. Anything short enough to see the shape of would look like staining.
static const float MACRO_PERIOD = 800.0;

// The detail layer is projected straight down, so it has to fade out as the ground turns
// vertical or it smears into streaks.
//
// In practice it never does. Painting this fade into the frame buffer and counting pixels
// across two maps -- a desert and a mountain map -- put 0% of visible terrain in the
// excluded band, and widening the band from 37-63 degrees to 60-75 moved the numbers not
// at all. Heightmap terrain in this game simply does not get that steep: the grid is
// 10-unit cells with byte heights, and what looks like a cliff is nearly always a separate
// model that never reaches this shader.
//
// So this is a guard against a degenerate case rather than something that shapes the
// image, and it is set where it will only ever catch one: full detail out to 60 degrees.
static const float SLOPE_FULL = 0.50;   // cos 60 deg
static const float SLOPE_NONE = 0.25;   // cos 75 deg

// Atlas geometry, fixed by TileData.h / TerrainTex.h. The slot grid these describe is
// the one updateTileTexturePositions places texture classes on.
static const float TILE_EXTENT     = 64.0;   // TILE_PIXEL_EXTENT
static const float SLOT_PITCH      = 72.0;   // TILE_PIXEL_EXTENT + TILE_OFFSET
static const float SLOT_ORIGIN     = 4.0;    // TILE_OFFSET/2
static const float CLASS_MAP_DIM   = 32.0;   // TERRAIN_CLASS_MAP_DIM

struct PS_INPUT
{
    float4 position : POSITION;
    float4 color    : COLOR0;
    float2 uv0      : TEXCOORD0;
    float2 uv1      : TEXCOORD1;
    float2 cloudUV  : TEXCOORD2;
    float2 noiseUV  : TEXCOORD3;
    float4 lightPos : TEXCOORD4;
    float3 worldPos : TEXCOORD5;
};

// Macro colour variation: which part of the map this is, rather than what is underfoot.
//
// Everything else the detail layer does moves brightness, so a whole map still reads as
// one colour. This shifts warm/cool and saturation over hundreds of world units, the way
// real ground goes greener where it holds water and paler where it is worn dry.
//
// Both axes are built to leave luminance alone. The temperature shift pushes red against
// blue and leaves green -- which carries most of the luminance -- untouched, and the
// saturation shift pivots around the pixel's own luminance. That matters because the
// brightness behaviour is already tuned and this must not quietly undo it.
float3 macroColour(float3 col, float3 worldPos, float scale)
{
    float strength = ColourParams.x;
    if (strength <= 0.0) {
        return col;
    }

    // Not slope-faded, unlike the rest of the layer: at this period the field barely
    // changes across the width of a steep face, so there is nothing to smear.
    float4 m = tex2D(DetailMap, worldPos.xy / (MACRO_PERIOD * scale));

    // Height and one gradient channel: two fields off the same texture that vary
    // differently across the map, which is all this needs of them.
    float warm = (m.b - 0.5) * 2.0;
    float sat  = (m.r - 0.5) * 2.0;

    col *= 1.0 + strength * float3(warm, 0.0, -warm);
    float lum = dot(col, float3(0.299, 0.587, 0.114));
    return lerp(lum.xxx, col, 1.0 + strength * sat);
}

// Albedo modulation and relief from the procedural detail layer, folded into a single
// multiplier on the already-lit terrain colour.
//
// The terrain carries no normal -- lighting is baked per vertex on the CPU -- so a field
// reads as a flat-shaded plane. The relief term perturbs the surface normal by the noise
// gradient and lights it against the same sun the bake used, then applies only the
// *difference* the perturbation makes. Where the detail is flat the difference is zero and
// the pixel keeps exactly the colour it had, so this adds surface without disturbing the
// existing lighting balance.
//
// Modulating the whole colour also modulates its ambient part, which a proper relight
// would leave alone. Splitting them would mean pushing the terrain's ambient and diffuse
// in as constants; at these strengths the error is not worth that.
//
// Ngeo is the true geometric normal, so no assumption is made that the ground is flat.
float3 applyTerrainDetail(float3 col, float3 worldPos, float3 Ngeo)
{
    if (DetailParams.x <= 0.0) {
        return col;
    }

    // Guarded: the scale arrives from INI, and a zero would divide the projection into
    // infinities and take the whole terrain with it.
    float scale = max(DetailParams.w, 0.01);

    // Colour first, and independently of slope -- see macroColour.
    col = macroColour(col, worldPos, scale);

    // Light and shade fade out as the ground turns vertical; see SLOPE_FULL.
    float amount = saturate((Ngeo.z - SLOPE_NONE) / (SLOPE_FULL - SLOPE_NONE));
    if (amount <= 0.0) {
        return col;
    }

    float4 a = tex2D(DetailMap, worldPos.xy / (DETAIL_PERIOD_A * scale));
    float4 b = tex2D(DetailMap, worldPos.xy / (DETAIL_PERIOD_B * scale));

    // Gradients are 0.5-biased; summing the two octaves gives the finer one its say
    // without letting either dominate.
    float2 grad   = (a.rg - 0.5) + (b.rg - 0.5);
    float  height = (a.b + b.b) * 0.5;

    // Perturb the normal by the gradient. Valid while the surface is near horizontal,
    // which the slope fade has already guaranteed by the time this runs.
    float3 Nd = normalize(Ngeo - float3(grad * DetailParams.z, 0.0));

    float3 L = normalize(SunDir.xyz);
    float relief = saturate(dot(Nd, L)) - saturate(dot(Ngeo, L));

    // Height is [0,1] with a mean near 0.5, so centring it keeps the average brightness
    // where it was instead of darkening the whole map.
    float albedo = (height - 0.5) * 2.0;

    return col * (1.0 + amount * (relief + DetailParams.y * albedo));
}

// Bounds of the texture class a UV falls in: xy = rect min, zw = rect size, both in
// atlas UV space. w is 0 when no class claims the slot, which is the caller's signal
// to leave the sample alone.
//
// The slot comes from the UV directly -- classes sit on a uniform SLOT_PITCH grid --
// but the slot alone does not say how big the class is or where it started, since a
// class covers width x width slots. That part is looked up. See
// TerrainClassMapTextureClass.
float4 classRect(float2 uv)
{
    float2 texel = uv * AtlasParams.xy;
    float2 slot  = floor((texel - SLOT_ORIGIN) / SLOT_PITCH);

    float4 entry = tex2D(ClassMap, (slot + 0.5) / CLASS_MAP_DIM);
    // Bytes came back as n/255; recover the integers exactly.
    float  width  = floor(entry.r * 255.0 + 0.5);
    float2 offset = floor(entry.gb * 255.0 + 0.5);

    float2 originTexel = SLOT_ORIGIN + (slot - offset) * SLOT_PITCH;
    float2 sizeTexel   = width * TILE_EXTENT;

    // entry.a is 0 on an unclaimed slot; zeroing the size propagates that.
    return float4(originTexel * AtlasParams.zw,
                  sizeTexel * AtlasParams.zw * step(0.5, entry.a));
}

// Two-dimensional hash. Only used to place tile offsets, so it needs to decorrelate
// neighbouring lattice cells and nothing more.
float2 hash2(float2 p)
{
    float2 k = float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)));
    return frac(sin(k) * 43758.5453);
}

// Weights and vertex ids of the three nearest points of a triangular lattice.
// A triangular lattice rather than a square one because three samples cover the plane
// with no gaps, where a square grid needs four.
void triangleLattice(float2 p, out float3 w, out float2 v0, out float2 v1, out float2 v2)
{
    // Skew into a lattice whose cells are triangles, then read barycentrics off the
    // fractional part.
    const float2x2 toSkewed = float2x2(1.0, 0.0, -0.57735027, 1.15470054);
    float2 skewed = mul(toSkewed, p);

    float2 baseId = floor(skewed);
    float2 f      = frac(skewed);
    float  third  = 1.0 - f.x - f.y;

    if (third > 0.0) {
        w  = float3(third, f.y, f.x);
        v0 = baseId;
        v1 = baseId + float2(0.0, 1.0);
        v2 = baseId + float2(1.0, 0.0);
    } else {
        w  = float3(-third, 1.0 - f.y, 1.0 - f.x);
        v0 = baseId + float2(1.0, 1.0);
        v1 = baseId + float2(1.0, 0.0);
        v2 = baseId + float2(0.0, 1.0);
    }
}

// One base-atlas sample, with the terrain type's own tiling broken up.
//
// The artwork tiles seamlessly cell to cell -- WorldBuilder assigns each cell a quadrant
// by position (getTileNdxForClass), so a texture class is one continuous image repeating
// every width*2 cells. That continuity is why the repeat cannot be broken by varying
// anything per cell: any per-cell change lands on a seam the art intends to be invisible.
//
// So vary it per *region* instead and blend the regions. Three lattice points, each with
// its own offset into the class rect, weighted by distance. Offsetting inside a rect that
// is itself seamless keeps every sample continuous, and the blend between regions is
// smooth, so no new edge is introduced anywhere.
//
// The derivatives are the caller's, taken from the unoffset UV: the offsets jump between
// lattice cells and wrap inside the rect, and letting the hardware infer mip level from
// UVs that do that gives a blurred line along every jump.
// Alpha comes through with the colour. Within a class rect it is a flat 1 (every real
// tile is opaque) and 0 outside, so blending it across offsets changes nothing -- which
// is why the cross-blend's validity mask can be read off this rather than costing a
// separate unoffset tap.
float4 stochasticSample(float2 uv, float2 worldXY, float2 ddxUV, float2 ddyUV)
{
    float4 rect = classRect(uv);
    if (TilingParams.x < 0.5 || rect.z <= 0.0 || rect.w <= 0.0) {
        return tex2Dgrad(BaseSampler, uv, ddxUV, ddyUV);
    }

    // Where this pixel sits inside the class rect, as a repeating [0,1) coordinate.
    float2 local = (uv - rect.xy) / rect.zw;

    float3 w;
    float2 v0, v1, v2;
    triangleLattice(worldXY / TilingParams.y, w, v0, v1, v2);

    // Sharpen so most pixels are dominated by one offset. A flat barycentric blend of
    // three samples averages the texture toward its mean and reads as a blurry wash;
    // narrowing the transitions keeps the contrast the artwork had.
    w = w * w * w;
    w /= dot(w, 1.0.xxx);

    // frac() puts every offset sample back inside the rect. Bilinear taps that land on
    // the rect's edge read the 4-texel wrapped border TerrainTextureClass::update draws
    // around each class, so the wrap is filtered correctly rather than bleeding into
    // whatever sits next in the atlas.
    float4 c0 = tex2Dgrad(BaseSampler, rect.xy + frac(local + hash2(v0)) * rect.zw, ddxUV, ddyUV);
    float4 c1 = tex2Dgrad(BaseSampler, rect.xy + frac(local + hash2(v1)) * rect.zw, ddxUV, ddyUV);
    float4 c2 = tex2Dgrad(BaseSampler, rect.xy + frac(local + hash2(v2)) * rect.zw, ddxUV, ddyUV);

    return w.x * c0 + w.y * c1 + w.z * c2;
}

float unpackDepth(float4 rgba)
{
    // Weights are 255, matching shadowdepth_ps's pack -- see the note there.
    return dot(rgba.xyz, float3(1.0, 1.0 / 255.0, 1.0 / (255.0 * 255.0)));
}

// Cast-shadow term for the terrain. The out-of-frustum case is folded in with a mask
// rather than an early-out, which keeps the texture fetches in uniform flow.
//
// This is where cast shadows are seen most: almost every shadow in a normal frame falls
// on the ground rather than on a unit. It used to be a 2x2 box of point comparisons,
// which is what made those shadows stair-step. That filter was chosen because a wider
// one overflowed the ps_2_0 arithmetic slots alongside the terrain blend; the shader now
// targets ps_3_0 so it can run the same filter unit_pbr_ps does.
float terrainShadow(float4 lightPos)
{
    // Guard the divide: a degenerate w yields inf, then NaN, and NaN survives every
    // operation after it -- the pixel is simply gone. Cheaper to be certain here.
    float3 ndc = lightPos.xyz / max(abs(lightPos.w), 1e-6);
    float2 uv  = ndc.xy * float2(0.5, -0.5) + 0.5;
    float inBounds = step(0.0, uv.x) * step(uv.x, 1.0) * step(0.0, uv.y) * step(uv.y, 1.0);

    // Bias arrives per frame (see unit_pbr_ps): the sun frustum is fitted to the camera,
    // so a texel's world size -- and with it the bias needed -- changes with the zoom.
    const float texel = ShadowParams.z;   // 1/SHADOW_MAP_SIZE, fed per frame

    // Bilinear-weighted PCF over a 3x3 texel footprint. The taps have to be point-sampled
    // -- depth is packed across RGB and hardware filtering would interpolate the packed
    // bytes -- so the smoothing comes from weighting the comparisons by where the pixel
    // sits inside its texel. That weighting, not the tap count, is what removes the
    // stepping: a box of point comparisons still snaps every tap to the texel grid.
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
            float tapLit = (ndc.z - ShadowParams.x > stored) ? 0.0 : 1.0;
            lit += tapLit * wx[x] * wy[y];
        }
    // Weights sum to 3 per axis ((1-f) + 1 + 1 + f), so 9 over the kernel.
    // Outside the sun frustum, or with shadowing off, everything is lit.
    return saturate(lerp(1.0, lit / 9.0, inBounds * ShadowParams.y));
}

float4 main(PS_INPUT input) : COLOR
{
    // Cross-blend the base tile (uv0) and neighbour tile (uv1). Both go through the
    // same lattice: uv1 is the neighbouring terrain type's own quadrant, so if only
    // uv0 were varied, the neighbour bleeding across a type boundary would not line up
    // with the same texture drawn in the cell next door.
    //
    // Derivatives come off the unoffset UVs, in uniform flow, and are handed down --
    // see stochasticSample.
    float2 dx0 = ddx(input.uv0), dy0 = ddy(input.uv0);
    float2 dx1 = ddx(input.uv1), dy1 = ddy(input.uv1);

    // Geometric normal straight off the world position's derivatives. The terrain is
    // faceted geometry so this is constant across a triangle, which is fine: it is used
    // for a slope test and as the reference the relief term subtracts off, and in the
    // latter the facet cancels. Sign-corrected rather than relying on a winding order
    // that the triangle flip for blending does not preserve.
    float3 dpx  = ddx(input.worldPos);
    float3 dpy  = ddy(input.worldPos);
    float3 Ngeo = normalize(cross(dpx, dpy));
    Ngeo *= (Ngeo.z < 0.0) ? -1.0 : 1.0;


    float4 tile0 = stochasticSample(input.uv0, input.worldPos.xy, dx0, dy0);
    float4 tile1 = stochasticSample(input.uv1, input.worldPos.xy, dx1, dy1);
    float  blend = tile1.a * input.color.a;
    float3 col   = lerp(tile0.rgb, tile1.rgb, blend) * input.color.rgb;

    col = applyTerrainDetail(col, input.worldPos, Ngeo);

    // Multiplicative overlays, matching the fixed-function noise/cloud pass, which
    // blends over the terrain with SRCBLEND=DESTCOLOR / DESTBLEND=ZERO -- i.e. a plain
    // framebuffer * overlayTexture multiply at unit strength. (An earlier build scaled
    // the darkening up by a "strength" factor to make the overlays more visible, but
    // that pushed the terrain noticeably darker and cooler than the original; the
    // faithful match is a straight multiply.)
    float3 cloudTex = tex2D(CloudSampler, input.cloudUV).rgb;
    float3 noiseTex = tex2D(NoiseSampler, input.noiseUV).rgb;

    // Off layers lerp to white (no effect); safe with an unbound sampler and no
    // ps_2_0 dynamic branching.
    float3 cloud = lerp(float3(1.0, 1.0, 1.0), cloudTex, OverlayEnable.x);
    float3 noise = lerp(float3(1.0, 1.0, 1.0), noiseTex, OverlayEnable.y);

    // Cast shadows: the terrain colour has its lighting baked in, so darken toward a
    // floor (ambient) rather than to black where occluded from the sun.
    const float SHADOW_MIN = 0.35;
    float shadow = terrainShadow(input.lightPos);
    col *= lerp(SHADOW_MIN, 1.0, shadow);

    return float4(col * cloud * noise, 1.0);
}

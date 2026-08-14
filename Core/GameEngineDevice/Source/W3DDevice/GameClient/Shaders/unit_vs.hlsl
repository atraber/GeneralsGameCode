// Unit vertex shader.
//
// Replaces the fixed-function transform & lighting path for 3D meshes with a
// programmable equivalent:
//  * transform to clip space,
//  * when fixed-function lighting is enabled, accumulate the scene ambient plus
//    up to four directional lights (matching the engine's LightEnvironment),
//  * when lighting is disabled, pass through the pre-lit vertex colour.

row_major float4x4 WorldViewProj : register(c0);  // object -> clip space
row_major float4x4 WorldView     : register(c4);  // object -> camera (view) space

float4 LightDir0     : register(c8);   // camera-space direction toward the light
float4 LightDiffuse0 : register(c9);   // 0 when the light is disabled
float4 LightDir1     : register(c10);
float4 LightDiffuse1 : register(c11);
float4 LightDir2     : register(c12);
float4 LightDiffuse2 : register(c13);
float4 LightDir3     : register(c14);
float4 LightDiffuse3 : register(c15);
float4 SceneAmbient  : register(c16);  // equivalent scene ambient (D3DRS_AMBIENT)
// x: 0 = pre-lit, 1 = lit, 2 = texture-only (see the branches in main).
// y: ambient comes from the vertex colour rather than the material (no-normal path).
// z: this draw is effect geometry -- a rotor disc, a glow, a light shaft, a laser, a
//    puff of smoke. Light, not matter. It is not shaded by the sun and not shaded by
//    the clouds, whatever its lighting mode says, because it is not lit by them in the
//    first place: it emits. Setting it forces shadowReceive to 0, which takes out both
//    terms at once (see where lightPos and cloudPos are written).
float4 LightingParams : register(c17);
float4 MatAmbient    : register(c18);  // material ambient colour (house-colour tint)
float4 MatEmissive   : register(c19);  // material emissive colour
float4 MatDiffuse    : register(c20);  // material diffuse colour (house-colour tint)

// Texture coordinate generation, mirroring D3DTSS_TEXCOORDINDEX / the texture matrix.
//   TexGenCtl.x = stage 0 source, .y = stage 1 source
//     0 = pass the mesh coordinates through
//     1 = camera space position   (D3DTSS_TCI_CAMERASPACEPOSITION)
//     2 = camera space normal     (D3DTSS_TCI_CAMERASPACENORMAL)
//     3 = reflection vector       (D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR)
//   TexGenCtl.z / .w = 1 when that stage applies its texture matrix.
float4 TexGenCtl : register(c21);
row_major float4x4 TexMatrix0 : register(c24);
row_major float4x4 TexMatrix1 : register(c28);

// object -> sun clip space, for sampling the shadow map in the pixel shader. Combined
// on the CPU because this path only has the object->camera matrix otherwise, and the
// shadow lookup needs world space.

// The cloud shadow is projected straight down, so all a vertex shader needs to hand on is
// where this pixel sits on the ground plane. Only two columns of the object->world matrix
// are required for that, which is why they arrive as a pair of vectors rather than a whole
// matrix -- this path otherwise never needs world space.
float4 WorldAxisX : register(c22);   // object -> world X
float4 WorldAxisY : register(c23);   // object -> world Y

row_major float4x4 WorldSunVP : register(c32);

// x = how far to lift the shadow lookup off this surface along its normal, in world
// units. y (the leftover depth bias) is read by the pixel shader, not here.
float4 ShadowMeshParams : register(c36);

struct VS_INPUT
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float4 color    : COLOR0;      // vertex diffuse (pre-lit colour / alpha)
    float2 texcoord : TEXCOORD0;
};

struct VS_OUTPUT
{
    float4 position  : POSITION;
    // The lit colour travels in a TEXCOORD and not in COLOR0, which is where it belongs by
    // name and where it lived until HDR. A ps_3_0 COLOR interpolator is defined to clamp to
    // [0,1]: a vertex colour of 4.0 arrives at the pixel shader as 1.0, silently, and no
    // amount of unclamping either end recovers it. TEXCOORD interpolators carry the full
    // float range, so anything that has to stay bright between the two stages goes through
    // one. Every consumer (unit_ps, unit_detail_ps) declares the same semantic.
    float4 color     : TEXCOORD4;
    float2 texcoord  : TEXCOORD0;  // stage 0 coordinates
    float2 texcoord1 : TEXCOORD1;  // stage 1 coordinates
    float4 lightPos  : TEXCOORD2;  // position in the sun's clip space (cast shadows)
    float3 cloudPos  : TEXCOORD3;  // xy = world position on the ground plane, z = receives sun
    // The clip position again, so the pixel shader can find itself on screen and read the
    // depth prepass. POSITION is not readable in a pixel shader, hence the copy.
    float4 screenPos : TEXCOORD5;
};

// Normalizing a zero-length vector yields NaN, and NaN survives everything downstream --
// including multiplication by a zero weight, which is how the weighted selection below
// would carry one -- so a single NaN poisons the lit colour and the generated texture
// coordinates alike, and the pixel ends up black (or, on an additive pass, invisible).
//
// Effect meshes carry zero normals: the fixed-function pipeline never needed them, since
// it takes N.L with the raw normal, gets no diffuse contribution, and falls back to the
// ambient term. Scaling by rsqrt(max(len2, tiny)) reproduces exactly that -- a zero
// normal stays zero, and any real normal is normalized as before.
float3 Safe_Normalize(float3 v)
{
    return v * rsqrt(max(dot(v, v), 1e-12));
}

// Selects one of the coordinate sources without branching: the weights are 1 only for
// the matching mode, so this compiles to a handful of arithmetic instructions.
//
// The padding each source gets is not free choice: it has to be the vector the fixed
// function pipeline would have handed the texture matrix, because the matrices come
// straight from the mappers that were written against it. D3D pads an n-component
// coordinate set with a 1 in slot n and zeroes after it, so a 2-D mesh coordinate is
// (u, v, 1, 0) -- which is why every 2-D mapper puts its translation in _31/_32 and why
// the row-major mul below has to see a 1 in .z to pick it up. Passing (u, v, 0, 1)
// instead multiplies that translation by zero and reads _41, which no mapper writes:
// scale and rotation survive, translation silently disappears. That is what stopped
// tank treads scrolling (LinearOffset), and it applies equally to the Grid, Rotate and
// SineLinearOffset mappers, which all write the same slot.
//
// The camera-space sources are genuine 3-D vectors and keep the (x, y, z, 1) the fixed
// function pipeline gives them -- the shroud projection depends on that w.
float4 Select_TexGen_Source(float mode, float2 meshUV, float3 viewPos,
                            float3 viewNormal, float3 reflection)
{
    float w0 = saturate(1.0 - abs(mode - 0.0));
    float w1 = saturate(1.0 - abs(mode - 1.0));
    float w2 = saturate(1.0 - abs(mode - 2.0));
    float w3 = saturate(1.0 - abs(mode - 3.0));
    return w0 * float4(meshUV, 1.0, 0.0)
         + w1 * float4(viewPos, 1.0)
         + w2 * float4(viewNormal, 1.0)
         + w3 * float4(reflection, 1.0);
}

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;

    output.position = mul(float4(input.position, 1.0), WorldViewProj);
    output.screenPos = output.position;

    // Only meshes actually lit by the sun receive its shadow. Texture-only overlays and
    // pre-lit meshes (effects, which carry their own baked colour) keep it out of their
    // way -- shadowReceive 0 sends lightPos outside the map, which the pixel shader's
    // out-of-frustum path already treats as lit.
    float shadowReceive = 0.0;

    if (LightingParams.x > 1.5)
    {
        // Texture-only pass: the fixed-function stage selects the texture alone (no
        // diffuse), e.g. an unlit detail/overlay multiplied or added over the already-lit
        // base. Output white so the pixel shader passes the raw texture through; applying
        // lighting here would darken the overlay a second time.
        output.color = float4(1.0, 1.0, 1.0, 1.0);
    }
    else if (LightingParams.x > 0.5)
    {
        // The engine's LightEnvironment supplies the light directions already
        // transformed into camera (view) space, so the normal must be taken to the
        // same space -- object * World * View -- for N.L to be correct. Transforming
        // by World alone leaves N in world space; where the object's View is identity
        // (camera-relative world) that happens to coincide, but for meshes drawn with a
        // real view matrix it makes the shading swing as the camera rotates. Using the
        // combined WorldView matrix is correct for both.
        float3 N = Safe_Normalize(mul(input.normal, (float3x3)WorldView));
        // Directional diffuse light (matching the engine's LightEnvironment).
        float3 diffuseLight = LightDiffuse0.rgb * saturate(dot(N, LightDir0.xyz));
        diffuseLight += LightDiffuse1.rgb * saturate(dot(N, LightDir1.xyz));
        diffuseLight += LightDiffuse2.rgb * saturate(dot(N, LightDir2.xyz));
        diffuseLight += LightDiffuse3.rgb * saturate(dot(N, LightDir3.xyz));
        // Fixed-function lit equation with the engine's global colour sources
        // (ambient + diffuse both sourced from the material, matching the vertex
        // material defaults):
        //   emissive(material) + ambient(material) * sceneAmbient
        //                      + diffuse(material) * diffuse light
        // The house-colour tint rides in via the material ambient AND diffuse
        // (Recolor_Vertex_Material sets both to the team colour). Modulating the
        // directional light by the material diffuse is what actually tints the mesh
        // under the (white) sun -- without it the diffuse light washes the tint out to
        // white. Normal meshes have a white material ambient/diffuse and zero emissive,
        // so this reduces to the previous SceneAmbient + diffuseLight (no change).
        float3 lit = MatEmissive.rgb
                   + MatAmbient.rgb * SceneAmbient.rgb
                   + MatDiffuse.rgb * diffuseLight;
        // Carry the vertex alpha through. Lighting produces a colour, not an opacity, and
        // the fixed-function pipeline sources the diffuse alpha from the vertex colour
        // unless D3DRS_DIFFUSEMATERIALSOURCE says otherwise -- which the wrapper already
        // resolves and hands to the pixel shader, where TexCtl.z picks between this alpha
        // and the material's. Forcing 1.0 here made that choice dead code and threw the
        // vertex alpha away for every lit mesh.
        //
        // Effect geometry is what that costs. A glow cone, a beacon's light shaft, a rotor
        // disc: all lit meshes whose fade lives entirely in the vertex alpha and not in any
        // texture. Handed alpha 1 they blend as if opaque, and where the material alpha
        // stood in for them they vanished outright -- which is why soft-blended geometry
        // was once excluded from this path wholesale, an exclusion broad enough to catch
        // ordinary blended passes on buildings and split those meshes across two pipelines.
        // Stage 0 alpha combines that do not source the diffuse at all are unaffected: the
        // wrapper folds this factor to 1 for them.
        // Not saturated. It was, and had to be, while this colour travelled in COLOR0 to an
        // 8-bit target -- both would have clamped it anyway, so the saturate only made the
        // clamp explicit. On a floating-point target it is the one thing standing between a
        // strongly lit surface and the range the tone curve exists to compress, and the
        // colour now travels in a TEXCOORD precisely so that it survives the trip.
        output.color = float4(lit, input.color.a);
        shadowReceive = 1.0;
    }
    else
    {
        // Pre-lit meshes pass the vertex colour and alpha straight through.
        output.color = input.color;
    }

    // Effect geometry emits rather than reflects, so nothing that blocks the sun dims
    // it: a laser crossing a building's shadow is the same laser. The lit branch above
    // has already claimed the sun for anything with lighting enabled, and some effect
    // meshes are lit ones -- a rotor disc carries a normal and no vertex colour -- so
    // this has to override that decision rather than sit inside it.
    shadowReceive *= (LightingParams.z > 0.5) ? 0.0 : 1.0;

    // Emissive gain: what actually makes this scene high dynamic range.
    //
    // Everything above is bounded by its sources -- textures are 8-bit, the light
    // environment is normalised, vertex colours are D3DCOLOR -- so unclamping the lit path
    // by itself produces a scene that still never exceeds 1.0 and a tone curve with nothing
    // to compress. A muzzle flash has to be told it is brighter than white, because nothing
    // in the asset says so: the art was authored for a pipeline where 1.0 was the ceiling
    // and the flash was drawn at the ceiling.
    //
    // The wrapper sets this above 1 only for *additive* effect draws, and that restriction
    // is the whole design. Additive geometry is light being added to the frame -- flashes,
    // tracers, beams, explosions -- and multiplying it is meaningful. Alpha-blended effect
    // geometry is smoke and dust, which occlude rather than emit; scaling those would make
    // a dust cloud glow. Both are MESH_TECHNIQUE_EFFECT, so the technique alone cannot tell
    // them apart and the blend mode has to.
    //
    // Alpha is untouched. On a SRCALPHA/ONE pass alpha is coverage, and scaling it would
    // change how much of the flash is there rather than how bright it is.
    output.color.rgb *= max(LightingParams.w, 1.0);

    // Texture coordinates. Camera-space generation needs the vertex in view space and,
    // for the reflection vector, the view-space normal -- the same WorldView matrix the
    // lighting uses. Each stage then optionally runs through its texture matrix, matching
    // the fixed-function D3DTTFF_COUNT2 transform.
    float3 viewPos    = mul(float4(input.position, 1.0), WorldView).xyz;
    float3 viewNormal = Safe_Normalize(mul(input.normal, (float3x3)WorldView));
    float3 reflection = reflect(Safe_Normalize(viewPos), viewNormal);

    float4 gen0 = Select_TexGen_Source(TexGenCtl.x, input.texcoord, viewPos, viewNormal, reflection);
    float4 gen1 = Select_TexGen_Source(TexGenCtl.y, input.texcoord, viewPos, viewNormal, reflection);

    output.texcoord  = lerp(gen0.xy, mul(gen0, TexMatrix0).xy, TexGenCtl.z);
    output.texcoord1 = lerp(gen1.xy, mul(gen1, TexMatrix1).xy, TexGenCtl.w);

    // Normal offset: look the shadow map up not at this surface but a little way off it
    // along its own normal. That is what keeps a surface out of its own shadow, and it
    // does the job for a fraction of the depth licence a compare bias needs -- a bias has
    // to cover the depth the surface gains across a shadow texel, which for a roof under
    // a low sun runs to several world units and erases everything the building casts on
    // itself. Offsetting instead moves such a shadow by the offset rather than removing
    // it. See the note where the distance is computed in W3DView.
    //
    // The displacement is applied in object space and carried through WorldSunVP, which
    // is exactly the sun-clip image of a world displacement of the same size along the
    // world normal (the object normal is unit length and these transforms are rigid).
    float3 offsetPos = input.position
                     + Safe_Normalize(input.normal) * ShadowMeshParams.x;

    // (2,2,2,1) lands well outside the shadow map's [0,1] UV range, so a mesh that does
    // not receive shadows takes the pixel shader's "outside the sun frustum -> lit" path.
    //
    // Selected, not lerped. lerp evaluates a + t*(b-a), so at t=0 it still multiplies
    // b by zero -- and 0 * inf is NaN, which then survives everything downstream and
    // takes the pixel with it. That is the same trap Safe_Normalize above exists to
    // avoid; a degenerate sun matrix must not be able to reach a mesh that is not even
    // receiving shadows.
    float4 sunClip = mul(float4(offsetPos, 1.0), WorldSunVP);
    output.lightPos = (shadowReceive > 0.5) ? sunClip : float4(2.0, 2.0, 2.0, 1.0);

    // A cloud blocks the sun, so whatever receives the sun's shadow receives its clouds:
    // gate both on the same decision rather than letting them disagree about which
    // meshes the sun reaches.
    output.cloudPos = float3(dot(float4(input.position, 1.0), WorldAxisX),
                             dot(float4(input.position, 1.0), WorldAxisY),
                             shadowReceive);
    return output;
}

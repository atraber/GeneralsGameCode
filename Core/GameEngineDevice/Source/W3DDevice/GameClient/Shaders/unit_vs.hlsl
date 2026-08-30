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
float4 LightingParams : register(c17); // x > 0.5 => fixed-function lighting enabled
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
    float4 color     : COLOR0;
    float2 texcoord  : TEXCOORD0;  // stage 0 coordinates
    float2 texcoord1 : TEXCOORD1;  // stage 1 coordinates
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
        // Lit meshes are opaque (as with the previous fixed-function path);
        // transparency for these is driven by the texture alpha in the pixel shader.
        output.color = float4(saturate(lit), 1.0);
    }
    else
    {
        // Pre-lit meshes pass the vertex colour and alpha straight through.
        output.color = input.color;
    }

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
    return output;
}

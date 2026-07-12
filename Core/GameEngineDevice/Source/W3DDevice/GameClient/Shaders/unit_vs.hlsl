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

struct VS_INPUT
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float4 color    : COLOR0;      // vertex diffuse (pre-lit colour / alpha)
    float2 texcoord : TEXCOORD0;
};

struct VS_OUTPUT
{
    float4 position : POSITION;
    float4 color    : COLOR0;
    float2 texcoord : TEXCOORD0;
};

// Normalizing a zero-length vector yields NaN, and NaN survives everything downstream --
// including multiplication by a zero weight -- so a single one poisons the lit colour and
// the pixel ends up black (or, on an additive pass, invisible).
//
// Effect meshes carry zero normals: the fixed-function pipeline never needed them, since
// it takes N.L with the raw normal, gets no diffuse contribution, and falls back to the
// ambient term. Scaling by rsqrt(max(len2, tiny)) reproduces exactly that -- a zero
// normal stays zero, and any real normal is normalized as before.
float3 Safe_Normalize(float3 v)
{
    return v * rsqrt(max(dot(v, v), 1e-12));
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

    output.texcoord = input.texcoord;
    return output;
}

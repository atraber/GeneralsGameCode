// Surface-normal inspector (pixel, Shader Model 3). Pairs with debugnormal_vs.
//
// Draws the interpolated camera-space normal as colour. The mapping is the conventional
// one -- n*0.5+0.5 -- so the reading is the usual one and does not have to be learned:
// facing the camera is pale blue, facing up in view space is green, facing right is red,
// and anything facing away is dark.

#include "shadermodel.hlsli"

struct PS_INPUT { PS_INPUT_POSITION float3 normal : TEXCOORD0; };

float4 main(PS_INPUT input) : PS_TARGET
{
    // Normalised here rather than in the vertex shader. Interpolating unit vectors across
    // a triangle does not produce unit vectors, and the shortfall is largest exactly where
    // the normals differ most -- which is where this mode is being read.
    float len = length(input.normal);

    // A zero-length normal is a real finding, not a division to be papered over: it means
    // the mesh carries a normal attribute that was never filled in. Flagged in a colour
    // the n*0.5+0.5 mapping can never produce, so it cannot be mistaken for a direction.
    if (len < 1.0e-4)
        return float4(1.0, 0.0, 1.0, 1.0);   // magenta: degenerate normal

    float3 n = input.normal / len;
    return float4(n * 0.5 + 0.5, 1.0);
}

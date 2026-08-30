// Flat debug tint (pixel, Shader Model 3).
//
// Returns one constant colour. Bound in place of whatever pixel shader a draw would
// otherwise have used, by DEBUG_VIS_MESH_TECHNIQUE (see debugvis.h), so that a frame
// can be read as a map of which pipeline drew what.
//
// It declares no interpolants at all, which is what lets it stand in for every shader
// in the engine without a variant per vertex shader. D3D9 requires a pixel shader's
// inputs to be a subset of what the vertex shader produces, not a match, so a shader
// that reads nothing pairs with unit_vs, terrain_vs, tree_vs, road_vs and water_vs
// alike. The alternative -- one tint shader per vertex format -- would be five
// shaders that have to be kept in step with five others, to say the same thing.
//
// The register is deliberately far above the ones real shaders use (water_ps reaches
// c26). The engine sets it just before swapping the shader in, and on a draw where
// the swap does not happen -- the tint shader failed to load -- writing a register
// nothing declares has to be harmless.
float4 DebugTint : register(c31);

float4 main() : COLOR
{
    // Alpha comes from the constant, not from 1.0. The blend mode is left exactly as
    // the draw set it, so an additive or alpha-blended pass still composites the way
    // it did; forcing opaque here would make every effect in the scene draw as a solid
    // card and destroy the layering the mode is meant to show.
    return DebugTint;
}

// Unit vertex shader -- two mesh coordinate sets (D3DFVF_TEX2 and up).
//
// Identical to unit_vs in every other respect. It exists so a pass whose stage 1
// samples the mesh's second coordinate set can be drawn by the shader path at all:
// the detail combine those passes use is one unit_detail_ps already reproduces, and
// the only thing that kept ~16000 sorted draws a window on fixed function was that
// no mesh vertex shader carried the coordinates.
//
// Bound only when the vertex format in the stream really has a second set -- see
// Map_Texture_Coord_Source and needsUvSet1 in dx8wrapper.cpp.

#define UNIT_VS_UV2 1
#include "unit_vs_body.hlsli"

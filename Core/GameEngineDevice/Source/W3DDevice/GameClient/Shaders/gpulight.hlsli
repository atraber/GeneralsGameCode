// GpuLight -- one record in LightBuffer, the StructuredBuffer<GpuLight> the clustered
// path binds at t8 (GFX_FIRST_PIXEL_BUFFER_SLOT, gfxdevice.h). The C++ twin every
// packer/uploader agrees with is
//     Core/Libraries/Source/WWVegas/WW3D2/gpulight.h
// see the comment there for the full 64-byte layout and why it grew past the plan's
// first 48-byte sketch. THE TWO MUST AGREE BYTE FOR BYTE -- if you change one of these
// two files, change the other in the same commit.
//
// LightBuffer itself is NOT declared here. Following the pattern shadow.hlsli already
// sets: a shared .hlsli declares the struct (and, there, the filter function) but leaves
// the register binding to the caller, because different shaders want the buffer under
// different names or none at all. A shader that wants it writes, after including this
// file:
//     StructuredBuffer<GpuLight> LightBuffer : register(t8);
// C3 (the clustered lighting plan) uploads this buffer every frame; no shader reads
// it yet -- that is C5's job, and until it lands this file has no consumer at all.

#ifndef RTS_SHADER_GPULIGHT_HLSLI
#define RTS_SHADER_GPULIGHT_HLSLI

struct GpuLight
{
    float4 posRange;    // xyz = world-space position; w = attenuation range, world units
    float4 colorType;   // rgb = light colour * intensity, linear, PI-convention; w = type: 0 = point, 1 = spot
    float4 spotDirCos;  // xyz = spot direction, world space, normalized (point: zeroed, unused); w = cos(outer half-angle)
    float4 spotInner;   // x = cos(inner half-angle): synthesized smoothstep-edge convenience, not authored
                         // data (point: -1). y = LightClass::Get_Spot_Exponent(), the authored Phong falloff
                         // exponent -- use this to reproduce the real look, not .x (point: 0, i.e. no falloff).
                         // z, w reserved. See gpulight.h for the full reasoning.
};

#endif  // RTS_SHADER_GPULIGHT_HLSLI

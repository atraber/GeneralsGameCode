/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// gpulight.h
//
// GpuLight -- one record in LightBuffer, the StructuredBuffer<GpuLight> bound at t8 that
// the clustered lighting plan section 1.2 assigns to clustered local lights. This is
// the C++ side. The HLSL twin every consuming shader includes is
//     Core/GameEngineDevice/Source/W3DDevice/GameClient/Shaders/gpulight.hlsli
// THE TWO MUST AGREE BYTE FOR BYTE. A StructuredBuffer offers no compile-time check that
// the CPU and GPU sides describe the same bytes the same way, and this project has been
// bitten more than once by two shaders disagreeing about a shared value (the PBR
// irradiance convention notes are the sharpest example). If you change one of these two
// files, change the other in the same commit.
//
// 64 bytes, not the 48 the plan's first sketch used. The plan's float4 spotDirCos carries
// a spot light's outer half-angle cosine in .w; giving the cone a soft edge needs a second,
// inner cosine to smoothstep across, and LightClass (WW3D2/light.h) has nothing to supply
// it from -- Get_Spot_Angle() is one outer edge and Get_Spot_Exponent() is a Phong-style
// falloff exponent, not a second cone. Rather than steal a component from posRange (whose
// .w is range, which C4's sphere culling and C5's attenuation both want in world units, not
// inverted or otherwise encoded) or leave half of a squeezed-in float2 unused, this widens
// the record by one float4 and says so here. See W3DGpuLightList.cpp's Pack_Light for how
// the inner cosine is synthesised from LightClass's single outer angle.
//
// GpuLightListClass (GameEngineDevice/W3DDevice/GameClient/W3DGpuLightList.h) is what
// fills an array of these, culls, and uploads them; nothing in this header does that.

#pragma once

#include "WWMath/vector4.h"

struct GpuLight
{
	Vector4		posRange;		// xyz = world-space position; w = attenuation range, world units (0 is never valid -- see Pack_Light)
	Vector4		colorType;		// rgb = light colour * intensity, linear, PI-convention (see unit_pbr_ps's irradiance comment); w = type: 0 = point, 1 = spot
	Vector4		spotDirCos;		// xyz = spot direction, world space, normalized (point: zeroed, unused); w = cos(outer half-angle). See gpulight.hlsli for the HLSL side.
	Vector4		spotInner;		// x = cos(inner half-angle): a SYNTHESIZED convenience for a shader that wants a
									// smoothstep edge, not authored data -- see Pack_Light's SPOT_INNER_FRACTION.
									// y = LightClass::Get_Spot_Exponent() (light.h), unmodified: the engine's spot
									// falloff is authored as a Phong exponent (pow(cosTheta, SpotExponent)), not an
									// inner/outer cone, so THIS is the value that reproduces a map author's actual
									// look. A shader reading .x instead of .y is choosing a different edge, not a
									// cheaper version of the same one -- pick .y unless the smoothstep look is what
									// you want. Point lights: -1 in .x (see Pack_Light), 0 in .y ("no falloff" --
									// pow(cosTheta, 0) == 1). z, w reserved -- a per-light shadow-map index is the
									// one the plan already names (section 5, "no shadow-casting point/spot lights...
									// yet")
};

// If this fires, gpulight.hlsli's byte layout has drifted from this one. Fix whichever
// side moved, not the assert -- see the file comment above for why the two must match.
static_assert(sizeof(GpuLight) == 64, "GpuLight must match its HLSL twin (gpulight.hlsli) byte for byte");

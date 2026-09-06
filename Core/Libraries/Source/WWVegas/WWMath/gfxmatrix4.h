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

// A 4x4 float matrix in the row-major, row-vector convention, and the handful of
// operations this engine's graphics path performs on one.
//
// WHY THIS IS NOT Matrix4x4, which is the question anyone reading it will ask first.
//
// WWMath's Matrix4x4 is column-vector: it multiplies M*v, so a translation lives in the
// last *column*, and To_D3DMATRIX transposes on the way out. Everything on the device side
// of this engine -- the tracked transforms in DX8Wrapper, the sorting node's captured
// world and view, every matrix that leaves as a vertex-shader constant -- is the other
// convention, v*M, translation in the last *row*. That is not a preference; it is what the
// shaders and the fixed-function pipeline were written against.
//
// Until now the type carrying those values was D3DXMATRIX, which meant <d3dx9.h> in
// fourteen files, which meant <d3d9.h> in the same translation unit as any header that
// wanted to transcribe a D3D constant, which is why gfxstatewords.h still has to include
// <d3d9types.h> instead of holding its own numbers. This type is what replaces it.
//
// It is deliberately layout-identical to D3DMATRIX (and therefore to D3DXMATRIX, which
// derives from it), so a tracked transform can be reinterpreted as one at no cost and with
// no copy -- the same thing the D3DX code did, spelled in a name that does not name a dead
// API.
//
// The convention is stated here once so that the answer to "does this need a transpose?"
// is a lookup and not a judgement:
//
//   * m[i][j] is row i, column j.  _11.._44 name the same storage, _<row><column>.
//   * a point is a row vector on the left: v' = v * M.
//   * Gfx_Matrix_Multiply(out, a, b) is out = a*b, so a is applied first.
//   * translation is _41.._43. Rotation rows are the transformed basis vectors.
//
// Getting that backwards on a matrix bound as a vertex-shader constant does not draw the
// geometry wrong -- it draws nothing at all, which reads as "the feature broke" rather
// than "the matrix is transposed", and is why this move was deferred four times.
#pragma once

#include "WWLib/always.h"
#include "vector3.h"
#include "vector4.h"

#ifndef GFX_PI
#define GFX_PI 3.141592654f
#endif

struct GfxMatrix4
{
	union {
		struct {
			float _11, _12, _13, _14;
			float _21, _22, _23, _24;
			float _31, _32, _33, _34;
			float _41, _42, _43, _44;
		};
		float m[4][4];
	};

	GfxMatrix4() {}
	GfxMatrix4(float a11, float a12, float a13, float a14,
			   float a21, float a22, float a23, float a24,
			   float a31, float a32, float a33, float a34,
			   float a41, float a42, float a43, float a44)
	{
		_11 = a11; _12 = a12; _13 = a13; _14 = a14;
		_21 = a21; _22 = a22; _23 = a23; _24 = a24;
		_31 = a31; _32 = a32; _33 = a33; _34 = a34;
		_41 = a41; _42 = a42; _43 = a43; _44 = a44;
	}

	float * operator [] (int i) { return m[i]; }
	const float * operator [] (int i) const { return m[i]; }
};

// out = a*b. Safe when out aliases either input, which every caller in this tree relies on
// (the world-view-projection chain multiplies a matrix into itself twice).
GfxMatrix4 * Gfx_Matrix_Multiply(GfxMatrix4 * out, const GfxMatrix4 * a, const GfxMatrix4 * b);
GfxMatrix4 * Gfx_Matrix_Identity(GfxMatrix4 * out);
GfxMatrix4 * Gfx_Matrix_Transpose(GfxMatrix4 * out, const GfxMatrix4 * m);
// Returns null and writes nothing when the matrix is singular; det may be null.
GfxMatrix4 * Gfx_Matrix_Inverse(GfxMatrix4 * out, float * det, const GfxMatrix4 * m);
GfxMatrix4 * Gfx_Matrix_Translation(GfxMatrix4 * out, float x, float y, float z);
GfxMatrix4 * Gfx_Matrix_Scaling(GfxMatrix4 * out, float sx, float sy, float sz);
GfxMatrix4 * Gfx_Matrix_RotationZ(GfxMatrix4 * out, float angle);
// Left-handed, the convention this engine's projection and depth range are built for.
GfxMatrix4 * Gfx_Matrix_LookAtLH(GfxMatrix4 * out, const Vector3 * eye,
								 const Vector3 * at, const Vector3 * up);
GfxMatrix4 * Gfx_Matrix_OrthoOffCenterLH(GfxMatrix4 * out, float l, float r,
										 float b, float t, float zn, float zf);

WWINLINE GfxMatrix4 operator * (const GfxMatrix4 & a, const GfxMatrix4 & b)
{
	GfxMatrix4 out;
	Gfx_Matrix_Multiply(&out, &a, &b);
	return out;
}

// v' = v * m, the row-vector product. The _Coord and _Normal forms are the two things a
// caller ever wants of a Vector3: a position (w = 1, then the perspective divide) and a
// direction (w = 0, so translation does not reach it).
Vector4 * Gfx_Vec4_Transform(Vector4 * out, const Vector4 * v, const GfxMatrix4 * m);
Vector4 * Gfx_Vec3_Transform(Vector4 * out, const Vector3 * v, const GfxMatrix4 * m);
Vector3 * Gfx_Vec3_TransformCoord(Vector3 * out, const Vector3 * v, const GfxMatrix4 * m);
Vector3 * Gfx_Vec3_TransformNormal(Vector3 * out, const Vector3 * v, const GfxMatrix4 * m);

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

// The row-vector 4x4 maths that used to come from D3DX. See gfxmatrix4.h for the
// convention, and gfxmatrix4_verify.cpp for the control that says these agree with the
// functions they replace.
#include "gfxmatrix4.h"

#include <math.h>

GfxMatrix4 * Gfx_Matrix_Multiply(GfxMatrix4 * out, const GfxMatrix4 * a, const GfxMatrix4 * b)
{
	// Into a temporary, because callers alias: Gfx_Matrix_Multiply(&wvp, &wvp, &proj) is
	// how every world-view-projection chain in this engine is written.
	GfxMatrix4 r;
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			r.m[i][j] = a->m[i][0] * b->m[0][j]
					  + a->m[i][1] * b->m[1][j]
					  + a->m[i][2] * b->m[2][j]
					  + a->m[i][3] * b->m[3][j];
		}
	}
	*out = r;
	return out;
}

GfxMatrix4 * Gfx_Matrix_Identity(GfxMatrix4 * out)
{
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j)
			out->m[i][j] = (i == j) ? 1.0f : 0.0f;
	return out;
}

GfxMatrix4 * Gfx_Matrix_Transpose(GfxMatrix4 * out, const GfxMatrix4 * m)
{
	GfxMatrix4 r;
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j)
			r.m[i][j] = m->m[j][i];
	*out = r;
	return out;
}

GfxMatrix4 * Gfx_Matrix_Inverse(GfxMatrix4 * out, float * det, const GfxMatrix4 * m)
{
	// Cofactor expansion on the raw array. An inverse does not care which convention the
	// matrix is read in -- inv(M) has the same elements either way -- so this is the one
	// operation here that could have been borrowed from Matrix4x4 unchanged. It is written
	// out anyway so that the whole file reads in one convention.
	const float a00 = m->m[0][0], a01 = m->m[0][1], a02 = m->m[0][2], a03 = m->m[0][3];
	const float a10 = m->m[1][0], a11 = m->m[1][1], a12 = m->m[1][2], a13 = m->m[1][3];
	const float a20 = m->m[2][0], a21 = m->m[2][1], a22 = m->m[2][2], a23 = m->m[2][3];
	const float a30 = m->m[3][0], a31 = m->m[3][1], a32 = m->m[3][2], a33 = m->m[3][3];

	const float s0 = a00 * a11 - a10 * a01;
	const float s1 = a00 * a12 - a10 * a02;
	const float s2 = a00 * a13 - a10 * a03;
	const float s3 = a01 * a12 - a11 * a02;
	const float s4 = a01 * a13 - a11 * a03;
	const float s5 = a02 * a13 - a12 * a03;

	const float c5 = a22 * a33 - a32 * a23;
	const float c4 = a21 * a33 - a31 * a23;
	const float c3 = a21 * a32 - a31 * a22;
	const float c2 = a20 * a33 - a30 * a23;
	const float c1 = a20 * a32 - a30 * a22;
	const float c0 = a20 * a31 - a30 * a21;

	const float d = s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
	if (det) *det = d;
	if (d == 0.0f) return nullptr;
	const float inv = 1.0f / d;

	GfxMatrix4 r;
	r.m[0][0] = ( a11 * c5 - a12 * c4 + a13 * c3) * inv;
	r.m[0][1] = (-a01 * c5 + a02 * c4 - a03 * c3) * inv;
	r.m[0][2] = ( a31 * s5 - a32 * s4 + a33 * s3) * inv;
	r.m[0][3] = (-a21 * s5 + a22 * s4 - a23 * s3) * inv;

	r.m[1][0] = (-a10 * c5 + a12 * c2 - a13 * c1) * inv;
	r.m[1][1] = ( a00 * c5 - a02 * c2 + a03 * c1) * inv;
	r.m[1][2] = (-a30 * s5 + a32 * s2 - a33 * s1) * inv;
	r.m[1][3] = ( a20 * s5 - a22 * s2 + a23 * s1) * inv;

	r.m[2][0] = ( a10 * c4 - a11 * c2 + a13 * c0) * inv;
	r.m[2][1] = (-a00 * c4 + a01 * c2 - a03 * c0) * inv;
	r.m[2][2] = ( a30 * s4 - a31 * s2 + a33 * s0) * inv;
	r.m[2][3] = (-a20 * s4 + a21 * s2 - a23 * s0) * inv;

	r.m[3][0] = (-a10 * c3 + a11 * c1 - a12 * c0) * inv;
	r.m[3][1] = ( a00 * c3 - a01 * c1 + a02 * c0) * inv;
	r.m[3][2] = (-a30 * s3 + a31 * s1 - a32 * s0) * inv;
	r.m[3][3] = ( a20 * s3 - a21 * s1 + a22 * s0) * inv;

	*out = r;
	return out;
}

GfxMatrix4 * Gfx_Matrix_Translation(GfxMatrix4 * out, float x, float y, float z)
{
	Gfx_Matrix_Identity(out);
	out->_41 = x; out->_42 = y; out->_43 = z;
	return out;
}

GfxMatrix4 * Gfx_Matrix_Scaling(GfxMatrix4 * out, float sx, float sy, float sz)
{
	Gfx_Matrix_Identity(out);
	out->_11 = sx; out->_22 = sy; out->_33 = sz;
	return out;
}

GfxMatrix4 * Gfx_Matrix_RotationZ(GfxMatrix4 * out, float angle)
{
	const float c = cosf(angle), s = sinf(angle);
	Gfx_Matrix_Identity(out);
	out->_11 =  c; out->_12 = s;
	out->_21 = -s; out->_22 = c;
	return out;
}

GfxMatrix4 * Gfx_Matrix_LookAtLH(GfxMatrix4 * out, const Vector3 * eye,
								 const Vector3 * at, const Vector3 * up)
{
	Vector3 zaxis = *at - *eye;
	zaxis.Normalize();
	Vector3 xaxis;
	Vector3::Cross_Product(*up, zaxis, &xaxis);
	xaxis.Normalize();
	Vector3 yaxis;
	Vector3::Cross_Product(zaxis, xaxis, &yaxis);

	out->_11 = xaxis.X; out->_12 = yaxis.X; out->_13 = zaxis.X; out->_14 = 0.0f;
	out->_21 = xaxis.Y; out->_22 = yaxis.Y; out->_23 = zaxis.Y; out->_24 = 0.0f;
	out->_31 = xaxis.Z; out->_32 = yaxis.Z; out->_33 = zaxis.Z; out->_34 = 0.0f;
	out->_41 = -Vector3::Dot_Product(xaxis, *eye);
	out->_42 = -Vector3::Dot_Product(yaxis, *eye);
	out->_43 = -Vector3::Dot_Product(zaxis, *eye);
	out->_44 = 1.0f;
	return out;
}

GfxMatrix4 * Gfx_Matrix_OrthoOffCenterLH(GfxMatrix4 * out, float l, float r,
										 float b, float t, float zn, float zf)
{
	Gfx_Matrix_Identity(out);
	out->_11 = 2.0f / (r - l);
	out->_22 = 2.0f / (t - b);
	out->_33 = 1.0f / (zf - zn);
	out->_41 = (l + r) / (l - r);
	out->_42 = (t + b) / (b - t);
	out->_43 = zn / (zn - zf);
	return out;
}

Vector4 * Gfx_Vec4_Transform(Vector4 * out, const Vector4 * v, const GfxMatrix4 * m)
{
	const float x = v->X, y = v->Y, z = v->Z, w = v->W;
	out->X = x * m->_11 + y * m->_21 + z * m->_31 + w * m->_41;
	out->Y = x * m->_12 + y * m->_22 + z * m->_32 + w * m->_42;
	out->Z = x * m->_13 + y * m->_23 + z * m->_33 + w * m->_43;
	out->W = x * m->_14 + y * m->_24 + z * m->_34 + w * m->_44;
	return out;
}

Vector4 * Gfx_Vec3_Transform(Vector4 * out, const Vector3 * v, const GfxMatrix4 * m)
{
	const Vector4 v4(v->X, v->Y, v->Z, 1.0f);
	return Gfx_Vec4_Transform(out, &v4, m);
}

Vector3 * Gfx_Vec3_TransformCoord(Vector3 * out, const Vector3 * v, const GfxMatrix4 * m)
{
	const float x = v->X, y = v->Y, z = v->Z;
	const float w = x * m->_14 + y * m->_24 + z * m->_34 + m->_44;
	const float ow = (w != 0.0f) ? 1.0f / w : 0.0f;
	const float ox = (x * m->_11 + y * m->_21 + z * m->_31 + m->_41) * ow;
	const float oy = (x * m->_12 + y * m->_22 + z * m->_32 + m->_42) * ow;
	const float oz = (x * m->_13 + y * m->_23 + z * m->_33 + m->_43) * ow;
	out->X = ox; out->Y = oy; out->Z = oz;
	return out;
}

Vector3 * Gfx_Vec3_TransformNormal(Vector3 * out, const Vector3 * v, const GfxMatrix4 * m)
{
	const float x = v->X, y = v->Y, z = v->Z;
	const float ox = x * m->_11 + y * m->_21 + z * m->_31;
	const float oy = x * m->_12 + y * m->_22 + z * m->_32;
	const float oz = x * m->_13 + y * m->_23 + z * m->_33;
	out->X = ox; out->Y = oy; out->Z = oz;
	return out;
}

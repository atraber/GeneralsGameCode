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

// TEMPORARY -- delete with the last <d3dx9math.h> in the tree.
//
// The old and the new maths, run side by side on the same inputs, so that "these agree"
// is a number rather than a reading. This is the control the D3DX move was deferred four
// times for want of: Matrix4x4 is column-major, D3DXMATRIX is row-major, and a wrong
// transposition looks completely plausible from one camera angle.
//
// It lives in WW3D2 rather than beside gfxmatrix4.cpp because this is the library that
// links the D3D9 SDK; WWMath does not, and it should not start now for a file that is
// going to be deleted.
//
// Every case reports max|new - old| over the whole 4x4 (or 4-vector). The last two rows are
// the POSITIVE CONTROL: the same comparison against a deliberately transposed multiply and
// a deliberately mirrored rotation. If those two do not report a large error, the harness
// is not running and neither are the zeros above it.
#include "always.h"

#ifdef RTS_DEBUG

#include "gfxmatrix4.h"
#include "wwdebug.h"

// dx8fvf.cpp, the replacement for D3DXGetFVFVertexSize.
extern unsigned Gfx_FVF_Vertex_Size(unsigned FVF);

#include <d3dx9.h>
#include <math.h>

static inline float Bigger(float a, float b) { return (a > b) ? a : b; }

static float Max_Diff(const GfxMatrix4 & a, const D3DXMATRIX & b)
{
	float worst = 0.0f;
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j) {
			const float d = fabsf(a.m[i][j] - b.m[i][j]);
			if (d > worst) worst = d;
		}
	return worst;
}

static float Max_Diff4(const Vector4 & a, const D3DXVECTOR4 & b)
{
	float worst = fabsf(a.X - b.x);
	worst = Bigger(worst, fabsf(a.Y - b.y));
	worst = Bigger(worst, fabsf(a.Z - b.z));
	worst = Bigger(worst, fabsf(a.W - b.w));
	return worst;
}

static float Max_Diff3(const Vector3 & a, const D3DXVECTOR3 & b)
{
	float worst = fabsf(a.X - b.x);
	worst = Bigger(worst, fabsf(a.Y - b.y));
	worst = Bigger(worst, fabsf(a.Z - b.z));
	return worst;
}

// A deterministic generator, so a discrepancy is reproducible and a rerun after a fix is a
// comparison and not a new sample.
static unsigned s_seed = 12345u;
static float Rnd()
{
	s_seed = s_seed * 1664525u + 1013904223u;
	return ((float)((s_seed >> 8) & 0xffff) / 32768.0f) - 1.0f;   // [-1,1)
}

void Gfx_Verify_Against_D3DX()
{
	const int N = 20000;
	float wMul = 0, wInv = 0, wTrn = 0, wIdn = 0, wTsl = 0, wScl = 0, wRot = 0;
	float wLook = 0, wOrtho = 0, wV4T = 0, wV3T = 0, wCoord = 0, wNorm = 0;
	float wCtlTranspose = 0, wCtlRotation = 0;
	int singular = 0, invRan = 0;

	for (int n = 0; n < N; ++n) {
		GfxMatrix4 a, b;
		for (int i = 0; i < 4; ++i)
			for (int j = 0; j < 4; ++j) { a.m[i][j] = Rnd() * 8.0f; b.m[i][j] = Rnd() * 8.0f; }
		const D3DXMATRIX & da = *reinterpret_cast<const D3DXMATRIX *>(&a);
		const D3DXMATRIX & db = *reinterpret_cast<const D3DXMATRIX *>(&b);

		GfxMatrix4 g; D3DXMATRIX d;
		Gfx_Matrix_Multiply(&g, &a, &b);
		D3DXMatrixMultiply(&d, &da, &db);
		wMul = Bigger(wMul, Max_Diff(g, d));
		// The control: the product with the operands swapped is what a mis-transposed
		// conversion produces, and it has to look wrong here or this harness proves nothing.
		D3DXMatrixMultiply(&d, &db, &da);
		wCtlTranspose = Bigger(wCtlTranspose, Max_Diff(g, d));

		Gfx_Matrix_Transpose(&g, &a);
		D3DXMatrixTranspose(&d, &da);
		wTrn = Bigger(wTrn, Max_Diff(g, d));

		float gdet = 0.0f, ddet = 0.0f;
		GfxMatrix4 * gp = Gfx_Matrix_Inverse(&g, &gdet, &a);
		D3DXMATRIX * dp = D3DXMatrixInverse(&d, &ddet, &da);
		if (gp == nullptr || dp == nullptr) {
			++singular;
		} else {
			++invRan;
			// A 4x4 inverse of a random matrix can be large; compare relative to its scale.
			float scale = 1.0f;
			for (int i = 0; i < 4; ++i)
				for (int j = 0; j < 4; ++j) scale = Bigger(scale, fabsf(d.m[i][j]));
			wInv = Bigger(wInv, Max_Diff(g, d) / scale);
		}

		Gfx_Matrix_Identity(&g);
		D3DXMatrixIdentity(&d);
		wIdn = Bigger(wIdn, Max_Diff(g, d));

		const float x = Rnd() * 100.0f, y = Rnd() * 100.0f, z = Rnd() * 100.0f;
		Gfx_Matrix_Translation(&g, x, y, z);
		D3DXMatrixTranslation(&d, x, y, z);
		wTsl = Bigger(wTsl, Max_Diff(g, d));

		Gfx_Matrix_Scaling(&g, x, y, z);
		D3DXMatrixScaling(&d, x, y, z);
		wScl = Bigger(wScl, Max_Diff(g, d));

		const float ang = Rnd() * 6.2831853f;
		Gfx_Matrix_RotationZ(&g, ang);
		D3DXMatrixRotationZ(&d, ang);
		wRot = Bigger(wRot, Max_Diff(g, d));
		// Control: the opposite sense of rotation, which is the other way to get this wrong.
		D3DXMatrixRotationZ(&d, -ang);
		wCtlRotation = Bigger(wCtlRotation, Max_Diff(g, d));

		Vector3 eye(Rnd() * 50.0f, Rnd() * 50.0f, Rnd() * 50.0f + 60.0f);
		Vector3 at(Rnd() * 50.0f, Rnd() * 50.0f, Rnd() * 50.0f);
		Vector3 up(Rnd(), Rnd(), Rnd() + 2.0f);
		Gfx_Matrix_LookAtLH(&g, &eye, &at, &up);
		D3DXMatrixLookAtLH(&d, reinterpret_cast<const D3DXVECTOR3 *>(&eye),
						   reinterpret_cast<const D3DXVECTOR3 *>(&at),
						   reinterpret_cast<const D3DXVECTOR3 *>(&up));
		wLook = Bigger(wLook, Max_Diff(g, d));

		const float l = -fabsf(Rnd()) * 100.0f - 1.0f, r = fabsf(Rnd()) * 100.0f + 1.0f;
		const float bo = -fabsf(Rnd()) * 100.0f - 1.0f, t = fabsf(Rnd()) * 100.0f + 1.0f;
		const float zn = 1.0f, zf = fabsf(Rnd()) * 2000.0f + 10.0f;
		Gfx_Matrix_OrthoOffCenterLH(&g, l, r, bo, t, zn, zf);
		D3DXMatrixOrthoOffCenterLH(&d, l, r, bo, t, zn, zf);
		wOrtho = Bigger(wOrtho, Max_Diff(g, d));

		const Vector4 v4(Rnd() * 20.0f, Rnd() * 20.0f, Rnd() * 20.0f, Rnd() * 2.0f);
		Vector4 gv4; D3DXVECTOR4 dv4;
		Gfx_Vec4_Transform(&gv4, &v4, &a);
		D3DXVec4Transform(&dv4, reinterpret_cast<const D3DXVECTOR4 *>(&v4), &da);
		wV4T = Bigger(wV4T, Max_Diff4(gv4, dv4));

		const Vector3 v3(Rnd() * 20.0f, Rnd() * 20.0f, Rnd() * 20.0f);
		Gfx_Vec3_Transform(&gv4, &v3, &a);
		D3DXVec3Transform(&dv4, reinterpret_cast<const D3DXVECTOR3 *>(&v3), &da);
		wV3T = Bigger(wV3T, Max_Diff4(gv4, dv4));

		Vector3 gv3; D3DXVECTOR3 dv3;
		Gfx_Vec3_TransformNormal(&gv3, &v3, &a);
		D3DXVec3TransformNormal(&dv3, reinterpret_cast<const D3DXVECTOR3 *>(&v3), &da);
		wNorm = Bigger(wNorm, Max_Diff3(gv3, dv3));

		// Coord divides by w, so on a random matrix it can be unbounded. Use the one shape
		// the game actually transforms a coordinate by: a view-projection, whose w is a
		// depth and never near zero for a point in front of the camera.
		GfxMatrix4 vp;
		Gfx_Matrix_LookAtLH(&vp, &eye, &at, &up);
		Gfx_Matrix_Multiply(&vp, &vp, &g);
		const D3DXMATRIX & dvp = *reinterpret_cast<const D3DXMATRIX *>(&vp);
		Gfx_Vec3_TransformCoord(&gv3, &v3, &vp);
		D3DXVec3TransformCoord(&dv3, reinterpret_cast<const D3DXVECTOR3 *>(&v3), &dvp);
		wCoord = Bigger(wCoord, Max_Diff3(gv3, dv3));
	}

	WWDEBUG_SAY(("GFXMATH VERIFY: %d samples against D3DX, max |new-old| per operation", N));
	WWDEBUG_SAY(("  multiply %.3e  transpose %.3e  inverse(rel) %.3e (%d ran, %d singular)",
				 wMul, wTrn, wInv, invRan, singular));
	WWDEBUG_SAY(("  identity %.3e  translation %.3e  scaling %.3e  rotationZ %.3e",
				 wIdn, wTsl, wScl, wRot));
	WWDEBUG_SAY(("  lookAtLH %.3e  orthoOffCenterLH %.3e", wLook, wOrtho));
	WWDEBUG_SAY(("  vec4Transform %.3e  vec3Transform %.3e  transformNormal %.3e  transformCoord %.3e",
				 wV4T, wV3T, wNorm, wCoord));
	WWDEBUG_SAY(("  POSITIVE CONTROL (must be large): swapped multiply %.3e  mirrored rotation %.3e",
				 wCtlTranspose, wCtlRotation));

	// The other thing that came out of D3DX: the flexible vertex format's size in bytes.
	// Swept over every legal FVF the format can express -- nine position types, the four
	// optional components, nine texture-coordinate counts and every per-set size code --
	// rather than over the fourteen this engine happens to build, because the cost of the
	// wider sweep is nothing and the narrower one only proves the cases somebody thought of.
	{
		static const unsigned pos[] = { 0, D3DFVF_XYZ, D3DFVF_XYZRHW, D3DFVF_XYZB1,
										D3DFVF_XYZB2, D3DFVF_XYZB3, D3DFVF_XYZB4,
										D3DFVF_XYZB5, D3DFVF_XYZW };
		int cases = 0, bad = 0, firstBadFVF = 0, firstBadNew = 0, firstBadOld = 0;
		for (int p = 0; p < 9; ++p) {
			for (int bits = 0; bits < 16; ++bits) {
				unsigned base = pos[p];
				if (bits & 1) base |= D3DFVF_NORMAL;
				if (bits & 2) base |= D3DFVF_PSIZE;
				if (bits & 4) base |= D3DFVF_DIFFUSE;
				if (bits & 8) base |= D3DFVF_SPECULAR;
				for (unsigned tc = 0; tc <= 8; ++tc) {
					unsigned fvf = base | (tc << D3DFVF_TEXCOUNT_SHIFT);
					// Two size codes per set is 4^8 combinations at tc=8, so walk a
					// deterministic slice of them rather than all: every set given the
					// same code, then a rotating pattern.
					for (int pass = 0; pass < 5; ++pass) {
						unsigned f = fvf;
						for (unsigned i = 0; i < tc; ++i) {
							const unsigned code = (pass < 4) ? (unsigned)pass : ((i + p) & 3);
							f |= code << (16 + i * 2);
						}
						const unsigned mine = Gfx_FVF_Vertex_Size(f);
						const unsigned theirs = D3DXGetFVFVertexSize(f);
						++cases;
						if (mine != theirs) {
							if (!bad) { firstBadFVF = (int)f; firstBadNew = (int)mine; firstBadOld = (int)theirs; }
							++bad;
						}
					}
				}
			}
		}
		// The control: one FVF answered deliberately wrong, so a run that prints 0 bad has
		// shown the comparison can also print something else.
		const unsigned ctlNew = Gfx_FVF_Vertex_Size(D3DFVF_XYZ | D3DFVF_TEX1);
		const unsigned ctlOld = D3DXGetFVFVertexSize(D3DFVF_XYZ | D3DFVF_TEX2);
		WWDEBUG_SAY(("  FVF vertex size: %d legal formats swept, %d disagree (first 0x%x: new %d old %d)",
					 cases, bad, firstBadFVF, firstBadNew, firstBadOld));
		WWDEBUG_SAY(("  POSITIVE CONTROL (must differ): XYZ|TEX1 = %d against XYZ|TEX2 = %d",
					 ctlNew, ctlOld));
	}
}

#endif // RTS_DEBUG

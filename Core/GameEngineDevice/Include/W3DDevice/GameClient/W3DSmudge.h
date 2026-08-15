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

// FILE: W3DSmudge.h /////////////////////////////////////////////////////////

#pragma once

#include "GameClient/Smudge.h"
#include "WWLib/sharebuf.h"

class SmudgeGroupClass;	//forward reference.
class Vector3;
class Vector4;
class TextureClass;
class SurfaceClass;
class RenderInfoClass;
class DX8IndexBufferClass;

//#define USE_COPY_RECTS	1	//this was the old method that didn't render to texture. Just copied backbuffer into texture. Slow on Nvidia.

class W3DSmudgeManager final : public SmudgeManager
{
public:
	W3DSmudgeManager();
	virtual ~W3DSmudgeManager() override;

	virtual void init() override;
	virtual void reset () override;

	void render (RenderInfoClass &rinfo);
	virtual void ReleaseResources() override;
	virtual void ReAcquireResources() override;

private:
	Bool testHardwareSupport();		///<test if video card supports the effect.
	void createBackgroundTexture();	///<allocate the scene copy in the scene's own colour format.
	void refreshBackgroundTexture();	///<rebuild it if the scene changed format (HDR on/off).
	Bool captureBackground(SurfaceClass *sceneSurface);	///<StretchRect the scene into it.

	enum { MAX_POINTS_PER_GROUP = 512 };

	SmudgeGroupClass *m_smudgeGroup;							///< the point group that contains all of the particles
	ShareBufferClass<Vector3> *m_posBuffer;			///< array of particle positions
	ShareBufferClass<unsigned int> *m_RGBABuffer;		///< array of particle color and alpha
	ShareBufferClass<float> *m_sizeBuffer;			///< array of particle sizes

	TextureClass *m_backgroundTexture;
	///The colour format m_backgroundTexture was built for, as a plain D3DFORMAT value -- kept
	///untyped so this header does not have to pull in D3D. A change of scene format (HDR
	///coming up, or falling back to 8-bit) has to be noticed: StretchRect will not convert
	///between floating point and 8-bit, so a mismatch stops the copy silently.
	UnsignedInt m_backgroundFormat;
	DX8IndexBufferClass	*m_indexBuffer;
	Int m_backBufferWidth;
	Int m_backBufferHeight;
};

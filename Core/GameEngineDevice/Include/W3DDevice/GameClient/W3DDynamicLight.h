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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// W3DDynamicLight.h
// Class to generate texture for terrain.
// Author: John Ahlquist, April 2001

#pragma once

#include "WW3D2/light.h"
#include "Lib/BaseType.h"
class HeightMapRenderObjClass;

/*************************************************************************
**                             W3DDynamicLight
***************************************************************************/
class W3DDynamicLight : public LightClass
{
friend class BaseHeightMapRenderObjClass;
friend class HeightMapRenderObjClass;
protected:
	// TheSuperHackers @feature andytraber 07/09/2026 C7 of the clustered lighting plan.
	// A block of heightmap bookkeeping used to sit here -- m_priorEnable, m_processMe and two
	// integer bounding rectangles (this frame's and last frame's) in map-cell coordinates.
	// Its only purpose was to tell HeightMapRenderObjClass::On_Frame_Update which terrain
	// vertex-buffer tiles a light had entered or left since the previous frame, so the CPU
	// could re-light exactly those. That whole path is gone and nothing else ever read the
	// fields, so they went with it, along with cull(), which tested the same rectangle.
	Bool		m_enabled;

	Bool		m_decayRange;
	Bool		m_decayColor;
	UnsignedInt m_curDecayFrameCount;
	UnsignedInt m_curIncreaseFrameCount;
	UnsignedInt m_decayFrameCount;
	UnsignedInt m_increaseFrameCount;
	Real		m_targetRange;
	Vector3 m_targetAmbient;
	Vector3 m_targetDiffuse;


public:
	W3DDynamicLight();
	virtual ~W3DDynamicLight() override;

public:
	virtual void					On_Frame_Update() override;

	void setEnabled(Bool enabled) { m_enabled = enabled; m_decayRange = false; m_decayFrameCount = 0; m_decayColor = false; m_increaseFrameCount = 0;};
	Bool isEnabled() {return m_enabled;};


	/// 0 frameIncreaseTime means it starts out full size/intensity, 0 decay time means it lasts forever.
	void setFrameFade(UnsignedInt frameIncreaseTime, UnsignedInt decayFrameTime);
	void setDecayRange() {m_decayRange = true;};
	void setDecayColor() {m_decayColor = true;};
};

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

// TerrainTex.h
// Class to generate texture for terrain.
// Author: John Ahlquist, April 2001

#pragma once

//#define DO_8STAGE_TERRAIN_PASS		//optimized terrain rendering for Nvidia based cards

#include "WW3D2/texture.h"
#include "WWMath/matrix3d.h"
#include "Common/AsciiString.h"

class WorldHeightMap;
#define TILE_OFFSET 8

// Side of the terrain class map (see TerrainClassMapTextureClass). One texel per tile
// slot in the base atlas; 2048/(64+8) = 28 slots across, rounded up to a power of two.
#define TERRAIN_CLASS_MAP_DIM 32

// Side of the procedural terrain detail texture (see TerrainDetailTextureClass).
#define TERRAIN_DETAIL_DIM 256
/** ***********************************************************************
**                             TerrainTextureClass
***************************************************************************/
class TerrainTextureClass : public TextureClass
{
	W3DMPO_CODE(TerrainTextureClass)
protected:
	virtual void Apply(unsigned int stage) override;

public:
		/// Create texture for a height map.
		TerrainTextureClass(int height);

		/// Create texture for a height map.
		TerrainTextureClass(int height, int width);

		// just use default destructor. ~TerrainTextureClass();
public:
	int update(WorldHeightMap *htMap); ///< Sets the pixels, and returns the actual height of the texture.
	Bool updateFlat(WorldHeightMap *htMap, Int xCell, Int yCell, Int cellWidth, Int pixelsPerCell); ///< Sets the pixels.
	void setLOD(Int LOD);
};


/** ***********************************************************************
**                          TerrainClassMapTextureClass
**
** A tiny side-table for the base atlas, one texel per 72x72-texel tile slot.
**
** The stochastic-tiling shader has to re-sample a terrain type at an offset and
** wrap back around, which means it needs the bounds of the texture class the
** pixel belongs to. It can derive the slot from the UV (the slots are a uniform
** grid), but not the class: a class occupies width x width slots and nothing in
** the UV says whether width is 1, 2 or 4, nor which slot the class started at.
**
** So each slot records it: R = class width in tiles, G/B = this slot's offset
** from the class's origin slot, A = 0 for slots no class covers. Point sampled,
** never filtered, 4KB. Rebuilt whenever the atlas is.
***************************************************************************/
class TerrainClassMapTextureClass : public TextureClass
{
	W3DMPO_CODE(TerrainClassMapTextureClass)
public:
		TerrainClassMapTextureClass();

	void update(WorldHeightMap *htMap);	///< Fills in the slot table from the class list.
};

/** ***********************************************************************
**                          TerrainDetailTextureClass
**
** Procedural fBm noise, generated at load, that gives the ground surface
** definition the base artwork cannot carry.
**
** The base atlas is about 3 texels per world unit, and terrain lighting is
** baked per vertex, so a field reads as a flat-shaded plane with a pattern on
** it. This supplies the two things that fixes: a fine albedo modulation, and a
** gradient the shader lights per pixel as relief.
**
** RG = the height field's gradient, 0.5-biased; B = the height itself, used as
** the albedo modulation; A unused. Generating it rather than shipping a file
** keeps the "no new art assets" constraint, and lets the field be built
** periodic so it tiles with WRAP addressing.
***************************************************************************/
class TerrainDetailTextureClass : public TextureClass
{
	W3DMPO_CODE(TerrainDetailTextureClass)
public:
		TerrainDetailTextureClass();

	void update();	///< Generates the noise. Depends on nothing; call once after creation.
};

class AlphaTerrainTextureClass : public TextureClass
{
	W3DMPO_CODE(AlphaTerrainTextureClass)
protected:
		virtual void Apply(unsigned int stage) override;
public:
		// Create texture for a height map.
		AlphaTerrainTextureClass(TextureClass *pBaseTex );

		// just use default destructor. ~TerrainTextureClass();

};

/** ***********************************************************************
**                             AlphaEdgeTextureClass
***************************************************************************/
class AlphaEdgeTextureClass : public TextureClass
{
	W3DMPO_CODE(AlphaEdgeTextureClass)
protected:
	virtual void Apply(unsigned int stage) override;
	int update256(WorldHeightMap *htMap);///< Sets the pixels, and returns the actual height of the texture.

public:
		/// Create texture for a height map.
		AlphaEdgeTextureClass(int height, MipCountType mipLevelCount = MIP_LEVELS_3 );

		// just use default destructor. ~TerrainTextureClass();
public:
	int update(WorldHeightMap *htMap); ///< Sets the pixels, and returns the actual height of the texture.

};

class LightMapTerrainTextureClass : public TextureClass
{
	W3DMPO_CODE(LightMapTerrainTextureClass)
protected:
		virtual void Apply(unsigned int stage) override;

public:
		// Create texture from a height map.
		LightMapTerrainTextureClass( AsciiString name, MipCountType mipLevelCount = MIP_LEVELS_ALL );

		// just use default destructor.
};

class ScorchTextureClass : public TextureClass
{
	W3DMPO_CODE(ScorchTextureClass)
protected:
		virtual void Apply(unsigned int stage) override;

public:
		// Create texture.
		ScorchTextureClass( MipCountType mipLevelCount = MIP_LEVELS_3 );

		// just use default destructor. ~ScorchTextureClass();
};

class CloudMapTerrainTextureClass : public TextureClass
{
	W3DMPO_CODE(CloudMapTerrainTextureClass)
protected:
		virtual void Apply(unsigned int stage) override;

protected:
		float m_xSlidePerSecond ;	 ///< How far the clouds move per second.
		float m_ySlidePerSecond ;	 ///< How far the clouds move per second.
		int	  m_curTick;
		float m_xOffset;
		float m_yOffset;


public:
		// Create texture from a height map.
		CloudMapTerrainTextureClass( MipCountType mipLevelCount = MIP_LEVELS_ALL );

		// just use default destructor. ~TerrainTextureClass();

		void restore();
};

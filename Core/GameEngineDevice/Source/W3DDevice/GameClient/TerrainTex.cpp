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

// FILE: TerrainTex.cpp ////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//
//                       Westwood Studios Pacific.
//
//                       Confidential Information
//                Copyright (C) 2001 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: TerrainTex.cpp
//
// Created:   John Ahlquist, April 2001
//
// Desc:      TextureClass overrides to perform custom texturing for the terrain.
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//         Includes
//-----------------------------------------------------------------------------
#include <stdlib.h>

#include "W3DDevice/GameClient/TerrainTex.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "W3DDevice/GameClient/TileData.h"
#include "Common/GlobalData.h"
#include "WW3D2/dx8wrapper.h"
#include "d3dx8tex.h"

/******************************************************************************
						TerrainTextureClass
******************************************************************************/
//-----------------------------------------------------------------------------
//         Public Functions
//-----------------------------------------------------------------------------

//=============================================================================
// TerrainTextureClass::TerrainTextureClass
//=============================================================================
/** Constructor. Calls parent constructor to create a 32 bit per pixel D3D
texture of the desired height and mip level.

The atlas used to be A1R5G5B5. Five bits per channel is not enough for the
terrain: every tile that is mostly one hue with a slow gradient across it --
sand, dirt, dry grass, which is most of the ground in most maps -- banded
visibly, and the bands sat still under the camera so they read as part of the
texture rather than as noise. Widening to eight bits per channel doubles the
atlas (2048 x pow2Height x 4 bytes) and costs nothing else; the tile source
data was always 24-bit, so this stops discarding it. */
//=============================================================================
TerrainTextureClass::TerrainTextureClass(int height) :
	TextureClass(TEXTURE_WIDTH, height,
		WW3D_FORMAT_A8R8G8B8, MIP_LEVELS_3 )
{
}

//=============================================================================
// TerrainTextureClass::TerrainTextureClass
//=============================================================================
/** Constructor. Calls parent constructor to create a 32 bit per pixel D3D
texture of the desired height and mip level. This is the flat-terrain (low LOD)
variant; it is filled by updateFlat and follows the base atlas's format. */
//=============================================================================
TerrainTextureClass::TerrainTextureClass(int height, int width) :
	TextureClass(width, height,
		WW3D_FORMAT_A8R8G8B8, MIP_LEVELS_ALL )
{
}


//=============================================================================
// TerrainTextureClass::update
//=============================================================================
/** Sets the tile bitmap data into the texture.  The tiles are placed with 4
	pixel borders around them, so that when the tiles are scaled and bilinearly
	interpolated, you don't get seams between the tiles.  */
//=============================================================================
int TerrainTextureClass::update(WorldHeightMap *htMap)
{
	// D3DTexture is our texture;

	GfxSurface *surface_level;
	WW3DSurfaceDescription surface_desc;
	GfxMappedRect locked_rect;
	surface_level = DX8Wrapper::Get_DX8_Texture_Surface_Level(Peek_D3D_Base_Texture(), 0);
	DX8Wrapper::Describe_DX8_Surface(surface_level, surface_desc);
	if (surface_desc.Width < TEXTURE_WIDTH) {
		DX8Wrapper::Release_DX8_Surface_Resource(surface_level);
		return 0;
	}

	DX8Wrapper::Map_DX8_Surface(surface_level, nullptr, GFX_MAP_WRITE, locked_rect);

	Int tilePixelExtent = TILE_PIXEL_EXTENT;
	Int tilesPerRow = surface_desc.Width/(2*TILE_PIXEL_EXTENT+TILE_OFFSET);
	tilesPerRow *= 2;
//	Int numRows = surface_desc.Height/(tilePixelExtent+TILE_OFFSET);
#ifdef RTS_DEBUG
	//DEBUG_ASSERTCRASH(tilesPerRow*numRows >= htMap->m_numBitmapTiles, ("Too many tiles."));
	DEBUG_ASSERTCRASH((Int)surface_desc.Width >= tilePixelExtent*tilesPerRow, ("Bitmap too small."));
#endif
	if (surface_desc.Format == WW3D_FORMAT_A8R8G8B8) {
		Int tileNdx;
		Int pixelBytes = 4;
		for (tileNdx=0; tileNdx < htMap->m_numBitmapTiles; tileNdx++) {
			TileData *pTile = htMap->getSourceTile(tileNdx);
			if (!pTile) continue;
			ICoord2D position = pTile->m_tileLocationInTexture;
			if (position.x<=0) continue; // all real tile offsets start at 2.  jba.

			Int i,j;
			for (j=0; j<tilePixelExtent; j++) {
				UnsignedByte *pBGR = pTile->getRGBDataForWidth(tilePixelExtent);
				pBGR += (tilePixelExtent-1-j)*TILE_BYTES_PER_PIXEL*tilePixelExtent; // invert to match.
				Int row = position.y+j;
				UnsignedByte *pBGRX = ((UnsignedByte*)locked_rect.Data) +
							(row)*surface_desc.Width*pixelBytes;

				Int column = position.x;
				pBGRX += column*pixelBytes;
				for (i=0; i<tilePixelExtent; i++) {
					// Source is BGR; the atlas is ARGB8888 and terrain is always opaque.
					*((UnsignedInt*)pBGRX) = 0xFF000000 | (pBGR[2]<<16) | (pBGR[1]<<8) | pBGR[0];
					pBGRX +=pixelBytes;
					pBGR +=TILE_BYTES_PER_PIXEL;
				}
			}
		}
		// Now draw the 4 pixel border around each tile class.
		Int texClass;
		for (texClass=0; texClass<htMap->m_numTextureClasses; texClass++) {
			Int width = htMap->m_textureClasses[texClass].width;
			ICoord2D origin = htMap->m_textureClasses[texClass].positionInTexture;
			if (origin.x<=0) continue;
			width *= TILE_PIXEL_EXTENT;
			// Duplicate 4 columns of pixels before and after.
			Int j;
			for (j=0; j<width; j++) {
				Int row = origin.y+j;
				UnsignedByte *pBGRX = ((UnsignedByte*)locked_rect.Data) +
							(row)*surface_desc.Width*pixelBytes;

				Int column = origin.x;
				pBGRX += column*pixelBytes;
				// copy before
				memcpy(pBGRX-(4)*pixelBytes, pBGRX+(width-4)*pixelBytes, 4*pixelBytes);
				// copy after
				memcpy(pBGRX+(width*pixelBytes), pBGRX, 4*pixelBytes);
			}

			// Duplicate 4 rows of pixels before and after.
			for (j=0; j<4; j++) {
				// copy before.
				Int row = origin.y-j-1;
				UnsignedByte *pBGRX = ((UnsignedByte*)locked_rect.Data) +
							(row)*surface_desc.Width*pixelBytes;
				UnsignedByte *target = pBGRX+(origin.x-4)*pixelBytes;
				memcpy(target, target+width*surface_desc.Width*pixelBytes, (width+8)*pixelBytes);
				// copy after.
				row = origin.y+j;
				pBGRX = ((UnsignedByte*)locked_rect.Data) +
							(row)*surface_desc.Width*pixelBytes;
				target = pBGRX+(origin.x-4)*pixelBytes;
				memcpy(target+width*surface_desc.Width*pixelBytes, target, (width+8)*pixelBytes);
			}

		}

	}
	DX8Wrapper::Unmap_DX8_Surface(surface_level);
	DX8Wrapper::Release_DX8_Surface_Resource(surface_level);
	DX8Wrapper::Generate_DX8_Mips(Peek_D3D_Base_Texture(), 0);
	if (WW3D::Get_Texture_Reduction()) {
		DX8Wrapper::Set_DX8_Texture_Detail_Level(Peek_D3D_Base_Texture(), WW3D::Get_Texture_Reduction());
	}
	return(surface_desc.Height);
}


//=============================================================================
// TerrainTextureClass::setLOD
//=============================================================================
/** Sets the lod of the texture to be loaded into the video card.  */
//=============================================================================
void TerrainTextureClass::setLOD(Int LOD)
{
	if (Peek_D3D_Texture()) DX8Wrapper::Set_DX8_Texture_Detail_Level(Peek_D3D_Base_Texture(), LOD);
}
//=============================================================================
// TerrainTextureClass::update
//=============================================================================
/** Sets the tile bitmap data into the texture.  The tiles are placed with 4
	pixel borders around them, so that when the tiles are scaled and bilinearly
	interpolated, you don't get seams between the tiles.  */
//=============================================================================
Bool TerrainTextureClass::updateFlat(WorldHeightMap *htMap, Int xCell, Int yCell, Int cellWidth, Int pixelsPerCell)
{
	// D3DTexture is our texture;

	GfxSurface *surface_level;
	WW3DSurfaceDescription surface_desc;
	GfxMappedRect locked_rect;
	surface_level = DX8Wrapper::Get_DX8_Texture_Surface_Level(Peek_D3D_Base_Texture(), 0);
	DX8Wrapper::Describe_DX8_Surface(surface_level, surface_desc);
	DEBUG_ASSERTCRASH((Int)surface_desc.Width == cellWidth*pixelsPerCell, ("Bitmap too small."));
	DEBUG_ASSERTCRASH((Int)surface_desc.Height == cellWidth*pixelsPerCell, ("Bitmap too small."));
	if (surface_desc.Width != cellWidth*pixelsPerCell) {
		return false;
	}

	DX8Wrapper::Map_DX8_Surface(surface_level, nullptr, GFX_MAP_WRITE, locked_rect);


	if (surface_desc.Format == WW3D_FORMAT_A8R8G8B8) {

		Int pixelBytes = 4;
		Int cellX, cellY;
		for (cellX = 0; cellX < cellWidth; cellX++) {
			for (cellY = 0; cellY < cellWidth; cellY++) {
				UnsignedByte *pBGRX_data = ((UnsignedByte*)locked_rect.Data);
				UnsignedByte *pBGR = htMap->getPointerToTileData(xCell+cellX, yCell+cellY, pixelsPerCell);
				if (pBGR == nullptr) continue; // past end of defined terrain. [3/24/2003]
				Int k, l;
				for (k=pixelsPerCell-1; k>=0; k--) {
					UnsignedByte *pBGRX = pBGRX_data + (pixelsPerCell*(cellWidth-cellY-1)+k)*surface_desc.Width*pixelBytes +
						cellX*pixelsPerCell*pixelBytes;
					for (l=0; l<pixelsPerCell; l++) {
						// Source is BGR; the atlas is ARGB8888 and terrain is always opaque.
						*((UnsignedInt*)pBGRX) = 0xFF000000 | (pBGR[2]<<16) | (pBGR[1]<<8) | pBGR[0];
						pBGRX +=pixelBytes;
						pBGR +=TILE_BYTES_PER_PIXEL;
					}
				}
			}
		}
	}

	DX8Wrapper::Unmap_DX8_Surface(surface_level);
	DX8Wrapper::Release_DX8_Surface_Resource(surface_level);
	DX8Wrapper::Generate_DX8_Mips(Peek_D3D_Base_Texture(), 0);
	return(surface_desc.Height);
}

//=============================================================================
// TerrainTextureClass::Apply
//=============================================================================
/** Sets the texture as the current D3D texture, and does some custom setup
(standard D3D setup, but beyond the scope of W3D).  */
//=============================================================================
void TerrainTextureClass::Apply(unsigned int stage)
{
	FF_SITE("TerrainTextureClass::Apply");
	// Do the base apply.
	TextureClass::Apply(stage);
}

/******************************************************************************
					TerrainClassMapTextureClass
******************************************************************************/

//=============================================================================
// TerrainClassMapTextureClass::TerrainClassMapTextureClass
//=============================================================================
TerrainClassMapTextureClass::TerrainClassMapTextureClass() :
	TextureClass(TERRAIN_CLASS_MAP_DIM, TERRAIN_CLASS_MAP_DIM,
		WW3D_FORMAT_A8R8G8B8, MIP_LEVELS_1 )
{
}

//=============================================================================
// TerrainClassMapTextureClass::update
//=============================================================================
/** Records, for every tile slot the atlas has, which texture class covers it and
	where that class begins. See the class comment for why the shader cannot work
	this out from a UV on its own.

	Must be called after updateTileTexturePositions has placed the classes. */
//=============================================================================
void TerrainClassMapTextureClass::update(WorldHeightMap *htMap)
{
	GfxSurface *surface_level;
	WW3DSurfaceDescription surface_desc;
	GfxMappedRect locked_rect;
	surface_level = DX8Wrapper::Get_DX8_Texture_Surface_Level(Peek_D3D_Base_Texture(), 0);
	DX8Wrapper::Describe_DX8_Surface(surface_level, surface_desc);
	if (surface_desc.Format != WW3D_FORMAT_A8R8G8B8) {
		DX8Wrapper::Release_DX8_Surface_Resource(surface_level);
		return;
	}
	DX8Wrapper::Map_DX8_Surface(surface_level, nullptr, GFX_MAP_WRITE, locked_rect);

	// Alpha 0 everywhere means "no class here"; the shader falls back to a plain
	// unoffset sample for those, so an incomplete table degrades rather than breaks.
	Int x, y;
	for (y = 0; y < TERRAIN_CLASS_MAP_DIM; y++) {
		UnsignedInt *row = (UnsignedInt*)((UnsignedByte*)locked_rect.Data + y*locked_rect.Pitch);
		for (x = 0; x < TERRAIN_CLASS_MAP_DIM; x++) {
			row[x] = 0;
		}
	}

	const Int pitch = TILE_PIXEL_EXTENT + TILE_OFFSET;
	Int texClass;
	for (texClass = 0; texClass < htMap->m_numTextureClasses; texClass++) {
		const Int width = htMap->m_textureClasses[texClass].width;
		const ICoord2D origin = htMap->m_textureClasses[texClass].positionInTexture;
		// x <= 0 marks a class that found no room in the atlas -- same test the tile
		// fill and the UV lookup use.
		if (origin.x <= 0 || width <= 0) continue;

		// positionInTexture is the class rect's top-left in texels; the slots it
		// occupies follow from the uniform grid updateTileTexturePositions placed it on.
		const Int originCol = (origin.x - TILE_OFFSET/2) / pitch;
		const Int originRow = (origin.y - TILE_OFFSET/2) / pitch;

		Int i, j;
		for (j = 0; j < width; j++) {
			for (i = 0; i < width; i++) {
				const Int col = originCol + i;
				const Int row = originRow + j;
				if (col < 0 || col >= TERRAIN_CLASS_MAP_DIM) continue;
				if (row < 0 || row >= TERRAIN_CLASS_MAP_DIM) continue;
				UnsignedInt *dst = (UnsignedInt*)((UnsignedByte*)locked_rect.Data + row*locked_rect.Pitch);
				dst[col] = 0xFF000000 | ((UnsignedInt)width << 16) | ((UnsignedInt)i << 8) | (UnsignedInt)j;
			}
		}
	}

	DX8Wrapper::Unmap_DX8_Surface(surface_level);
	DX8Wrapper::Release_DX8_Surface_Resource(surface_level);
}

/******************************************************************************
					TerrainDetailTextureClass
******************************************************************************/

//=============================================================================
// Periodic value noise.
//
// The lattice index is wrapped to the octave's period before hashing, which is what
// makes the finished field tile: sampling at u and u+1 hits the same lattice points,
// so the texture can be addressed with WRAP and shows no seam.
//=============================================================================
static Real detailLatticeValue(Int x, Int y, Int period, UnsignedInt octave)
{
	x = ((x % period) + period) % period;
	y = ((y % period) + period) % period;
	UnsignedInt h = (UnsignedInt)x * 374761393u + (UnsignedInt)y * 668265263u + octave * 2246822519u;
	h = (h ^ (h >> 13)) * 1274126177u;
	h ^= h >> 16;
	return (Real)(h & 0xFFFFFFu) / (Real)0xFFFFFF;
}

static Real detailValueNoise(Real x, Real y, Int period, UnsignedInt octave)
{
	const Int xi = (Int)floorf(x);
	const Int yi = (Int)floorf(y);
	const Real xf = x - (Real)xi;
	const Real yf = y - (Real)yi;
	// Smoothstep the interpolant; linear interpolation between lattice points leaves
	// creases along the lattice that read as a grid once this is lit as relief.
	const Real u = xf*xf*(3.0f - 2.0f*xf);
	const Real v = yf*yf*(3.0f - 2.0f*yf);

	const Real a = detailLatticeValue(xi,   yi,   period, octave);
	const Real b = detailLatticeValue(xi+1, yi,   period, octave);
	const Real c = detailLatticeValue(xi,   yi+1, period, octave);
	const Real d = detailLatticeValue(xi+1, yi+1, period, octave);

	const Real top = a + (b - a)*u;
	const Real bot = c + (d - c)*u;
	return top + (bot - top)*v;
}

//=============================================================================
// TerrainDetailTextureClass::TerrainDetailTextureClass
//=============================================================================
TerrainDetailTextureClass::TerrainDetailTextureClass() :
	TextureClass(TERRAIN_DETAIL_DIM, TERRAIN_DETAIL_DIM,
		WW3D_FORMAT_A8R8G8B8, MIP_LEVELS_ALL )
{
}

//=============================================================================
// TerrainDetailTextureClass::update
//=============================================================================
/** Builds the noise field and its gradient.

	Four octaves, each on a lattice whose period divides the texture, so the whole
	field is periodic. The mip chain matters as much as the base level here: filtering
	drives the gradient channels toward 0.5 and the height toward its mean, so the
	relief and the albedo detail both fade out with distance on their own rather than
	shimmering into aliasing. */
//=============================================================================
void TerrainDetailTextureClass::update()
{
	GfxSurface *surface_level;
	WW3DSurfaceDescription surface_desc;
	GfxMappedRect locked_rect;
	surface_level = DX8Wrapper::Get_DX8_Texture_Surface_Level(Peek_D3D_Base_Texture(), 0);
	DX8Wrapper::Describe_DX8_Surface(surface_level, surface_desc);
	if (surface_desc.Format != WW3D_FORMAT_A8R8G8B8) {
		DX8Wrapper::Release_DX8_Surface_Resource(surface_level);
		return;
	}
	DX8Wrapper::Map_DX8_Surface(surface_level, nullptr, GFX_MAP_WRITE, locked_rect);

	const Int dim = TERRAIN_DETAIL_DIM;
	Real *height = MSGNEW("TerrainDetail") Real[dim*dim];

	Int x, y;
	for (y = 0; y < dim; y++) {
		for (x = 0; x < dim; x++) {
			const Real u = (Real)x / (Real)dim;
			const Real v = (Real)y / (Real)dim;
			// Octaves start at 2 lattice cells across the texture, not 8. The first cut
			// started four times finer and read as busy: its top octave put a feature
			// every half a world unit, well under a pixel at playing distance, so it
			// registered as grain over the ground rather than as shape in it. Starting
			// coarse puts the finest feature at roughly a terrain cell instead.
			Real sum = 0.0f, amp = 0.5f, total = 0.0f;
			Int period = 2;
			for (UnsignedInt o = 0; o < 4; o++) {
				sum   += amp * detailValueNoise(u*period, v*period, period, o);
				total += amp;
				amp   *= 0.5f;
				period *= 2;
			}
			height[y*dim + x] = sum / total;
		}
	}

	// Central differences, wrapped, so the gradient is periodic too. The scale brings a
	// typical texel-to-texel step into the byte range without clipping the steep ones
	// flat; anything past the range is clamped rather than wrapped, which would invert
	// the lighting on the few texels that reach it.
	//
	// fBm is scale-invariant in slope -- halving the amplitude while doubling the
	// frequency leaves each octave contributing the same gradient -- so moving the
	// octaves four steps coarser divided the whole field's gradient by four. The scale
	// is multiplied by four to match, which keeps the relief as deep as it was and only
	// stretches it out. Lower TerrainDetailRelief if that is too much.
	const Real GRADIENT_SCALE = 24.0f;
	for (y = 0; y < dim; y++) {
		UnsignedInt *row = (UnsignedInt*)((UnsignedByte*)locked_rect.Data + y*locked_rect.Pitch);
		for (x = 0; x < dim; x++) {
			const Real hL = height[y*dim + ((x - 1 + dim) % dim)];
			const Real hR = height[y*dim + ((x + 1) % dim)];
			const Real hD = height[((y - 1 + dim) % dim)*dim + x];
			const Real hU = height[((y + 1) % dim)*dim + x];

			Real gx = (hR - hL) * GRADIENT_SCALE;
			Real gy = (hU - hD) * GRADIENT_SCALE;
			if (gx < -1.0f) gx = -1.0f; else if (gx > 1.0f) gx = 1.0f;
			if (gy < -1.0f) gy = -1.0f; else if (gy > 1.0f) gy = 1.0f;

			const Int r = (Int)((gx*0.5f + 0.5f) * 255.0f + 0.5f);
			const Int g = (Int)((gy*0.5f + 0.5f) * 255.0f + 0.5f);
			const Int b = (Int)(height[y*dim + x] * 255.0f + 0.5f);
			row[x] = 0xFF000000 | ((UnsignedInt)r << 16) | ((UnsignedInt)g << 8) | (UnsignedInt)b;
		}
	}

	delete [] height;
	DX8Wrapper::Unmap_DX8_Surface(surface_level);
	DX8Wrapper::Release_DX8_Surface_Resource(surface_level);
	DX8Wrapper::Generate_DX8_Mips(Peek_D3D_Base_Texture(), 0);
}

/******************************************************************************
						AlphaTerrainTextureClass
******************************************************************************/
//-----------------------------------------------------------------------------
//         Public Functions
//-----------------------------------------------------------------------------

//=============================================================================
// AlphaTerrainTextureClass::AlphaTerrainTextureClass
//=============================================================================
/** Constructor. Calls parent constructor to creat a throw away 8x8 texture,
then uses the base texture's D3D texture. This way the base tiles pass, drawn
using TerrainTextureClass shares the same texture with the blended edges pass,
saving lots of texture memory, and preventing seams between blended tiles. */
//=============================================================================
AlphaTerrainTextureClass::AlphaTerrainTextureClass( TextureClass *pBaseTex ):
	TextureClass(8, 8,
		WW3D_FORMAT_A8R8G8B8, MIP_LEVELS_1 )
{
	// Attach the base texture's d3d texture.
	GfxTexture * d3d_tex = pBaseTex->Peek_D3D_Texture();
	Set_D3D_Base_Texture(d3d_tex);
}


//=============================================================================
// AlphaTerrainTextureClass::Apply
//=============================================================================
/** Sets the texture as the current D3D texture, and does some custom setup.
This may be applied in either single pass, as the second texture in the pipe,
or multipass.  If stage==0, we are doing multipass and we set up the pipe
for a single texture.  If stage==1, then we are doing a single pass, and we
set up the pipe so that we blend onto the base texture in stage 0.
(standard D3D setup, but beyond the scope of W3D). */
//=============================================================================
void AlphaTerrainTextureClass::Apply(unsigned int stage)
{
	FF_SITE("AlphaTerrainTextureClass::Apply");
	// Do the base apply.
	TextureClass::Apply(stage);

	// Set the bilinear or trilinear filtering.
	if (TheGlobalData && (TheGlobalData->m_bilinearTerrainTex || TheGlobalData->m_trilinearTerrainTex)) {
		DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
	} else {
		DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MINFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MAGFILTER, D3DTEXF_POINT);
	}
	if (TheGlobalData && TheGlobalData->m_trilinearTerrainTex) {
		DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
	} else {
		DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MIPFILTER, D3DTEXF_POINT);
	}
	// Since we are using multiple distinct tiles, the textures doesn't wrap, so clamp it.
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
	// Now setup the texture pipeline.
	if (stage==0) {
		// Modulate the diffuse color with the texture as lighting comes from diffuse.
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_MODULATE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 1 );
		// Blend the result using the alpha. (came from diffuse mod texture)
		DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,true);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
		DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
		// Disable stage 2.
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_DISABLE );
		DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
	}	else if (stage==1) {

		if (TheGlobalData && !TheGlobalData->m_multiPassTerrain)
		{
			///@todo: Remove 8-Stage Nvidia hack after drivers are fixed.
			//This method is a backdoor specific to Nvidia based cards.  It will fail on
			//other hardware.  Allows single pass blend of 2 textures and post modulate diffuse.
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 0);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP, D3DTOP_ADD);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, 1);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_DIFFUSE | D3DTA_COMPLEMENT | D3DTA_ALPHAREPLICATE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_ADD);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG1, D3DTA_TFACTOR | D3DTA_COMPLEMENT);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);

			DX8Wrapper::Set_DX8_Texture(2, nullptr);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_COLOROP, D3DTOP_MODULATE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_TEXCOORDINDEX, 2);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_COLORARG2, D3DTA_TEXTURE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 2, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);

			DX8Wrapper::Set_DX8_Texture(3, nullptr);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_TEXCOORDINDEX, 3);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_COLORARG1, D3DTA_DIFFUSE | 0 | D3DTA_ALPHAREPLICATE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 3, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);

			DX8Wrapper::Set_DX8_Texture(4, nullptr);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_COLOROP, D3DTOP_MODULATE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_TEXCOORDINDEX, 4);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_COLORARG1, D3DTA_CURRENT);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 4, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

			DX8Wrapper::Set_DX8_Texture(5, nullptr);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 5, D3DTSS_COLOROP, D3DTOP_ADD);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 5, D3DTSS_TEXCOORDINDEX, 5);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 5, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 5, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 5, D3DTSS_ALPHAOP,   D3DTOP_ADD);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 5, D3DTSS_ALPHAARG1, D3DTA_TFACTOR | D3DTA_COMPLEMENT);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 5, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);

			DX8Wrapper::Set_DX8_Texture(6, nullptr);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 6, D3DTSS_COLOROP, D3DTOP_MODULATE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 6, D3DTSS_TEXCOORDINDEX, 6);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 6, D3DTSS_COLORARG1, D3DTA_TFACTOR);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 6, D3DTSS_COLORARG2, D3DTA_TFACTOR);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 6, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 6, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 6, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);

			DX8Wrapper::Set_DX8_Texture(7, nullptr);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 7, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 7, D3DTSS_TEXCOORDINDEX, 7);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 7, D3DTSS_COLORARG1, D3DTA_TFACTOR);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 7, D3DTSS_COLORARG2, D3DTA_TFACTOR);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 7, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 7, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
			DX8Wrapper::Set_DX8_Texture_Stage_State( 7, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
		}
		else
		{
  			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1 );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1 );

			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_MODULATE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_CURRENT );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
			DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1 );
		}
	}
}


/******************************************************************************
						LightMapTerrainTextureClass
******************************************************************************/
//-----------------------------------------------------------------------------
//         Public Functions
//-----------------------------------------------------------------------------

//=============================================================================
// LightMapTerrainTextureClass::LightMapTerrainTextureClass
//=============================================================================
/** Constructor. Calls parent constructor to load the .tga texture. */
//=============================================================================
LightMapTerrainTextureClass::LightMapTerrainTextureClass(AsciiString name, MipCountType mipLevelCount) :
TextureClass(name.isEmpty()?"TSNoiseUrb.tga":name.str(),name.isEmpty()?"TSNoiseUrb.tga":name.str(), mipLevelCount )
{
	Get_Filter().Set_Min_Filter(TextureFilterClass::FILTER_TYPE_BEST);
	Get_Filter().Set_Mag_Filter(TextureFilterClass::FILTER_TYPE_BEST);
	Get_Filter().Set_U_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_REPEAT);
	Get_Filter().Set_V_Addr_Mode(TextureFilterClass::TEXTURE_ADDRESS_REPEAT);
}

#define STRETCH_FACTOR ((float)(1/(63.0*MAP_XY_FACTOR/2))) /* covers 63/2 tiles */

//=============================================================================
// LightMapTerrainTextureClass::Apply
//=============================================================================
/** Sets the texture as the current D3D texture, and does some custom setup.
The LightMapTerrainTextureClass may be applied by itself, or with the
CloudMapTerrainTextureClass.  This may be applied in either single pass,
as the second texture in the pipe,
or multipass.  If stage==0, we are doing multipass and we set up the pipe
for a single texture.  If stage==1, then we are doing a single pass, and we
set up the pipe so that we blend onto the cloud map texture in stage 0.
Also, texture is mapped using the x/y coordinates of the map, saving us
yet another set of uv coordinates.
(standard D3D setup, but beyond the scope of W3D). */
//=============================================================================
void LightMapTerrainTextureClass::Apply(unsigned int stage)
{
	FF_SITE("LightMapTerrainTextureClass::Apply");
	TextureClass::Apply(stage);
}









/******************************************************************************
						AlphaEdgeTextureClass
******************************************************************************/
//-----------------------------------------------------------------------------
//         Public Functions
//-----------------------------------------------------------------------------

/**
* AlphaEdgeTextureClass - Generates the alpha edge blending for terrain.
*
*/
AlphaEdgeTextureClass::AlphaEdgeTextureClass( int height, MipCountType mipLevelCount) :
//	TextureClass("EdgingTemplate.tga","EdgingTemplate.tga", mipLevelCount )
	TextureClass(TEXTURE_WIDTH, height, WW3D_FORMAT_A8R8G8B8, mipLevelCount )
{

}

int AlphaEdgeTextureClass::update256(WorldHeightMap *htMap)
{
	return 1;
}

int AlphaEdgeTextureClass::update(WorldHeightMap *htMap)
{
	// D3DTexture is our texture;

	GfxSurface *surface_level;
	WW3DSurfaceDescription surface_desc;
	GfxMappedRect locked_rect;
	surface_level = DX8Wrapper::Get_DX8_Texture_Surface_Level(Peek_D3D_Base_Texture(), 0);
	DX8Wrapper::Map_DX8_Surface(surface_level, nullptr, GFX_MAP_WRITE, locked_rect);
	DX8Wrapper::Describe_DX8_Surface(surface_level, surface_desc);

	Int tilePixelExtent = TILE_PIXEL_EXTENT; // blend tiles are 1/4 tiles.
//	Int tilesPerRow = surface_desc.Width / (tilePixelExtent+8);

//	Int numRows = surface_desc.Height/(tilePixelExtent+8);

	if (surface_desc.Format == WW3D_FORMAT_A8R8G8B8) {
#if 1
#if 1
		Int cellX, cellY;
		for (cellX = 0; (UnsignedInt)cellX < surface_desc.Width; cellX++) {
			for (cellY = 0; cellY < surface_desc.Height; cellY++) {
				UnsignedByte *pBGR = ((UnsignedByte *)locked_rect.Data)+(cellY*surface_desc.Width+cellX)*4;
				pBGR[2] = 255-cellY/2;
				pBGR[0] = cellX/2;
				pBGR[3] = cellX/2;  // alpha.
				pBGR[3] = 128;  // alpha.
			}
		}
#endif
#if 1
		Int tileNdx;
		Int pixelBytes = 4;
		for (tileNdx=0; tileNdx < htMap->m_numEdgeTiles; tileNdx++) {
			TileData *pTile = htMap->getEdgeTile(tileNdx);
			if (!pTile) continue;
			ICoord2D position = pTile->m_tileLocationInTexture;
			if (position.x<=0) continue; // all real edge offsets start at 4.  jba.
			Int i,j;
			Int column = position.x;
			for (j=0; j<tilePixelExtent; j++) {
				Int row = position.y+j;
				UnsignedByte *pBGR = htMap->getEdgeTile(tileNdx)->getRGBDataForWidth(tilePixelExtent);
				pBGR += (tilePixelExtent-1-j)*TILE_BYTES_PER_PIXEL*tilePixelExtent; // invert to match.
				UnsignedByte *pBGRX = ((UnsignedByte*)locked_rect.Data) +
							(row)*surface_desc.Width*pixelBytes;
				pBGRX += column*pixelBytes;

				for (i=0; i<tilePixelExtent; i++) {
					pBGRX[0] = pBGR[0];  //r
					pBGRX[1] = pBGR[1];	//g
					pBGRX[2] = pBGR[2];	//b
					if (pBGR[0]==0 && pBGR[1]==0 && pBGR[2]==0) {
						pBGRX[3] = 0x80;
					} else if (pBGR[0]==0xff && pBGR[1]==0xff && pBGR[2]==0xff) {
						pBGRX[3] = 0x00;
					}	else {
						pBGRX[3] = 0xff;
					}

					pBGRX += pixelBytes;
					pBGR += TILE_BYTES_PER_PIXEL;
				}
			}
		}
#endif
#endif
	}
	DX8Wrapper::Unmap_DX8_Surface(surface_level);
	DX8Wrapper::Release_DX8_Surface_Resource(surface_level);
	DX8Wrapper::Generate_DX8_Mips(Peek_D3D_Base_Texture(), 0);
	return(surface_desc.Height);
}

void AlphaEdgeTextureClass::Apply(unsigned int stage)
{
	FF_SITE("AlphaEdgeTextureClass::Apply");
	// Do the base apply.
	TextureClass::Apply(stage);
}


/******************************************************************************
						CloudMapTerrainTextureClass
******************************************************************************/
//-----------------------------------------------------------------------------
//         Public Functions
//-----------------------------------------------------------------------------

//=============================================================================
// CloudMapTerrainTextureClass::CloudMapTerrainTextureClass
//=============================================================================
/** Constructor. Calls parent constructor to load the .tga texture, and sets
up the "sliding" parameters for the clouds to slide over the terrain. */
//=============================================================================
CloudMapTerrainTextureClass::CloudMapTerrainTextureClass(MipCountType mipLevelCount) :
	TextureClass(TERRAIN_CLOUD_DIM, TERRAIN_CLOUD_DIM,
		WW3D_FORMAT_A8R8G8B8, mipLevelCount )
{
}

//=============================================================================
// CloudMapTerrainTextureClass::update
//=============================================================================
/** Generates the cloud coverage field.

	Coverage rather than brightness: the noise is thresholded so most of the field
	is clear sky and cloud appears as distinct patches. A raw fBm would put every
	part of the map under partial shade all of the time, which is exactly the
	"uniform" look this is meant to replace -- what sells a cloud shadow is the
	contrast with the sunlit ground around it, and there has to be sunlit ground.

	The threshold is soft rather than hard so patch edges stay believable; a hard
	cut gives shadows a cut-out look and aliases badly once minified. */
//=============================================================================
void CloudMapTerrainTextureClass::update()
{
	GfxSurface *surface_level;
	WW3DSurfaceDescription surface_desc;
	GfxMappedRect locked_rect;
	surface_level = DX8Wrapper::Get_DX8_Texture_Surface_Level(Peek_D3D_Base_Texture(), 0);
	DX8Wrapper::Describe_DX8_Surface(surface_level, surface_desc);
	if (surface_desc.Format != WW3D_FORMAT_A8R8G8B8) {
		DX8Wrapper::Release_DX8_Surface_Resource(surface_level);
		return;
	}
	DX8Wrapper::Map_DX8_Surface(surface_level, nullptr, GFX_MAP_WRITE, locked_rect);

	// Where the threshold sits decides how much of the map is under cloud at once.
	//
	// Worth knowing if these are ever retuned: averaging four octaves concentrates the
	// field tightly around 0.5 rather than spreading it over [0,1], so the useful
	// thresholds all sit in a narrow band and intuition about them is badly wrong. A
	// first guess of 0.56 left 4% coverage. These give 16% per layer, and since the
	// shader unions two layers the combined figure is about 30% -- a third of the map
	// under cloud, two thirds in full sun. The measured value is logged below.
	const Real CLOUD_THRESHOLD = 0.49f;
	const Real CLOUD_EDGE      = 0.15f;

	const Int dim = TERRAIN_CLOUD_DIM;
	Real coverageSum = 0.0f;

	Int x, y;
	for (y = 0; y < dim; y++) {
		UnsignedInt *row = (UnsignedInt*)((UnsignedByte*)locked_rect.Data + y*locked_rect.Pitch);
		for (x = 0; x < dim; x++) {
			const Real u = (Real)x / (Real)dim;
			const Real v = (Real)y / (Real)dim;

			// Four octaves from 3 cells across the texture. Projected over ~1800 world
			// units that puts the broadest shapes near 600 units and the finest near 75 --
			// the range a real cloud deck covers, rather than the 20-unit speckle the old
			// 128x128 gave.
			Real sum = 0.0f, amp = 0.5f, total = 0.0f;
			Int period = 3;
			for (UnsignedInt o = 0; o < 4; o++) {
				sum   += amp * detailValueNoise(u*period, v*period, period, o + 64);
				total += amp;
				amp   *= 0.5f;
				period *= 2;
			}
			const Real h = sum / total;

			// Smoothstep the threshold.
			Real t = (h - CLOUD_THRESHOLD) / CLOUD_EDGE;
			if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
			const Real coverage = t*t*(3.0f - 2.0f*t);
			coverageSum += coverage;

			// Stored as brightness (1 = full sun) rather than coverage, because the
			// fixed-function fallback still multiplies the terrain by this texture
			// directly. Storing coverage there would multiply the ground by nearly zero
			// and render it black. The shader recovers coverage as 1 - stored.
			const Int c = (Int)((1.0f - coverage) * 255.0f + 0.5f);
			row[x] = 0xFF000000 | ((UnsignedInt)c << 16) | ((UnsignedInt)c << 8) | (UnsignedInt)c;
		}
	}

	DX8Wrapper::Unmap_DX8_Surface(surface_level);
	DX8Wrapper::Release_DX8_Surface_Resource(surface_level);
	DX8Wrapper::Generate_DX8_Mips(Peek_D3D_Base_Texture(), 0);

	DEBUG_LOG(("CLOUD FIELD: %dx%d, mean coverage %.1f%% (threshold %.2f, edge %.2f)",
		dim, dim, 100.0f * coverageSum / (Real)(dim*dim), CLOUD_THRESHOLD, CLOUD_EDGE));
}

//=============================================================================
// CloudMapTerrainTextureClass::Apply
//=============================================================================
/** Sets the texture as the current D3D texture, and does some custom setup.
The CloudMapTerrainTextureClass may be applied by itself, or with the
LightMapTerrainTexture.  This may be applied in either single pass,
as the first texture in the pipe with LightMapTerrainTextureClass as the
second stage of the pape, or multipass.  We setup for stage 0, assuming that
we are the only texture, as LightMapTerrainTexture will adjust for multitexture
if it is applied to stage 1.
Also, texture is mapped using the x/y coordinates of the map, saving us
yet another set of uv coordinates.
(standard D3D setup, but beyond the scope of W3D). */
//=============================================================================
void CloudMapTerrainTextureClass::Apply(unsigned int stage)
{
	// Nothing but the base apply is left. The camera-space texgen, the scrolling texture
	// matrix and the two-stage combine that used to be here had already been commented out
	// of the build; the programmable terrain shader does the cloud itself, from the drift
	// TerrainShader2Stage::getCloudScroll still owns.
	TextureClass::Apply(stage);
}

//=============================================================================
// CloudMapTerrainTextureClass::restore
//=============================================================================
/** Cleans up any custom settings to the texturing pipeline that may not be
understood by w3d. */
//=============================================================================
void CloudMapTerrainTextureClass::restore()
{
	FF_SITE("CloudMapTerrainTextureClass::restore");
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_MODULATE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );

	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 0 );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_MODULATE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );

	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXCOORDINDEX, 0 );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,false);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);


	if (TheGlobalData && !TheGlobalData->m_multiPassTerrain)
	{
		///@todo: Remove 8-Stage Nvidia hack after drivers are fixed.
		//This method is a backdoor specific to Nvidia based cards.  It will fail on
		//other hardware.  Allows single pass blend of 2 textures and post modulate diffuse.
		Int i;
		for (i=0; i<8; i++) {
			DX8Wrapper::Set_DX8_Texture_Stage_State( i, D3DTSS_COLOROP, D3DTOP_DISABLE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( i, D3DTSS_TEXCOORDINDEX, i);
			DX8Wrapper::Set_DX8_Texture_Stage_State( i, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( i, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( i, D3DTSS_ALPHAOP,   D3DTOP_DISABLE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( i, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
			DX8Wrapper::Set_DX8_Texture_Stage_State( i, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

			DX8Wrapper::Set_DX8_Texture(i, nullptr);
		}
	}
}

/******************************************************************************
						ScorchTextureClass
******************************************************************************/
//-----------------------------------------------------------------------------
//         Public Functions
//-----------------------------------------------------------------------------

//=============================================================================
// ScorchTextureClass::ScorchTextureClass
//=============================================================================
/** Constructor. Calls parent constructor to load the .tga texture. */
//=============================================================================
/// @todo - get "EXScorch01.tga" from not hard coded location.
ScorchTextureClass::ScorchTextureClass(MipCountType mipLevelCount) :
	TextureClass("EXScorch01.tga","EXScorch01.tga", mipLevelCount )
// Hack to disable texture reduction.
//	TextureClass("EXScorch01.tga","EXScorch01.tga", mipLevelCount,WW3D_FORMAT_UNKNOWN,true,false)
{
}

//=============================================================================
// ScorchTextureClass::Apply
//=============================================================================
/** Sets the texture as the current D3D texture, and does some custom setup.
The ScorchTextureClass is applied by iteself, as it's mesh is a subset of the
terrain mesh.
(standard D3D setup, but beyond the scope of W3D). */
//=============================================================================
void ScorchTextureClass::Apply(unsigned int stage)
{
	FF_SITE("ScorchTextureClass::Apply");
	// Do the base apply.
	TextureClass::Apply(stage);
	// Setup bilinear or trilinear filtering as specified in global data.
	if (TheGlobalData && (TheGlobalData->m_bilinearTerrainTex || TheGlobalData->m_trilinearTerrainTex)) {
		DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
		DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
	} else {
		DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MINFILTER, D3DTEXF_POINT);
		DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MAGFILTER, D3DTEXF_POINT);
	}
	if (TheGlobalData && TheGlobalData->m_trilinearTerrainTex) {
		DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
	} else {
		DX8Wrapper::Set_DX8_Texture_Stage_State(stage, D3DTSS_MIPFILTER, D3DTEXF_POINT);
	}

	DX8Wrapper::Set_DX8_Texture_Stage_State(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
	// Now setup the texture pipeline.

	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_COLOROP,   D3DTOP_MODULATE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1 );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 0, D3DTSS_TEXCOORDINDEX, 0 );
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE,true);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);

	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_COLOROP,   D3DTOP_DISABLE );
	DX8Wrapper::Set_DX8_Texture_Stage_State( 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
}



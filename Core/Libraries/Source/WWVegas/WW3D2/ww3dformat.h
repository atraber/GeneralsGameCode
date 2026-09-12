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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : WW3D                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/ww3dformat.h                           $*
 *                                                                                             *
 *              Original Author:: Nathaniel Hoffman                                            *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               *
 *                                                                                             *
 *                     $Modtime:: 06/27/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 12                                                          $*
 *                                                                                             *
 * 06/27/02 KM Z Format support																						*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * Vector4_to_Color - converts a vector4 to the format in format                               *
 * Color_to_Vector4 - converts a color in the format described in format to a Vector4          *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once

#include "WWLib/always.h"
#include "WWLib/wwstring.h"

class Vector4;
class Targa;

/*
** Enum for possible surface formats. This is a small subset of the D3DFORMAT
** enum which lists the formats supported by DX8; we will add new members to
** this list as needed (keeping it in the same order as D3DFORMAT).
** NOTE: Whenever this is changed, formconv.h/.cpp must be modified as well
** (that contains the code for converting between this and D3DFORMAT)..
**
** The format names use the D3DFORMAT conventions:
**      A = Alpha
**      R = Red
**      G = Green
**      B = Blue
**      X = Unused Bits
**      P = Palette
**      L = Luminance
**
**      Further, the order of the pieces are from MSB first; hence
**      WW3D_FORMAT_A8L8 indicates that the high byte of this two byte
**      format is alpha.
*/
enum WW3DFormat {
	WW3D_FORMAT_UNKNOWN=0,
	WW3D_FORMAT_R8G8B8,
	WW3D_FORMAT_A8R8G8B8,
	WW3D_FORMAT_X8R8G8B8,
	WW3D_FORMAT_R5G6B5,
	WW3D_FORMAT_X1R5G5B5,
	WW3D_FORMAT_A1R5G5B5,
	WW3D_FORMAT_A4R4G4B4,
	WW3D_FORMAT_R3G3B2,
	WW3D_FORMAT_A8,
	WW3D_FORMAT_A8R3G3B2,
	WW3D_FORMAT_X4R4G4B4,
	WW3D_FORMAT_A8P8,
	WW3D_FORMAT_P8,
	WW3D_FORMAT_L8,
	WW3D_FORMAT_A8L8,
	WW3D_FORMAT_A4L4,
	WW3D_FORMAT_U8V8,		// Bumpmap
	WW3D_FORMAT_L6V5U5,	// Bumpmap
	WW3D_FORMAT_X8L8V8U8,	// Bumpmap
	WW3D_FORMAT_DXT1,
	WW3D_FORMAT_DXT2,
	WW3D_FORMAT_DXT3,
	WW3D_FORMAT_DXT4,
	WW3D_FORMAT_DXT5,
	// Sixteen bits of floating point per channel -- the format the scene is rendered into
	// when HDR is on, which it is by default. This one breaks the "same order as D3DFORMAT"
	// rule the list above follows, because D3DFMT_A16B16G16R16F is 113 and the conversion
	// the other way is a table capped at 62; it is converted by name instead, the way the
	// DXT codes already are.
	WW3D_FORMAT_A16B16G16R16F,
	// Single-channel 32-bit float -- the shadow map render target. D3DFMT_R32F is 114,
	// also above the table cap, so it too is converted by name.
	WW3D_FORMAT_R32F,
	WW3D_FORMAT_COUNT	// Used only to determine number of surface formats
};

// depth stencil buffer formats
enum WW3DZFormat
{
	WW3D_ZFORMAT_UNKNOWN=0,
	WW3D_ZFORMAT_D16_LOCKABLE, // 16-bit z-buffer bit depth. This is an application-lockable surface format.
	WW3D_ZFORMAT_D32, // 32-bit z-buffer bit depth.
	WW3D_ZFORMAT_D15S1, // 16-bit z-buffer bit depth where 15 bits are reserved for the depth channel and 1 bit is reserved for the stencil channel.
	WW3D_ZFORMAT_D24S8, // 32-bit z-buffer bit depth using 24 bits for the depth channel and 8 bits for the stencil channel.
	WW3D_ZFORMAT_D16, // 16-bit z-buffer bit depth.
	WW3D_ZFORMAT_D24X8, // 32-bit z-buffer bit depth using 24 bits for the depth channel.
	WW3D_ZFORMAT_D24X4S4, // 32-bit z-buffer bit depth using 24 bits for the depth channel and 4 bits for the stencil channel.
	WW3D_ZFORMAT_COUNT
};

/*
** How many samples a surface carries. The value *is* the sample count -- which is how
** D3D9 numbers D3DMULTISAMPLE_TYPE and how D3D11 numbers DXGI_SAMPLE_DESC::Count -- so
** the conversion either way is a range check and a cast, and counts this list does not
** name still survive the round trip.
**
** D3D9's D3DMULTISAMPLE_NONMASKABLE (1) is the one value that is not a sample count. It
** has no counterpart here and nothing in the engine can produce one: the only thing that
** ever chooses a multisample mode is DX8Wrapper::Set_MSAA_Mode, from the four values
** ww3d.cpp offers it, and the back buffer reports back whatever it was created with.
*/
enum WW3DMultiSampleType {
	WW3D_MULTISAMPLE_NONE = 0,
	WW3D_MULTISAMPLE_2X = 2,
	WW3D_MULTISAMPLE_4X = 4,
	WW3D_MULTISAMPLE_8X = 8,
	WW3D_MULTISAMPLE_16X = 16
};

/*
** What a surface is, without saying it in any graphics API's words. This is what the
** engine is allowed to learn about a surface it did not create, and what it may hand
** back to create another one like it.
*/
struct WW3DSurfaceDescription {
	unsigned Width;
	unsigned Height;
	WW3DFormat Format;
	WW3DMultiSampleType MultiSample;
};

// Utility function - not much used otherwise it would use an array.
// NOTE: when adding values to WW3DFormat add here also (if they have alpha).
inline bool Has_Alpha(WW3DFormat format) {
	switch (format) {
		case WW3D_FORMAT_A8R8G8B8:
		case WW3D_FORMAT_A1R5G5B5:
		case WW3D_FORMAT_A4R4G4B4:
		case WW3D_FORMAT_A8:
		case WW3D_FORMAT_A8R3G3B2:
		case WW3D_FORMAT_A8P8:
		case WW3D_FORMAT_A8L8:
		case WW3D_FORMAT_A4L4:
		case WW3D_FORMAT_DXT2:
		case WW3D_FORMAT_DXT3:
		case WW3D_FORMAT_DXT4:
		case WW3D_FORMAT_DXT5:
		case WW3D_FORMAT_A16B16G16R16F:
			return true;
			break;
		default:
			return false;
			break;
	};
}

inline int Alpha_Bits(WW3DFormat format) {
	switch (format) {
		case WW3D_FORMAT_A8R8G8B8:
		case WW3D_FORMAT_A8:
		case WW3D_FORMAT_A8R3G3B2:
		case WW3D_FORMAT_A8P8:
		case WW3D_FORMAT_A8L8:
			return 8;
		case WW3D_FORMAT_A16B16G16R16F:
			return 16;
			break;
		case WW3D_FORMAT_A4R4G4B4:
		case WW3D_FORMAT_A4L4:
		case WW3D_FORMAT_DXT3:
		case WW3D_FORMAT_DXT4:
		case WW3D_FORMAT_DXT5:
			return 4;
			break;
		case WW3D_FORMAT_A1R5G5B5:
		case WW3D_FORMAT_DXT2:
			return 1;
			break;
		default:
			return 0;
			break;
	};
}

// Color convertion routines

// The color will be returned as an unsigned int always
// any unused bits will be garbage
void Vector4_to_Color(unsigned int *outc,const Vector4 &inc,const WW3DFormat format);

// If the format does not support alpha
// the alpha will be garbage
void Color_to_Vector4(Vector4* outc,const unsigned int inc,const WW3DFormat format);

// Define matching WW3D format based from Targa header.
//
// dest_format - WW3DFormat that can be used as a destination (D3D surface) on current hardware
// src_format - WW3DFormat that represents the format the bitmap is stored in the targa file.
// src_bpp - bytes per pixel in the source surface
// targa - reference to the targa object...
void Get_WW3D_Format(WW3DFormat& dest_format,WW3DFormat& src_format,unsigned& src_bpp,const Targa& targa);

// The same as above, but doesn't validate the device - only checks the source format.
void Get_WW3D_Format(WW3DFormat& src_format,unsigned& src_bpp,const Targa& targa);

// Get valid texture format (on current hardware) that is closest to the given format (for instance, 32 bit ARGB8888 would
// return 16 bit ARGB4444 if the device doesn't support 32 bit textures).
// Pass false to the second parameter if you don't wish to consider compressed textures on hardware that supports them.
// The parameter has no effect on hardware that doesn't support compression.
WW3DFormat Get_Valid_Texture_Format(WW3DFormat format,bool is_compression_allowed);

unsigned Get_Bytes_Per_Pixel(WW3DFormat format);

unsigned ARGB_Color_To_WW3D_Color(WW3DFormat format, unsigned argb);

void Get_WW3D_Format_Name(WW3DFormat format, StringClass& name);
void Get_WW3D_MultiSample_Name(WW3DMultiSampleType type, StringClass& name);
void Get_WW3D_ZFormat_Name(WW3DZFormat format, StringClass& name);

unsigned Get_Num_Depth_Bits(WW3DZFormat zformat);
unsigned Get_Num_Stencil_Bits(WW3DZFormat zformat);

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

// Writing an image out, without D3DX.
//
// Save_Surface_To_File is D3DXSaveSurfaceToFileA under D3D9 and there is no D3DX11. This
// is what replaces it, and it exists as its own translation unit for two reasons: to keep
// the encoder out of the D3D11 backend's compile, and because the copy of
// stb_image_write that GameEngineDevice already builds is in the layer *above* this one.
// WW3D2 is not allowed to depend upwards, so it builds its own with STB_IMAGE_WRITE_STATIC
// -- which makes every symbol internal and cannot collide with the other copy at link
// time. The duplicated encoder is a few kilobytes and the alternative is a link
// dependency in the wrong direction.

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

bool Gfx_Write_Png_RGB(const char * path, unsigned width, unsigned height,
	const unsigned char * rgb)
{
	if (path == nullptr || rgb == nullptr || width == 0 || height == 0) return false;
	// Three bytes per pixel, rows top to bottom, exactly as W3DScreenshot has always
	// handed them over.
	return stbi_write_png(path, (int)width, (int)height, 3, rgb, (int)(width * 3)) != 0;
}

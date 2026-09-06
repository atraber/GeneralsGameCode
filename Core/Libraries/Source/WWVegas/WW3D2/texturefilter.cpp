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
 *                     $Archive:: ww3d2/texturefilter.cpp												$*
 *                                                                                             *
 *                  $Org Author:: Kenny Mitchell                                              $*
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               *
 *                                                                                             *
 *                     $Modtime:: 08/05/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 1                                                          $*
 *                                                                                             *
 * 08/05/02 KM Texture filter class abstraction																			*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "texturefilter.h"
#include "dx8wrapper.h"

const char* const TextureFilterClass::TextureFilterModeString[TEXTURE_FILTER_COUNT] = {
	"None",
	"Point",
	"Bilinear",
	"Trilinear",
	"Anisotropic"
};

TextureFilterClass::TextureFilterMode TextureFilterClass::getTextureFilterMode(const char* str) {
	for (int i = 0; i < TextureFilterClass::TEXTURE_FILTER_COUNT; ++i) {
		if (stricmp(str, TextureFilterClass::TextureFilterModeString[i]) == 0) {
			return (TextureFilterClass::TextureFilterMode)i;
		}
	}

	return TextureFilterClass::TEXTURE_FILTER_NONE;
}

// The three tables resolve the engine's four abstract quality levels -- none, fast, best,
// default -- into the sampler vocabulary, once, from what the hardware reports. They used
// to hold D3DTEXF_ constants; they hold SamplerStateClass filters now, so that nothing
// outside DX8Wrapper::Set_Sampler names a D3D filter at all.
SamplerStateClass::FilterType _MinTextureFilters[MAX_TEXTURE_STAGES][TextureFilterClass::FILTER_TYPE_COUNT];
SamplerStateClass::FilterType _MagTextureFilters[MAX_TEXTURE_STAGES][TextureFilterClass::FILTER_TYPE_COUNT];
SamplerStateClass::FilterType _MipMapFilters[MAX_TEXTURE_STAGES][TextureFilterClass::FILTER_TYPE_COUNT];

/*************************************************************************
**                             TextureFilterClass
*************************************************************************/
TextureFilterClass::TextureFilterClass(MipCountType mip_level_count)
:	TextureMinFilter(FILTER_TYPE_DEFAULT),
	TextureMagFilter(FILTER_TYPE_DEFAULT),
	UAddressMode(TEXTURE_ADDRESS_REPEAT),
	VAddressMode(TEXTURE_ADDRESS_REPEAT)
{
	if (mip_level_count!=MIP_LEVELS_1)
	{
		MipMapFilter=FILTER_TYPE_DEFAULT;
	}
	else
	{
		MipMapFilter=FILTER_TYPE_NONE;
	}
}

//**********************************************************************************************
//! Apply filters (legacy)
/*!
*/
void TextureFilterClass::Apply(unsigned int stage)
{
	// One description, not five state words. The mip filter is stated here as well as the
	// other four because a texture's mip filter is a property of the texture -- whether it
	// was built with mip levels -- and this is the texture speaking.
	SamplerStateClass sampler = DX8Wrapper::Get_Sampler(stage);
	sampler.Set_Min_Filter(_MinTextureFilters[stage][TextureMinFilter]);
	sampler.Set_Mag_Filter(_MagTextureFilters[stage][TextureMagFilter]);
	sampler.Set_Mip_Filter(_MipMapFilters[stage][MipMapFilter]);
	sampler.Set_U_Address(Get_U_Addr_Mode() == TEXTURE_ADDRESS_CLAMP
		? SamplerStateClass::ADDRESS_CLAMP : SamplerStateClass::ADDRESS_WRAP);
	sampler.Set_V_Address(Get_V_Addr_Mode() == TEXTURE_ADDRESS_CLAMP
		? SamplerStateClass::ADDRESS_CLAMP : SamplerStateClass::ADDRESS_WRAP);
	DX8Wrapper::Set_Sampler(stage, sampler);
}

//**********************************************************************************************
//! Init filters (legacy)
/*!
*/
void TextureFilterClass::_Init_Filters(TextureFilterMode texture_filter, AnisotropicFilterMode anisotropy_level)
{
	const DX8Caps& dx8caps=*DX8Wrapper::Get_Current_Caps();

	// TheSuperHackers @info Init zero stage filter defaults, point filtering is the lowest type for non mip filtering
	_MinTextureFilters[0][FILTER_TYPE_NONE]=SamplerStateClass::FILTER_POINT;
	_MagTextureFilters[0][FILTER_TYPE_NONE]=SamplerStateClass::FILTER_POINT;
	_MipMapFilters[0][FILTER_TYPE_NONE]=SamplerStateClass::FILTER_NONE;

	// Bilinear
	_MinTextureFilters[0][FILTER_TYPE_FAST]=SamplerStateClass::FILTER_LINEAR;
	_MagTextureFilters[0][FILTER_TYPE_FAST]=SamplerStateClass::FILTER_LINEAR;
	_MipMapFilters[0][FILTER_TYPE_FAST]=SamplerStateClass::FILTER_POINT;

	// Anisotropic - MipMap interlayer filtering only goes up to linear
	_MinTextureFilters[0][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_ANISOTROPIC;
	_MagTextureFilters[0][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_ANISOTROPIC;
	_MipMapFilters[0][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_LINEAR;

	// TheSuperHackers @feature Mauller 08/03/2026 Add full support for all texture filtering modes;
	// None, Point, Bilinear, Trilinear, Anisotropic.
	BOOL FilterSupported = false;
	switch (texture_filter) {

	default:
		// TheSuperHackers @info if we have an invalid filter_type, set the filtering to none
		DEBUG_CRASH(("Invalid filter type passed into TextureFilterClass::_Init_Filters()"));
		FALLTHROUGH;

	case TEXTURE_FILTER_NONE:

		_MinTextureFilters[0][FILTER_TYPE_FAST]=SamplerStateClass::FILTER_POINT;
		_MagTextureFilters[0][FILTER_TYPE_FAST]=SamplerStateClass::FILTER_POINT;
		_MipMapFilters[0][FILTER_TYPE_FAST]=SamplerStateClass::FILTER_NONE;

		_MinTextureFilters[0][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_POINT;
		_MagTextureFilters[0][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_POINT;
		_MipMapFilters[0][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_NONE;
		break;

	case TEXTURE_FILTER_POINT:

		_MinTextureFilters[0][FILTER_TYPE_FAST]=SamplerStateClass::FILTER_POINT;
		_MagTextureFilters[0][FILTER_TYPE_FAST]=SamplerStateClass::FILTER_POINT;
		_MipMapFilters[0][FILTER_TYPE_FAST]=SamplerStateClass::FILTER_POINT;

		_MinTextureFilters[0][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_POINT;
		_MagTextureFilters[0][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_POINT;
		_MipMapFilters[0][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_POINT;
		break;

	case TEXTURE_FILTER_BILINEAR:

		FilterSupported = dx8caps.Support_Linear_Filter();

		if (FilterSupported) {
			_MinTextureFilters[0][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_LINEAR;
			_MagTextureFilters[0][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_LINEAR;
		}
		else {
			_MinTextureFilters[0][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_POINT;
			_MagTextureFilters[0][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_POINT;
		}

		_MipMapFilters[0][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_POINT;
		break;

	case TEXTURE_FILTER_TRILINEAR:

		FilterSupported = dx8caps.Support_Linear_Filter();

		if (FilterSupported) {
			_MinTextureFilters[0][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_LINEAR;
			_MagTextureFilters[0][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_LINEAR;
		}
		else {
			_MinTextureFilters[0][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_POINT;
			_MagTextureFilters[0][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_POINT;
		}

		if (dx8caps.Support_Mip_Linear_Filter()) {
			_MipMapFilters[0][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_LINEAR;
		}
		else {
			// TheSuperHackers @info if only linear mipmap filtering is unsupported,
			// Trilinear filtering becomes Bilinear filtering by default
			_MipMapFilters[0][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_POINT;
		}
		break;

	case TEXTURE_FILTER_ANISOTROPIC:

		// Unconditional, and that is a bug fix rather than a simplification.
		//
		// This used to ask DX8Caps::Support_Anisotropic_Filter, and under D3D9 the answer
		// on this adapter was NO: GfxDeviceD3D9::Fill_Device_Caps required both
		// D3DPTFILTERCAPS_MAGFANISOTROPIC and MINFANISOTROPIC, and an RTX 4060 reports
		// anisotropic minification only. So the else branch below ran and choosing
		// "Anisotropic" in the options gave you FILTER_POINT -- point sampling, which is
		// worse than the Bilinear setting it was chosen over. Nobody noticed because the
		// harness seed says Bilinear and this fork had never once been taken in a measured
		// session; Phase 9 took it and measured 108008 differing pixels under D3D9 against
		// 85251 under D3D11, which is what made the two branches visible.
		//
		// Anisotropic filtering is not optional in Direct3D 10 or later -- it is required
		// of every feature level this backend will create a device at -- so there is no
		// longer a device that can answer no.
		_MinTextureFilters[0][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_ANISOTROPIC;
		_MagTextureFilters[0][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_ANISOTROPIC;
		FilterSupported = true;

		// Set the Anisotropic filtering level for all stages
		_Set_Max_Anisotropy(anisotropy_level);

		if (dx8caps.Support_Mip_Linear_Filter()) {
			_MipMapFilters[0][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_LINEAR;
		}
		else {
			_MipMapFilters[0][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_POINT;
		}
		break;

	}


	// For stages above zero, set best filter to the same as the stage zero
	int i=1;
	for (;i<MAX_TEXTURE_STAGES;++i) {
		_MinTextureFilters[i][FILTER_TYPE_NONE]=_MinTextureFilters[0][FILTER_TYPE_NONE];
		_MagTextureFilters[i][FILTER_TYPE_NONE]=_MagTextureFilters[0][FILTER_TYPE_NONE];
		_MipMapFilters[i][FILTER_TYPE_NONE]=_MipMapFilters[0][FILTER_TYPE_NONE];

		_MinTextureFilters[i][FILTER_TYPE_FAST]=_MinTextureFilters[0][FILTER_TYPE_FAST];
		_MagTextureFilters[i][FILTER_TYPE_FAST]=_MagTextureFilters[0][FILTER_TYPE_FAST];
		_MipMapFilters[i][FILTER_TYPE_FAST]=_MipMapFilters[0][FILTER_TYPE_FAST];

		// When Anisotropic filtering is used, all stages above zero use trilinear filtering
		if (_MagTextureFilters[0][FILTER_TYPE_BEST]==SamplerStateClass::FILTER_ANISOTROPIC) {
			_MagTextureFilters[i][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_LINEAR;
		}
		else {
			_MagTextureFilters[i][FILTER_TYPE_BEST]=_MagTextureFilters[0][FILTER_TYPE_BEST];
		}

		if (_MinTextureFilters[0][FILTER_TYPE_BEST]==SamplerStateClass::FILTER_ANISOTROPIC) {
			_MinTextureFilters[i][FILTER_TYPE_BEST]=SamplerStateClass::FILTER_LINEAR;
		}
		else {
			_MinTextureFilters[i][FILTER_TYPE_BEST]=_MinTextureFilters[0][FILTER_TYPE_BEST];
		}
		_MipMapFilters[i][FILTER_TYPE_BEST]=_MipMapFilters[0][FILTER_TYPE_BEST];

	}

	// Set default to best. The level of best filter mode is controlled by the input parameter.
	for (i=0;i<MAX_TEXTURE_STAGES;++i) {
		_MinTextureFilters[i][FILTER_TYPE_DEFAULT]=_MinTextureFilters[i][FILTER_TYPE_BEST];
		_MagTextureFilters[i][FILTER_TYPE_DEFAULT]=_MagTextureFilters[i][FILTER_TYPE_BEST];
		_MipMapFilters[i][FILTER_TYPE_DEFAULT]=_MipMapFilters[i][FILTER_TYPE_BEST];
	}

}


//**********************************************************************************************
//! Set mip mapping filter (legacy)
/*!
*/
void TextureFilterClass::Set_Mip_Mapping(FilterType mipmap)
{
//	if (mipmap != FILTER_TYPE_NONE && Get_Mip_Level_Count() <= 1 && Is_Initialized())
//	{
//		WWASSERT_PRINT(0, "Trying to enable MipMapping on texture w/o Mip levels!");
//		return;
//	}
	MipMapFilter=mipmap;
}

//**********************************************************************************************
//! Set anisotropic filter level
/*!
*/
void TextureFilterClass::_Set_Max_Anisotropy(AnisotropicFilterMode mode)
{
	for (int stage = 0; stage < MAX_TEXTURE_STAGES; ++stage) {
		SamplerStateClass sampler = DX8Wrapper::Get_Sampler(stage);
		sampler.Set_Anisotropy((unsigned)mode);
		DX8Wrapper::Set_Sampler(stage, sampler);
	}
}

//**********************************************************************************************
//! Set default min filter (legacy)
/*!
*/
void TextureFilterClass::_Set_Default_Min_Filter(FilterType filter)
{
	for (int i=0;i<MAX_TEXTURE_STAGES;++i)
	{
		_MinTextureFilters[i][FILTER_TYPE_DEFAULT]=_MinTextureFilters[i][filter];
	}
}


//**********************************************************************************************
//! Set default mag filter (legacy)
/*!
*/
void TextureFilterClass::_Set_Default_Mag_Filter(FilterType filter)
{
	for (int i=0;i<MAX_TEXTURE_STAGES;++i)
	{
		_MagTextureFilters[i][FILTER_TYPE_DEFAULT]=_MagTextureFilters[i][filter];
	}
}

//**********************************************************************************************
//! Set default mip filter (legacy)
/*!
*/
void TextureFilterClass::_Set_Default_Mip_Filter(FilterType filter)
{
	for (int i=0;i<MAX_TEXTURE_STAGES;++i)
	{
		_MipMapFilters[i][FILTER_TYPE_DEFAULT]=_MipMapFilters[i][filter];
	}
}

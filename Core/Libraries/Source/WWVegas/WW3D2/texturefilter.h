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
 *                     $Archive:: ww3d2/texturefilter.h												$*
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

#pragma once

#ifndef DX8_WRAPPER_H
//#include "dx8wrapper.h"
#endif

enum MipCountType
{
	MIP_LEVELS_ALL=0,		// generate all mipmap levels down to 1x1 size
	MIP_LEVELS_1,			// no mipmapping at all (just one mip level)
	MIP_LEVELS_2,
	MIP_LEVELS_3,
	MIP_LEVELS_4,
	MIP_LEVELS_5,
	MIP_LEVELS_6,
	MIP_LEVELS_7,
	MIP_LEVELS_8,
	MIP_LEVELS_10,
	MIP_LEVELS_11,
	MIP_LEVELS_12,
	MIP_LEVELS_MAX			// This isn't to be used (use MIP_LEVELS_ALL instead), it is just an enum for creating static tables etc.
};


// A sampler description: how the texture bound to a slot is filtered and addressed.
//
// The same idea ShaderClass is for blend, depth and cull state, and for the same reason.
// D3D9 fuses three separate things into one texture-stage index -- which texture, how it
// is sampled, and which shader register reads it -- and it writes the middle one a field
// at a time, so "linear, clamped" is four indexed state words rather than a value. Every
// other API separates the three: D3D11 builds an immutable sampler object from a complete
// description and binds it at s#, with the texture at t#, and does not require the two
// numbers to match.
//
// A description that has an identity is what makes such an object cacheable, so the seven
// fields the engine actually varies are packed into one word and that word is the key.
// Two descriptions that compare equal describe the same sampler; a backend may hold one
// object per distinct key and look it up by Get_Key().
//
// Three D3D sampler states are deliberately absent: the mip LOD bias, the maximum mip
// level and the border colour. The engine writes each of them exactly once, to its API's
// own default, and never varies it; a backend supplies its own default for all three. If
// one ever becomes a decision it becomes a field here and the key gets wider -- which is
// the point of the key being a private detail rather than a documented layout.
//
// The enumerations below are the engine's, not D3D's, and are numbered independently of
// it: DX8Wrapper::Set_Sampler is where they meet an API's constants.
enum SamplerShiftConstants
{
	SAMPLER_SHIFT_MINFILTER		= 0,	// 2 bits
	SAMPLER_SHIFT_MAGFILTER		= 2,	// 2 bits
	SAMPLER_SHIFT_MIPFILTER		= 4,	// 2 bits
	SAMPLER_SHIFT_ADDRESSU		= 6,	// 3 bits
	SAMPLER_SHIFT_ADDRESSV		= 9,	// 3 bits
	SAMPLER_SHIFT_ADDRESSW		= 12,	// 3 bits
	SAMPLER_SHIFT_ANISOTROPY	= 15,	// 5 bits, 1..16
	SAMPLER_SHIFT_COMPARE		= 20	// 4 bits
};

class SamplerStateClass
{
public:

	enum FilterType
	{
		FILTER_NONE = 0,		// mip only: no mip filtering
		FILTER_POINT,
		FILTER_LINEAR,
		FILTER_ANISOTROPIC
	};

	enum AddressType
	{
		ADDRESS_WRAP = 0,
		ADDRESS_MIRROR,
		ADDRESS_CLAMP,
		ADDRESS_BORDER,
		ADDRESS_MIRROR_ONCE
	};

	// A comparison sampler: the read returns the result of comparing a reference value
	// against each texel, filtered, instead of the texel. This is hardware PCF -- with a
	// linear filter the four texels around the lookup are each compared and the results
	// bilinearly weighted. COMPARE_NONE is an ordinary sampler.
	enum CompareType
	{
		COMPARE_NONE = 0,
		COMPARE_LESS_EQUAL,		// passes (1) when reference <= texel
		COMPARE_GREATER_EQUAL
	};

	SamplerStateClass() : Bits(0) {}

	// The value nothing a caller builds can equal, so that every field of the next
	// description bound is treated as a change. This is what the wrapper resets a slot to
	// when it stops being able to say what the device holds; see Set_Sampler.
	static SamplerStateClass Unknown() { SamplerStateClass s; s.Bits = 0xFFFFFFFFu; return s; }

	FilterType	Get_Min_Filter() const	{ return (FilterType)Get(SAMPLER_SHIFT_MINFILTER, 3); }
	FilterType	Get_Mag_Filter() const	{ return (FilterType)Get(SAMPLER_SHIFT_MAGFILTER, 3); }
	FilterType	Get_Mip_Filter() const	{ return (FilterType)Get(SAMPLER_SHIFT_MIPFILTER, 3); }
	AddressType	Get_U_Address() const	{ return (AddressType)Get(SAMPLER_SHIFT_ADDRESSU, 7); }
	AddressType	Get_V_Address() const	{ return (AddressType)Get(SAMPLER_SHIFT_ADDRESSV, 7); }
	AddressType	Get_W_Address() const	{ return (AddressType)Get(SAMPLER_SHIFT_ADDRESSW, 7); }
	unsigned	Get_Anisotropy() const	{ return Get(SAMPLER_SHIFT_ANISOTROPY, 31); }
	CompareType	Get_Compare() const		{ return (CompareType)Get(SAMPLER_SHIFT_COMPARE, 15); }

	void Set_Min_Filter(FilterType f)	{ Set(SAMPLER_SHIFT_MINFILTER, 3, (unsigned)f); }
	void Set_Mag_Filter(FilterType f)	{ Set(SAMPLER_SHIFT_MAGFILTER, 3, (unsigned)f); }
	void Set_Mip_Filter(FilterType f)	{ Set(SAMPLER_SHIFT_MIPFILTER, 3, (unsigned)f); }
	void Set_U_Address(AddressType a)	{ Set(SAMPLER_SHIFT_ADDRESSU, 7, (unsigned)a); }
	void Set_V_Address(AddressType a)	{ Set(SAMPLER_SHIFT_ADDRESSV, 7, (unsigned)a); }
	void Set_W_Address(AddressType a)	{ Set(SAMPLER_SHIFT_ADDRESSW, 7, (unsigned)a); }
	void Set_Anisotropy(unsigned n)		{ Set(SAMPLER_SHIFT_ANISOTROPY, 31, n); }
	void Set_Compare(CompareType c)		{ Set(SAMPLER_SHIFT_COMPARE, 15, (unsigned)c); }

	// The two shapes almost every caller wants, stated once. Both set the minification,
	// magnification and addressing and leave the mip filter alone, because whether a
	// texture has mip levels is a property of the texture and not of the pass.
	void Set_Filter(FilterType minification, FilterType magnification)
	{
		Set_Min_Filter(minification);
		Set_Mag_Filter(magnification);
	}
	void Set_Address(AddressType u, AddressType v)
	{
		Set_U_Address(u);
		Set_V_Address(v);
	}

	// Chainable edits, for the ordinary "the sampler this slot already has, with these
	// fields changed" -- which is what almost every caller means, because filtering is
	// inherited state and a pass that says nothing about the mip filter means it.
	//
	// They return by value rather than mutating, so a whole binding is one statement and
	// needs no local; that matters because several of these sites sit directly under a
	// switch label, where a declaration would not be legal.
	SamplerStateClass With_Filter(FilterType minification, FilterType magnification) const
	{ SamplerStateClass s(*this); s.Set_Filter(minification, magnification); return s; }
	SamplerStateClass With_Min_Filter(FilterType f) const
	{ SamplerStateClass s(*this); s.Set_Min_Filter(f); return s; }
	SamplerStateClass With_Mag_Filter(FilterType f) const
	{ SamplerStateClass s(*this); s.Set_Mag_Filter(f); return s; }
	SamplerStateClass With_Mip_Filter(FilterType f) const
	{ SamplerStateClass s(*this); s.Set_Mip_Filter(f); return s; }
	SamplerStateClass With_Address(AddressType u, AddressType v) const
	{ SamplerStateClass s(*this); s.Set_Address(u, v); return s; }
	SamplerStateClass With_U_Address(AddressType u) const
	{ SamplerStateClass s(*this); s.Set_U_Address(u); return s; }
	SamplerStateClass With_V_Address(AddressType v) const
	{ SamplerStateClass s(*this); s.Set_V_Address(v); return s; }
	SamplerStateClass With_W_Address(AddressType w) const
	{ SamplerStateClass s(*this); s.Set_W_Address(w); return s; }
	SamplerStateClass With_Anisotropy(unsigned n) const
	{ SamplerStateClass s(*this); s.Set_Anisotropy(n); return s; }
	SamplerStateClass With_Compare(CompareType c) const
	{ SamplerStateClass s(*this); s.Set_Compare(c); return s; }

	unsigned Get_Key() const { return Bits; }

	bool operator==(const SamplerStateClass & other) const { return Bits == other.Bits; }
	bool operator!=(const SamplerStateClass & other) const { return Bits != other.Bits; }

private:
	unsigned Get(unsigned shift, unsigned mask) const { return (Bits >> shift) & mask; }
	void Set(unsigned shift, unsigned mask, unsigned value)
	{
		Bits = (Bits & ~(mask << shift)) | ((value & mask) << shift);
	}

	unsigned Bits;
};

// NOTE: Since "texture wrapping" (NOT TEXTURE WRAP MODE - THIS IS
// SOMETHING ELSE) is a global state that affects all texture stages,
// and this class only affects its own stage, we will not worry about
// it for now. Later (probably when we implement world-oriented
// environment maps) we will consider where to put it.

// This is legacy and should be phased out into wwshade shader states
// keeping as an abstracted class for now to support this transition later
class TextureFilterClass
{
public:

	enum FilterType
	{
		FILTER_TYPE_NONE,
		FILTER_TYPE_FAST,
		FILTER_TYPE_BEST,
		FILTER_TYPE_DEFAULT,
		FILTER_TYPE_COUNT
	};

	enum TextureFilterMode
	{
		TEXTURE_FILTER_NONE,
		TEXTURE_FILTER_POINT,
		TEXTURE_FILTER_BILINEAR,
		TEXTURE_FILTER_TRILINEAR,
		TEXTURE_FILTER_ANISOTROPIC,
		TEXTURE_FILTER_COUNT
	};

	static const char* const TextureFilterModeString[TEXTURE_FILTER_COUNT];

	static TextureFilterMode getTextureFilterMode(const char* str);

	enum AnisotropicFilterMode
	{
		TEXTURE_FILTER_ANISOTROPIC_2X = 2,
		TEXTURE_FILTER_ANISOTROPIC_4X = 4,
		TEXTURE_FILTER_ANISOTROPIC_8X = 8,
		TEXTURE_FILTER_ANISOTROPIC_16X = 16
	};

	enum TxtAddrMode
	{
		TEXTURE_ADDRESS_REPEAT=0,
		TEXTURE_ADDRESS_CLAMP
	};

	TextureFilterClass(MipCountType mip_level_count=MIP_LEVELS_1);

	void Apply(unsigned int stage);

	// Filter and MIPmap settings:
	FilterType Get_Min_Filter() const { return TextureMinFilter; }
	FilterType Get_Mag_Filter() const { return TextureMagFilter; }
	FilterType Get_Mip_Mapping() const { return MipMapFilter; }
	void Set_Min_Filter(FilterType filter) { TextureMinFilter=filter; }
	void Set_Mag_Filter(FilterType filter) { TextureMagFilter=filter; }
	void Set_Mip_Mapping(FilterType mipmap);

	// Texture address mode
	TxtAddrMode Get_U_Addr_Mode() const { return UAddressMode; }
	TxtAddrMode Get_V_Addr_Mode() const { return VAddressMode; }
	void Set_U_Addr_Mode(TxtAddrMode mode) { UAddressMode=mode; }
	void Set_V_Addr_Mode(TxtAddrMode mode) { VAddressMode=mode; }

	// These need to be called after device has been created
	static void _Init_Filters(TextureFilterMode texture_filter, AnisotropicFilterMode anisotropy_level);
	static void _Set_Max_Anisotropy(AnisotropicFilterMode mode);

	static void _Set_Default_Min_Filter(FilterType filter);
	static void _Set_Default_Mag_Filter(FilterType filter);
	static void _Set_Default_Mip_Filter(FilterType filter);

private:
	// State not contained in the Direct3D texture object:
	FilterType TextureMinFilter;
	FilterType TextureMagFilter;
	FilterType MipMapFilter;
	TxtAddrMode UAddressMode;
	TxtAddrMode VAddressMode;
};

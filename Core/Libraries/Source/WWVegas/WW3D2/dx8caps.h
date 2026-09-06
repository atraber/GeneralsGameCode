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
 *                 Project Name : DX8 Caps                                                     *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/dx8caps.h                              $*
 *                                                                                             *
 *              Original Author:: Hector Yee                                                   *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               *
 *                                                                                             *
 *                     $Modtime:: 06/27/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 24                                                          $*
 *                                                                                             *
 * 06/27/02 KM Z Format support																						*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once

#include "WWLib/always.h"
#include "ww3dformat.h"

/*
** The D3D9-shaped half of this: the raw capability struct and the interface used to
** ask about formats. Defined in dx8caps.cpp so that nothing including this header sees
** a D3D type or needs d3d9.h in scope. Eleven files include it and none of them wants
** either -- they want the questions above, which are now all answered without one.
*/
struct DX8CapsPrivate;

class DX8Caps
{
public:
	enum VendorIdType {
		VENDOR_UNKNOWN,
		VENDOR_NVIDIA,
		VENDOR_ATI,
		VENDOR_INTEL,
		VENDOR_S3,
		VENDOR_POWERVR,
		VENDOR_MATROX,
		VENDOR_3DFX,
		VENDOR_3DLABS,
		VENDOR_CIRRUSLOGIC,
		VENDOR_RENDITION,
		VENDOR_VMWARE,

		VENDOR_COUNT
	};

	// Eight DeviceType<vendor> enums were here, naming every part those vendors shipped
	// before 2003 so that Vendor_Specific_Hacks could work around each one. None of
	// them can create a Direct3D 11 device; deleted in Phase 10 with the tables and
	// the driver-version blacklist. The vendor enum stays -- one vendor hack is still
	// live on this machine, see Vendor_Specific_Hacks.

	// Takes nothing from D3D. The adapter, the device and the adapter identifier all
	// come from DX8Wrapper, which is where they already lived; passing them in only
	// put three D3D types in the signature of a header eleven files include. The
	// second constructor, which took a D3DCAPS8 already filled in, had no callers.
	DX8Caps(WW3DFormat display_format);
	/// Device enumeration, before an adapter has been chosen and while there is no
	/// device to probe: reads the named adapter's caps instead.
	DX8Caps(WW3DFormat display_format, unsigned adapter_index);
	~DX8Caps();
	static void Shutdown();
	// Nine questions were asked here and are not any more, because the answer stopped
	// being a property of the adapter and became a property of the API. TnL, cube maps
	// and multipass are unconditional; bump environment maps, ModulateAlphaAddColor,
	// DOT3 and point sprites are fixed-function features that no backend after D3D9
	// has; the fog allowance and the display-format whitelist existed only to be
	// switched off by hardware hacks that are gone.
	bool Support_DXTC() const { return SupportDXTC; }
	bool Support_NPatches() const { return SupportNPatches; }
	bool Support_ZBias() const { return SupportZBias; }
	// Whether the device has a gamma ramp of its own. False under D3D11, which has none
	// outside exclusive full-screen -- and is why DX8Wrapper::Get_Display_Gamma hands the
	// curve to the frame instead.
	bool Support_Gamma() const { return supportGamma; }

	int Get_Max_Textures_Per_Pass() const { return MaxTexturesPerPass; }

	// -------------------------------------------------------------------------
	//
	// Vertex shader support. Version number is split in major and minor, such that 1.0 would
	// have 1 as major and 0 as minor version number.
	//
	// -------------------------------------------------------------------------

	int Get_Vertex_Shader_Major_Version() const { return 0xff&(VertexShaderVersion>>8); }
	int Get_Vertex_Shader_Minor_Version() const { return 0xff&(VertexShaderVersion); }
	int Get_Pixel_Shader_Major_Version() const { return 0xff&(PixelShaderVersion>>8); }
	int Get_Pixel_Shader_Minor_Version() const { return 0xff&(PixelShaderVersion); }
	int Get_Max_Simultaneous_Textures()	const { return MaxSimultaneousTextures;}

	bool Support_Texture_Format(WW3DFormat format) const { return SupportTextureFormat[format]; }
	bool Support_Render_To_Texture_Format(WW3DFormat format) const { return SupportRenderToTextureFormat[format]; }
	bool Support_Depth_Stencil_Format(WW3DZFormat format) const { return SupportDepthStencilFormat[format]; }

	// -------------------------------------------------------------------------
	//
	// What the adapter can do, asked one question at a time.
	//
	// This used to hand the raw D3DCAPS8 back and let every caller pick a field out of
	// it, which put a D3D struct in the public API of a header eleven files include.
	// Every reader outside this file wanted one of these seven answers.
	//
	// -------------------------------------------------------------------------

	unsigned Get_Max_Texture_Width() const { return MaxTextureWidth; }
	unsigned Get_Max_Texture_Height() const { return MaxTextureHeight; }
	unsigned Get_Max_Volume_Extent() const { return MaxVolumeExtent; }
	/// Zero means unlimited, which is what D3D means by it too.
	unsigned Get_Max_Texture_Aspect_Ratio() const { return MaxTextureAspectRatio; }

	bool Support_Linear_Filter() const { return SupportLinearFilter; }
	bool Support_Mip_Linear_Filter() const { return SupportMipLinearFilter; }
	bool Support_Color_Write_Enable() const { return SupportColorWriteEnable; }

	/// The fixed-function texture-combine ops the adapter has, as D3DTEXOPCAPS_ bits.
	/// Still a D3D bitmask because ShaderClass::Apply -- the fixed-function combine path,
	/// which is a phase of its own -- tests two dozen of them one at a time. A number,
	/// not a struct, so no header outside this one has to know the shape of anything.
	unsigned Get_Texture_Op_Caps() const { return TextureOpCaps; }

	const StringClass& Get_Log() const { return CapsLog; }
	const StringClass& Get_Compact_Log() const { return CompactLog; }


private:
	static VendorIdType Define_Vendor(unsigned vendor_id);

	// Every one of these was handed the same member the caller already had.
	void Compute_Caps(WW3DFormat display_format);
	void Init_Caps();
	// Print the neutral GfxDeviceCaps struct, field by field, in words that do not depend
	// on which backend filled it -- so two logs diff without translation. Every field where
	// the backends disagree is a fork in the engine, and after the D3D9 backend goes there
	// is nothing left to compare against.
	void Log_Caps_Table(const char* source);
	void Check_Texture_Format_Support(WW3DFormat display_format);
	void Check_Render_To_Texture_Support(WW3DFormat display_format);
	void Check_Depth_Stencil_Support(WW3DFormat display_format);
	void Check_Texture_Compression_Support();
	void Check_Shader_Support();
	void Check_Maximum_Texture_Support();
	void Vendor_Specific_Hacks();

	// Copied out of D3DCAPS8 at Compute_Caps, so nothing outside this file needs the
	// struct to read them.
	unsigned MaxTextureWidth;
	unsigned MaxTextureHeight;
	unsigned MaxVolumeExtent;
	unsigned MaxTextureAspectRatio;
	unsigned TextureOpCaps;
	bool SupportLinearFilter;
	bool SupportMipLinearFilter;
	bool SupportColorWriteEnable;

	bool SupportDXTC;
	bool supportGamma;
	bool SupportNPatches;
	bool SupportTextureFormat[WW3D_FORMAT_COUNT];
	bool SupportRenderToTextureFormat[WW3D_FORMAT_COUNT];
	bool SupportDepthStencilFormat[WW3D_ZFORMAT_COUNT];
	bool SupportZBias;
	int MaxTexturesPerPass;
	int VertexShaderVersion;
	int PixelShaderVersion;
	int MaxSimultaneousTextures;
	unsigned DriverBuildVersion;
	VendorIdType VendorId;
	StringClass DriverDLL;
	DX8CapsPrivate * Private;
	StringClass CapsLog;
	StringClass CompactLog;
};

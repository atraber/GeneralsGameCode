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
 *                 Project Name : dx8 caps                                                     *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/dx8caps.cpp                            $*
 *                                                                                             *
 *              Original Author:: Hector Yee                                                   *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               *
 *                                                                                             *
 *                     $Modtime:: 06/27/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 31                                                          $*
 *                                                                                             *
 * 06/27/02 KM Z Format support																						*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "WWLib/always.h"
#include "dx8caps.h"
#include "dx8wrapper.h"
#include "formconv.h"

// What the adapter said, out of the header so that nothing including it has to know the
// shape of either. Both are neutral now -- the D3D9 structs they were filled from stay
// inside the backend -- so this is here for the header's sake and not for the API's.
struct DX8CapsPrivate
{
	GfxDeviceCaps	Caps;
	GfxAdapterInfo	Info;
	GfxAdapterClass*	Adapter;
	unsigned		AdapterIndex;
};
#pragma warning (disable : 4201)		// nonstandard extension - nameless struct
#include <windows.h>
#include <mmsystem.h>

static StringClass CapsWorkString;

#define DXLOG(n) CapsWorkString.Format n ; CapsLog+=CapsWorkString;
#define COMPACTLOG(n) CapsWorkString.Format n ; CompactLog+=CapsWorkString;

static const char* const VendorNames[]={
	"Unknown",
	"NVidia",
	"ATI",
	"Intel",
	"S3",
	"PowerVR",
	"Matrox",
	"3Dfx",
	"3DLabs",
	"CirrusLogic",
	"Rendition",
	"VMware",
};
static_assert(ARRAY_SIZE(VendorNames) == DX8Caps::VENDOR_COUNT, "Incorrect array size");

DX8Caps::VendorIdType DX8Caps::Define_Vendor(unsigned vendor_id)
{
	switch (vendor_id) {
	case 0x3d3d:
	case 0x104c: return VENDOR_3DLABS;
	case 0x12D2: // STB - NVIDIA's Riva128
	case 0x14AF: // Guillemot's NVIDIA based cards
	case 0x10de: return VENDOR_NVIDIA;
	case 0x1002: return VENDOR_ATI;
	case 0x8086: return VENDOR_INTEL;
	case 0x5333: return VENDOR_S3;
	case 0x104A: return VENDOR_POWERVR;
	case 0x102B: return VENDOR_MATROX;
	case 0x1142: // Alliance based reference cards
	case 0x109D: // Macronix based reference cards
	case 0x121A: return VENDOR_3DFX;
	case 0x15AD: return VENDOR_VMWARE;
	default:
		return VENDOR_UNKNOWN;
	}
}

// The device-id tables and the eight Get_<vendor>_Device functions were here: about four
// hundred lines naming every Rage, Voodoo, Savage, Kyro, Permedia, GeForce 2 and Parhelia
// that shipped before 2003, so that Vendor_Specific_Hacks could work around each one's
// driver bugs. Not one of those parts can create a Direct3D 11 device, so every branch
// that read a device id was unreachable rather than merely unlikely. The VENDOR table
// stays, because one vendor hack does still fire -- see Vendor_Specific_Hacks.

DX8Caps::DX8Caps(WW3DFormat display_format)
	:
	Private(new DX8CapsPrivate),
	MaxTexturesPerPass(0)
{
	memset(Private, 0, sizeof(*Private));
	Private->Adapter = DX8Wrapper::Get_Adapter();
	Private->AdapterIndex = DX8Wrapper::Get_Adapter_Index();
	Init_Caps();
	Compute_Caps(display_format);
	Log_Caps_Table("device");
}

// Device enumeration: each adapter is asked about in turn, before one of them has been
// chosen and while there is no device to probe. So the caps come off the adapter rather
// than off a device, there is no software/hardware vertex-processing pass, and hardware
// transform and lighting is read straight out of what the adapter claims.
DX8Caps::DX8Caps(WW3DFormat display_format, unsigned adapter_index)
	:
	Private(new DX8CapsPrivate),
	MaxTexturesPerPass(0)
{
	memset(Private, 0, sizeof(*Private));
	Private->Adapter = DX8Wrapper::Get_Adapter();
	Private->AdapterIndex = adapter_index;
	if (Private->Adapter != nullptr) {
		Private->Adapter->Query_Capabilities(adapter_index, Private->Caps);
		Private->Adapter->Get_Adapter_Info(adapter_index, Private->Info);
	}
	Compute_Caps(display_format);
	Log_Caps_Table("adapter");
}

// ----------------------------------------------------------------------------
//
// The capability table, printed identically by whichever backend filled it.
//
// Neither backend logged its caps until Phase 9, so nobody had ever seen the two answers
// side by side -- and every field where they differ is a branch the engine takes one way
// under D3D9 and the other under D3D11, silently, with nothing failing to compile. This
// prints the whole neutral struct in one block, one field per line, in a fixed order and
// fixed words, so that a diff of two logs is the whole measurement.
//
// ----------------------------------------------------------------------------
void DX8Caps::Log_Caps_Table(const char* source)
{
	const GfxDeviceCaps& c = Private->Caps;
	WWDEBUG_SAY(("GFX CAPS (%s, adapter %u):", source, c.AdapterOrdinal));
#define CAPBOOL(field) WWDEBUG_SAY(("  %-26s %s", #field, c.field ? "yes" : "no"))
#define CAPUINT(field) WWDEBUG_SAY(("  %-26s %u", #field, c.field))
	CAPBOOL(HardwareTransformAndLighting);
	CAPBOOL(NPatches);
	CAPBOOL(FullScreenGamma);
	CAPBOOL(CubeMaps);
	CAPBOOL(ColorWriteEnable);
	CAPBOOL(BumpEnvmap);
	CAPBOOL(BumpEnvmapLuminance);
	CAPBOOL(ModulateAlphaAddColor);
	CAPBOOL(DotProduct3);
	CAPBOOL(PointSprites);
	CAPBOOL(LinearFilter);
	CAPBOOL(MipLinearFilter);
	CAPBOOL(AnisotropicFilter);
	CAPUINT(MaxTextureWidth);
	CAPUINT(MaxTextureHeight);
	CAPUINT(MaxVolumeExtent);
	CAPUINT(MaxTextureAspectRatio);
	CAPUINT(MaxSimultaneousTextures);
	// Packed major<<8 | minor, as the API packs them.
	WWDEBUG_SAY(("  %-26s %u.%u", "VertexShaderVersion",
		c.VertexShaderVersion >> 8, c.VertexShaderVersion & 0xff));
	WWDEBUG_SAY(("  %-26s %u.%u", "PixelShaderVersion",
		c.PixelShaderVersion >> 8, c.PixelShaderVersion & 0xff));
	WWDEBUG_SAY(("  %-26s 0x%08x", "FixedFunctionCombineOps", c.FixedFunctionCombineOps));
#undef CAPBOOL
#undef CAPUINT
}

DX8Caps::~DX8Caps()
{
	delete Private;
}


//Don't really need this but I added this function to free static variables so
//they don't show up in our memory manager as a leak. -MW 7-22-03
void DX8Caps::Shutdown()
{
	CapsWorkString.Release_Resources();
}

// ----------------------------------------------------------------------------
//
// Init the caps structure
//
// ----------------------------------------------------------------------------

void DX8Caps::Init_Caps()
{
	// The backend asks the device, in whichever vertex-processing mode gives the answer the
	// engine wants -- which under D3D9 means asking twice. That dance is the API's, so it
	// lives there.
	if (DX8Wrapper::Gfx == nullptr)
		return;
	DX8Wrapper::Gfx->Query_Capabilities(Private->Caps);
	if (Private->Adapter != nullptr)
		Private->Adapter->Get_Adapter_Info(Private->AdapterIndex, Private->Info);
}

// ----------------------------------------------------------------------------
//
// Compute the caps bits
//
// ----------------------------------------------------------------------------
void DX8Caps::Compute_Caps(WW3DFormat display_format)
{
	const GfxAdapterInfo& adapter_id = Private->Info;
//	Init_Caps(D3DDevice);


	// Everything anybody outside this file used to reach into D3DCAPS8 for, answered
	// once here. The struct itself does not leave dx8caps.cpp any more.
	MaxTextureWidth       = Private->Caps.MaxTextureWidth;
	MaxTextureHeight      = Private->Caps.MaxTextureHeight;
	MaxVolumeExtent       = Private->Caps.MaxVolumeExtent;
	MaxTextureAspectRatio = Private->Caps.MaxTextureAspectRatio;
	TextureOpCaps         = Private->Caps.FixedFunctionCombineOps;
	SupportLinearFilter      = Private->Caps.LinearFilter;
	SupportMipLinearFilter   = Private->Caps.MipLinearFilter;
	SupportColorWriteEnable  = Private->Caps.ColorWriteEnable;

	CapsLog="";
	CompactLog="";
	DXLOG(("Video Card: %s\r\n",adapter_id.Description));
	DXLOG(("Driver: %s\r\n",adapter_id.Driver));

	DriverDLL=adapter_id.Driver;
	const unsigned Product = adapter_id.DriverProduct;
	const unsigned Version = adapter_id.DriverVersionNumber;
	const unsigned SubVersion = adapter_id.DriverSubVersion;
	DriverBuildVersion = adapter_id.DriverBuildVersion;

	DXLOG(("Product=%d, Version=%d, SubVersion=%d, Build=%d\r\n",Product, Version, SubVersion, DriverBuildVersion));

	VendorId=Define_Vendor(adapter_id.VendorId);
	// Make a guess - if driver doesn't intruduce itself and the name starts with 3, what could it possibly be?
	if (VendorId==VENDOR_UNKNOWN) {
		if (DriverDLL[0]=='3') VendorId=VENDOR_3DFX;
	}
	COMPACTLOG(("%s\t",VendorNames[VendorId]));
	DXLOG(("Video Card Chip Vendor: %s\r\n",VendorNames[VendorId]));
	COMPACTLOG(("\t%d\t",DriverBuildVersion));

	DXLOG(("\r\n"));

	DXLOG(("Vendor id: 0x%x\r\n",adapter_id.VendorId));
	DXLOG(("Device id: 0x%x\r\n",adapter_id.DeviceId));
	DXLOG(("SubSys id: 0x%x\r\n",adapter_id.SubSystemId));
	DXLOG(("Revision: %d\r\n",adapter_id.Revision));

	DXLOG(("Device identifier: %s\r\n",adapter_id.DeviceIdentifier));


	SupportNPatches = Private->Caps.NPatches;
	SupportZBias = true;
	supportGamma = Private->Caps.FullScreenGamma;

	DXLOG(("NPatch support: %s\r\n",SupportNPatches ? "Yes" : "No"));
	DXLOG(("ZBias support: %s\r\n",SupportZBias ? "Yes" : "No"));
	DXLOG(("Gamma support: %s\r\n",supportGamma ? "Yes" : "No"));

	Check_Texture_Format_Support(display_format);
	Check_Render_To_Texture_Support(display_format);
	Check_Depth_Stencil_Support(display_format);
	Check_Texture_Compression_Support();
	Check_Shader_Support();
	Check_Maximum_Texture_Support();

	MaxTexturesPerPass=(int)Private->Caps.MaxSimultaneousTextures;

	DXLOG(("Max textures per pass: %d\r\n",MaxTexturesPerPass));

	Vendor_Specific_Hacks();
	CapsWorkString="";
}

// ----------------------------------------------------------------------------
//
// Check compressed texture support
//
// ----------------------------------------------------------------------------

void DX8Caps::Check_Texture_Compression_Support()
{
	SupportDXTC=SupportTextureFormat[WW3D_FORMAT_DXT1]|
		SupportTextureFormat[WW3D_FORMAT_DXT2]|
		SupportTextureFormat[WW3D_FORMAT_DXT3]|
		SupportTextureFormat[WW3D_FORMAT_DXT4]|
		SupportTextureFormat[WW3D_FORMAT_DXT5];
	DXLOG(("Texture compression support: %s\r\n",SupportDXTC ? "Yes" : "No"));
}

void DX8Caps::Check_Texture_Format_Support(WW3DFormat display_format)
{
	if (display_format==WW3D_FORMAT_UNKNOWN) {
		for (unsigned i=0;i<WW3D_FORMAT_COUNT;++i) {
			SupportTextureFormat[i]=false;
		}
		return;
	}
	for (unsigned i=0;i<WW3D_FORMAT_COUNT;++i) {
		if (i==WW3D_FORMAT_UNKNOWN) {
			SupportTextureFormat[i]=false;
		}
		else {
			WW3DFormat format=(WW3DFormat)i;
			SupportTextureFormat[i]=Private->Adapter != nullptr &&
				Private->Adapter->Supports_Texture_Format(
					Private->Caps.AdapterOrdinal, display_format, format, GFX_FORMAT_TEXTURE);
			if (SupportTextureFormat[i]) {
				StringClass name(0,true);
				Get_WW3D_Format_Name(format,name);
				DXLOG(("Supports texture format: %s\r\n",name.str()));
			}
		}
	}
}

void DX8Caps::Check_Render_To_Texture_Support(WW3DFormat display_format)
{
	if (display_format==WW3D_FORMAT_UNKNOWN) {
		for (unsigned i=0;i<WW3D_FORMAT_COUNT;++i) {
			SupportRenderToTextureFormat[i]=false;
		}
		return;
	}
	for (unsigned i=0;i<WW3D_FORMAT_COUNT;++i) {
		if (i==WW3D_FORMAT_UNKNOWN) {
			SupportRenderToTextureFormat[i]=false;
		}
		else {
			WW3DFormat format=(WW3DFormat)i;
			SupportRenderToTextureFormat[i]=Private->Adapter != nullptr &&
				Private->Adapter->Supports_Texture_Format(
					Private->Caps.AdapterOrdinal, display_format, format, GFX_FORMAT_RENDER_TARGET);
			if (SupportRenderToTextureFormat[i]) {
				StringClass name(0,true);
				Get_WW3D_Format_Name(format,name);
				DXLOG(("Supports render-to-texture format: %s\r\n",name.str()));
			}
		}
	}
}

//**********************************************************************************************
//! Check Depth Stencil Format Support
/*! KJM
*/
void DX8Caps::Check_Depth_Stencil_Support(WW3DFormat display_format)
{
	if (display_format==WW3D_FORMAT_UNKNOWN)
	{
		for (unsigned i=0;i<WW3D_ZFORMAT_COUNT;++i)
		{
			SupportDepthStencilFormat[i]=false;
		}
		return;
	}


	for (unsigned i=0;i<WW3D_ZFORMAT_COUNT;++i)
	{
		if (i==WW3D_ZFORMAT_UNKNOWN)
		{
			SupportDepthStencilFormat[i]=false;
		}
		else
		{
			WW3DZFormat format=(WW3DZFormat)i;
			SupportDepthStencilFormat[i]=Private->Adapter != nullptr &&
				Private->Adapter->Supports_Depth_Texture_Format(
					Private->Caps.AdapterOrdinal, display_format, format);

			if (SupportDepthStencilFormat[i])
			{
				StringClass name(0,true);
				Get_WW3D_ZFormat_Name(format,name);
				DXLOG(("Supports depth stencil format: %s\r\n",name.str()));
			}
		}
	}
}

void DX8Caps::Check_Maximum_Texture_Support()
{
	MaxSimultaneousTextures=(int)Private->Caps.MaxSimultaneousTextures;
}

void DX8Caps::Check_Shader_Support()
{
	VertexShaderVersion=(int)Private->Caps.VertexShaderVersion;
	PixelShaderVersion=(int)Private->Caps.PixelShaderVersion;
	DXLOG(("Vertex shader version: %d.%d, pixel shader version: %d.%d\r\n",
		(VertexShaderVersion>>8)&0xff,VertexShaderVersion&0xff,
		(PixelShaderVersion>>8)&0xff,PixelShaderVersion&0xff));
}

// ----------------------------------------------------------------------------
//
// Implement some vendor-specific hacks to fix certain driver bugs that can't be
// avoided otherwise.
//
// ----------------------------------------------------------------------------

void DX8Caps::Vendor_Specific_Hacks()
{
	// One hack survives, and it is live on the machine this port is measured on.
	//
	// Everything else here worked around a specific pre-2003 part -- Rage Pro
	// multitexturing, Voodoo3 render-to-texture, Matrox G400 combiner arguments, Savage
	// 2000, Kyro fog, a GeForce2 MX resolution cap -- selected by device id out of tables
	// that Phase 10 deleted, because none of those parts can create a Direct3D 11 device.
	// The VMWare DOT3 workaround went too: DOT3 is a fixed-function combiner op and there
	// is no combiner, so nothing reads that capability any more.
	//
	// The NVidia block is different. This machine is an RTX 4060 and the branch runs on
	// every startup: it turns DXT1 off, which is a live decision about which format the
	// texture loader asks for, not a workaround for hardware nobody has. Whether a 2002
	// claim about DXT1 still holds on a 2024 part is a real question and it is NOT this
	// phase's -- changing it would move pixels, and the point of this phase is that
	// nothing moves. Left exactly as it was, and written down so it is asked deliberately
	// next time rather than deleted by accident this time.
	if (VendorId==VENDOR_NVIDIA)
	{
		if (SupportNPatches) {
			DXLOG(("NVidia Driver reported N-Patch support, disabling.\r\n"));
		}
		if (SupportTextureFormat[WW3D_FORMAT_DXT1]) {
			DXLOG(("Disabling DXT1 support on NVidia hardware.\r\n"));
		}

		SupportNPatches = false;	// Driver incorrectly report N-Patch support
		SupportTextureFormat[WW3D_FORMAT_DXT1] = false;			// DXT1 is broken on NVidia hardware
		SupportDXTC=
			SupportTextureFormat[WW3D_FORMAT_DXT1]|
			SupportTextureFormat[WW3D_FORMAT_DXT2]|
			SupportTextureFormat[WW3D_FORMAT_DXT3]|
			SupportTextureFormat[WW3D_FORMAT_DXT4]|
			SupportTextureFormat[WW3D_FORMAT_DXT5];
	}
}

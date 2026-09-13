/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "Lib/BaseType.h"
#include "W3DDevice/GameClient/W3DVolumetricFog.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "Common/GlobalData.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/camera.h"
#include "WW3D2/vertmaterial.h"
#include "WW3D2/shader.h"
#include "WW3D2/ww3d.h"
#include "WW3D2/ww3dformat.h"
#include "WWMath/matrix4.h"

// Static configuration defaults
Bool  VolumetricFogClass::s_active             = TRUE;
float VolumetricFogClass::s_density            = 0.0006f;
float VolumetricFogClass::s_heightFalloff      = 0.005f;
float VolumetricFogClass::s_groundHeight       = 0.0f;
float VolumetricFogClass::s_anisotropy         = 0.55f;
float VolumetricFogClass::s_sunShaftIntensity  = 0.35f;
float VolumetricFogClass::s_ambientIntensity   = 0.15f;
float VolumetricFogClass::s_lightBoost         = 25.0f;

static bool Env_Flag(const char * name, bool defaultVal)
{
	const char * val = ::getenv(name);
	if (!val) return defaultVal;
	return ::atoi(val) != 0;
}

static float Env_Float(const char * name, float defaultVal)
{
	const char * val = ::getenv(name);
	if (!val) return defaultVal;
	return (float)::atof(val);
}

VolumetricFogClass::VolumetricFogClass()
	: m_volumeScatter(nullptr),
	  m_volumeIntegrated(nullptr),
	  m_scatterCS(0),
	  m_integrateCS(0),
	  m_compositePS(0),
	  m_shadersLoaded(FALSE),
	  m_loadFailed(FALSE)
{
	// options.ini decides, the environment variable overrides it. Before this the only
	// control was the variable, so "on" was not a choice anybody had made.
	const bool optionDefault = (TheGlobalData != nullptr)
		? (TheGlobalData->m_useVolumetricFog != FALSE) : true;
	s_active = Env_Flag("W3D_VOLUMETRIC_FOG", optionDefault);
	s_density = Env_Float("W3D_FOG_DENSITY", s_density);
	s_heightFalloff = Env_Float("W3D_FOG_HEIGHT_FALLOFF", s_heightFalloff);
	s_anisotropy = Env_Float("W3D_FOG_ANISOTROPY", s_anisotropy);
	s_sunShaftIntensity = Env_Float("W3D_FOG_SUN_SHAFTS", s_sunShaftIntensity);
	s_ambientIntensity = Env_Float("W3D_FOG_AMBIENT", s_ambientIntensity);
	s_lightBoost = Env_Float("W3D_FOG_LIGHT_BOOST", s_lightBoost);
}

VolumetricFogClass::~VolumetricFogClass()
{
	Release_Resources();
}

void VolumetricFogClass::Init()
{
	Ensure_Textures();
	Ensure_Shaders();
}

void VolumetricFogClass::Shutdown()
{
	Release_Resources();
}

Bool VolumetricFogClass::Is_Active()
{
	return s_active;
}

void VolumetricFogClass::Set_Active(Bool active)
{
	s_active = active;
}

Bool VolumetricFogClass::Would_Contribute(unsigned localLightCount)
{
	if (!s_active)
		return FALSE;

	// No medium, nothing to scatter in, whatever the lights do.
	if (s_density <= 0.0f)
		return FALSE;

	// The three sources of in-scattering, all off: no ambient haze, no sun shafts, and no
	// punctual light anywhere in the froxel volume. The pass would integrate to zero and
	// composite a transparent quad, having first made the whole scene draw itself again to
	// produce the depth it reads. At the shipped defaults ambient and sun shafts are both
	// nonzero, so this fires only when somebody has tuned them to zero -- which is exactly
	// the case where the cost is pure waste and nothing said so.
	if (s_ambientIntensity <= 0.0f && s_sunShaftIntensity <= 0.0f && localLightCount == 0)
		return FALSE;

	return TRUE;
}

void VolumetricFogClass::Ensure_Textures()
{
	if (m_volumeScatter != nullptr && m_volumeIntegrated != nullptr)
		return;

	GfxDeviceClass * gfx = DX8Wrapper::Gfx;
	if (gfx == nullptr)
		return;

	if (m_volumeScatter == nullptr)
	{
		m_volumeScatter = gfx->Create_Volume_Texture(
			FROXEL_GRID_X, FROXEL_GRID_Y, FROXEL_GRID_Z, 1,
			WW3D_FORMAT_A16B16G16R16F, GFX_USAGE_UAV);
	}

	if (m_volumeIntegrated == nullptr)
	{
		m_volumeIntegrated = gfx->Create_Volume_Texture(
			FROXEL_GRID_X, FROXEL_GRID_Y, FROXEL_GRID_Z, 1,
			WW3D_FORMAT_A16B16G16R16F, GFX_USAGE_UAV);
	}

	if (m_volumeScatter == nullptr || m_volumeIntegrated == nullptr)
	{
		WWDEBUG_SAY(("VolumetricFogClass: Failed to create 3D volume textures (120x68x32 RGBA16F).\n"));
	}
}

void VolumetricFogClass::Ensure_Shaders()
{
	if (m_shadersLoaded || m_loadFailed)
		return;

	if (!DX8Wrapper::Has_Device())
		return;

	DWORD scatter = 0, integrate = 0, composite = 0;

	if (FAILED(W3DShaderManager::LoadAndCreateD3DShader(
			"shaders\\volumetric_scatter_cs.sm5", nullptr, 0,
			W3DShaderManager::SHADER_STAGE_COMPUTE, &scatter)) || scatter == 0)
	{
		WWDEBUG_SAY(("VolumetricFogClass: volumetric_scatter_cs.sm5 failed to load.\n"));
		m_loadFailed = TRUE;
		return;
	}

	if (FAILED(W3DShaderManager::LoadAndCreateD3DShader(
			"shaders\\volumetric_integrate_cs.sm5", nullptr, 0,
			W3DShaderManager::SHADER_STAGE_COMPUTE, &integrate)) || integrate == 0)
	{
		WWDEBUG_SAY(("VolumetricFogClass: volumetric_integrate_cs.sm5 failed to load.\n"));
		DX8Wrapper::Release_Compute_Shader(scatter);
		m_loadFailed = TRUE;
		return;
	}

	if (FAILED(W3DShaderManager::LoadAndCreateD3DShader(
			"shaders\\volumetric_composite_ps.sm5", nullptr, 0,
			W3DShaderManager::SHADER_STAGE_PIXEL, &composite)) || composite == 0)
	{
		WWDEBUG_SAY(("VolumetricFogClass: volumetric_composite_ps.sm5 failed to load.\n"));
		DX8Wrapper::Release_Compute_Shader(scatter);
		DX8Wrapper::Release_Compute_Shader(integrate);
		m_loadFailed = TRUE;
		return;
	}

	m_scatterCS = scatter;
	m_integrateCS = integrate;
	m_compositePS = composite;
	m_shadersLoaded = TRUE;
}

void VolumetricFogClass::Release_Resources()
{
	if (m_scatterCS != 0)
	{
		DX8Wrapper::Release_Compute_Shader(m_scatterCS);
		m_scatterCS = 0;
	}
	if (m_integrateCS != 0)
	{
		DX8Wrapper::Release_Compute_Shader(m_integrateCS);
		m_integrateCS = 0;
	}
	if (m_compositePS != 0)
	{
		DX8Wrapper::Release_Pixel_Shader(m_compositePS);
		m_compositePS = 0;
	}
	m_shadersLoaded = FALSE;
	m_loadFailed = FALSE;

	if (m_volumeScatter != nullptr)
	{
		DX8Wrapper::Release_DX8_Resource(m_volumeScatter);
		m_volumeScatter = nullptr;
	}
	if (m_volumeIntegrated != nullptr)
	{
		DX8Wrapper::Release_DX8_Resource(m_volumeIntegrated);
		m_volumeIntegrated = nullptr;
	}
}

void VolumetricFogClass::Update_Constants(CameraClass & camera)
{
	if (!DX8Wrapper::Has_Device())
		return;

	const Vector3 camPos = camera.Get_Position();
	const Vector3 camRight = camera.Get_Right_Dir();
	const Vector3 camUp = camera.Get_Up_Dir();

	Matrix4x4 d3dProj;
	camera.Get_D3D_Projection_Matrix(&d3dProj);
	const float proj33 = d3dProj[2][2];
	const float proj43 = d3dProj[2][3];

	Vector3 sunDir(0.0f, 0.0f, 1.0f);
	Vector3 sunColor(1.0f, 1.0f, 1.0f);
	if (TheGlobalData != nullptr)
	{
		sunDir.Set(-TheGlobalData->m_terrainLightPos[0].x,
		           -TheGlobalData->m_terrainLightPos[0].y,
		           -TheGlobalData->m_terrainLightPos[0].z);
		sunDir.Normalize();

		sunColor.Set(TheGlobalData->m_terrainDiffuse[0].red,
		             TheGlobalData->m_terrainDiffuse[0].green,
		             TheGlobalData->m_terrainDiffuse[0].blue);
	}

	Vector4 fogConstants[12];

	// Slot 9: FogParams0
	fogConstants[0].Set(s_density, s_heightFalloff, s_groundHeight, s_anisotropy);

	// Slot 10: FogParams1
	fogConstants[1].Set((float)FROXEL_GRID_X, (float)FROXEL_GRID_Y, (float)FROXEL_GRID_Z, s_lightBoost);

	// Slot 11: FogSunDir
	fogConstants[2].Set(sunDir.X, sunDir.Y, sunDir.Z, s_sunShaftIntensity);

	// Slot 12: FogSunColor
	const float activeFlag = (s_active && !m_loadFailed) ? 1.0f : 0.0f;
	fogConstants[3].Set(sunColor.X, sunColor.Y, sunColor.Z, activeFlag);

	// Slot 13: FogCameraPos
	fogConstants[4].Set(camPos.X, camPos.Y, camPos.Z, s_ambientIntensity);

	// Slot 14: FogCameraRight
	fogConstants[5].Set(camRight.X, camRight.Y, camRight.Z, proj33);

	// Slot 15: FogCameraUp
	fogConstants[6].Set(camUp.X, camUp.Y, camUp.Z, proj43);

	// Slots 16..19: FogSunVP0..3
	fogConstants[7].Set(DX8Wrapper::m_sunVP[0],  DX8Wrapper::m_sunVP[1],  DX8Wrapper::m_sunVP[2],  DX8Wrapper::m_sunVP[3]);
	fogConstants[8].Set(DX8Wrapper::m_sunVP[4],  DX8Wrapper::m_sunVP[5],  DX8Wrapper::m_sunVP[6],  DX8Wrapper::m_sunVP[7]);
	fogConstants[9].Set(DX8Wrapper::m_sunVP[8],  DX8Wrapper::m_sunVP[9],  DX8Wrapper::m_sunVP[10], DX8Wrapper::m_sunVP[11]);
	fogConstants[10].Set(DX8Wrapper::m_sunVP[12], DX8Wrapper::m_sunVP[13], DX8Wrapper::m_sunVP[14], DX8Wrapper::m_sunVP[15]);

	// Slot 20: FogShadowParams
	fogConstants[11].Set(DX8Wrapper::m_shadowParams[0], DX8Wrapper::m_shadowParams[1],
		DX8Wrapper::m_shadowParams[2], (float)DX8Wrapper::SHADOW_MAP_SIZE);

	DX8Wrapper::Set_Frame_Constants_At(9, fogConstants, 12);
}

void VolumetricFogClass::Render(CameraClass & camera, GfxBuffer * lightBuffer,
	GfxBuffer * clusterGrid, GfxBuffer * lightIndexList, GfxTexture * depthTexture)
{
	if (!s_active || m_loadFailed)
		return;

	Ensure_Textures();
	Ensure_Shaders();

	if (!m_shadersLoaded || m_volumeScatter == nullptr || m_volumeIntegrated == nullptr || depthTexture == nullptr)
		return;

	GfxDeviceClass * gfx = DX8Wrapper::Gfx;
	if (gfx == nullptr)
		return;

	// ---------------------------------------------------------------------------
	// Pass 1: Froxel In-Scattering Compute Dispatch
	// ---------------------------------------------------------------------------
	gfx->Set_Compute_Shader((GfxShaderHandle)m_scatterCS);
	gfx->Set_Compute_RW_Texture(0, m_volumeScatter);
	gfx->Set_Compute_Buffer(0, lightBuffer);
	gfx->Set_Compute_Buffer(1, clusterGrid);
	gfx->Set_Compute_Buffer(2, lightIndexList);

	// The directional shadow map the depth pass writes -- the same texture every lit
	// shader samples through shadow.hlsli. This used to read DX8Wrapper::Get_Shadow_Map(0),
	// which is the legacy ZTextureClass array nothing in the engine ever writes: it was
	// always null, so t3 was never bound and the sun-shaft term read zeros -- which the
	// shader below cannot tell apart from "every froxel is in shadow".
	GfxTexture * shadowTex = DX8Wrapper::Has_Shadow_Map() ? DX8Wrapper::m_pShadowMap : nullptr;
	if (shadowTex != nullptr)
		gfx->Set_Compute_Texture(3, shadowTex);

	const unsigned groupsX = (FROXEL_GRID_X + 7) / 8;
	const unsigned groupsY = (FROXEL_GRID_Y + 7) / 8;
	gfx->Dispatch(groupsX, groupsY, FROXEL_GRID_Z);

	gfx->Set_Compute_RW_Texture(0, nullptr);
	gfx->Set_Compute_Buffer(0, nullptr);
	gfx->Set_Compute_Buffer(1, nullptr);
	gfx->Set_Compute_Buffer(2, nullptr);
	if (shadowTex != nullptr)
		gfx->Set_Compute_Texture(3, nullptr);
	gfx->Set_Compute_Shader(0);

	// ---------------------------------------------------------------------------
	// Pass 2: Froxel Raymarch Integration Compute Dispatch
	// ---------------------------------------------------------------------------
	gfx->Set_Compute_Shader((GfxShaderHandle)m_integrateCS);
	gfx->Set_Compute_Texture(0, m_volumeScatter);
	gfx->Set_Compute_RW_Texture(0, m_volumeIntegrated);

	gfx->Dispatch(groupsX, groupsY, 1);

	gfx->Set_Compute_RW_Texture(0, nullptr);
	gfx->Set_Compute_Texture(0, nullptr);
	gfx->Set_Compute_Shader(0);

	// ---------------------------------------------------------------------------
	// Pass 3: Fullscreen Quad Composite onto Scene Render Target
	// ---------------------------------------------------------------------------
	D3DVIEWPORT9 vp;
	if (!DX8Wrapper::Get_DX8_Viewport(vp))
		return;

	unsigned savedAlphaBlend = 0;
	unsigned savedSrcBlend = 0;
	unsigned savedDestBlend = 0;
	unsigned savedBlendOp = 0;
	unsigned savedZEnable = 0;
	unsigned savedZWrite = 0;
	unsigned savedColorWrite = 0;
	unsigned savedCull = 0;

	DX8Wrapper::Get_DX8_Render_State(D3DRS_ALPHABLENDENABLE, savedAlphaBlend);
	DX8Wrapper::Get_DX8_Render_State(D3DRS_SRCBLEND, savedSrcBlend);
	DX8Wrapper::Get_DX8_Render_State(D3DRS_DESTBLEND, savedDestBlend);
	DX8Wrapper::Get_DX8_Render_State(D3DRS_BLENDOP, savedBlendOp);
	DX8Wrapper::Get_DX8_Render_State(D3DRS_ZENABLE, savedZEnable);
	DX8Wrapper::Get_DX8_Render_State(D3DRS_ZWRITEENABLE, savedZWrite);
	DX8Wrapper::Get_DX8_Render_State(D3DRS_COLORWRITEENABLE, savedColorWrite);
	DX8Wrapper::Get_DX8_Render_State(D3DRS_CULLMODE, savedCull);

	// Blend mode: SrcBlend = ONE, DestBlend = SRC_ALPHA (inscatter + scene * transmittance)
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, TRUE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, D3DBLEND_ONE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND, D3DBLEND_SRCALPHA);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZENABLE, FALSE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE, FALSE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_CULLMODE, D3DCULL_NONE);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE,
		D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);

	DX8Wrapper::Set_DX8_Texture(0, m_volumeIntegrated);
	DX8Wrapper::Set_Sampler(0, DX8Wrapper::Get_Sampler(0)
		.With_Filter(SamplerStateClass::FILTER_LINEAR, SamplerStateClass::FILTER_LINEAR)
		.With_Mip_Filter(SamplerStateClass::FILTER_LINEAR)
		.With_Address(SamplerStateClass::ADDRESS_CLAMP, SamplerStateClass::ADDRESS_CLAMP)
		.With_W_Address(SamplerStateClass::ADDRESS_CLAMP));

	DX8Wrapper::Set_DX8_Texture(1, depthTexture);
	DX8Wrapper::Set_Sampler(1, DX8Wrapper::Get_Sampler(1)
		.With_Filter(SamplerStateClass::FILTER_POINT, SamplerStateClass::FILTER_POINT)
		.With_Mip_Filter(SamplerStateClass::FILTER_NONE)
		.With_Address(SamplerStateClass::ADDRESS_CLAMP, SamplerStateClass::ADDRESS_CLAMP));

	DX8Wrapper::Set_Pixel_Shader(m_compositePS);

	W3DShaderManager::drawScreenQuad(0.0f, 0.0f, (float)vp.Width, (float)vp.Height,
		0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
		0xffffffff, W3DShaderManager::SCREEN_QUAD_PIXEL_CALLER, "volumetricFog");

	DX8Wrapper::Set_Pixel_Shader(0);
	DX8Wrapper::Set_DX8_Texture(0, nullptr);
	DX8Wrapper::Set_DX8_Texture(1, nullptr);

	DX8Wrapper::Set_DX8_Render_State(D3DRS_ALPHABLENDENABLE, savedAlphaBlend);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_SRCBLEND, savedSrcBlend);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_DESTBLEND, savedDestBlend);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_BLENDOP, savedBlendOp);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZENABLE, savedZEnable);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_ZWRITEENABLE, savedZWrite);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_COLORWRITEENABLE, savedColorWrite);
	DX8Wrapper::Set_DX8_Render_State(D3DRS_CULLMODE, savedCull);

	DX8Wrapper::Invalidate_Cached_Shader();
}

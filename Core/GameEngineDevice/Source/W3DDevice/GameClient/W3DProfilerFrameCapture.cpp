/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
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

#ifdef PROFILER_ENABLED

#include "../../../Include/W3DDevice/GameClient/W3DProfilerFrameCapture.h"
#include "../../../Include/W3DDevice/GameClient/W3DShaderManager.h"

#include "WW3D2/dx8wrapper.h"
#include "WW3D2/surfaceclass.h"
#include "WW3D2/texture.h"
#include "WW3D2/ww3d.h"
#include "WW3D2/ww3dformat.h"
#include "WWMath/wwmath.h"
#include <cstring>

W3DProfilerFrameCapture::W3DProfilerFrameCapture()
{
}

W3DProfilerFrameCapture::~W3DProfilerFrameCapture()
{
	if (m_swizzleShader)
	{
		// Needs the device itself: this is the frame capture tool. It creates its own
		// textures and shader and reads the render target back; it is not part of the
		// render path and only runs when a capture is asked for.
		DX8Wrapper::_Get_D3D_Device8()->DeletePixelShader(m_swizzleShader);
		m_swizzleShader = 0;
	}
}

bool W3DProfilerFrameCapture::ShouldReuseLastCapture(UnsignedInt currentTimeMs) const
{
	return PROFILER_FRAME_IMAGE_INTERVAL_MS > 0
		&& currentTimeMs - m_lastCaptureTimeMs < PROFILER_FRAME_IMAGE_INTERVAL_MS
		&& !m_lastCapturePixels.empty();
}

void W3DProfilerFrameCapture::Capture(UnsignedInt displayWidth, UnsignedInt displayHeight)
{
	if (!PROFILER_IS_CONNECTED)
		return;

	// the profiler expects an image every render frame. resend the last capture if we're inside the capture interval.
	const UnsignedInt currentTimeMs = WW3D::Get_Logic_Time_Milliseconds();
	if (ShouldReuseLastCapture(currentTimeMs))
	{
		PROFILER_FRAME_IMAGE(m_lastCapturePixels.data(), PROFILER_FRAME_IMAGE_SIZE, m_lastCaptureHeight, 0, false);
		return;
	}

	// The BGRA -> RGBA swizzle. This was a ps_1_4 shader assembled here at runtime with
	// D3DXAssembleShader, which D3D11 has no counterpart for; it is profilerswizzle_ps.hlsl
	// now, built with every other shader.
	if (!m_swizzleShader)
	{
		if (FAILED(W3DShaderManager::LoadAndCreateD3DShader("shaders\\profilerswizzle_ps.pso",
				nullptr, 0, false, &m_swizzleShader)))
			return;
	}

	// allocate render target
	TextureClass *renderTarget = DX8Wrapper::Create_Render_Target(PROFILER_FRAME_IMAGE_SIZE, PROFILER_FRAME_IMAGE_SIZE, WW3D_FORMAT_A8R8G8B8);
	if (!renderTarget)
		return;

	// allocate surface class
	const Real aspectRatio = (Real)displayHeight / (Real)displayWidth;
	unsigned int profilerImageHeight = min((int)WWMath::Round(PROFILER_FRAME_IMAGE_SIZE * aspectRatio), PROFILER_FRAME_IMAGE_SIZE);
	SurfaceClass *surfaceClass = NEW_REF(SurfaceClass, (PROFILER_FRAME_IMAGE_SIZE, profilerImageHeight, WW3D_FORMAT_A8R8G8B8));
	if (!surfaceClass)
	{
		REF_PTR_RELEASE(renderTarget);
		return;
	}

	// get the backbuffer
	SurfaceClass *backBuffer = DX8Wrapper::_Get_DX8_Back_Buffer();
	if (!backBuffer)
	{
		REF_PTR_RELEASE(surfaceClass);
		REF_PTR_RELEASE(renderTarget);
		return;
	}

	GfxSurface *backBufferSurface = backBuffer->Peek_D3D_Surface();
	WW3DSurfaceDescription backBufferSurfaceDesc;
	HRESULT hr = DX8Wrapper::Describe_DX8_Surface(backBufferSurface,
		backBufferSurfaceDesc) ? S_OK : E_FAIL;
	if (FAILED(hr))
	{
		REF_PTR_RELEASE(backBuffer);
		REF_PTR_RELEASE(surfaceClass);
		REF_PTR_RELEASE(renderTarget);
		return;
	}

	// allocate intermediate texture
	GfxTexture *intermediateTexture = DX8Wrapper::Create_DX8_Texture_Resource(
		backBufferSurfaceDesc.Width,
		backBufferSurfaceDesc.Height,
		1,
		backBufferSurfaceDesc.Format,
		GFX_USAGE_RENDER_TARGET);
	hr = (intermediateTexture != nullptr) ? S_OK : E_FAIL;
	if (FAILED(hr))
	{
		REF_PTR_RELEASE(backBuffer);
		REF_PTR_RELEASE(surfaceClass);
		REF_PTR_RELEASE(renderTarget);
		return;
	}

	// draw backbuffer to intermediate texture
	GfxSurface *intermediateTextureSurface =
		DX8Wrapper::Get_DX8_Texture_Surface_Level(intermediateTexture, 0);
	hr = (intermediateTextureSurface != nullptr) ? S_OK : E_FAIL;
	if (FAILED(hr))
	{
		REF_PTR_RELEASE(backBuffer);
		REF_PTR_RELEASE(surfaceClass);
		REF_PTR_RELEASE(renderTarget);
		DX8Wrapper::Release_DX8_Texture_Resource(intermediateTexture);
		return;
	}
	DX8Wrapper::_Copy_DX8_Rects(backBufferSurface, nullptr, 0, intermediateTextureSurface, nullptr);
	DX8Wrapper::Release_DX8_Surface_Resource(intermediateTextureSurface);
	intermediateTextureSurface = nullptr;

	// release the backbuffer
	backBufferSurface = nullptr;
	REF_PTR_RELEASE(backBuffer);

	// set render target to a small surface
	GfxSurface *smallRenderTargetSurface = renderTarget->Get_D3D_Surface_Level();
	WWASSERT(smallRenderTargetSurface != nullptr);
	DX8Wrapper::Set_Render_Target(smallRenderTargetSurface, false);

	// set viewport
	D3DVIEWPORT8 restoreViewport;
	DX8Wrapper::Get_DX8_Viewport(restoreViewport);

	SurfaceClass::SurfaceDescription smallRenderDesc;
	surfaceClass->Get_Description(smallRenderDesc);

	D3DVIEWPORT8 viewport;
	viewport.X = 0;
	viewport.Y = 0;
	viewport.Width = PROFILER_FRAME_IMAGE_SIZE;
	viewport.Height = smallRenderDesc.Height;
	viewport.MinZ = 0.0f;
	viewport.MaxZ = 1.0f;
	DX8Wrapper::Set_Viewport(&viewport);

	// Draw the intermediate texture scaled down onto the small target. The quad, its
	// half-texel offset and its vertex shader are W3DShaderManager's, shared with the bloom
	// chain and the debug views; this used to build its own D3DFVF_XYZRHW quad, which is a
	// fixed-function vertex draw however programmable its pixel half is.
	DX8Wrapper::Set_Pixel_Shader(m_swizzleShader);
	DX8Wrapper::Set_DX8_Texture(0, intermediateTexture);
	W3DShaderManager::drawScreenQuad(
		0.0f, 0.0f, (float)PROFILER_FRAME_IMAGE_SIZE, (float)smallRenderDesc.Height,
		0.0f, 0.0f, 1.0f, 1.0f,
		0.0f, 0.0f, 1.0f, 1.0f);
	DX8Wrapper::Set_Pixel_Shader(0);
	DX8Wrapper::Set_DX8_Texture(0, nullptr);
	DX8Wrapper::Set_Viewport(&restoreViewport);
	DX8Wrapper::Set_Render_Target(static_cast<GfxSurface *>(nullptr));

	// copy the small surface pixels from GPU to CPU
	RECT srcRect = { 0, 0, PROFILER_FRAME_IMAGE_SIZE, smallRenderDesc.Height };
	POINT dstPoint = { 0, 0 };
	DX8Wrapper::_Copy_DX8_Rects(
		smallRenderTargetSurface,
		&srcRect,
		1,
		surfaceClass->Peek_D3D_Surface(),
		&dstPoint);
	DX8Wrapper::Release_DX8_Surface_Resource(smallRenderTargetSurface);

	// send pixels to the profiler backend
	int pitch = 0;
	void *bits = surfaceClass->Lock(&pitch);
	if (bits)
	{
		const size_t rowBytes = (size_t)PROFILER_FRAME_IMAGE_SIZE * 4;
		m_lastCaptureHeight = smallRenderDesc.Height;
		m_lastCapturePixels.resize(rowBytes * m_lastCaptureHeight);

		const UnsignedByte *source = static_cast<const UnsignedByte *>(bits);
		UnsignedByte *destination = m_lastCapturePixels.data();
		for (UnsignedInt row = 0; row < m_lastCaptureHeight; ++row)
		{
			std::memcpy(destination + row * rowBytes, source + row * pitch, rowBytes);
		}

		PROFILER_FRAME_IMAGE(m_lastCapturePixels.data(), PROFILER_FRAME_IMAGE_SIZE, m_lastCaptureHeight, 0, false);
		surfaceClass->Unlock();
		m_lastCaptureTimeMs = currentTimeMs;
	}

	// cleanup
	DX8Wrapper::Release_DX8_Texture_Resource(intermediateTexture);
	intermediateTexture = nullptr;
	REF_PTR_RELEASE(surfaceClass);
	REF_PTR_RELEASE(renderTarget);
}

#endif // PROFILER_ENABLED

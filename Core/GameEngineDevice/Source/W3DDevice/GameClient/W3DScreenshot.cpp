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

#include "W3DDevice/GameClient/W3DScreenshot.h"
#include "Common/GlobalData.h"
#include "GameClient/GameText.h"
#include "GameClient/InGameUI.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/surfaceclass.h"
#include "WWLib/mpsc_intrusive_queue.h"
#include <stb_image_write.h>

struct ScreenshotThreadData
{
	ScreenshotThreadData()
		: pixelData(nullptr)
	{
	}

	~ScreenshotThreadData()
	{
		delete[] pixelData;
	}

	unsigned char* pixelData; // is owner
	unsigned int width;
	unsigned int height;
	unsigned int pitch;
	bool is16Bit;
	char userDataDirectory[_MAX_PATH];
	char subDirectory[_MAX_PATH];
	char leafname[_MAX_FNAME];
	int quality;
	ScreenshotFormat format;
	bool announce;
};

// TheInGameUI is not thread safe, so the screenshot threads cannot show the success message
// themselves. Each thread pushes the written filename onto this queue and the main thread
// shows all pending messages in W3D_UpdateScreenshotMessages, so no message is lost when
// multiple screenshot threads finish within the same frame.
struct ScreenshotWrittenMessage
{
	ScreenshotWrittenMessage* next;
	char leafname[_MAX_FNAME];
	bool announce;
};
static MPSCIntrusiveQueue<ScreenshotWrittenMessage> s_screenshotWrittenQueue;

static DWORD WINAPI screenshotThreadFunc(LPVOID param)
{
	ScreenshotThreadData* data = static_cast<ScreenshotThreadData*>(param);

	// TheSuperHackers @feature bobtista 08/07/2026 Save screenshots into a Screenshots subfolder
	// to keep the user data root folder tidy.
	// TheSuperHackers @tweak andytraber 17/08/2026 The subfolder is now the caller's, and can be
	// nested, so each component is created in turn -- CreateDirectory makes only the leaf.
	char pathname[_MAX_PATH];
	strlcpy(pathname, data->userDataDirectory, ARRAY_SIZE(pathname));
	const size_t rootLength = strlen(pathname);
	strlcat(pathname, data->subDirectory, ARRAY_SIZE(pathname));
	for (size_t i = rootLength; pathname[i] != '\0'; ++i)
	{
		if (pathname[i] == '\\')
		{
			pathname[i] = '\0';
			CreateDirectory(pathname, nullptr);
			pathname[i] = '\\';
		}
	}
	strlcat(pathname, data->leafname, ARRAY_SIZE(pathname));

	const unsigned int width = data->width;
	const unsigned int height = data->height;

	// Convert to R8G8B8 for stb_image_write.
	unsigned char* image = new unsigned char[3 * width * height];

	if (!data->is16Bit)
	{
		// Convert A8R8G8B8/X8R8G8B8 to R8G8B8
		for (unsigned int y = 0; y < height; ++y)
		{
			const unsigned int* srcLine = reinterpret_cast<const unsigned int*>(data->pixelData + y * data->pitch);
			for (unsigned int x = 0; x < width; ++x)
			{
				const unsigned int argb = srcLine[x];
				const unsigned int index = 3 * (x + y * width);
				image[index + 0] = (unsigned char)(argb >> 16); // r
				image[index + 1] = (unsigned char)(argb >> 8);  // g
				image[index + 2] = (unsigned char)(argb >> 0);  // b
			}
		}
	}
	else
	{
		// Convert R5G6B5 to R8G8B8
		for (unsigned int y = 0; y < height; ++y)
		{
			const unsigned short* srcLine = reinterpret_cast<const unsigned short*>(data->pixelData + y * data->pitch);
			for (unsigned int x = 0; x < width; ++x)
			{
				const unsigned short rgb = srcLine[x];
				const unsigned int index = 3 * (x + y * width);
				image[index + 0] = (unsigned char)((rgb & 0xF800) >> 8); // r
				image[index + 1] = (unsigned char)((rgb & 0x07E0) >> 3); // g
				image[index + 2] = (unsigned char)((rgb & 0x001F) << 3); // b
			}
		}
	}

	int success = 0;
	switch (data->format)
	{
		case SCREENSHOT_JPEG:
			success = stbi_write_jpg(pathname, width, height, 3, image, data->quality);
			break;
		case SCREENSHOT_PNG:
			success = stbi_write_png(pathname, width, height, 3, image, width * 3);
			break;
	}

	if (success)
	{
		ScreenshotWrittenMessage* message = new ScreenshotWrittenMessage;
		strlcpy(message->leafname, data->leafname, ARRAY_SIZE(message->leafname));
		message->announce = data->announce;
		s_screenshotWrittenQueue.Push(message);
	}
	else
	{
		DEBUG_LOG(("Failed to write screenshot %s", pathname));
	}

	delete[] image;
	delete data;

	return success;
}

void W3D_UpdateScreenshotMessages()
{
	ScreenshotWrittenMessage* message = s_screenshotWrittenQueue.Flush();
	while (message != nullptr)
	{
		DEBUG_LOG(("Screenshot written: %s", message->leafname));
		if (message->announce)
		{
			UnicodeString ufileName;
			ufileName.translate(message->leafname);
			TheInGameUI->message(TheGameText->fetch("GUI:ScreenCapture"), ufileName.str());
		}
		ScreenshotWrittenMessage* next = message->next;
		delete message;
		message = next;
	}
}

static constexpr const char* const ScreenshotFormatExtensions[] = { "jpg", "png" };
static_assert(ARRAY_SIZE(ScreenshotFormatExtensions) == SCREENSHOT_FORMAT_COUNT, "Incorrect array size");

void W3D_TakeCompressedScreenshot(ScreenshotFormat format, Int jpegQuality)
{
	// The filename is created here so the timestamp matches the capture time.
	char leafname[_MAX_FNAME];

	SYSTEMTIME st;
	GetLocalTime(&st);
	sprintf(leafname, "sshot_%04d%02d%02d_%02d%02d%02d_%03d",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

	W3D_TakeCompressedScreenshotNamed(format, jpegQuality, "Screenshots\\", leafname, TRUE);
}

void W3D_TakeCompressedScreenshotNamed(ScreenshotFormat format, Int jpegQuality,
	const char* subDirectory, const char* leafname, Bool announce)
{
	char leafnameWithExtension[_MAX_FNAME];
	sprintf(leafnameWithExtension, "%s.%s", leafname, ScreenshotFormatExtensions[format]);

	// TheSuperHackers @bugfix xezon 21/05/2025 Get the back buffer and create a copy of the surface.
	// Originally this code took the front buffer and tried to lock it. This does not work when the
	// render view clips outside the desktop boundaries. It crashed the game.
	SurfaceClass* surface = DX8Wrapper::_Get_DX8_Back_Buffer();
	SurfaceClass::SurfaceDescription surfaceDesc;
	surface->Get_Description(surfaceDesc);

	// TheSuperHackers @bugfix bobtista 08/07/2026 Support the 16 bit back buffer format that the
	// game uses when running in 16 bit color mode. Reading it with the 32 bit stride read garbage.
	const bool is32Bit = surfaceDesc.Format == WW3D_FORMAT_A8R8G8B8 || surfaceDesc.Format == WW3D_FORMAT_X8R8G8B8;
	const bool is16Bit = surfaceDesc.Format == WW3D_FORMAT_R5G6B5;

	if (!is32Bit && !is16Bit)
	{
		DEBUG_LOG(("Screenshot does not support back buffer format %d", (int)surfaceDesc.Format));
		surface->Release_Ref();
		return;
	}

	// The creation reference has to be given back. SurfaceClass::Attach takes a reference of
	// its own, so a surface created here and handed straight to the constructor arrives at two
	// references and leaves at one -- and stays there for the life of the process.
	//
	// This has always leaked; the second backend only made it visible, exactly as it did for
	// the seven shader handles Phase 8 found. Under D3D9 the object was an IDirect3DSurface9
	// the engine's own allocator never saw, so the leak report said nothing; the D3D11 backend
	// wraps a surface in an engine allocation and the same omission shows up as a block.
	// Measured on civ_buildings before the fix: one leaked 1280x720 offscreen surface per
	// screenshot, one for one dump and three for three.
	GfxSurface* rawSurface = DX8Wrapper::_Create_DX8_Surface(surfaceDesc.Width, surfaceDesc.Height, surfaceDesc.Format);
	SurfaceClass* surfaceCopy = NEW_REF(SurfaceClass, (rawSurface));
	DX8Wrapper::Release_DX8_Surface_Resource(rawSurface);
	DX8Wrapper::_Copy_DX8_Rects(surface->Peek_D3D_Surface(), nullptr, 0, surfaceCopy->Peek_D3D_Surface(), nullptr);

	surface->Release_Ref();
	surface = nullptr;

	struct Rect
	{
		int Pitch;
		void* pBits;
	} lrect;

	lrect.pBits = surfaceCopy->Lock(&lrect.Pitch);
	if (lrect.pBits == nullptr)
	{
		surfaceCopy->Release_Ref();
		return;
	}

	ScreenshotThreadData* threadData = new ScreenshotThreadData();
	threadData->width = surfaceDesc.Width;
	threadData->height = surfaceDesc.Height;
	threadData->pitch = lrect.Pitch;
	threadData->is16Bit = is16Bit;
	threadData->quality = jpegQuality;
	threadData->format = format;
	threadData->announce = (announce != FALSE);
	strlcpy(threadData->userDataDirectory, TheGlobalData->getPath_UserData().str(), ARRAY_SIZE(threadData->userDataDirectory));
	strlcpy(threadData->subDirectory, subDirectory, ARRAY_SIZE(threadData->subDirectory));
	strlcpy(threadData->leafname, leafnameWithExtension, ARRAY_SIZE(threadData->leafname));

	// Copy the locked surface with a single memcpy, including any row padding. The pixel
	// conversion and all file operations are done on the screenshot thread to keep the
	// main thread cheap.
	threadData->pixelData = new unsigned char[threadData->pitch * threadData->height];
	memcpy(threadData->pixelData, lrect.pBits, threadData->pitch * threadData->height);

	surfaceCopy->Unlock();
	surfaceCopy->Release_Ref();
	surfaceCopy = nullptr;

	const HANDLE hThread = CreateThread(nullptr, 0, screenshotThreadFunc, threadData, 0, nullptr);
	if (hThread)
	{
		CloseHandle(hThread);
	}
	else
	{
		delete threadData;
	}
}

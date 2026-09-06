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

//******************************************************************************************
//
// Earth And Beyond
// Copyright (c) 2002 Electronic Arts , Inc.  -  Westwood Studios
//
// File Name		: dx8webbrowser.cpp
// Description		: Implementation of D3D Embedded Browser wrapper.
// Author			: Darren Schueller
// Date of Creation	: 6/4/2002
//
//******************************************************************************************
// $Header: $
//******************************************************************************************

#include "dx8webbrowser.h"
#include "WW3D2/ww3d.h"
#include "dx8wrapper.h"

#if ENABLE_EMBEDDED_BROWSER

#if defined(_MSC_VER) && _MSC_VER < 1300

// Import the Browser Type Library
// BGC, the path for the dll file is pretty odd, no?
//      I'll leave it like this till I can figure out a
//      better way.
#import "../../../../../Run/BrowserEngine.dll" no_namespace

#else

#ifdef __MINGW32__
#include "Utility/comsupp_compat.h"  // MinGW COM support compatibility
#endif
#include <comutil.h>
#include <comip.h>

#include "EABrowserEngine/BrowserEngine.h"

typedef _com_ptr_t<_com_IIID<IFEBrowserEngine2, &__uuidof(IFEBrowserEngine2)>> IFEBrowserEngine2Ptr;

#endif

static	IFEBrowserEngine2Ptr	pBrowser = 0;

HWND		DX8WebBrowser::hWnd = nullptr;

bool DX8WebBrowser::Initialize(	const char* badpageurl,
											const char* loadingpageurl,
											const char* mousefilename,
											const char* mousebusyfilename)
{
	if(pBrowser == 0)
	{
		// Initialize COM
		CoInitialize(nullptr);

		// Create an instance of the browser control
		HRESULT hr = pBrowser.CreateInstance(__uuidof(FEBrowserEngine2));

		if(hr == REGDB_E_CLASSNOTREG)
		{
			HMODULE lib = ::LoadLibrary("BrowserEngine.DLL");
			if(lib)
			{
				FARPROC proc = ::GetProcAddress(lib,"DllRegisterServer");
				if(proc)
				{
					proc();
					// Create an instance of the browser control
					hr = pBrowser.CreateInstance(__uuidof(FEBrowserEngine2));
				}
				FreeLibrary(lib);
			}
		}

		// Initialize the browser.
		if(hr == S_OK)
		{
			// The control renders into the device itself, from outside this engine
			// entirely, so there was never anything here to translate: it wants a D3D9
			// device or it wants nothing. It asked the seam for one through
			// Peek_Native_Device, the D3D11 backend answered null, and this feature has
			// been off since D3D11 became the default. Phase 10 removed the D3D9 backend
			// and Peek_Native_Device with it, so the answer is now no by construction --
			// stated here rather than discovered at runtime.
			WWDEBUG_SAY(("Embedded browser: the control needs a Direct3D 9 device to render "
				"into, and there is no backend that can hand it one. Disabled."));
			pBrowser->Shutdown();
			pBrowser = 0;
			CoUninitialize();
			return false;
		}
		else
		{
			pBrowser = 0;
			return false;
		}
	}

	return true;
}

void DX8WebBrowser::Shutdown()
{
	if(pBrowser)
	{
		// Shutdown the browser
		pBrowser->Shutdown();

		// Release the smart pointer.
		pBrowser = 0;

		hWnd = nullptr;

		// Shut down COM
		CoUninitialize();
	}
}


// ******************************************************************************************
// * Function Name: DX8WebBrowser::Update
// ******************************************************************************************
// * Description: 	Updates the browser image surfaces by copying the bits from the browser
// *						DCs to the D3D Image surfaces.
// *
// * Return Type:
// *
// * Argument:    	void
// *
// ******************************************************************************************
void	DX8WebBrowser::Update()
{
	if(pBrowser) pBrowser->D3DUpdate();
};


// ******************************************************************************************
// * Function Name: DX8WebBrowser::Render
// ******************************************************************************************
// * Description: 	Draws all browsers to the back buffer.
// *
// * Return Type:
// *
// * Argument:    	int backbufferindex
// *
// ******************************************************************************************
void	DX8WebBrowser::Render(int backbufferindex)
{
	if(pBrowser) pBrowser->D3DRender(backbufferindex);
};

// ******************************************************************************************
// * Function Name: DX8WebBrowser::CreateBrowser
// ******************************************************************************************
// * Description: 	Creates a browser window.
// *
// * Return Type:
// *
// * Argument:    	const char* browsername -	This is a "name" used to identify the
// *															browser instance.  Multiple browsers can
// *															be created, and are referenced using this name.
// * Argument:    	const char* url - The url to display.
// * Argument:    	int x - The position and size of the browser (in pixels)
// * Argument:    	int y
// * Argument:    	int w
// * Argument:    	int h
// * Argument:    	int updateticks - When non-zero, this forces the browser image to get updated
// *												at the specified rate (number of milliseconds) regardless
// *												of paint messages.  When this is zero (the default) the browser
// *												image is only updated whenever a paint message is received.
// *
// ******************************************************************************************
void	DX8WebBrowser::CreateBrowser(const char* browsername, const char* url, int x, int y, int w, int h, int updateticks, LONG options, LPDISPATCH gamedispatch)
{
	WWDEBUG_SAY(("DX8WebBrowser::CreateBrowser - Creating browser with the name %s, url = %s, (x, y, w, h) = (%d, %d, %d, %d), update ticks = %d", browsername, url, x, y, h, w, updateticks));
	if(pBrowser)
	{
		_bstr_t brsname(browsername);
		pBrowser->CreateBrowser(brsname, _bstr_t(url), reinterpret_cast<long>(hWnd), x, y, w, h, options, gamedispatch);
		pBrowser->SetUpdateRate(brsname, updateticks);
	}
}


// ******************************************************************************************
// * Function Name: DX8WebBrowser::DestroyBrowser
// ******************************************************************************************
// * Description: 	Destroys the specified browser.  This closes the window and releases
// *						the browser instance.
// *
// * Return Type:
// *
// * Argument:    	const char* browsername - The name of the browser to destroy.
// *
// ******************************************************************************************
void	DX8WebBrowser::DestroyBrowser(const char* browsername)
{
	WWDEBUG_SAY(("DX8WebBrowser::DestroyBrowser - destroying browser %s", browsername));
	if(pBrowser)
		pBrowser->DestroyBrowser(_bstr_t(browsername));
}


// ******************************************************************************************
// * Function Name: DX8WebBrowser::Is_Browser_Open
// ******************************************************************************************
// * Description: 	This function checks to see if a browser of the specified name exists and
// *						is currently open.
// *
// * Return Type:
// *
// * Argument:    	const char* browsername - The name of the browser to test.
// *
// ******************************************************************************************
bool	DX8WebBrowser::Is_Browser_Open(const char* browsername)
{
	if(pBrowser == 0) return false;
#if defined(_MSC_VER) && _MSC_VER < 1300
	return (pBrowser->IsOpen(_bstr_t(browsername)) != 0);
#else
	long isOpen;
	return (pBrowser->IsOpen(_bstr_t(browsername), &isOpen) != 0);
#endif
}

// ******************************************************************************************
// * Function Name: DX8WebBrowser::Navigate
// ******************************************************************************************
// * Description: 	This function causes the browser to navigate to the specified page.
// *
// * Return Type:
// *
// * Argument:    	const char* browsername - The name of the browser to test.
// *						const char* url - The url to navigate to.
// *
// ******************************************************************************************
void	DX8WebBrowser::Navigate(const char* browsername, const char* url)
{
	if(pBrowser == 0) return;
	pBrowser->Navigate(_bstr_t(browsername),_bstr_t(url));
}

#endif

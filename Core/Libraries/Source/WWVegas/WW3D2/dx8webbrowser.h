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
// File Name		: dx8webbrowser.h
// Description		: Implementation of D3D Embedded Browser Wrapper
// Author			: Darren Schueller
// Date of Creation	: 6/4/2002
//
//******************************************************************************************
// $Header: $
//******************************************************************************************

#pragma once

#include <windows.h>
// LPDISPATCH, in CreateBrowser's signature. It used to arrive with the OLE headers that
// d3d9.h dragged in behind the D3D9 compat shim; the shim is gone, so this header names
// what it needs.
#include <oaidl.h>

// ***********************************
// Set this to 0 to remove all embedded browser code.
//
// Left on. It is a D3D9-only feature and not a porting problem: the control renders
// into the device itself from outside this codebase, so there is nothing to translate
// -- see Peek_Native_Device in gfxdevice.h, which is how Initialize now asks whether
// the running backend is one it can be handed. A backend that is not answers null and
// the browser turns itself off.
//
// Whether it has run in this decade is a separate question, and the answer here is no:
// FEBrowserEngine2 (CLSID {2B2CC8B0-2DC0-48C6-B6FD-C07820A6477E}) is not registered on
// this machine, and the LoadLibrary("BrowserEngine.DLL") fallback in Initialize cannot
// find one either because the deployed game directory has no such DLL -- only the
// repository's GeneralsMD/Run does. So CreateInstance fails, pBrowser stays null, and
// every other entry point here is already guarded on it. That is a reason to leave the
// switch alone rather than to design the seam around the feature.
//
#define ENABLE_EMBEDDED_BROWSER		1
//
// ***********************************

#if ENABLE_EMBEDDED_BROWSER

// These options must match the browser option bits defined in the BrowserEngine code.
// Look in febrowserengine.h
#define BROWSEROPTION_SCROLLBARS		0x0001
#define BROWSEROPTION_3DBORDER		0x0002


/**
** DX8WebBrowser
**
** DX8 interface wrapper class.  This encapsulates the BrowserEngine interface.
*/
class DX8WebBrowser
{
public:

	static bool			Initialize(	const char* badpageurl = 0,
											const char* loadingpageurl = 0,
											const char* mousefilename = 0,
											const char* mousebusyfilename = 0);			//Initialize the Embedded Browser

	static void			Shutdown();			// Shutdown the embedded browser.  Will close any open browsers.

	static void			Update();				// Copies all browser contexts to D3D Image surfaces.
	static void			Render(int backbufferindex);	//Draws all browsers to the backbuffer.

	// Creates a browser with the specified name
	static void			CreateBrowser(const char* browsername, const char* url, int x, int y, int w, int h, int updateticks = 0, LONG options = BROWSEROPTION_SCROLLBARS | BROWSEROPTION_3DBORDER, LPDISPATCH gamedispatch = 0);

	// Destroys the browser with the specified name
	static void			DestroyBrowser(const char* browsername);

	// Returns true if a browser with the specified name is open.
	static bool			Is_Browser_Open(const char* browsername);

	// Navigates the specified browser to the specified page.
	static void			Navigate(const char* browsername, const char* url);

private:
	// The window handle of the application.  This is initialized by Initialize().
	static				HWND						hWnd;
};

#endif

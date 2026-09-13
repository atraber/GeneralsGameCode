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

/**
 * MinGW-w64 Compatibility Layer for Wide Character Formatting
 *
 * In MinGW-w64, __USE_MINGW_ANSI_STDIO defaults to 1 under C99/C++ standards,
 * redirecting wide character formatting functions (vswprintf, swprintf, etc.)
 * to __mingw_vswprintf from libmingwex.a.
 *
 * According to ISO C99, in wide format strings, %s expects a narrow string
 * (const char*) which it converts from multibyte to wide char, while %ls
 * expects a wide string (const wchar_t*).
 *
 * However, the Generals / Zero Hour codebase and all localized string tables
 * (.csf files) were developed for MSVC / Windows CRT, where in wide format
 * strings, %s expects a wide string (const wchar_t*), %S / %hs expects a narrow
 * string (const char*), and %ls expects a wide string.
 *
 * When __mingw_vswprintf handles a format string like "Construction completed: %s"
 * with a wchar_t* argument (e.g. L"Barracks"), it interprets the wchar_t* as char*.
 * Because wchar_t on Windows is 16-bit little-endian, the second byte of 'B'
 * is 0x00 (null terminator in char*). Thus, __mingw_vswprintf reads only "B"
 * and produces "Construction completed: B".
 *
 * By overriding __mingw_vswprintf, __mingw_swprintf, __mingw_vsnwprintf,
 * __mingw_snwprintf, __mingw_vfwprintf, __mingw_fwprintf, __mingw_vwprintf,
 * and __mingw_wprintf to route to the Microsoft CRT implementations (_vsnwprintf,
 * __ms_vfwprintf, __ms_vwprintf), wide format strings are formatted according
 * to Windows CRT conventions matching the game's expectations.
 */

#ifdef __MINGW32__

#include <stdio.h>
#include <wchar.h>
#include <stdarg.h>

int __cdecl __mingw_vswprintf(wchar_t * __restrict__ _Buffer, size_t _BufferCount, const wchar_t * __restrict__ _Format, va_list _ArgList)
{
    if (_BufferCount == 0)
        return -1;
    // Microsoft's _vsnwprintf adheres to Windows format string rules (%s = wchar_t*, %S/%hs = char*)
    int result = _vsnwprintf(_Buffer, _BufferCount, _Format, _ArgList);
    // Ensure null-termination on truncation/boundary
    if (result == -1 || (size_t)result == _BufferCount)
    {
        _Buffer[_BufferCount - 1] = L'\0';
        return -1;
    }
    return result;
}

int __cdecl __mingw_swprintf(wchar_t * __restrict__ _Buffer, size_t _BufferCount, const wchar_t * __restrict__ _Format, ...)
{
    va_list _ArgList;
    va_start(_ArgList, _Format);
    int result = __mingw_vswprintf(_Buffer, _BufferCount, _Format, _ArgList);
    va_end(_ArgList);
    return result;
}

int __cdecl __mingw_vsnwprintf(wchar_t * __restrict__ _Buffer, size_t _BufferCount, const wchar_t * __restrict__ _Format, va_list _ArgList)
{
    return __mingw_vswprintf(_Buffer, _BufferCount, _Format, _ArgList);
}

int __cdecl __mingw_snwprintf(wchar_t * __restrict__ _Buffer, size_t _BufferCount, const wchar_t * __restrict__ _Format, ...)
{
    va_list _ArgList;
    va_start(_ArgList, _Format);
    int result = __mingw_vswprintf(_Buffer, _BufferCount, _Format, _ArgList);
    va_end(_ArgList);
    return result;
}

int __cdecl __mingw_vfwprintf(FILE * __restrict__ _File, const wchar_t * __restrict__ _Format, va_list _ArgList)
{
    return __ms_vfwprintf(_File, _Format, _ArgList);
}

int __cdecl __mingw_fwprintf(FILE * __restrict__ _File, const wchar_t * __restrict__ _Format, ...)
{
    va_list _ArgList;
    va_start(_ArgList, _Format);
    int result = __ms_vfwprintf(_File, _Format, _ArgList);
    va_end(_ArgList);
    return result;
}

int __cdecl __mingw_vwprintf(const wchar_t * __restrict__ _Format, va_list _ArgList)
{
    return __ms_vwprintf(_Format, _ArgList);
}

int __cdecl __mingw_wprintf(const wchar_t * __restrict__ _Format, ...)
{
    va_list _ArgList;
    va_start(_ArgList, _Format);
    int result = __ms_vwprintf(_Format, _ArgList);
    va_end(_ArgList);
    return result;
}

#endif // __MINGW32__

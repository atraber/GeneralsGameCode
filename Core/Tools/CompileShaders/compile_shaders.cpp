/*
**	Command & Conquer Generals(tm)
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

//-----------------------------------------------------------------------------
// compile_shaders -- HLSL source to D3D9 bytecode, with the compiler pinned.
//
// This exists instead of fxc.exe for one reason: fxc cannot be relied on to be
// present, and on Linux it is not present at all. fxc is only a command-line
// wrapper around D3DCompile in d3dcompiler_47.dll, and minidx9 -- which the build
// already fetches for the D3D9 headers and import libraries -- vendors that exact
// Microsoft DLL. Calling it directly gets the same compiler on every host without
// asking anyone to install a Windows SDK.
//
// The DLL is named on the command line rather than linked, and that is the whole
// point of the design. Linking d3dcompiler leaves the choice of implementation to
// the loader, and under Wine the loader answers with Wine's builtin -- which is
// vkd3d-shader's HLSL frontend, a different compiler with different Shader Model 3
// support. That substitution is silent: the build succeeds, the game runs, and the
// bytecode is not the bytecode Windows produces from the same source. Load_Compiler
// below refuses to proceed if it happens.
//-----------------------------------------------------------------------------

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//-----------------------------------------------------------------------------
// The pieces of the d3dcompiler interface this needs, declared rather than
// included. minidx9's D3Dcompiler.h drags in d3d11shader.h and d3d12shader.h, and
// through them the whole D3D11/D3D12 header set, which does not compile under VC6
// -- a toolchain this repository still builds with. The surface actually used is
// two methods on one interface and one function pointer, all of them stable since
// D3D10, so declaring them costs less than the include does.
//-----------------------------------------------------------------------------

struct IUnknownLite
{
	virtual HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) = 0;
	virtual ULONG   __stdcall AddRef() = 0;
	virtual ULONG   __stdcall Release() = 0;
};

// ID3DBlob, which is ID3D10Blob under another name. Method order is the vtable.
struct ID3DBlobLite : public IUnknownLite
{
	virtual void*  __stdcall GetBufferPointer() = 0;
	virtual SIZE_T __stdcall GetBufferSize() = 0;
};

// D3D_SHADER_MACRO under another name. A {NULL, NULL} entry terminates the array,
// which is how D3DCompile is told where the list stops.
struct ShaderMacro
{
	const char* Name;
	const char* Definition;
};

// pInclude is typed void* because this passes the standard include handler and does
// not need the real struct definition.
typedef HRESULT (__stdcall *PFN_D3DCompile)(
	LPCVOID        pSrcData,
	SIZE_T         SrcDataSize,
	LPCSTR         pSourceName,
	const ShaderMacro* pDefines,
	void*          pInclude,
	LPCSTR         pEntrypoint,
	LPCSTR         pTarget,
	UINT           Flags1,
	UINT           Flags2,
	ID3DBlobLite** ppCode,
	ID3DBlobLite** ppErrorMsgs);

// Resolves #include relative to the including file's directory, which is what the
// shaders expect of "tonemap.hlsli".
#define STANDARD_FILE_INCLUDE ((void*)(UINT_PTR)1)

// Level 1 is what fxc compiles at when no /O is given, which is how every shader in
// this tree was built and looked at up to now. Stated explicitly rather than left to
// the default so that it is a decision on record: raising it changes all 37 shaders'
// bytecode, and instruction selection at a different level is not obliged to produce
// bit-identical float results, so it wants a look at the game rather than a flag flip.
#define OPTIMIZATION_LEVEL1   0

//-----------------------------------------------------------------------------

static PFN_D3DCompile Load_Compiler(const char* dll_path)
{
	HMODULE module = LoadLibraryA(dll_path);
	if (!module) {
		printf("compile_shaders: cannot load %s (error %lu)\n", dll_path, GetLastError());
		return NULL;
	}

	// Confirm the file named on the command line is the file that got loaded. Wine
	// prefers its own builtin for a DLL it implements even when an explicit path is
	// given, unless the caller has overridden it -- and a builtin always resolves
	// into the system directory, where the vendored copy never lives. Comparing the
	// two paths textually would not work: the requested path arrives in whatever
	// form the build system generated, and the loaded one comes back in DOS form.
	char loaded[MAX_PATH];
	char system_dir[MAX_PATH];
	if (GetModuleFileNameA(module, loaded, MAX_PATH) &&
		GetSystemDirectoryA(system_dir, MAX_PATH))
	{
		const size_t system_len = strlen(system_dir);
		if (_strnicmp(loaded, system_dir, system_len) == 0) {
			printf("compile_shaders: refusing to compile with %s\n", loaded);
			printf("  Asked for %s, but the loader substituted a system copy.\n", dll_path);
			printf("  Under Wine that is the builtin (vkd3d-shader), which is a different\n");
			printf("  compiler and produces different bytecode. Set WINEDLLOVERRIDES to\n");
			printf("  d3dcompiler_47=n so the vendored Microsoft DLL is used instead.\n");
			return NULL;
		}
	}

	PFN_D3DCompile compile = (PFN_D3DCompile)GetProcAddress(module, "D3DCompile");
	if (!compile) {
		printf("compile_shaders: %s exports no D3DCompile\n", loaded);
		return NULL;
	}

	return compile;
}

static bool Compile_One(PFN_D3DCompile compile, const char* hlsl_path, const char* out_path,
						const char* target, const ShaderMacro* defines)
{
	FILE* file = fopen(hlsl_path, "rb");
	if (!file) {
		printf("compile_shaders: cannot open %s\n", hlsl_path);
		return false;
	}

	fseek(file, 0, SEEK_END);
	const long size = ftell(file);
	fseek(file, 0, SEEK_SET);
	if (size <= 0) {
		printf("compile_shaders: %s is empty\n", hlsl_path);
		fclose(file);
		return false;
	}

	char* source = (char*)malloc((size_t)size);
	const size_t read = fread(source, 1, (size_t)size, file);
	fclose(file);
	if (read != (size_t)size) {
		printf("compile_shaders: short read on %s\n", hlsl_path);
		free(source);
		return false;
	}

	ID3DBlobLite* code = NULL;
	ID3DBlobLite* errors = NULL;

	// The source path is passed so that diagnostics name the file and the include
	// handler has a directory to resolve against.
	const HRESULT hr = compile(
		source, (SIZE_T)size, hlsl_path,
		defines, STANDARD_FILE_INCLUDE,
		"main", target,
		OPTIMIZATION_LEVEL1, 0,
		&code, &errors);

	free(source);

	if (errors) {
		// Warnings arrive here on success too, and are worth seeing.
		printf("%s", (const char*)errors->GetBufferPointer());
		errors->Release();
	}

	if (FAILED(hr) || !code) {
		printf("compile_shaders: %s failed to compile (HRESULT 0x%08lx)\n", hlsl_path, (unsigned long)hr);
		if (code) code->Release();
		return false;
	}

	FILE* out = fopen(out_path, "wb");
	if (!out) {
		printf("compile_shaders: cannot create %s\n", out_path);
		code->Release();
		return false;
	}

	const size_t written = fwrite(code->GetBufferPointer(), 1, code->GetBufferSize(), out);
	const bool ok = (written == code->GetBufferSize());
	fclose(out);
	code->Release();

	if (!ok) {
		printf("compile_shaders: short write on %s\n", out_path);
		return false;
	}

	return true;
}

// Preprocessor definitions, one per trailing argument, spelled NAME=VALUE.
//
// Deliberately not passed for the Shader Model 3 build. The shaders default to model 3
// when nothing is defined, so that invocation's argument list is character for character
// what it has always been, and the bytecode it produces can be compared byte for byte
// against the build from before the shaders learned a second model. A define that is
// only ever absent cannot change the token stream it is absent from.
#define MAX_DEFINES 16

int main(int argc, char* argv[])
{
	if (argc < 5 || argc > 5 + MAX_DEFINES) {
		printf("Usage: compile_shaders <d3dcompiler_dll> <hlsl_path> <out_path> <target_profile> [NAME=VALUE ...]\n");
		return 1;
	}

	// Split in place at the '='. argv is writable and lives as long as the process, so
	// the pointers handed to D3DCompile stay valid for the call.
	ShaderMacro defines[MAX_DEFINES + 1];
	int define_count = 0;
	for (int i = 5; i < argc; ++i) {
		char* equals = strchr(argv[i], '=');
		if (!equals) {
			printf("compile_shaders: '%s' is not NAME=VALUE\n", argv[i]);
			return 1;
		}
		*equals = '\0';
		defines[define_count].Name = argv[i];
		defines[define_count].Definition = equals + 1;
		++define_count;
	}
	defines[define_count].Name = NULL;
	defines[define_count].Definition = NULL;

	const PFN_D3DCompile compile = Load_Compiler(argv[1]);
	if (!compile) {
		return 1;
	}

	return Compile_One(compile, argv[2], argv[3], argv[4],
					   define_count > 0 ? defines : NULL) ? 0 : 1;
}

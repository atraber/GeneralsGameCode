# Compiles the HLSL shaders used by the programmable (D3D9) render path into
# D3D9 bytecode, and stages them under ${CMAKE_BINARY_DIR}/shaders. The game loads
# them at runtime as loose files (e.g. "shaders\\unit_vs.vso"); the per-game
# install() rules copy this directory next to the executable.
#
# The compiler is the Microsoft d3dcompiler_47.dll that minidx9 already vendors for
# the D3D9 headers and import libraries, driven by the small compile_shaders tool
# below. It used to be whichever fxc.exe the host happened to have, found by globbing
# the Windows Kits directory -- which meant the bytecode depended on the machine, and
# on a machine without a Windows SDK there was no bytecode at all and the renderer
# silently fell back to fixed function. Pinning the compiler to a fetched dependency
# makes the output a property of this source tree, and makes Linux a supported host
# rather than a degraded one.

set(RTS_D3DCOMPILER_DLL "${dx9_SOURCE_DIR}/10/Redist/D3D/x86/d3dcompiler_47.dll")
if(NOT EXISTS "${RTS_D3DCOMPILER_DLL}")
    message(FATAL_ERROR "minidx9 did not provide ${RTS_D3DCOMPILER_DLL}")
endif()

add_executable(compile_shaders "${CMAKE_SOURCE_DIR}/Core/Tools/CompileShaders/compile_shaders.cpp")

# On a non-Windows host the tool is a Windows executable like everything else this
# build produces, so it runs under Wine -- which the Linux build environment already
# installs to run CMake, ninja and the compiler itself. The override is what makes
# the arrangement trustworthy: without it Wine answers a request for d3dcompiler_47
# with its own builtin (vkd3d-shader), quietly compiling with a different compiler.
# The tool checks for that too and refuses, so the two guards have to fail together
# for a bad build to get through.
set(_rts_shader_launcher "")
if(NOT CMAKE_HOST_WIN32)
    find_program(WINE_EXECUTABLE NAMES wine wine64)
    if(NOT WINE_EXECUTABLE)
        message(FATAL_ERROR
            "Wine is required to compile the HLSL shaders on this host, and was not found. "
            "Install wine, or set WINE_EXECUTABLE.")
    endif()
    set(_rts_shader_launcher ${CMAKE_COMMAND} -E env "WINEDLLOVERRIDES=d3dcompiler_47=n" ${WINE_EXECUTABLE})
endif()

set(RTS_SHADER_SRC_DIR "${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Source/W3DDevice/GameClient/Shaders")
set(RTS_SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/shaders" CACHE INTERNAL "Compiled shader output directory")

# The same sources compiled again at Shader Model 4. This was "checked, not shipped" for
# one phase: it existed so that "these shaders still compile as model 4" was a fact the
# build checked on every edit rather than a claim someone re-verified by hand when a
# second backend eventually arrived. The second backend has arrived, so the directory is
# installed alongside the model 3 one now, and W3DShaderManager::LoadAndCreateD3DShader
# picks between the two from the active backend. D3D9 still cannot use model 4 bytecode
# and still never loads a byte of it.
#
# A shader model is not a formatting difference. VPOS is the integer pixel coordinate in
# ps_3_0 and the pixel *centre* in SV_Position, tex2Dlod hides its level in a fourth
# component that SampleLevel takes as an argument, and SM4 defaults to column-major where
# these matrices are explicitly row-major. Each of those compiles clean and draws the wrong
# thing. Compiling both ways every build is what turns them from things to remember into
# things that fail loudly.
set(RTS_SHADER_SM4_OUT_DIR "${CMAKE_BINARY_DIR}/shaders-sm4" CACHE INTERNAL "Compiled Shader Model 4 output directory")

# Shaders to compile. The pipeline stage (and therefore the target profile and the
# output extension) is derived from the "_vs"/"_ps" suffix of each name.
set(_rts_shaders
    unit_vs
    # Same body as unit_vs, declaring the mesh's second coordinate set as well
    unit_uv2_vs
    unit_prelit_vs
    unit_ps
    unit_detail_ps
    terrain_vs
    terrain_ps
    road_vs
    road_ps
    water_vs
    water_ps
    unit_pbr_vs
    unit_pbr_ps
    # 2D interface (control bar, command bar, text)
    ui_vs
    ui_ps
    # Screen-space quads for the post-process chain, replacing D3DFVF_XYZRHW
    screenquad_vs
    # Projected alpha mask (screen cross-fade wipe, wireframe preview)
    mask_vs
    mask_ps
    # Bloom post-process (fullscreen pixel shaders)
    bloom_bright_ps
    bloom_blur_ps
    bloom_composite_ps
    # Trees, grass and bushes (wind sway + shroud)
    tree_vs
    tree_ps
    # Open-sea water (WaterType = 2), and the black-and-white screen filter
    wave_vs
    wave_ps
    bwfilter_ps
    # Legacy multitexture water combiners, taken when the programmable water path is
    # declined; both were ps_1_1 assembled at runtime until the assembler went away
    waterriver_ps
    watertrapezoid_ps
    # Frame capture for the Tracy profiler (RTS_PROFILE_TRACY only)
    profilerswizzle_ps
    # HDR scene -> 8-bit scene texture, at the end of render-to-texture
    tonemap_ps
    # Shadow-map depth pass
    shadowdepth_vs
    shadowdepth_ps
    # In-game debug visualizations (RTS_DEBUG only; see WW3D2/debugvis.h)
    debugtint_ps
    debugdepth_ps
    debugshadow_ps
    debugbloom_ps
    debugshroud_ps
    debugnormal_vs
    debugnormal_ps
    # ... and its particle-sprite variant (vertex alpha + dithered coverage)
    shadowdepthparticle_vs
    shadowdepthparticle_ps
)

file(MAKE_DIRECTORY "${RTS_SHADER_OUT_DIR}")
file(MAKE_DIRECTORY "${RTS_SHADER_SM4_OUT_DIR}")
set(_rts_shader_outputs "")
# Shared headers (tonemap.hlsli and friends). The compiler resolves #include relative to
# the including file, so nothing has to be passed on the command line -- but the build has
# no way to know a shader depends on one, and editing a curve that several shaders share
# while none of them rebuild is a genuinely confusing way to lose an afternoon. Every
# shader simply depends on all of them; there are few enough that the over-rebuild costs
# less than the mistake.
file(GLOB _rts_shader_headers "${RTS_SHADER_SRC_DIR}/*.hlsli")
foreach(_name ${_rts_shaders})
    # Everything targets Shader Model 3. The PBR path always needed it (instruction
    # count, registers, ddx/ddy), and holding the rest at 2_0 bought nothing but
    # limits: the terrain shadow filter had to be cut to a 2x2 box to fit the ps_2_0
    # arithmetic slots, which is precisely what made cast shadows stair-step on the
    # ground. D3D9 also forbids mixing model 3 and model 2 across a vs/ps pair, so
    # moving any pixel shader up drags its vertex shader with it regardless.
    set(_model "3_0")
    if(_name MATCHES "_vs$")
        set(_profile "vs_${_model}")
        set(_ext "vso")
    else()
        set(_profile "ps_${_model}")
        set(_ext "pso")
    endif()
    set(_src "${RTS_SHADER_SRC_DIR}/${_name}.hlsl")
    set(_out "${RTS_SHADER_OUT_DIR}/${_name}.${_ext}")
    # One invocation per shader, so that editing one shader recompiles one shader.
    add_custom_command(
        OUTPUT "${_out}"
        COMMAND ${_rts_shader_launcher} $<TARGET_FILE:compile_shaders> "${RTS_D3DCOMPILER_DLL}" "${_src}" "${_out}" ${_profile}
        DEPENDS "${_src}" ${_rts_shader_headers} compile_shaders
        COMMENT "hlsl ${_name}.hlsl -> ${_name}.${_ext} (${_profile})"
        VERBATIM
    )
    list(APPEND _rts_shader_outputs "${_out}")

    # ...and the same source at model 4. RTS_SHADER_MODEL is passed only here: the shaders
    # default to model 3 when it is undefined, so the invocation above keeps the argument
    # list it has always had and its bytecode stays comparable byte for byte with the build
    # before any of this. A define that is only ever absent from a compile cannot change it.
    set(_sm4_profile "ps_4_0")
    if(_name MATCHES "_vs$")
        set(_sm4_profile "vs_4_0")
    endif()
    set(_sm4_out "${RTS_SHADER_SM4_OUT_DIR}/${_name}.sm4")
    add_custom_command(
        OUTPUT "${_sm4_out}"
        COMMAND ${_rts_shader_launcher} $<TARGET_FILE:compile_shaders> "${RTS_D3DCOMPILER_DLL}" "${_src}" "${_sm4_out}" ${_sm4_profile} RTS_SHADER_MODEL=4
        DEPENDS "${_src}" ${_rts_shader_headers} compile_shaders
        COMMENT "hlsl ${_name}.hlsl -> ${_sm4_profile} (checked, not shipped)"
        VERBATIM
    )
    list(APPEND _rts_shader_outputs "${_sm4_out}")
endforeach()
add_custom_target(rts_shaders ALL DEPENDS ${_rts_shader_outputs})

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
    # The display gamma/brightness/contrast ramp, applied to the finished frame. This is
    # what SetDeviceGammaRamp used to do at scanout; D3D11 has no windowed equivalent.
    gamma_ps
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
set(_rts_shader_outputs "")
# Shared headers (tonemap.hlsli and friends). The compiler resolves #include relative to
# the including file, so nothing has to be passed on the command line -- but the build has
# no way to know a shader depends on one, and editing a curve that several shaders share
# while none of them rebuild is a genuinely confusing way to lose an afternoon. Every
# shader simply depends on all of them; there are few enough that the over-rebuild costs
# less than the mistake.
file(GLOB _rts_shader_headers "${RTS_SHADER_SRC_DIR}/*.hlsli")
foreach(_name ${_rts_shaders})
    # The pipeline stage decides the profile; RTS_SHADER_MODEL=5 is what the .hlsl files
    # compile with for SV_Position, SampleLevel and row-major matrices.
    set(_profile "ps_5_0")
    if(_name MATCHES "_vs$")
        set(_profile "vs_5_0")
    endif()
    set(_src "${RTS_SHADER_SRC_DIR}/${_name}.hlsl")
    set(_out "${RTS_SHADER_OUT_DIR}/${_name}.sm5")
    # One invocation per shader, so that editing one shader recompiles one shader.
    add_custom_command(
        OUTPUT "${_out}"
        COMMAND ${_rts_shader_launcher} $<TARGET_FILE:compile_shaders> "${RTS_D3DCOMPILER_DLL}" "${_src}" "${_out}" ${_profile} RTS_SHADER_MODEL=5
        DEPENDS "${_src}" ${_rts_shader_headers} compile_shaders
        COMMENT "hlsl ${_name}.hlsl -> ${_name}.sm5 (${_profile})"
        VERBATIM
    )
    list(APPEND _rts_shader_outputs "${_out}")
endforeach()
add_custom_target(rts_shaders ALL DEPENDS ${_rts_shader_outputs})

# Compiles the HLSL shaders used by the programmable (D3D9) render path into
# D3D9 bytecode with fxc, and stages them under ${CMAKE_BINARY_DIR}/shaders.
# The game loads them at runtime as loose files (e.g. "shaders\\unit_vs.vso");
# the per-game install() rules copy this directory next to the executable.

# Locate fxc from the Windows SDK.
find_program(FXC_EXECUTABLE
    NAMES fxc fxc.exe
    HINTS
        "$ENV{WindowsSdkVerBinPath}x86"
        "$ENV{WindowsSdkDir}bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x86"
    PATHS
        "C:/Program Files (x86)/Windows Kits/10/bin"
    PATH_SUFFIXES
        x86
    DOC "DirectX shader compiler (fxc.exe)"
)

if(NOT FXC_EXECUTABLE)
    # Fall back to the newest fxc under the default Windows Kits location.
    file(GLOB _fxc_candidates "C:/Program Files (x86)/Windows Kits/10/bin/*/x86/fxc.exe")
    if(_fxc_candidates)
        list(SORT _fxc_candidates)
        list(GET _fxc_candidates -1 FXC_EXECUTABLE)
    endif()
endif()

set(RTS_SHADER_SRC_DIR "${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Source/W3DDevice/GameClient/Shaders")
set(RTS_SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/shaders" CACHE INTERNAL "Compiled shader output directory")

# Shaders to compile. The pipeline stage (and therefore the fxc profile and the
# output extension) is derived from the "_vs"/"_ps" suffix of each name.
set(_rts_shaders
    unit_vs
    unit_prelit_vs
    unit_ps
    unit_detail_ps
    terrain_vs
    terrain_ps
    unit_pbr_vs
    unit_pbr_ps
    # Bloom post-process (fullscreen pixel shaders; ps_2_0)
    bloom_bright_ps
    bloom_blur_ps
    bloom_composite_ps
    # Shadow-map depth pass
    shadowdepth_vs
    shadowdepth_ps
)

if(FXC_EXECUTABLE)
    file(MAKE_DIRECTORY "${RTS_SHADER_OUT_DIR}")
    set(_rts_shader_outputs "")
    foreach(_name ${_rts_shaders})
        # PBR shaders need Shader Model 3 (more instructions/registers, ddx/ddy);
        # the rest target Shader Model 2.
        if(_name MATCHES "pbr")
            set(_model "3_0")
        else()
            set(_model "2_0")
        endif()
        if(_name MATCHES "_vs$")
            set(_profile "vs_${_model}")
            set(_ext "vso")
        else()
            set(_profile "ps_${_model}")
            set(_ext "pso")
        endif()
        set(_src "${RTS_SHADER_SRC_DIR}/${_name}.hlsl")
        set(_out "${RTS_SHADER_OUT_DIR}/${_name}.${_ext}")
        add_custom_command(
            OUTPUT "${_out}"
            COMMAND "${FXC_EXECUTABLE}" /nologo /T ${_profile} /E main /Fo "${_out}" "${_src}"
            DEPENDS "${_src}"
            COMMENT "fxc ${_name}.hlsl -> ${_name}.${_ext} (${_profile})"
            VERBATIM
        )
        list(APPEND _rts_shader_outputs "${_out}")
    endforeach()
    add_custom_target(rts_shaders ALL DEPENDS ${_rts_shader_outputs})
else()
    message(WARNING "fxc.exe not found; HLSL shaders will not be compiled. "
                    "The programmable render path will fall back to fixed-function at runtime.")
endif()

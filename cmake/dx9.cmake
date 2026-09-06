FetchContent_Declare(
    dx9
    GIT_REPOSITORY https://github.com/hrydgard/minidx9.git
    GIT_TAG        master
)

FetchContent_MakeAvailable(dx9)

# Remove the colliding Dcommon.h from minidx9 to use the correct Windows SDK dcommon.h
file(REMOVE ${dx9_SOURCE_DIR}/Include/Dcommon.h)

# Write a compatibility d3dx8math.h redirecting to d3dx9math.h for D3D9 compilation
file(WRITE ${dx9_SOURCE_DIR}/Include/d3dx8math.h "#pragma once\n#include <d3dx9math.h>\n")
file(WRITE ${dx9_SOURCE_DIR}/Include/d3dx8tex.h "#pragma once\n#include <d3dx9tex.h>\n")
file(WRITE ${dx9_SOURCE_DIR}/Include/d3dx8.h "#pragma once\n#include <d3d9.h>\n#include <d3dx9.h>\n")


add_library(d3d9lib INTERFACE)
target_include_directories(d3d9lib INTERFACE ${dx9_SOURCE_DIR}/Include ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
target_link_libraries(d3d9lib INTERFACE
    ${dx9_SOURCE_DIR}/Lib/x86/d3d9.lib
    ${dx9_SOURCE_DIR}/Lib/x86/d3dx9.lib
)

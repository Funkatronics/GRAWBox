cmake_minimum_required(VERSION 3.25)
project(grawbox_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ReXGlue bd833a2 with pgr4-native's patches 0021 and 0022: the Xenos headers and the DXBC shader
# translator the renderer is written against (BSD-3-Clause).
set(GRAWBOX_XENOS_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../.tools/rexglue-bd833a2" CACHE PATH "")
set(gfx "${GRAWBOX_XENOS_SDK}/src/graphics")

add_library(grawbox_native SHARED)
file(GLOB renderer CONFIGURE_DEPENDS src/*.cpp)
target_sources(grawbox_native PRIVATE
  ${renderer}
  ${GRAWBOX_XENOS_SDK}/src/core/string_buffer.cpp
  ${GRAWBOX_XENOS_SDK}/src/core/memory.cpp
  ${gfx}/xenos.cpp
  ${GRAWBOX_XENOS_SDK}/src/core/math_msvc.cpp
  ${gfx}/format/ucode.cpp
  ${gfx}/register_file.cpp
  ${gfx}/pipeline/shader/shader.cpp
  ${gfx}/pipeline/shader/translator.cpp
  ${gfx}/pipeline/shader/translator_disasm.cpp
  ${gfx}/pipeline/shader/dxbc.cpp
  ${gfx}/pipeline/shader/dxbc_translator.cpp
  ${gfx}/pipeline/shader/dxbc_translator_alu.cpp
  ${gfx}/pipeline/shader/dxbc_translator_fetch.cpp
  ${gfx}/pipeline/shader/dxbc_translator_memexport.cpp
  ${gfx}/pipeline/shader/dxbc_translator_om.cpp
  ${gfx}/pipeline/texture/info.cpp
  ${gfx}/pipeline/texture/info_formats.cpp
  ${gfx}/pipeline/texture/extent.cpp
  ${gfx}/pipeline/texture/util.cpp
  ${GRAWBOX_XENOS_SDK}/thirdparty/dxbc/DXBCChecksum.cpp
)
target_include_directories(grawbox_native PRIVATE
  compat include src ${GRAWBOX_XENOS_SDK}/include ${GRAWBOX_XENOS_SDK} ${GRAWBOX_THIRDPARTY}/xxHash ${GRAWBOX_THIRDPARTY}/simde ${GRAWBOX_THIRDPARTY}/spdlog/include
)
target_compile_definitions(grawbox_native PRIVATE
  GRAWBOX_NATIVE_BUILD NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
)
target_compile_options(grawbox_native PRIVATE -Wno-everything -ferror-limit=12 -mavx2 -mfma -mbmi -mf16c)
target_link_libraries(grawbox_native PRIVATE d3d12 dxgi dxguid d3dcompiler user32 gdi32 winmm dwmapi)

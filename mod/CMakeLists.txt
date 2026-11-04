cmake_minimum_required(VERSION 3.20)
project(BioShockAP LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The game is 32-bit: a 64-bit DLL would never be loaded.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Build for Win32: cmake -A Win32")
endif()

add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
# Static CRT: the DLL needs no Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# One version number per release, the apworld's: also in the log, the client and the DLL's properties.
set(BSAP_MANIFEST_PATH "${CMAKE_SOURCE_DIR}/../apworld/bioshock_infinite/archipelago.json")
file(READ "${BSAP_MANIFEST_PATH}" BSAP_MANIFEST)
string(JSON BSAP_VERSION GET "${BSAP_MANIFEST}" world_version)
string(REPLACE "." "," BSAP_VERSION_COMMAS "${BSAP_VERSION}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BSAP_MANIFEST_PATH}")
configure_file(src/version.rc.in "${CMAKE_BINARY_DIR}/version.rc" @ONLY)

add_library(BioShockAP SHARED
    "${CMAKE_BINARY_DIR}/version.rc"
    extern/minhook/src/buffer.c
    extern/minhook/src/hook.c
    extern/minhook/src/trampoline.c
    extern/minhook/src/hde/hde32.c
    extern/miniz/miniz.c
    src/dllmain.cpp
    src/xinput_proxy.cpp
    src/bridge.cpp
    src/ue3.cpp
    src/search.cpp
    src/engine_call.cpp
    src/trace.cpp
    src/pe_hook.cpp
    src/shop.cpp
    src/game_alloc.cpp
    src/world.cpp
    src/collect.cpp
    src/inventory.cpp
    src/savedata.cpp
    src/traps.cpp
    src/tears.cpp
    src/lockpick.cpp
    src/areas.cpp
    src/vigors.cpp
    src/overlay.cpp
    src/ui_fix.cpp
    src/ap_texture.cpp
    src/ap_net.cpp
    src/ap_client.cpp
    src/ap_menu.cpp
    src/ap_notify.cpp
)

# Shipped as xinput1_3.dll; the .def keeps the ordinals the game imports (2 and 3).
set_target_properties(BioShockAP PROPERTIES
    OUTPUT_NAME "xinput1_3"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)
target_link_options(BioShockAP PRIVATE "/DEF:${CMAKE_SOURCE_DIR}/src/xinput1_3.def")
target_include_directories(BioShockAP PRIVATE extern/minhook/include extern/miniz extern)
# Third-party code is compiled as is: its warnings are not ours to fix.
set_source_files_properties(extern/minhook/src/buffer.c extern/minhook/src/hook.c extern/minhook/src/trampoline.c
    extern/minhook/src/hde/hde32.c PROPERTIES COMPILE_OPTIONS "/W0")
# miniz: zlib (de)compression only, no files or zip archives.
set_source_files_properties(extern/miniz/miniz.c PROPERTIES COMPILE_DEFINITIONS "MINIZ_NO_STDIO;MINIZ_NO_ARCHIVE_APIS;MINIZ_NO_TIME" COMPILE_OPTIONS "/W0")
target_link_libraries(BioShockAP PRIVATE ws2_32 winhttp normaliz)
target_compile_definitions(BioShockAP PRIVATE BSAP_VERSION="${BSAP_VERSION}")
# Our code builds without any warning at the highest level: a new warning fails the build.
target_compile_options(BioShockAP PRIVATE /W4 /WX /utf-8)

# Test bench of the Archipelago loop without the game (tests/loop_test.cpp):
#   cmake --build build --config Release --target loop_test
add_executable(loop_test EXCLUDE_FROM_ALL tests/loop_test.cpp src/ap_client.cpp src/ap_net.cpp)
set_target_properties(loop_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/tests")
target_include_directories(loop_test PRIVATE extern)
target_link_libraries(loop_test PRIVATE winhttp normaliz)
target_compile_definitions(loop_test PRIVATE BSAP_VERSION="${BSAP_VERSION}")
target_compile_options(loop_test PRIVATE /W4 /WX /utf-8)

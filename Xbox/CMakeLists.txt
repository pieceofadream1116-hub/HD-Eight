# HD Eight: Mesen's emulation core in a standalone UWP app for Xbox Dev Mode (and Windows).
#
# Configure with the Visual Studio generator for WindowsStore, after building SDL3-uwp into ../out/uwp-deps
# (see Xbox/build.ps1, which does both):
#   cmake -S Xbox -B out/xbox -G "Visual Studio 17 2022" -A x64 -DCMAKE_SYSTEM_NAME=WindowsStore
#         -DCMAKE_SYSTEM_VERSION=10.0 -DCMAKE_PREFIX_PATH=out/uwp-deps
cmake_minimum_required(VERSION 3.24)
project(HDEight VERSION 0.1.0 LANGUAGES C CXX)

if (NOT CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
    message(FATAL_ERROR "HD Eight's Xbox build is a UWP app: configure with -DCMAKE_SYSTEM_NAME=WindowsStore")
endif ()

set(HDEIGHT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(HDEight_PACKAGE_VERSION "" CACHE STRING "Four-part package version (e.g. 0.1.0.12); CI sets it from the run number")

# Visual Studio's UWP projects turn on SDL checks (/sdl), which make CRT deprecation warnings errors
add_compile_options(/sdl-)
# Xbox Series X|S (Zen 2): AVX2 helps the HD filters (xBRZ, HQx) keep up at 60 frames a second
add_compile_options(/arch:AVX2)

include(FetchContent)

## Dear ImGui (menus)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)

find_package(SDL3 REQUIRED CONFIG)

## Mesen's sources, compiled the same way as the Linux makefile does (every .c/.cpp in these folders)
file(GLOB_RECURSE HDEIGHT_CORE_SOURCES CONFIGURE_DEPENDS "${HDEIGHT_ROOT}/Core/*.cpp")
file(GLOB_RECURSE HDEIGHT_UTIL_SOURCES CONFIGURE_DEPENDS "${HDEIGHT_ROOT}/Utilities/*.cpp" "${HDEIGHT_ROOT}/Utilities/*.c")
file(GLOB_RECURSE HDEIGHT_7Z_SOURCES CONFIGURE_DEPENDS "${HDEIGHT_ROOT}/SevenZip/*.c")
file(GLOB_RECURSE HDEIGHT_LUA_SOURCES CONFIGURE_DEPENDS "${HDEIGHT_ROOT}/Lua/*.c")
# The Unix-only luasocket files (Mesen's Windows project leaves them out too)
list(FILTER HDEIGHT_LUA_SOURCES EXCLUDE REGEX "/(serial|unix|unixdgram|unixstream|usocket)\\.c$")

set(HDEIGHT_WARNINGS_OFF /W0 /wd4996)
set(HDEIGHT_COMMON_DEFS NOMINMAX _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS HDEIGHT_UWP MESENOPTIMIZE)

add_library(mesen_c STATIC ${HDEIGHT_7Z_SOURCES} ${HDEIGHT_LUA_SOURCES})
target_include_directories(mesen_c PRIVATE "${HDEIGHT_ROOT}" "${HDEIGHT_ROOT}/Lua")
target_compile_definitions(mesen_c PRIVATE ${HDEIGHT_COMMON_DEFS})
target_compile_options(mesen_c PRIVATE ${HDEIGHT_WARNINGS_OFF})

add_library(mesen_core STATIC ${HDEIGHT_UTIL_SOURCES} ${HDEIGHT_CORE_SOURCES})
target_include_directories(mesen_core PUBLIC "${HDEIGHT_ROOT}" "${HDEIGHT_ROOT}/Core" "${HDEIGHT_ROOT}/Utilities")
target_compile_definitions(mesen_core PUBLIC ${HDEIGHT_COMMON_DEFS})
target_compile_options(mesen_core PRIVATE ${HDEIGHT_WARNINGS_OFF} /bigobj /MP /utf-8)
set_target_properties(mesen_core PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON VS_WINRT_COMPONENT FALSE)
target_link_libraries(mesen_core PUBLIC mesen_c ws2_32)

## The app
add_executable(HDEight WIN32
    src/main.cpp
    src/App.cpp
    src/App.h
    src/XboxRenderer.cpp
    src/XboxRenderer.h
    src/XboxAudio.cpp
    src/XboxAudio.h
    src/XboxInput.cpp
    src/XboxInput.h
    src/Covers.cpp
    src/Covers.h
    src/Library.cpp
    src/Library.h
    src/Settings.cpp
    src/Settings.h
    src/UwpPlatform.cpp
    src/UwpPlatform.h
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp"
)
target_include_directories(HDEight PRIVATE src "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
target_link_libraries(HDEight PRIVATE mesen_core SDL3::SDL3 xaudio2)
set_target_properties(HDEight PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_compile_options(HDEight PRIVATE /utf-8 /MP /W3 /wd4996 /EHsc)
# 16 MB stacks for every thread (Mesen's threads use the default), and a map file to look up crash spots
target_link_options(HDEight PRIVATE /STACK:16777216 "/MAP:${CMAKE_BINARY_DIR}/HDEight.map")

## UWP package
if (HDEight_PACKAGE_VERSION)
    set(HDEIGHT_VERSION4 "${HDEight_PACKAGE_VERSION}")
else ()
    set(HDEIGHT_VERSION4 "${PROJECT_VERSION}.0")
endif ()
message(STATUS "HD Eight: package version ${HDEIGHT_VERSION4}")
target_compile_definitions(HDEight PRIVATE "HDEIGHT_VERSION=\"${HDEIGHT_VERSION4}\"")

configure_file(Package.appxmanifest.in "${CMAKE_CURRENT_BINARY_DIR}/Package.appxmanifest" @ONLY)
target_sources(HDEight PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Package.appxmanifest")

file(GLOB HDEIGHT_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/Assets/*.png")
target_sources(HDEight PRIVATE ${HDEIGHT_ASSETS})
set_source_files_properties(${HDEIGHT_ASSETS} PROPERTIES VS_DEPLOYMENT_CONTENT 1 VS_DEPLOYMENT_LOCATION "Assets")

# Font, plus Mesen's NES game database (mappers, boards) and its Game Genie code database
set(HDEIGHT_RES
    "${CMAKE_CURRENT_SOURCE_DIR}/res/SplineSans-Bold.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/res/SplineSans-OFL.txt"
    "${HDEIGHT_ROOT}/UI/Dependencies/MesenNesDB.txt"
    "${HDEIGHT_ROOT}/UI/Dependencies/Internal/CheatDb.Nes.json")
target_sources(HDEight PRIVATE ${HDEIGHT_RES})
set_source_files_properties(${HDEIGHT_RES} PROPERTIES VS_DEPLOYMENT_CONTENT 1 VS_DEPLOYMENT_LOCATION "res")

set(HDEIGHT_CERT "${CMAKE_CURRENT_BINARY_DIR}/HDEight_TemporaryKey.pfx")
configure_file("${CMAKE_ROOT}/Templates/Windows/Windows_TemporaryKey.pfx" "${HDEIGHT_CERT}" COPYONLY)
target_sources(HDEight PRIVATE "${HDEIGHT_CERT}")

get_target_property(HDEIGHT_SDL_DLL SDL3::SDL3-shared IMPORTED_LOCATION_RELEASE)
if (NOT HDEIGHT_SDL_DLL)
    get_target_property(HDEIGHT_SDL_DLL SDL3::SDL3-shared IMPORTED_LOCATION)
endif ()
if (NOT HDEIGHT_SDL_DLL OR NOT EXISTS "${HDEIGHT_SDL_DLL}")
    message(FATAL_ERROR "SDL3.dll not found; build SDL3-uwp first (Xbox/build.ps1)")
endif ()
target_sources(HDEight PRIVATE "${HDEIGHT_SDL_DLL}")
set_source_files_properties("${HDEIGHT_SDL_DLL}" PROPERTIES VS_DEPLOYMENT_CONTENT 1)

set_target_properties(HDEight PROPERTIES VS_WINDOWS_TARGET_PLATFORM_MIN_VERSION "10.0.17763.0")

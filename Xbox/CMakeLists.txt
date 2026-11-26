# Genesis: Genesis Plus GX (Sega Genesis / Mega Drive, Sega CD, Master System, Game Gear) in a standalone UWP app
# for Xbox Dev Mode (and Windows), made the same way as HD Eight.
#
# Configure with the Visual Studio generator for WindowsStore, after building SDL3-uwp into ../out/uwp-deps:
#   cmake -S Xbox -B out/xbox -G "Visual Studio 17 2022" -A x64 -DCMAKE_SYSTEM_NAME=WindowsStore
#         -DCMAKE_SYSTEM_VERSION=10.0 -DCMAKE_PREFIX_PATH=out/uwp-deps
cmake_minimum_required(VERSION 3.24)
project(GenesisXbox VERSION 0.1.0 LANGUAGES C CXX)

if (NOT CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
    message(FATAL_ERROR "The Xbox build is a UWP app: configure with -DCMAKE_SYSTEM_NAME=WindowsStore")
endif ()

set(APP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(APP_PACKAGE_VERSION "" CACHE STRING "Four-part package version (e.g. 0.1.0.12); CI sets it from the run number")

# Visual Studio's UWP projects turn on SDL checks (/sdl), which make CRT deprecation warnings errors
add_compile_options(/sdl-)
# Xbox Series X|S (Zen 2)
add_compile_options(/arch:AVX2)

include(FetchContent)

## Dear ImGui (menus)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)

find_package(SDL3 REQUIRED CONFIG)

## Genesis Plus GX, built as a libretro core and linked straight into the app (the file list of its
## libretro/Makefile.common, with HAVE_CHD for Sega CD .chd discs)
set(GPGX "${APP_ROOT}")
set(GPGX_DEPS "${GPGX}/libretro/deps")
set(GPGX_COMMON "${GPGX}/libretro/libretro-common")
set(GPGX_DIRS core core/z80 core/m68k core/ntsc core/sound core/input_hw core/cd_hw core/cart_hw core/cart_hw/svp)
set(GPGX_SOURCES "${GPGX}/libretro/libretro.c")
foreach(dir ${GPGX_DIRS} core/sound/tremor)
    file(GLOB dir_sources "${GPGX}/${dir}/*.c")
    list(APPEND GPGX_SOURCES ${dir_sources})
endforeach()
list(APPEND GPGX_SOURCES
    "${GPGX_DEPS}/lzma-19.00/src/Alloc.c" "${GPGX_DEPS}/lzma-19.00/src/Bra86.c" "${GPGX_DEPS}/lzma-19.00/src/BraIA64.c"
    "${GPGX_DEPS}/lzma-19.00/src/CpuArch.c" "${GPGX_DEPS}/lzma-19.00/src/Delta.c" "${GPGX_DEPS}/lzma-19.00/src/LzFind.c"
    "${GPGX_DEPS}/lzma-19.00/src/Lzma86Dec.c" "${GPGX_DEPS}/lzma-19.00/src/LzmaDec.c" "${GPGX_DEPS}/lzma-19.00/src/LzmaEnc.c"
    "${GPGX_DEPS}/lzma-19.00/src/Sort.c"
    "${GPGX_DEPS}/libchdr/src/libchdr_bitstream.c" "${GPGX_DEPS}/libchdr/src/libchdr_cdrom.c" "${GPGX_DEPS}/libchdr/src/libchdr_chd.c"
    "${GPGX_DEPS}/libchdr/src/libchdr_flac.c" "${GPGX_DEPS}/libchdr/src/libchdr_huffman.c"
    "${GPGX_DEPS}/zstd/lib/common/entropy_common.c" "${GPGX_DEPS}/zstd/lib/common/error_private.c"
    "${GPGX_DEPS}/zstd/lib/common/fse_decompress.c" "${GPGX_DEPS}/zstd/lib/common/zstd_common.c"
    "${GPGX_DEPS}/zstd/lib/common/xxhash.c" "${GPGX_DEPS}/zstd/lib/decompress/huf_decompress.c"
    "${GPGX_DEPS}/zstd/lib/decompress/zstd_ddict.c" "${GPGX_DEPS}/zstd/lib/decompress/zstd_decompress.c"
    "${GPGX_DEPS}/zstd/lib/decompress/zstd_decompress_block.c"
    "${GPGX_DEPS}/zlib-1.2.11/adler32.c" "${GPGX_DEPS}/zlib-1.2.11/crc32.c" "${GPGX_DEPS}/zlib-1.2.11/inffast.c"
    "${GPGX_DEPS}/zlib-1.2.11/inflate.c" "${GPGX_DEPS}/zlib-1.2.11/inftrees.c" "${GPGX_DEPS}/zlib-1.2.11/zutil.c"
    # libretro-common, minus vfs_implementation.c: the app has its own (src/UwpVfs.cpp)
    "${GPGX_COMMON}/streams/file_stream.c" "${GPGX_COMMON}/streams/file_stream_transforms.c"
    "${GPGX_COMMON}/compat/fopen_utf8.c" "${GPGX_COMMON}/compat/compat_snprintf.c" "${GPGX_COMMON}/compat/compat_strl.c"
    "${GPGX_COMMON}/compat/compat_strcasestr.c" "${GPGX_COMMON}/compat/compat_posix_string.c"
    "${GPGX_COMMON}/encodings/encoding_utf.c" "${GPGX_COMMON}/file/file_path.c" "${GPGX_COMMON}/file/retro_dirent.c"
    "${GPGX_COMMON}/lists/string_list.c" "${GPGX_COMMON}/lists/dir_list.c" "${GPGX_COMMON}/memmap/memalign.c"
    "${GPGX_COMMON}/string/stdstring.c")

add_library(gpgx_core STATIC ${GPGX_SOURCES} src/UwpVfs.cpp)
set(GPGX_INCLUDES "${GPGX}/libretro" "${GPGX_COMMON}/include" "${GPGX_DEPS}/libchdr/include" "${GPGX_DEPS}/lzma-19.00/include"
    "${GPGX_DEPS}/zlib-1.2.11" "${GPGX_DEPS}/zstd/lib")
foreach(dir ${GPGX_DIRS} core/sound/minimp3)
    list(APPEND GPGX_INCLUDES "${GPGX}/${dir}")
endforeach()
target_include_directories(gpgx_core PUBLIC ${GPGX_INCLUDES})
target_compile_definitions(gpgx_core PRIVATE
    LSB_FIRST BYTE_ORDER=LITTLE_ENDIAN HAVE_ZLIB __LIBRETRO__ M68K_OVERCLOCK_SHIFT=20 Z80_OVERCLOCK_SHIFT=20
    HAVE_YM3438_CORE HAVE_OPLL_CORE MAXROMSIZE=33554432 "INLINE=static __inline" USE_LIBTREMOR USE_LIBCHDR
    _7ZIP_ST ZSTD_DISABLE_ASM USE_16BPP_RENDERING FRONTEND_SUPPORTS_RGB565 _CRT_SECURE_NO_DEPRECATE
    _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
target_compile_options(gpgx_core PRIVATE /W0 /wd4996 /MP)
set_target_properties(gpgx_core PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON VS_WINRT_COMPONENT FALSE)

## The app
add_executable(Genesis WIN32
    src/main.cpp
    src/App.cpp
    src/App.h
    src/Core.cpp
    src/Core.h
    src/XboxAudio.cpp
    src/XboxAudio.h
    src/XboxInput.cpp
    src/XboxInput.h
    src/Covers.cpp
    src/Covers.h
    src/KnownCheats.cpp
    src/KnownCheats.h
    src/Net.cpp
    src/Net.h
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
target_include_directories(Genesis PRIVATE src "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
target_link_libraries(Genesis PRIVATE gpgx_core SDL3::SDL3 xaudio2)
set_target_properties(Genesis PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_compile_options(Genesis PRIVATE /utf-8 /MP /W3 /wd4996 /EHsc)
target_compile_definitions(Genesis PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_options(Genesis PRIVATE /STACK:8388608 "/MAP:${CMAKE_BINARY_DIR}/Genesis.map")

## UWP package
if (APP_PACKAGE_VERSION)
    set(APP_VERSION4 "${APP_PACKAGE_VERSION}")
else ()
    set(APP_VERSION4 "${PROJECT_VERSION}.0")
endif ()
message(STATUS "Genesis: package version ${APP_VERSION4}")
target_compile_definitions(Genesis PRIVATE "APP_VERSION=\"${APP_VERSION4}\"")

configure_file(Package.appxmanifest.in "${CMAKE_CURRENT_BINARY_DIR}/Package.appxmanifest" @ONLY)
target_sources(Genesis PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Package.appxmanifest")

file(GLOB APP_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/Assets/*.png")
target_sources(Genesis PRIVATE ${APP_ASSETS})
set_source_files_properties(${APP_ASSETS} PROPERTIES VS_DEPLOYMENT_CONTENT 1 VS_DEPLOYMENT_LOCATION "Assets")

# Font, and Genesis Plus GX's license (it must travel with the app)
set(APP_RES
    "${CMAKE_CURRENT_SOURCE_DIR}/res/SplineSans-Bold.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/res/SplineSans-OFL.txt"
    "${APP_ROOT}/LICENSE.txt")
target_sources(Genesis PRIVATE ${APP_RES})
set_source_files_properties(${APP_RES} PROPERTIES VS_DEPLOYMENT_CONTENT 1 VS_DEPLOYMENT_LOCATION "res")

set(APP_CERT "${CMAKE_CURRENT_BINARY_DIR}/Genesis_TemporaryKey.pfx")
configure_file("${CMAKE_ROOT}/Templates/Windows/Windows_TemporaryKey.pfx" "${APP_CERT}" COPYONLY)
target_sources(Genesis PRIVATE "${APP_CERT}")

get_target_property(APP_SDL_DLL SDL3::SDL3-shared IMPORTED_LOCATION_RELEASE)
if (NOT APP_SDL_DLL)
    get_target_property(APP_SDL_DLL SDL3::SDL3-shared IMPORTED_LOCATION)
endif ()
if (NOT APP_SDL_DLL OR NOT EXISTS "${APP_SDL_DLL}")
    message(FATAL_ERROR "SDL3.dll not found; build SDL3-uwp first (Xbox/build.ps1)")
endif ()
target_sources(Genesis PRIVATE "${APP_SDL_DLL}")
set_source_files_properties("${APP_SDL_DLL}" PROPERTIES VS_DEPLOYMENT_CONTENT 1)

set_target_properties(Genesis PROPERTIES VS_WINDOWS_TARGET_PLATFORM_MIN_VERSION "10.0.17763.0")

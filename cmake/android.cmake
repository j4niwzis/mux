# SPDX-License-Identifier: AGPL-3.0-only
# The generated toolchain deliberately uses CMAKE_SYSTEM_NAME=Linux; which
# Android ABI it builds for is read from android/abis.json, by the processor
# the toolchain says: the first word of the ABI's sysroot triple.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../android/abis.json" mux_abis)
string(JSON mux_abi_count LENGTH "${mux_abis}")
math(EXPR mux_abi_last "${mux_abi_count} - 1")
unset(MUX_ANDROID_ABI)
foreach(index RANGE ${mux_abi_last})
  string(JSON abi MEMBER "${mux_abis}" ${index})
  string(JSON triple GET "${mux_abis}" ${abi} target triple)
  string(JSON sysroot_triple ERROR_VARIABLE none GET "${mux_abis}" ${abi} target sysroot-triple)
  if(none)
    set(sysroot_triple "${triple}")
  endif()
  string(REGEX REPLACE "-.*$" "" processor "${sysroot_triple}")
  if(CMAKE_SYSTEM_PROCESSOR STREQUAL processor)
    set(MUX_ANDROID_ABI "${abi}")
    string(JSON mux_cargo_target GET "${mux_abis}" ${abi} rust)
  endif()
endforeach()
if(NOT DEFINED MUX_ANDROID_ABI)
  message(FATAL_ERROR "No Android ABI in android/abis.json is built for ${CMAKE_SYSTEM_PROCESSOR}")
endif()
# Cargo still needs the Android triple for the vodozemac static library.
# Built with --target, its bridge headers are under target/<triple>/, which
# cmake-everywhere looks in itself (0.2.41) for the port's CARGO_INCLUDE.
cme_declare_port(NAME vodozemac CARGO_TARGET ${mux_cargo_target} CARGO_INCLUDE cxxbridge)
# AOSP's headers declare getentropy even below its introduction in API 28.
# Keep OpenSSL's kernel getrandom path for those older Android targets.
cme_declare_port(NAME openssl PATCHES
  "${CMAKE_CURRENT_LIST_DIR}/patches/openssl-android-getentropy.patch")
set(MUX_ANDROID_NATIVE_APP_GLUE "${MANDK_ROOT}/src/ndk/sources/android/native_app_glue"
    CACHE PATH "AOSP native_app_glue source directory")
set(MUX_ANDROID_SDL_SOURCE_DIR "" CACHE PATH "Optional local checkout of the native SDL fork")
set(mux_sdl_source)
if(MUX_ANDROID_SDL_SOURCE_DIR)
  list(APPEND mux_sdl_source SOURCE_DIR "${MUX_ANDROID_SDL_SOURCE_DIR}")
endif()
cme_declare_port(NAME sdl3 PROVIDES SDL3 sdl3 VERSION 3.5.0
  GITHUB_REPOSITORY j4niwzis/SDL GIT_TAG db82ec729c1bc2d85bd29f0377b2866472837164
  ${mux_sdl_source} LICENSE Zlib TARGETS SDL3::SDL3)
# Static, in libmux.so: the DEX loads libmux.so (sdl-native-dex --library),
# whose JNI_OnLoad is SDL's (android/exports.map).
cme_options(sdl3 "SDL_ANDROID_NATIVE_ACTIVITY ON" "SDL_ANDROID_JAR OFF"
  "SDL_SHARED OFF" "SDL_STATIC ON" "SDL_INSTALL OFF" "SDL_VULKAN OFF"
  "SDL_ANDROID_NATIVE_APP_GLUE ${MUX_ANDROID_NATIVE_APP_GLUE}")
# A system SDL does not carry this fork's entry point or generated bridge.
set(CME_SYSTEM_SDL3 OFF)

# One library, optimised whole: every object -- mux's and each dependency's,
# which cmake-everywhere builds with these flags -- is LLVM bitcode, and the
# link of libmux.so is full LTO over all of it (not CMake's IPO switch, which
# is ThinLTO for Clang). Symbols hidden unless exported, and what libmux.so
# exports is android/exports.map.
string(APPEND CMAKE_C_FLAGS " -flto=full -fvisibility=hidden")
string(APPEND CMAKE_CXX_FLAGS " -flto=full -fvisibility=hidden -fvisibility-inlines-hidden")
string(APPEND CMAKE_SHARED_LINKER_FLAGS " -flto=full")
string(APPEND CMAKE_MODULE_LINKER_FLAGS " -flto=full")
string(APPEND CMAKE_EXE_LINKER_FLAGS " -flto=full")
# The link options of an Android library: 16 KB pages, nothing undefined,
# only android/exports.map exported, and no symbol table in what is shipped.
set(MUX_ANDROID_LINK_OPTIONS "LINKER:-z,max-page-size=16384" "LINKER:--no-undefined"
  "LINKER:--version-script=${CMAKE_CURRENT_LIST_DIR}/../android/exports.map" "LINKER:--strip-all")

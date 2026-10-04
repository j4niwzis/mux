# SPDX-License-Identifier: AGPL-3.0-only
# The generated toolchain deliberately uses CMAKE_SYSTEM_NAME=Linux;
# Cargo still needs the Android triple for the vodozemac static library.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  # With --target, Cargo creates a cache root under the target triple.
  # cxx-build puts its shared bridge headers there; CME's CARGO_INCLUDE
  # is relative to the outer Cargo target directory.
  cme_declare_port(NAME vodozemac CARGO_TARGET aarch64-linux-android
    CARGO_INCLUDE aarch64-linux-android/cxxbridge)
else()
  message(FATAL_ERROR "Mux's initial Android package supports arm64-v8a")
endif()
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
  GITHUB_REPOSITORY j4niwzis/SDL GIT_TAG 48e01de77edffe884a7a881dc4a3ea2081e62148
  ${mux_sdl_source} LICENSE Zlib TARGETS SDL3::SDL3)
cme_options(sdl3 "SDL_ANDROID_NATIVE_ACTIVITY ON" "SDL_ANDROID_JAR OFF"
  "SDL_SHARED ON" "SDL_STATIC OFF" "SDL_INSTALL OFF" "SDL_VULKAN OFF"
  "SDL_ANDROID_NATIVE_APP_GLUE ${MUX_ANDROID_NATIVE_APP_GLUE}")
# A system SDL does not carry this fork's entry point or generated bridge.
set(CME_SYSTEM_SDL3 OFF)

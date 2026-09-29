cmake_minimum_required(VERSION 4.3.4 FATAL_ERROR)

# mux: one chat client for XMPP and Matrix, as C++26 modules.
#
#   mux.net     all I/O, on one io_context, run by fibers: TCP, TLS, DNS
#   mux.core    the model every protocol fills: accounts, conversations,
#               messages, presence -- and the changes to it
#   mux.xmpp    an XMPP account, through tern
#   mux.http    HTTPS requests from a fiber, with Beast
#   mux.matrix  a Matrix account, through loom
#   mux-cli     the network half on a terminal, until the window is there
#
# Every dependency comes through cmake-everywhere: the machine's copy where
# it has one with what is needed, and a pinned source where it has not.
include(${CMAKE_CURRENT_LIST_DIR}/cmake/import-std.cmake)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT COMMAND cme_declare_port AND NOT CMAKE_PROJECT_TOP_LEVEL_INCLUDES)
  set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/cmake/get_cme.cmake)
endif()

project(mux VERSION 0.1 LANGUAGES C CXX)

if(NOT COMMAND cme_provider)
  message(FATAL_ERROR
    "cmake-everywhere is not installed in this build, so nothing answers "
    "find_package. CMAKE_PROJECT_TOP_LEVEL_INCLUDES is only read by the "
    "top-level project() call: configure this directory, and not a "
    "subdirectory of it.")
endif()

option(MUX_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})
# The window: SDL3 and skiff, with Skia on GL.
option(MUX_UI "Build the window, and the mux program" ON)

# clang's -fconstexpr-steps: one value for mux and every library of the
# family under it. Each of them adds its own to what imports it, and CMake
# builds a module again for every set of flags it is imported with -- and
# the std module once for each of those. alef, chevron and tern said 1e8 and
# knot and loom 3e8, so a target importing both kinds had a set of flags
# none of them was built with, every module of theirs was built a second
# time, and the collation found std two ways ("Disagreement of the location
# of the 'std' module"). One value, set before any of them is read, and
# there is one set of flags.
set(MUX_CONSTEXPR_STEPS 300000000 CACHE STRING
  "clang's -fconstexpr-steps for mux and the family of libraries under it")
foreach(family IN ITEMS ALEF CHEVRON TERN KNOT LOOM)
  set(${family}_CONSTEXPR_STEPS ${MUX_CONSTEXPR_STEPS} CACHE STRING
    "clang's -fconstexpr-steps, as mux sets it for the whole family")
endforeach()

# The flags that decide which copy of std a target is compiled against are
# given as the libraries of the family give them: to what imports the
# library, not to the whole build. The std target itself is built without
# them, so everything that imports a module -- mux, its tests, googletest --
# is compiled against a std synthesized for its flags, one for all of them.
# Given to the whole build, the std target had them too, and a test that had
# no other difference was handed that one while googletest's interfaces
# named the synthesized one. The step count is the same number for all of
# the family (below), and alef's constant interpreter flag is on mux's
# modules as it is on alef's.

# The libraries of this family, each pinned to a commit.
cme_declare_port(NAME tern PROVIDES tern
  GITHUB_REPOSITORY j4niwzis/tern
  GIT_TAG eb6bc17dd8c7017a09a1fc645a5383cd6b90c7ba
  LICENSE AGPL-3.0-only
  SYSTEM NEVER
  TARGETS tern)
cme_options(tern "TERN_TESTS OFF" "TERN_EXAMPLES OFF")
cme_declare_port(NAME loom PROVIDES loom
  GITHUB_REPOSITORY j4niwzis/loom
  GIT_TAG c78b519b0c8cf26213134d3dc166725e3b9038b8
  LICENSE AGPL-3.0-only
  SYSTEM NEVER
  TARGETS loom)
cme_options(loom "LOOM_TESTS OFF")

# Boost: Asio for the sockets and the loop, Beast for Matrix's HTTP,
# Context for the fibers, and PFR as a module for what reads with it.
cme_find_package(Boost COMPONENTS asio beast pfr[modules] context REQUIRED)
cme_find_package(OpenSSL REQUIRED)

# Boost's definitions reach what links Boost, and nothing else, as in the
# libraries under mux: defined for the whole build they were on the std
# target too, and mux's modules were matched to it rather than to the std
# the family's modules were built against.

cme_find_package(tern REQUIRED)
# Of loom, what mux uses: the state a sync leaves, and the parts of the API it
# calls -- logging in, sending, syncing, finding the homeserver.
cme_find_package(loom COMPONENTS state cs-leaving cs-login cs-message_pagination cs-receipts cs-redaction cs-room_send cs-sync cs-wellknown REQUIRED)

# One library of mux's modules, as the libraries it stands on have: modules
# are what separates them. The window's two join it where the window is
# built.
add_library(mux_modules)
target_sources(mux_modules PUBLIC FILE_SET CXX_MODULES BASE_DIRS src
  FILES src/core.cc src/net.cc src/xmpp.cc src/http.cc src/matrix.cc)
target_compile_features(mux_modules PUBLIC cxx_std_26)
# The flags that decide which std module a target gets: the same on every
# library of modules in the build, or two std modules meet in one importer.
set(mux_std_flags
  $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-fconstexpr-steps=${MUX_CONSTEXPR_STEPS}>
  $<$<AND:$<COMPILE_LANG_AND_ID:CXX,Clang>,$<VERSION_GREATER_EQUAL:$<CXX_COMPILER_VERSION>,23>>:-fno-experimental-new-constant-interpreter>)
target_compile_options(mux_modules PUBLIC ${mux_std_flags})
target_link_libraries(mux_modules PUBLIC tern loom Boost::asio Boost::beast Boost::context
  OpenSSL::SSL OpenSSL::Crypto)

# The accounts kept between runs: a library of its own, on knot alone, so
# that it builds and is tested without the protocols.
add_library(mux_config)
target_sources(mux_config PUBLIC FILE_SET CXX_MODULES BASE_DIRS src FILES src/config.cc)
target_compile_features(mux_config PUBLIC cxx_std_26)
target_compile_options(mux_config PUBLIC ${mux_std_flags})
target_link_libraries(mux_config PUBLIC knot)
target_link_libraries(mux_modules PUBLIC mux_config)
foreach(name IN ITEMS core net xmpp http matrix)
  add_library(mux_${name} ALIAS mux_modules)
endforeach()

if(MUX_UI)
  # skiff and skiff-widgets say the rest of what they are in their own trees.
  cme_declare_port(NAME skiff PROVIDES skiff
    GITHUB_REPOSITORY j4niwzis/skiff
    GIT_TAG 7c9b5bc1de72d34325767143f8b40e3ff5fd77a7)
  cme_declare_port(NAME skiff-widgets PROVIDES skiff-widgets skiff_widgets
    GITHUB_REPOSITORY j4niwzis/skiff-widgets
    GIT_TAG 617b9b3097fe66a67fe6be3912c789db67e172ef)
  # Skia as skiff draws with it, on GL, with the system's fonts read from
  # their directory.
  set(SKIFF_SKIA_COMPONENTS gl fontmgr-directory CACHE STRING "" FORCE)
  cme_find_package(Skia REQUIRED COMPONENTS gl png jpeg freetype fontmgr-directory)
  cme_find_package(SDL3 REQUIRED)
  cme_find_package(skiff-widgets REQUIRED)
  # skiff's modules are C++23 and do not carry the family's flags, so they
  # would be built with a std module of their own: given the same standard
  # and the same flags here.
  foreach(target IN ITEMS skiff::skiff skiff::widgets)
    if(TARGET ${target})
      get_target_property(aliased ${target} ALIASED_TARGET)
      if(aliased)
        set(target ${aliased})
      endif()
      get_target_property(imported ${target} IMPORTED)
      if(NOT imported)
        target_compile_options(${target} PUBLIC ${mux_std_flags})
        set_target_properties(${target} PROPERTIES CXX_STANDARD 26)
      endif()
    endif()
  endforeach()

  target_sources(mux_modules PUBLIC FILE_SET CXX_MODULES BASE_DIRS src
    FILES src/host.cc src/ui.cc)
  target_link_libraries(mux_modules PUBLIC SDL3::SDL3 skiff::skiff skiff::widgets)
  add_executable(mux src/main.cc)
  target_link_libraries(mux PRIVATE mux_modules)
endif()

add_executable(mux-cli src/cli.cc)
target_link_libraries(mux-cli PRIVATE mux_modules)

if(MUX_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

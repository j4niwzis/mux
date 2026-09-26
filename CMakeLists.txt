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
# The window: SDL3 and skiff, with Skia on GL. Off until the cmake-everywhere
# release that has the SDL3 port is the one pinned.
option(MUX_UI "Build the window, and the mux program" OFF)

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

# The libraries of this family, each pinned to a commit.
cme_declare_port(NAME tern PROVIDES tern
  GITHUB_REPOSITORY j4niwzis/tern
  GIT_TAG dfe83171341c2cfed94015728559e8b28eef999a
  LICENSE AGPL-3.0-only
  SYSTEM NEVER
  TARGETS tern)
cme_options(tern "TERN_TESTS OFF" "TERN_EXAMPLES OFF")
cme_declare_port(NAME loom PROVIDES loom
  GITHUB_REPOSITORY j4niwzis/loom
  GIT_TAG 3728814120170495044d46286524a34c94c34743
  LICENSE AGPL-3.0-only
  SYSTEM NEVER
  TARGETS loom)
cme_options(loom "LOOM_TESTS OFF")

# Boost: Asio for the sockets and the loop, Beast for Matrix's HTTP,
# Context for the fibers, and PFR as a module for what reads with it.
cme_find_package(Boost COMPONENTS asio beast pfr[modules] context REQUIRED)
cme_find_package(OpenSSL REQUIRED)

# What Boost's targets define for whatever links them, defined for
# everything built here -- the libraries of the family below included.
#
# A module is built once for each set of flags it is imported with, and std
# once for each of those. mux.net and mux.http link Boost and so carry its
# definitions (BOOST_CONTEXT_STATIC_LINK and the rest) to what imports them;
# mux.matrix imports mux.http and loom, so loom's modules were built a
# second time with Boost's definitions, knot's under them a third, and the
# collation found std two ways. With the definitions everywhere, there is
# one set of flags. Read from the targets rather than written out here, so
# that what Boost says is what is used.
function(mux_definitions_of out)
  set(found "")
  set(pending ${ARGN})
  set(seen "")
  while(pending)
    list(POP_FRONT pending one)
    if(one IN_LIST seen OR NOT TARGET ${one})
      continue()
    endif()
    list(APPEND seen ${one})
    get_target_property(aliased ${one} ALIASED_TARGET)
    if(aliased)
      list(APPEND pending ${aliased})
      continue()
    endif()
    get_target_property(defined ${one} INTERFACE_COMPILE_DEFINITIONS)
    if(defined)
      list(APPEND found ${defined})
    endif()
    get_target_property(linked ${one} INTERFACE_LINK_LIBRARIES)
    if(linked)
      foreach(next IN LISTS linked)
        # $<LINK_ONLY:x> carries no definitions; anything else that is a
        # generator expression is left alone.
        if(NOT next MATCHES "^\\$<")
          list(APPEND pending ${next})
        endif()
      endforeach()
    endif()
  endwhile()
  list(REMOVE_DUPLICATES found)
  set(${out} "${found}" PARENT_SCOPE)
endfunction()
mux_definitions_of(mux_boost_definitions Boost::asio Boost::beast Boost::context Boost::pfr)
message(STATUS "mux: defined everywhere, as Boost defines it: ${mux_boost_definitions}")
add_compile_definitions(${mux_boost_definitions})

# And alef's constant interpreter, for everything: alef turns clang 23's new
# one off for itself and for what imports it -- tern does, knot and loom do
# not -- so a target importing both kinds had the one flag set nothing was
# built with. The same flag, with the same condition, everywhere.
add_compile_options(
  $<$<AND:$<COMPILE_LANG_AND_ID:CXX,Clang>,$<VERSION_GREATER_EQUAL:$<CXX_COMPILER_VERSION>,23>>:-fno-experimental-new-constant-interpreter>)

cme_find_package(tern REQUIRED)
cme_find_package(loom REQUIRED)

# One module to a target, as in the libraries it stands on.
function(mux_module target file)
  add_library(${target})
  target_sources(${target}
    PUBLIC FILE_SET CXX_MODULES BASE_DIRS src FILES ${file})
  target_compile_features(${target} PUBLIC cxx_std_26)
  if(ARGN)
    target_link_libraries(${target} PUBLIC ${ARGN})
  endif()
  target_compile_options(${target} PUBLIC
    $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-fconstexpr-steps=${MUX_CONSTEXPR_STEPS}>)
endfunction()

mux_module(mux_core src/core.cc)
mux_module(mux_net src/net.cc tern Boost::asio Boost::context OpenSSL::SSL OpenSSL::Crypto)
mux_module(mux_xmpp src/xmpp.cc mux_core mux_net tern)
mux_module(mux_http src/http.cc mux_net Boost::beast Boost::asio OpenSSL::SSL)
mux_module(mux_matrix src/matrix.cc mux_core mux_net mux_http
           loom_api loom_ev loom_state loom_cs_login loom_cs_room_send loom_cs_sync loom_cs_wellknown)

if(MUX_UI)
  # skiff and skiff-widgets say the rest of what they are in their own trees.
  cme_declare_port(NAME skiff PROVIDES skiff
    GITHUB_REPOSITORY j4niwzis/skiff
    GIT_TAG e2a4cd97a7c6ab947cc3b48da0bf983a8bc01281)
  cme_declare_port(NAME skiff-widgets PROVIDES skiff-widgets skiff_widgets
    GITHUB_REPOSITORY j4niwzis/skiff-widgets
    GIT_TAG 13d1c1ba4e90aed6188b05061e7930f859de3c48)
  # Skia as skiff draws with it, on GL, with the system's fonts read from
  # their directory.
  set(SKIFF_SKIA_COMPONENTS gl fontmgr-directory CACHE STRING "" FORCE)
  cme_find_package(Skia REQUIRED COMPONENTS gl png jpeg freetype fontmgr-directory)
  cme_find_package(SDL3 REQUIRED)
  cme_find_package(skiff-widgets REQUIRED)

  mux_module(mux_host src/host.cc SDL3::SDL3 skiff::skiff)
  mux_module(mux_ui src/ui.cc mux_core skiff::widgets skiff::skiff)
  add_executable(mux src/main.cc)
  target_link_libraries(mux PRIVATE mux_host mux_ui mux_xmpp mux_matrix)
endif()

add_executable(mux-cli src/cli.cc)
target_link_libraries(mux-cli PRIVATE mux_xmpp mux_matrix)

if(MUX_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

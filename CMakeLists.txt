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
endfunction()

mux_module(mux_core src/core.cc)
mux_module(mux_net src/net.cc tern Boost::asio Boost::context OpenSSL::SSL OpenSSL::Crypto)
mux_module(mux_xmpp src/xmpp.cc mux_core mux_net tern)
mux_module(mux_http src/http.cc mux_net Boost::beast Boost::asio OpenSSL::SSL)
mux_module(mux_matrix src/matrix.cc mux_core mux_net mux_http
           loom_api loom_ev loom_state loom_cs_login loom_cs_room_send loom_cs_sync loom_cs_wellknown)

add_executable(mux-cli src/cli.cc)
target_link_libraries(mux-cli PRIVATE mux_xmpp mux_matrix)

if(MUX_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

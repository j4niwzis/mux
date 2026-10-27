# Voice calls, as Element makes and takes them: the call's button in a chat's
# head, its view under it, its card over the window, and the call itself.
# Built where MUX_PLUGIN_CALLS is on (cmake/plugins.cmake). Its connection,
# through WebRTC -- libdatachannel, and libnice, libSRTP and usrsctp under
# it -- where MUX_CALLS is on too; off, nothing of them is fetched or built,
# and the media module is the one that connects nothing (media_none.cc).
option(MUX_CALLS "Voice calls' connection, through WebRTC (libdatachannel)" OFF)
set(media media_none.cc)
set(libraries)
if(MUX_CALLS)
  # libjuice silently ignores TURN TCP/TLS. Build the pinned library with
  # libnice so relay URLs supplied by the homeserver are actually usable.
  # An installed libdatachannel does not advertise which ICE backend it
  # uses, so it cannot satisfy this requirement without rebuilding.
  include("${PROJECT_SOURCE_DIR}/cmake/calls-deps.cmake")
  mux_find_package(LibNice REQUIRED)
  # The libnice API requires explicit restarts when ICE credentials change.
  # Keep the backend and that fix tied to the pinned source revision.
  cme_declare_port(NAME libdatachannel PROVIDES LibDataChannel datachannel
    VERSION 0.24.6
    GITHUB_REPOSITORY paullouisageneau/libdatachannel GIT_TAG v0.24.6
    DEPENDS openssl plog usrsctp libsrtp muxnice
    SYSTEM NEVER
    PATCHES
      "${PROJECT_SOURCE_DIR}/cmake/patches/libdatachannel-install-option.patch"
      "${PROJECT_SOURCE_DIR}/cmake/patches/libdatachannel-configuration-algorithm.patch"
      "${PROJECT_SOURCE_DIR}/cmake/patches/libdatachannel-nice-restart.patch"
    OPTIONS
      "LIBDATACHANNEL_INSTALL OFF" "CMAKE_CXX_MODULE_STD OFF" "CMAKE_CXX_SCAN_FOR_MODULES OFF"
      "BUILD_SHARED_LIBS OFF" "USE_SYSTEM_PLOG ON" "USE_SYSTEM_USRSCTP ON" "USE_SYSTEM_SRTP ON"
      "USE_NICE ON" "NO_WEBSOCKET ON" "NO_EXAMPLES ON" "NO_TESTS ON"
    LICENSE MPL-2.0 TARGETS LibDataChannel::LibDataChannel CHECK_HEADER rtc/rtc.hpp)
  function(cme_adapt_libdatachannel source binary)
    cme_export_variable(LibDataChannel LibDataChannel_FOUND TRUE)
  endfunction()
  mux_find_package(LibDataChannel REQUIRED)
  set(media relay.cc media.cc)
  set(libraries LibDataChannel::LibDataChannel)
endif()
mux_plugin(calls ORDER 40
  CHANGES   changes.cc
  TYPES     types.cc
  MATRIX    matrix.cc
  UI        ${media} ui.cc
  APP       app.cc
  LIBRARIES ${libraries})

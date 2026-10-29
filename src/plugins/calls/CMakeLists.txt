# Voice calls, as Element makes and takes them: the call's button in a chat's
# head, its view under it, its card over the window, and the call itself.
# Built where MUX_PLUGIN_CALLS is on (cmake/plugins.cmake). Its connection,
# through WebRTC -- libdatachannel, and libnice, libSRTP and usrsctp under
# it -- where MUX_CALLS is on too; off, nothing of them is fetched or built,
# and the media module is the one that connects nothing (media_none.cc).
option(MUX_CALLS "Voice calls' connection, through WebRTC (libdatachannel)" OFF)
set(media media_none.cc)
set(libraries)
if(MUX_CALLS AND CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set(media media_web.cc)
elseif(MUX_CALLS)
  # cme owns the library recipes, host generators and ICE backend patches.
  # This feature builds a known libnice backend with TURN TCP/TLS support.
  cme_features(libdatachannel nice)
  mux_find_package(LibDataChannel REQUIRED)
  set(media relay.cc media.cc)
  set(libraries LibDataChannel::LibDataChannel)
endif()
mux_plugin(calls ORDER 40
  SUMMARY "Voice calls, as Element makes and takes them: the call's button in a chat's head, its view under it, its card over the window, and the call itself."
  CHANGES   changes.cc
  TYPES     types.cc
  MATRIX    matrix.cc
  UI        ${media} ui.cc
  APP       app.cc
  LIBRARIES ${libraries}
  LOOM  cs-voip)

# Threads, as Element's panel: the room's threads listed in the info's
# place, one opened -- its root and answers as the chat's own list has its
# messages, and a field to answer in it -- and the button in the chat's
# head. Built where MUX_PLUGIN_THREADS is on (cmake/plugins.cmake).
mux_plugin(threads ORDER 45
  TYPES  types.cc
  UI     ui.cc
  UI_TOP ui_top.cc
  APP    app.cc)

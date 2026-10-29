# Spaces: a room holding rooms, as Element's and Telegram's folders -- in
# the chat list, what a space shows (its rooms, down every level), what
# Home leaves out of them, a space a folder, not a chat; its bars' icons,
# nested a level down. Built where MUX_PLUGIN_SPACES is on
# (cmake/plugins.cmake).
mux_plugin(spaces ORDER 5
  TYPES  types.cc
  UI_TOP ui_top.cc
  APP    app.cc)

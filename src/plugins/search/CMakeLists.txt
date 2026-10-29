# Finding in a chat, as tdesktop's search in a chat: a bar in place of the
# chat's head, what is found listed down the side, stepped through and gone
# to. Built where MUX_PLUGIN_SEARCH is on (cmake/plugins.cmake).
mux_plugin(search ORDER 1
  SUMMARY "Finding in a chat, as tdesktop's search in a chat: a bar in place of the chat's head, what is found listed down the side, stepped through and gone to."
  TYPES types.cc
  UI    ui.cc
  APP   app.cc)

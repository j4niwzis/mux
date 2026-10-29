# Typing notifications: that the user is typing, told to the chat where the
# settings let it (at most every twenty seconds while it goes on, and its
# end); and who is typing in a chat, said in its head as Telegram says it.
# Built where MUX_PLUGIN_TYPING is on (cmake/plugins.cmake).
mux_plugin(typing ORDER 25
  TYPES types.cc
  UI    ui.cc
  APP   app.cc)

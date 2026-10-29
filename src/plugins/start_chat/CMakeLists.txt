# Start chat, as Element's: who to talk to, found as it is typed -- among
# those one already chats with, and in the server's directory -- and one's
# own link to send; the same dialog inviting someone to a room. Built where
# MUX_PLUGIN_START_CHAT is on (cmake/plugins.cmake).
mux_plugin(start_chat ORDER 65
  SUMMARY "Start chat, as Element's: who to talk to, found as it is typed -- among those one already chats with, and in the server's directory -- and one's own link to send; the same dialog inviting someone to a room."
  TYPES types.cc
  UI    ui.cc
  APP   app.cc)

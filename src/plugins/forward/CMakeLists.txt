# Forwarding: a message, or the messages selected, to another of the
# account's chats, chosen in a box with a search. Built where
# MUX_PLUGIN_FORWARD is on (cmake/plugins.cmake).
mux_plugin(forward ORDER 35
  SUMMARY "Forwarding: a message, or the messages selected, to another of the account's chats, chosen in a box with a search."
  TYPES types.cc
  UI    ui.cc
  APP   app.cc)

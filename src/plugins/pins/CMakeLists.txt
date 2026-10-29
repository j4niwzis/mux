# Pinned messages, as Telegram shows them: the bar under a chat's head, the
# pin above the view, pressed to go to it and swiped through; Pin and Unpin
# in a message's menu, written as the room's state over Matrix. Built where
# MUX_PLUGIN_PINS is on (cmake/plugins.cmake).
mux_plugin(pins ORDER 35
  SUMMARY "Pinned messages, as Telegram shows them: the bar under a chat's head, the pin above the view, pressed to go to it and swiped through; Pin and Unpin in a message's menu, written as the room's state over Matrix."
  TYPES  types.cc
  MATRIX matrix.cc
  UI     ui.cc
  APP    app.cc)

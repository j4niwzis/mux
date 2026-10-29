# The developer tools, as Element's: a message's source, from its menu; a
# room's state and account data explored and written, and an event of any
# type sent, from the room's settings. Over Matrix. Built where
# MUX_PLUGIN_DEVTOOLS is on (cmake/plugins.cmake).
mux_plugin(devtools ORDER 60
  SUMMARY "The developer tools, as Element's: a message's source, from its menu; a room's state and account data explored and written, and an event of any type sent, from the room's settings."
  CHANGES changes.cc
  TYPES   types.cc
  MATRIX  matrix.cc
  UI      ui.cc
  APP     app.cc
  REQUIRES room_settings)

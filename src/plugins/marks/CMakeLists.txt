# Telegram's @ and heart: the mentions of the user and the reactions to
# their messages not seen yet -- kept on disk across runs, their buttons
# over the list's corner, gone to, listed, and the mentions seen shared
# with the account's other sessions where its protocol does. Built where
# MUX_PLUGIN_MARKS is on (cmake/plugins.cmake).
mux_plugin(marks ORDER 30
  TYPES  types.cc
  UI     ui.cc
  UI_TOP ui_top.cc
  APP    app.cc)

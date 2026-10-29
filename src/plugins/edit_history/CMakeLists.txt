# A message's edit history: its earlier versions, each the chat's own bubble,
# from its menu. Built where MUX_PLUGIN_EDIT_HISTORY is on
# (cmake/plugins.cmake).
mux_plugin(edit_history ORDER 40
  SUMMARY "A message's edit history: its earlier versions, each the chat's own bubble, from its menu."
  TYPES  types.cc
  UI     ui.cc
  UI_TOP ui_top.cc
  APP    app.cc)

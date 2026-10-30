# Avatars: a chat's picture beside its row's text in the list -- put there
# by a rule on the row's text piece (contract::shapes), the core drawing
# none of its own. Its rules in its UI_TOP layer, where it may name the
# pieces. Built where MUX_PLUGIN_AVATARS is on (cmake/plugins.cmake).
mux_plugin(avatars ORDER 5
  SUMMARY "Avatars: a chat's picture beside its row in the list, put there by a rule on the row's text (the core draws none of its own)."
  TYPES  types.cc
  UI_TOP ui_top.cc)

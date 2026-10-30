# Avatars: a chat's picture beside its row in the list -- put there by
# shaping the row's text (contract::shape<views::chat_row>), the core
# drawing none of its own. Built where MUX_PLUGIN_AVATARS is on
# (cmake/plugins.cmake).
mux_plugin(avatars ORDER 5
  SUMMARY "Avatars: a chat's picture beside its row in the list, put there by shaping the row (the core draws none of its own)."
  TYPES types.cc
  UI    ui.cc)

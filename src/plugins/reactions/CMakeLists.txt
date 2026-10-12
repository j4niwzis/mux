# Reactions shown on a message: their strip at its foot, a chip for each,
# pressed to react with it too. Built where MUX_PLUGIN_REACTIONS is on
# (cmake/plugins.cmake).
mux_plugin(reactions ORDER 10
  TYPES types.cc
  UI    ui.cc)

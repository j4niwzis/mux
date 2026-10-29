# Markdown in what is typed, as Element reads it -- quotes, code, emphasis,
# strong, strike, links, lists -- made the HTML a Matrix message carries,
# beside the field's own formatting. Built where MUX_PLUGIN_MARKDOWN is on
# (cmake/plugins.cmake); without it, what is typed goes as it is, with the
# field's formatting alone.
mux_plugin(markdown ORDER 70
  TYPES  types.cc
  MATRIX matrix.cc)

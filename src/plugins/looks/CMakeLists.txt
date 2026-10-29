# What a chat looks like, chosen at a level -- every chat's, an account's,
# one chat's: its background (as the level over it, the theme's, a plain
# colour, or a picture of one's own) and how its bubbles and the panels
# round it look. The pickers on Appearance, an account's page and a room's
# Appearance tab; what is chosen kept. The looks in effect, drawn, are the
# window's. Built where MUX_PLUGIN_LOOKS is on (cmake/plugins.cmake).
mux_plugin(looks ORDER 85
  SUMMARY "What a chat looks like, chosen at a level -- every chat's, an account's, one chat's: its background (as the level over it, the theme's, a plain colour, or a picture of one's own) and how its bubbles and the panels round it look."
  TYPES  types.cc
  UI_TOP ui_top.cc
  APP    app.cc)

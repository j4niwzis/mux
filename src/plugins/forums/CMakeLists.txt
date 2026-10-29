# Forums: a space shown as one chat in the chat list, its rooms in it as
# topics, as Telegram's -- or a protocol's own forum -- chosen in a space's
# settings and kept as the plugin's own. Built where MUX_PLUGIN_FORUMS is on
# (cmake/plugins.cmake); without it every space is a space.
mux_plugin(forums ORDER 6
  SUMMARY "Forums: a space shown as one chat in the chat list, its rooms in it as topics, as Telegram's -- or a protocol's own forum -- chosen in a space's settings and kept as the plugin's own."
  TYPES types.cc
  UI    ui.cc
  APP   app.cc
  REQUIRES room_settings)

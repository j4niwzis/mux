# What mux keeps, and how: Settings' Storage page (the seal, the limits in
# memory and on disk, clearing, deleted messages, room events) and Files
# page (pictures dropped on the window). Built where MUX_PLUGIN_STORAGE is on
# (cmake/plugins.cmake); without it, the settings are kept as they are,
# unseen.
mux_plugin(storage ORDER 87
  TYPES types.cc
  UI    ui.cc
  APP   app.cc)

# What is done to a picture dropped on the window before it is sent:
# Settings' Files page, and the settings it keeps as its own (metadata
# removed, the file renamed). Built where MUX_PLUGIN_FILES is on
# (cmake/plugins.cmake); without it, pictures are sent as they are.
mux_plugin(files ORDER 87
  TYPES types.cc
  UI    ui.cc
  APP   app.cc)

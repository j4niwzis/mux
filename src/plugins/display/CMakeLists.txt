# How the window moves and is drawn: Settings' Animations page (full,
# reduced, none) and Rendering page (OpenGL or software, the frames'
# switches). Built where MUX_PLUGIN_DISPLAY is on (cmake/plugins.cmake);
# without it, the settings are kept and read as they are, unseen.
mux_plugin(display ORDER 88
  TYPES types.cc
  UI    ui.cc
  APP   app.cc)

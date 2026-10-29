# How the window moves and is drawn: Settings' Animations page (full,
# reduced, none) and Rendering page (OpenGL or software, the frames'
# switches). Built where MUX_PLUGIN_DISPLAY is on (cmake/plugins.cmake);
# its settings are its own (contract::settings_kept); without it, the
# file keeps them as they were, and the window is drawn with OpenGL.
mux_plugin(display ORDER 88
  TYPES types.cc
  UI    ui.cc
  APP   app.cc)

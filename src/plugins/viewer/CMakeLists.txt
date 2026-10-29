# The media viewer: a picture, an avatar or a video seen whole, over the
# window -- zoomed, dragged, saved, a video played. Built where
# MUX_PLUGIN_VIEWER is on (cmake/plugins.cmake); without it a picture
# pressed opens nothing.
mux_plugin(viewer ORDER 30
  SUMMARY "The media viewer: a picture, an avatar or a video seen whole, over the window -- zoomed, dragged, saved, a video played."
  TYPES types.cc
  UI    ui.cc
  APP   app.cc)

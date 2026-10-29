# GIFs: the saved ones, a page of the media panel, sent from there. Built
# where MUX_PLUGIN_GIFS is on (cmake/plugins.cmake).
mux_plugin(gifs ORDER 30
  SUMMARY "GIFs: the saved ones, a page of the media panel, sent from there."
  TYPES types.cc
  UI    ui.cc
  APP   app.cc
  REQUIRES emoji_picker)

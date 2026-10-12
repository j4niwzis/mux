# Stickers sent: the media panel's page of them, and a sticker made a
# favourite from its menu. Those received are shown with the rest of a
# message either way. Built where MUX_PLUGIN_STICKERS is on
# (cmake/plugins.cmake).
mux_plugin(stickers ORDER 20
  TYPES types.cc
  UI    ui.cc
  APP   app.cc)

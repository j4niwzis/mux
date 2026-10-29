# Settings' Plugins page: every plugin built in, mux's own and third
# parties' (MUX_EXTRA_PLUGINS), listed with what each does; and a page for
# each, with its repository, its licence and its README (mux.plugins.info).
# Built where MUX_PLUGIN_CATALOG is on (cmake/plugins.cmake).
mux_plugin(catalog ORDER 89
  SUMMARY "Settings' Plugins page: every plugin built in, and a page for each with its repository, licence and README."
  TYPES types.cc
  UI    ui.cc
  APP   app.cc)

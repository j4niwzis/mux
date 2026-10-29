# About mux: its version and commit, its license, the log copied, and the
# libraries it is built with and their licenses -- Settings' pages. Built
# where MUX_PLUGIN_ABOUT is on (cmake/plugins.cmake).
mux_plugin(about ORDER 90
  TYPES types.cc
  UI    ui.cc
  APP   app.cc)

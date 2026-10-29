# Explore rooms, as Cinny's explorer: a server's directory searched, a
# space's rooms and spaces listed, a room joined from there. Built where
# MUX_PLUGIN_EXPLORE is on (cmake/plugins.cmake).
mux_plugin(explore ORDER 55
  SUMMARY "Explore rooms, as Cinny's explorer: a server's directory searched, a space's rooms and spaces listed, a room joined from there."
  TYPES types.cc
  UI    ui.cc
  APP   app.cc)

# A room's or a space's settings: the dialog, its tabs -- the client's
# General, Notifications and Appearance, and those other plugins give it
# through its own contracts (room_admin's, packs', devtools') -- and its
# part. Built where MUX_PLUGIN_ROOM_SETTINGS is on (cmake/plugins.cmake);
# without it, Manage opens nothing.
mux_plugin(room_settings ORDER 10
  SUMMARY "A room's or a space's settings: the dialog, its tabs -- the client's General, Notifications and Appearance, and those other plugins give it through its own contracts (room_admin's, packs', devtools') -- and its part."
  TYPES types.cc
  UI    ui.cc
  APP   app.cc)

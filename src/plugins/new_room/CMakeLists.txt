# Create a room, as Element's CreateRoomDialog: a name, a topic, who can
# join, the address it is found by, encryption and federation -- from the
# drawer, or a space's menu (a room or a space made in it). Built where
# MUX_PLUGIN_NEW_ROOM is on (cmake/plugins.cmake).
mux_plugin(new_room ORDER 70
  SUMMARY "Create a room, as Element's CreateRoomDialog: a name, a topic, who can join, the address it is found by, encryption and federation -- from the drawer, or a space's menu (a room or a space made in it)."
  TYPES types.cc
  UI    ui.cc
  APP   app.cc)

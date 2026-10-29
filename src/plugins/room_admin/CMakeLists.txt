# A room's admin tabs in its settings -- Matrix's General, Members and
# Permissions, as Cinny's; an XMPP group chat's Room, Configuration and Roles
# & Permissions -- with what they keep and what their presses do. One
# plugin, a realization for each protocol turned on: its layers' modules
# are written here from those (types.cc.in, ui.cc.in, app.cc.in), each
# protocol's a partition of them. Built where MUX_PLUGIN_ROOM_ADMIN is on
# (cmake/plugins.cmake), on the room_settings plugin's contracts.
set(admin_protocols)
if(MUX_PROTOCOL_MATRIX)
  list(APPEND admin_protocols matrix)
endif()
if(MUX_PROTOCOL_XMPP)
  list(APPEND admin_protocols xmpp)
endif()
if(NOT admin_protocols)
  return()
endif()
set(ADMIN_IMPORTS "")
set(admin_realizations)
set(admin_bases)
set(admin_inits)
set(ADMIN_USINGS "")
set(admin_types types_feature.cc)
set(admin_ui ui_common.cc)
set(admin_app)
foreach(protocol IN LISTS admin_protocols)
  string(APPEND ADMIN_IMPORTS "export import :${protocol};\n")
  list(APPEND admin_realizations "${protocol}_admin")
  list(APPEND admin_bases "public ${protocol}_glue")
  list(APPEND admin_inits "${protocol}_glue{&shared}")
  string(APPEND ADMIN_USINGS "  using ${protocol}_glue::apply;\n  using ${protocol}_glue::take;\n")
  list(APPEND admin_types types_${protocol}.cc)
  list(APPEND admin_ui ui_${protocol}.cc)
  list(APPEND admin_app app_${protocol}.cc)
endforeach()
string(REPLACE ";" ", " ADMIN_REALIZATIONS "${admin_realizations}")
string(REPLACE ";" ", " ADMIN_BASES "${admin_bases}")
string(REPLACE ";" ", " ADMIN_INITS "${admin_inits}")
set(admin_generated ${mux_generated}/plugins/room_admin)
configure_file(types.cc.in ${admin_generated}/types.cc @ONLY)
configure_file(ui.cc.in ${admin_generated}/ui.cc @ONLY)
configure_file(app.cc.in ${admin_generated}/app.cc @ONLY)
mux_plugin(room_admin ORDER 12
  TYPES    ${admin_generated}/types.cc ${admin_types}
  UI       ${admin_generated}/ui.cc ${admin_ui}
  APP      ${admin_generated}/app.cc ${admin_app}
  REQUIRES room_settings)

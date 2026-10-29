# room_admin

A plugin of [mux](https://github.com/j4niwzis/mux).

A room's admin tabs in its settings -- Matrix's General, Members and Permissions, as Cinny's; an XMPP group chat's Room, Configuration and Roles & Permissions -- with what they keep and what their presses do. One plugin, a realization for each protocol turned on: its layers' modules are written here from those (types.cc.in, ui.cc.in, app.cc.in), each protocol's a partition of them. Built where MUX_PLUGIN_ROOM_ADMIN is on, on the room_settings plugin's contracts.

Licence: AGPL-3.0-only, as mux.

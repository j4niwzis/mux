# Slash commands typed in the field, as other clients have them: /shrug,
# /rainbow, /raw, /open a chat, /join a room, /react to the last message,
# /jump-to-date, /help -- and the
# plugins' own (contract::slash_commands), all suggested as they are typed
# (the suggestions plugin). Built where MUX_PLUGIN_COMMANDS is on
# (cmake/plugins.cmake).
mux_plugin(commands ORDER 21
  SUMMARY "Slash commands typed in the field: /shrug, /rainbow, /raw, /open, /join, /react, /jump-to-date, /help, and the plugins' own."
  CHANGES changes.cc
  TYPES   types.cc
  MATRIX  matrix.cc
  APP     app.cc
  LOOM    cs-room_event_by_timestamp)

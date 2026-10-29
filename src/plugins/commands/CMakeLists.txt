# Slash commands typed in the field, as other clients have them: /shrug,
# /open a chat, /join a room, /react to the last message, /help -- and the
# plugins' own (contract::slash_commands), all suggested as they are typed
# (the suggestions plugin). Built where MUX_PLUGIN_COMMANDS is on
# (cmake/plugins.cmake).
mux_plugin(commands ORDER 21
  SUMMARY "Slash commands typed in the field: /shrug, /open, /join, /react, /help, and the plugins' own."
  TYPES types.cc
  APP   app.cc)

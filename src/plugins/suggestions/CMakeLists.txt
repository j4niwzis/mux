# The list over the chat's composer, as Telegram's: the members matching
# what follows an @, or the emoji matching what follows a ':' -- Up and
# Down through it, Enter, Tab or a press picks, Esc closes it. Built where
# MUX_PLUGIN_SUGGESTIONS is on (cmake/plugins.cmake).
mux_plugin(suggestions ORDER 20
  TYPES types.cc
  UI    ui.cc
  APP   app.cc)

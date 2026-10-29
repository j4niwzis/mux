# Link previews, as Telegram's: a message's first link's page -- its site,
# title, a line about it and its picture -- under its text, pressed to open
# it. Built where MUX_PLUGIN_PREVIEWS is on (cmake/plugins.cmake).
mux_plugin(previews ORDER 15
  SUMMARY "Link previews, as Telegram's: a message's first link's page -- its site, title, a line about it and its picture -- under its text, pressed to open it."
  TYPES types.cc
  UI    ui.cc)

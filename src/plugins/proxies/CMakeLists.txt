# Proxies, as Gajim's Manage Proxies: the profiles in Settings, listed,
# added, edited and deleted; and an account's Proxy page, which of them it
# connects through. Connecting through one is the network's own. Built where
# MUX_PLUGIN_PROXIES is on (cmake/plugins.cmake).
mux_plugin(proxies ORDER 60
  SUMMARY "Proxies, as Gajim's Manage Proxies: the profiles in Settings, listed, added, edited and deleted; and an account's Proxy page, which of them it connects through."
  TYPES types.cc
  UI    ui.cc
  APP   app.cc)

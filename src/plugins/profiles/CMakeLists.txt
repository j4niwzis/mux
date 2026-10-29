# An account's Profile page: its name, pronouns, biography and banner, and
# its presence, as its protocol has them. Built where MUX_PLUGIN_PROFILES is
# on (cmake/plugins.cmake); the profiles themselves -- people's cards --
# are the client's either way.
mux_plugin(profiles ORDER 25
  SUMMARY "An account's Profile page: its name, pronouns, biography and banner, and its presence, as its protocol has them."
  TYPES types.cc
  UI    ui.cc)

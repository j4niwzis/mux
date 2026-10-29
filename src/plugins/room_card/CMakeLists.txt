# A room not joined, as a link names it or an invite brings it: its card,
# as the person card is laid out -- its photo, name, how many are in it,
# what it is about, its ID -- filled as its server answers, and joined,
# knocked on, or its invite declined from there. Built where
# MUX_PLUGIN_ROOM_CARD is on (cmake/plugins.cmake).
mux_plugin(room_card ORDER 75
  SUMMARY "A room not joined, as a link names it or an invite brings it: its card, as the person card is laid out -- its photo, name, how many are in it, what it is about, its ID -- filled as its server answers, and joined, knocked on, or its invite declined from there."
  TYPES  types.cc
  UI_TOP ui_top.cc
  APP    app.cc)

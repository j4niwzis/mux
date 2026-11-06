# Read receipts shown, as Element shows them: who has read up to a message,
# their small faces at the row's right under it, three at most and the rest
# counted. Built where MUX_PLUGIN_RECEIPTS is on (cmake/plugins.cmake).
mux_plugin(receipts ORDER 45
  SUMMARY "Read receipts shown, as Element shows them: who has read up to a message, their small faces at the row's right under it, three at most and the rest counted."
  MODEL   model.cc
  CHANGES changes.cc
  TYPES   types.cc
  UI      ui.cc
  UI_TOP  ui_top.cc
  MATRIX  matrix.cc)

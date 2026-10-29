# Emojis & Stickers, as Cinny edits them (MSC2545): one's own pack, from
# Settings and an account's page; a room's, from its settings -- listed,
# opened, edited, saved, deleted, added to one's account, and images
# uploaded for them. The packs in use -- custom emoji and stickers in chats,
# the pickers -- are the client's own. Built where MUX_PLUGIN_PACKS is on
# (cmake/plugins.cmake).
mux_plugin(packs ORDER 50
  SUMMARY "Emojis & Stickers, as Cinny edits them (MSC2545): one's own pack, from Settings and an account's page; a room's, from its settings -- listed, opened, edited, saved, deleted, added to one's account, and images uploaded for them."
  CHANGES changes.cc
  TYPES   types.cc
  MATRIX  matrix.cc
  UI      ui.cc
  APP     app.cc
  REQUIRES room_settings)

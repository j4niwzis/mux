# End-to-end encryption for Matrix: the account made with it -- Olm and
# Megolm (loom.crypto, on vodozemac), emoji verification, cross-signing, the
# key backup and secret storage -- in place of the client's plain one, which
# reads no encrypted message and sends nothing into an encrypted room.
# Built where MUX_PLUGIN_ENCRYPTION is on (cmake/plugins.cmake).
mux_plugin(encryption ORDER 2
  CHANGES changes.cc
  TYPES  types.cc
  UI     ui.cc
  APP    app.cc
  MATRIX matrix.cc account.cc verification.cc keys.cc events.cc backup.cc)

# End-to-end encryption for Matrix: the account made with it -- Olm and
# Megolm (loom.crypto, on vodozemac), emoji verification, cross-signing, the
# key backup and secret storage -- in place of the client's plain one, which
# reads no encrypted message and sends nothing into an encrypted room.
# Built where MUX_PLUGIN_ENCRYPTION is on (cmake/plugins.cmake), and Matrix
# with it: it is Matrix's encryption, nothing without it.
if(NOT MUX_PROTOCOL_MATRIX)
  return()
endif()
mux_plugin(encryption ORDER 2
  SUMMARY "End-to-end encryption for Matrix: the account made with it -- Olm and Megolm (loom.crypto, on vodozemac), emoji verification, cross-signing, the key backup and secret storage -- in place of the client's plain one, which reads no encrypted message and sends nothing into an encrypted room."
  CHANGES changes.cc
  TYPES  types.cc
  UI     ui.cc ui_page.cc
  APP    app.cc
  MATRIX matrix.cc account.cc verification.cc keys.cc events.cc backup.cc
  LOOM  crypto cs-keys cs-to_device cs-cross_signing cs-key_backup)

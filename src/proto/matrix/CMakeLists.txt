# Matrix: built where MUX_PROTOCOL_MATRIX is on (cmake/protocols.cmake).
mux_protocol(matrix ORDER 20
  CONFIG state.cc kept.cc
  CORE   changes.cc points.cc requests.cc make.cc links.cc
         client/matrix.cc client/base.cc client/oauth.cc client/names.cc client/account.cc
         client/requests.cc client/requests_more.cc client/requests_send.cc
         client/sync.cc client/sync_crypto.cc client/sync_keys.cc client/events.cc client/media.cc
  UI     ui_form.cc ui_manage.cc ui_sessions.cc ui_tools.cc ui.cc
  UI_IMPL manage_general.cc manage_members.cc manage_permissions.cc
          manage_packs.cc manage_developer.cc
  APP    demo.cc app.cc
  SPLIT  app/accounts_sync.cc app/accounts_requests.cc app/accounts_events.cc app/accounts_media.cc
  POINTS mux.proto.matrix mux.proto.matrix.requests)

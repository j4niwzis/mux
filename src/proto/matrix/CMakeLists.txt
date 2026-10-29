# Matrix: built where MUX_PROTOCOL_MATRIX is on (cmake/protocols.cmake).
mux_protocol(matrix ORDER 20
  CONFIG state.cc kept.cc
  CORE   changes.cc points.cc requests.cc make.cc links.cc
         client/matrix.cc client/base.cc client/oauth.cc client/names.cc client/account.cc
         client/requests.cc client/requests_more.cc client/requests_send.cc
         client/sync.cc client/sync_keys.cc client/events.cc client/media.cc
  UI     ui_form.cc ui_sessions.cc ui_tools.cc ui.cc
  APP    demo.cc app.cc
  SPLIT  app/accounts_sync.cc app/accounts_requests.cc app/accounts_events.cc app/accounts_media.cc
  POINTS mux.proto.matrix mux.proto.matrix.requests
  # The parts of loom its client imports.
  LOOM  state crypto cs-joining cs-leaving cs-login cs-registration cs-oauth_server_metadata
        cs-message_pagination cs-event_context cs-receipts cs-redaction cs-room_send cs-rooms
        cs-sync cs-filter cs-typing cs-wellknown cs-room_state cs-kicking cs-banning cs-inviting
        cs-create_room cs-account_data cs-content_repo cs-authed_content_repo cs-room_summary
        cs-list_public_rooms cs-space_hierarchy cs-sliding_sync cs-versions cs-users cs-relations
        cs-threads_list cs-profile cs-presence cs-device_management cs-room_upgrades cs-directory
        cs-capabilities cs-knocking cs-keys cs-to_device cs-cross_signing cs-key_backup cs-pusher
        cs-pushrules cs-voip)

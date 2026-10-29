# XMPP: built where MUX_PROTOCOL_XMPP is on (cmake/protocols.cmake).
mux_protocol(xmpp ORDER 10
  CONFIG state.cc kept.cc
  CORE   changes.cc points.cc client/xmpp.cc make.cc links.cc
  UI     ui.cc
  APP    demo.cc app.cc
  SPLIT  app/accounts.cc)

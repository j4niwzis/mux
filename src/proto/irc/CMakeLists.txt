# IRC: built where MUX_PROTOCOL_IRC is on (cmake/protocols.cmake), on cairn.
mux_protocol(irc ORDER 30
  CONFIG state.cc kept.cc
  CORE   changes.cc points.cc client/irc.cc make.cc links.cc
  UI     ui.cc
  APP    demo.cc app.cc
  SPLIT  app/accounts.cc)

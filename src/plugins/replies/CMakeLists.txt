# Replies: a message answered -- Reply and Quote & Reply from its menu, a
# swipe on it, a reaction's row -- the field then said to answer it, over
# it, in the chat or in the thread open where the message is in it.
# Built where MUX_PLUGIN_REPLIES is on (cmake/plugins.cmake).
mux_plugin(replies ORDER 3
  SUMMARY "Replies: a message answered from its menu, a swipe or a reaction's row, the field then said to answer it."
  TYPES types.cc
  UI    ui.cc
  APP   app.cc)

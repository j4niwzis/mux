# The emoji panel, the media panel: its emoji, and the pages plugins give it
# (stickers, GIFs) -- opened from the chat's input or a thread's, docked
# under a finger. Built where MUX_PLUGIN_EMOJI_PICKER is on
# (cmake/plugins.cmake). The emoji kept and their grid are the client's:
# messages and reactions draw them either way.
mux_plugin(emoji_picker ORDER 15
  TYPES types.cc
  UI    ui.cc
  APP   app.cc)

# Notifications: what is told of outside the chat in view -- a message
# come, an invite -- by the desktop's notifications or mux's own, with the
# chime, as the settings say; a notification's actions (its chat opened,
# read, answered); and UnifiedPush, which wakes mux for them. Built where
# MUX_PLUGIN_NOTIFICATIONS is on (cmake/plugins.cmake).
mux_plugin(notifications ORDER 80
  SUMMARY "Notifications: what is told of outside the chat in view -- a message come, an invite -- by the desktop's notifications or mux's own, with the chime, as the settings say; a notification's actions (its chat opened, read, answered); and UnifiedPush, which wakes mux for them."
  TYPES types.cc
  UI    ui.cc
  APP   app.cc)

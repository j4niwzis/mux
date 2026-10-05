// SPDX-License-Identifier: AGPL-3.0-only
// The desktop's notifications, as org.freedesktop.Notifications takes them:
// one call on the session bus.
export module mux.platform.freedesktop.notifications_backend;

import std;
import chevron.escape;
import mux.platform.freedesktop.bus;

export namespace mux::platform::notifications::backend {

// A notification on the desktop: the title over the text, with an "Open"
// action -- with the sound theme's sound for a message (the spec's
// sound-name, message-new-instant), or none (suppress-sound). False where
// there is no bus, or it did not take it.
inline bool notify(std::string_view title, std::string_view text, bool sound) {
  using namespace freedesktop::bus;
  auto bus = session::open();
  if (!bus)
    return false;
  writer body;
  body.string("mux");     // app_name
  body.u32(0);            // replaces_id
  body.string("");        // app_icon
  // The body may be read as markup (the spec's body-markup): what a message
  // says is escaped, so that none of it is a tag -- an <a>, an <img> of a
  // file (review 5).
  // What a message says is escaped, so that none of it is a tag.
  const std::string escaped = chevron::escaped(text);
  body.string(title);     // summary
  body.string(escaped);   // body
  body.array(4, [&] {     // actions: key, label
    body.string("default");
    body.string("Open");
  });
  body.array(8, [&] {  // hints: a{sv}, one entry
    body.align(8);
    if (sound) {
      body.string("sound-name");
      body.signature("s");
      body.string("message-new-instant");
    } else {
      body.string("suppress-sound");
      body.signature("b");
      body.u32(1);
    }
  });
  body.i32(-1);           // expire_timeout: the server's
  if (!bus->send(message(kind::call, 0, bus->next_serial(),
                         {.path = "/org/freedesktop/Notifications", .interface = "org.freedesktop.Notifications",
                          .member = "Notify", .destination = "org.freedesktop.Notifications",
                          .signature = "susssasa{sv}i"},
                         body.bytes())))
    return false;
  // A reply read, so that the bus has the call before the socket goes.
  bool closed = false;
  (void)bus->read(closed);
  return true;
}

}  // namespace mux::platform::notifications::backend

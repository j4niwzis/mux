# mux

**Multi-protocol Unified eXchange**: one chat client for Matrix and XMPP,
written in C++26 modules, drawn with Skia in an SDL3 window -- on Linux and
on Android. It looks and behaves like Telegram Desktop, with Element's
Matrix features behind it.

## Features

**Both protocols**

- Accounts side by side, each with its own proxy; a chat can be listed in
  another account's list too, or moved there.
- The chat list as Telegram's: folders, a bar of spaces at the side and/or
  the top, spaces shown as forums (their rooms as topics), pinned and muted
  chats, drafts.
- Messages: replies, quotes, edits with their history (as AyuGram shows
  it), deletion, reactions, forwarding, selecting several, pinning, threads,
  read receipts, typing, link previews, "View source".
- Unseen mentions and reactions, each listed and gone to (Telegram's @ and
  heart); jumping to a message and back.
- The composer as Telegram's: bold, italic, underline, strikethrough,
  spoilers, code and links -- by its shortcuts, the field's menu, Ctrl+K,
  or Markdown as it is typed; quotes and code blocks; `--` and `<< >>`
  typography; mentions with `@`, emoji with `:word`; pasting keeps the
  formatting.
- Pictures, videos (FFmpeg), voice and files: a send box with a caption,
  thumbnails and blurhash, metadata stripped on request; custom emoji,
  stickers and GIFs (MSC2545 packs, editable).
- Looks: themes, accent colours, bubbles or lines, chat backgrounds
  (Telegram's patterns, per account, space or chat), interface scale,
  window opacity and blur; GPU (OpenGL) or software drawing.
- Notifications at four levels -- the client, an account, a space, a chat
  -- through freedesktop notifications, Android's, or UnifiedPush.
- Local data -- settings, passwords, chats kept, drafts, keys -- sealed
  under a passphrase on request; the chats read are kept on disk and read
  from there before the server is asked.

**Matrix**

- Sign in with a password, or through the server's OIDC provider in the
  browser; registration with its interactive auth stages.
- Sync, sliding sync where the server has it; history paged from the
  server and from disk.
- End-to-end encryption, received: Olm and Megolm through vodozemac;
  cross-signing, emoji verification, key backup and secret storage,
  room key export and import. (Sending encrypted is not done yet.)
- Interactive auth for what asks it again -- signing sessions out,
  cross-signing -- with the password, or in the browser for SSO and OIDC
  accounts.
- Sessions: listed, renamed, verified, signed out.
- Rooms and spaces managed as Element's: name, topic, addresses, join
  rules (space members included), history visibility, encryption, roles
  and power levels, upgrades; a space's rooms added, removed and created in
  it; leaving a space with its rooms or some of them; knocking.
- Explore: a server's public directory, paged, or a space's rooms.
- Calls (`MUX_CALLS`): voice over WebRTC, through libdatachannel; Element's
  call view.
- Developer tools: a room's state explored, custom events sent.

**XMPP**

- TLS, SASL (SCRAM with channel binding), in-band registration
  (XEP-0077) with the server's form or CAPTCHA.
- Roster and presence, chat messages, history (MAM), corrections
  (XEP-0308), replies, message styling sent (XEP-0393), stream management,
  file upload.

## Libraries

| library | what mux takes from it |
| --- | --- |
| [tern](https://github.com/j4niwzis/tern) | XMPP: the stream, SASL, roster, presence, messages, XEPs; inboxes, so each part of the client waits for its own stanzas |
| [loom](https://github.com/j4niwzis/loom) | Matrix: every client-server endpoint and event as types, a sync's state, the crypto (over vodozemac) |
| [knot](https://github.com/j4niwzis/knot) | JSON, typed, for Matrix and for mux's own files |
| [chevron](https://github.com/j4niwzis/chevron) | XML, typed, under tern; HTML for Matrix's formatted bodies |
| [alef](https://github.com/j4niwzis/alef) | Unicode: normalization, casing, graphemes, IDNA, PRECIS, emoji -- its tables read from Unicode's files at compile time |
| [splice](https://github.com/j4niwzis/splice) | `spl::variant`, overload sets, erased calls, fields of aggregates |
| [skiff](https://github.com/j4niwzis/skiff) | the scene: layout, input routing, text, accessibility, frame scheduling, on Skia |
| [skiff-widgets](https://github.com/j4niwzis/skiff-widgets) | the controls: text areas (extendable: formats, blocks), buttons, tabs, sliders, dialogs |
| [cmake-everywhere](https://github.com/j4niwzis/cmake-everywhere) | every dependency, pinned: built here or taken from the system, natively or cross-compiled |

and on SDL3 (a fork, for Android's NativeActivity), Skia, Boost.Asio and
Boost.Beast, OpenSSL, vodozemac, FFmpeg, Opus and libdatachannel.

## Architecture

```
  mux.platform.*    the SDL3 window, input, IME, clipboard, dialogs, audio,
       |            video, notifications, push, files -- Linux and Android
  mux.ui            the screens, as skiff nodes (partitions mux.ui:*)
       |    ^
  requests  changes
       v    |
  mux.app.*         the program: one part per concern (outbox, reading,
       |            history, marks, rooms, manage, calls, ...), applying
       |            the UI's requests and the protocols' changes
  mux.core          the model: accounts, conversations, messages -- one for
       |            every protocol -- and the mailbox between the threads
  mux.app.network   the accounts, on the network's own thread
       |
  mux.proto.*       each protocol's client, registered in one place
   /         \
 xmpp        matrix     tern on Asio + TLS; loom's requests over Beast
   \         /
  mux.net, mux.http, mux.vault     the io_context and TLS, HTTP, sealed files
  mux.logic.*       what is decided without I/O: Markdown, links, search,
                    reading, notifications -- tested on its own
```

- **Two threads.** The network runs on one `io_context`, as stackful
  coroutines; the UI on SDL's thread. The network tells the program what
  happened as **changes** (typed, one variant), through a mailbox drained
  between frames; the UI asks for what the user does as **requests** (one
  variant), applied by the program's parts. Neither side blocks on the
  other.
- **One model.** `mux.core` holds both protocols' accounts, chats and
  messages in one shape; what only one protocol has is its own part
  (`mux.proto.<protocol>`), reached through its state's type.
- **Protocols are a closed set.** A protocol is a tag type in one list
  (`src/proto/tags.cc`); its client, its changes, requests, settings
  pages and dialogs are found by overloads on its types. The accounts are
  a variant of each protocol's client, visited.
- **Sans-I/O below, I/O here.** tern and loom do the protocols and nothing
  else; sockets, TLS, files and the clock are mux's.
- **Static in release, erased while developing.** A release build is
  instantiated whole; a development build erases handlers and walks and
  splits the accounts into units of their own, built in parallel
  (`MUX_SPLIT_ACCOUNTS`).

## Building

C++26 modules: clang 23 (libc++ or libstdc++), CMake 4 and Ninja.
cmake-everywhere fetches and pins every dependency, or takes it from the
system where it is there.

```
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Options: `MUX_CALLS` (voice calls), `MUX_VIDEO` (FFmpeg), `MUX_GL`,
`MUX_GIF`, `MUX_WEBP`, `MUX_CLI` (a terminal client), `MUX_TESTS`.

- **Cross-compiling:** a CMake toolchain file with a sysroot and
  `CMAKE_C_COMPILER_TARGET` / `CMAKE_CXX_COMPILER_TARGET`; Rust (for
  vodozemac) needs the target's standard library -- `rustup target add`,
  or cargo's `-Z build-std`.
- **Android:** `cmake/android.cmake` and `cmake/android-apk.cmake` build
  the APK (see `.github/workflows/android.yml`).
- **Linux desktop entry:** `packaging/linux/mux.desktop`, for
  `/usr/local/bin/mux`.

The code is written as [CODE_STYLE.md](CODE_STYLE.md) says.

## Licence

GNU Affero General Public License, version 3 only (`AGPL-3.0-only`) -- the
text is in `LICENSE`.

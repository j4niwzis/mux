# mux

**Multi-protocol Unified eXchange**: one chat client for XMPP and Matrix,
written in C++26 on the libraries beside it:

| library | what mux takes from it |
| --- | --- |
| [tern](https://github.com/j4niwzis/tern) | XMPP: the stream, SASL, roster, presence, messages, XEPs (disco, caps, carbons, MAM, stream management); inboxes, so each part of the client waits for its own stanzas |
| [loom](https://github.com/j4niwzis/loom) | Matrix: every client-server endpoint as types, every event as types, `loom::client::state` for what a sync leaves |
| [knot](https://github.com/j4niwzis/knot) | JSON, for Matrix and for mux's own files |
| [chevron](https://github.com/j4niwzis/chevron) | XML, under tern |
| [skiff](https://github.com/j4niwzis/skiff) | the scene: layout, stylesheets, input routing, accessibility, frame scheduling, on Skia |
| [skiff-widgets](https://github.com/j4niwzis/skiff-widgets) | the controls: text boxes, buttons, tab bars, sliders, dropdowns |

and on SDL3 (the window, input, IME, GPU contexts), Skia (drawing, on the
GPU where there is one and in software where there is not), Boost.Asio and
Boost.Beast (sockets, TLS, HTTP), OpenSSL (TLS), and vodozemac (Matrix's
Olm and Megolm).

Linux first; Android after it, which is part of why the window is SDL's.

## Shape

```
          mux.ui          screens: accounts, the conversation list, a conversation, the composer, logins
            |
          mux.host        the SDL3 window: events into skiff, a Skia surface out, frames when skiff asks
            |
          mux.core        accounts, conversations, messages, presence -- one model for both protocols
          /       \
   mux.xmpp       mux.matrix
   tern on        loom's requests over Beast;
   Asio + TLS     /sync long-polled into loom::client::state
          \       /
          mux.net         one io_context; TLS streams; stackful coroutines (Boost.Context)
```

- **One thread of control for the protocols.** Everything network-facing runs
  on one `io_context`, as stackful coroutines (`boost::asio::spawn`): tern's
  scheduler concept is implemented on it (`current`, `park`, `wake`), and a
  Matrix request is a coroutine that waits for its answer. No callback
  chains and no stackless coroutines.
- **The UI thread** is SDL's. The two meet in a queue of changes to
  `mux.core`'s model, drained once a frame; the UI never blocks on the
  network, and the network never touches a drawable.
- **No type erasure.** The protocols are a closed set: the accounts a user
  has are `std::variant<xmpp::account, matrix::account>`, dispatched with
  `std::visit`; what differs between them is a concept both implement.
- **Sans-I/O below, I/O here.** tern and loom do the protocols and nothing
  else; mux is where sockets, TLS, files and the clock are.

## The model (mux.core)

- **account**: a protocol, an address (`user@example.com` or
  `@user:example.org`), its connection state, and its settings.
- **conversation**: an XMPP chat with a contact, an XMPP MUC room, a Matrix
  room (a DM being a room marked in `m.direct`). Its name, avatar, unread
  counts, the last message, typing users, and whether it is encrypted.
- **message**: sender, time, body (plain and formatted), what it replies to,
  edits and redactions applied, reactions, delivery and read state, and the
  protocol's own id -- so an edit, a reaction or a receipt finds it.
- **presence**: per contact, with status text.
- Kept on disk between runs (knot's JSON, one file per account): sync tokens,
  `loom::client::state`, the roster version, MAM positions, keys (through
  vodozemac's pickles, encrypted).

## First milestones

1. mux.net and mux.xmpp: log in to an XMPP account over TLS, roster and
   presence, send and receive chat messages; mux.core's model filled.
2. mux.matrix: log in, sync, rooms and timelines, send messages; the same
   model filled.
3. mux.host and mux.ui: the window, the conversation list, a conversation,
   the composer -- the two accounts side by side.
4. History (MAM, /messages), edits, replies, reactions, receipts, typing.
5. Matrix E2EE through vodozemac; OMEMO after it.
6. Android.

## Building

C++26 modules: clang 23 with libc++, CMake 4 and Ninja, as the libraries
above. Dependencies come through cmake-everywhere, pinned to commits.

## Licence

GNU Affero General Public License, version 3 only (`AGPL-3.0-only`) -- the
text is in `LICENSE`.

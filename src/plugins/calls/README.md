# calls

A plugin of [mux](https://github.com/j4niwzis/mux).

Voice calls, as Element makes and takes them: the call's button in a chat's head, its view under it, its card over the window, and the call itself. Built where MUX_PLUGIN_CALLS is on. Its connection, through WebRTC -- libdatachannel, and libnice, libSRTP and usrsctp under it -- where MUX_CALLS is on too; off, nothing of them is fetched or built, and the media module is the one that connects nothing (media_none.cc).

Licence: AGPL-3.0-only, as mux.

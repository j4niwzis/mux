// SPDX-License-Identifier: AGPL-3.0-only
// The Matrix account as the program runs it: the members defined in its
// media partition, instantiated here alone -- one of four units the account
// is built in, in parallel (see network.cc).
// Part of mux.app.network, where the account is declared extern.
module mux.app.network;
import std;
import knot;
import loom.api;
import loom.ev;
import loom.state;
import mux.core;
import mux.config;
import mux.http;
import mux.net;
import mux.matrix;

namespace mux::matrix {
template void account<mux::app::post_change>::fetch_media(std::string source, media_use_t use, int size, bool crop);
template void account<mux::app::post_change>::send_file(std::string room, std::string local, std::string bytes, std::string name, std::string mimetype, bool image, int width, int height, std::string caption, std::optional<std::string> reply_to);
}  // namespace mux::matrix

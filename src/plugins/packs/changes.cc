// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.packs.changes -- What an account tells the program of the
// packs being edited -- listed, saved or taken away, an image uploaded --
// joined into mux::change_t (mux.plugins.changes), and taken by the
// plugin's part. Light: under mux.core, on its ids alone.
export module mux.plugin.packs.changes;

import std;
import mux.core.ids;

export namespace mux::plugins::packs {

// The packs of a room, or one's own, as asked for to edit.
struct packs_listed {
  account_id by;
  std::optional<std::string> room;
  std::vector<emote_pack> packs;
};
// A pack saved -- or taken away, where `removed` -- or not.
struct pack_saved {
  account_id by;
  emote_pack pack;
  bool removed = false;
  bool done = false;
};
// An image uploaded for a pack being edited: its mxc://, none where it
// was not -- for the editor's opening it was chosen in.
struct pack_picture_uploaded {
  account_id by;
  pack_picture picture;
  bool done = false;
  pack_opening opening;
};
using changes = mux::change_list<packs_listed, pack_saved, pack_picture_uploaded>;

inline std::string describe(const packs_listed& one) { return std::format("{} packs", one.packs.size()); }
inline std::string describe(const pack_saved& one) {
  return std::format("pack {} {}", one.pack.key, one.done ? (one.removed ? "taken away" : "saved") : "not saved");
}
inline std::string describe(const pack_picture_uploaded& one) { return std::format("uploaded {}", one.picture.url); }

}  // namespace mux::plugins::packs

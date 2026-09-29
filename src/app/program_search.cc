// SPDX-License-Identifier: AGPL-3.0-only
// The program: finding in a chat.
module mux.app.program;

import std;
import knot;
import skia;
import mux.core;
import mux.config;
import mux.net;
import mux.xmpp;
import mux.matrix;
import mux.media;
import mux.host;
import mux.ui;
import skiff.paint;
import skiff.scene;
import mux.app.network;
import mux.app.demo;
import mux.app.store;
import mux.app.requests;
import mux.app.words;

namespace mux::app {

void app::apply(const request::open_search&) {
  auto& screen = root().main();
  if (!screen.chosen)
    return;
  if (!searching || searching->in != *screen.chosen)
    searching = search_state{*screen.chosen, {}, {}, std::nullopt};
  screen.show_search(true);
  scene.focus(screen.search.field);
}

void app::apply(const request::close_search&) {
  searching.reset();
  root().main().show_search(false);
}

void app::apply(const request::search_typed& one) {
  if (!searching)
    return;
  searching->query = one.text;
  searching->found = this->find_in(searching->in, one.text);
  searching->at.reset();
  this->search_to(true);
}

void app::apply(const request::search_step& one) { this->search_to(one.older); }

void app::search_to(bool older) {
  if (!searching)
    return;
  auto& screen = root().main();
  const std::size_t count = searching->found.size();
  if (count > 0) {
    if (!searching->at)
      searching->at = 0;
    else if (older && *searching->at + 1 < count)
      ++*searching->at;
    else if (!older && *searching->at > 0)
      --*searching->at;
    screen.jump_to(searching->found[*searching->at]);
  }
  screen.search.show_found(searching->at, count, !searching->query.empty());
}

auto app::find_in(const mux::conversation_id& in, std::string_view query) -> std::vector<std::string> {
  const std::string asked = mux::ui::folded(query);
  if (asked.empty())
    return {};
  std::map<std::string, mux::message> all = store.everything(in);
  if (const mux::conversation* chat = model->find(in))
    for (const mux::message& one : chat->timeline)
      all.insert_or_assign(one.id, one);
  std::vector<const mux::message*> hits;
  for (const auto& [id, one] : all) {
    if (one.redacted)
      continue;
    const bool in_text = mux::ui::folded(one.body.plain).contains(asked);
    const bool in_name = one.attachment && mux::ui::folded(one.attachment->name).contains(asked);
    if (in_text || in_name)
      hits.push_back(&one);
  }
  std::ranges::sort(hits, std::ranges::greater{}, &mux::message::at);
  std::vector<std::string> out;
  for (const mux::message* one : hits)
    out.push_back(one->id);
  return out;
}

}  // namespace mux::app

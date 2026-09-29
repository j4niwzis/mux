// SPDX-License-Identifier: AGPL-3.0-only
// The program: links followed to rooms, people and messages.
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

auto app::percent_decoded(std::string_view text) -> std::string {
  std::string out;
  for (std::size_t at = 0; at < text.size(); ++at) {
    if (text[at] == '%' && at + 2 < text.size() + 0 && at + 2 <= text.size() - 1) {
      unsigned value = 0;
      if (std::from_chars(text.data() + at + 1, text.data() + at + 3, value, 16).ec == std::errc{}) {
        out += static_cast<char>(value);
        at += 2;
        continue;
      }
    }
    out += text[at];
  }
  return out;
}

auto app::link_target_of(std::string_view url) -> std::optional<link_target> {
  link_target out;
  std::string_view query;
  const auto split_query = [&](std::string_view& path) {
    if (const auto mark = path.find('?'); mark != std::string_view::npos) {
      query = path.substr(mark + 1);
      path = path.substr(0, mark);
    }
  };
  const auto read_via = [&] {
    for (std::size_t at = 0; at < query.size();) {
      auto end = query.find('&', at);
      if (end == std::string_view::npos)
        end = query.size();
      const std::string_view pair = query.substr(at, end - at);
      if (pair.starts_with("via="))
        out.via.push_back(percent_decoded(pair.substr(4)));
      at = end + 1;
    }
  };
  if (url.starts_with("https://matrix.to/#/")) {
    std::string_view path = url.substr(20);
    split_query(path);
    read_via();
    const auto slash = path.find('/');
    out.id = percent_decoded(path.substr(0, slash));
    if (slash != std::string_view::npos)
      out.event = percent_decoded(path.substr(slash + 1));
  } else if (url.starts_with("matrix:")) {
    std::string_view path = url.substr(7);
    split_query(path);
    read_via();
    const auto part = [&]() {
      const auto slash = path.find('/');
      const std::string_view one = path.substr(0, slash);
      path = slash == std::string_view::npos ? std::string_view() : path.substr(slash + 1);
      return one;
    };
    // Its parts' names (MSC2312), read into what they are.
    const uri_part_t kind = uri_part_of(part());
    const std::string name = percent_decoded(part());
    const std::optional<char> sigil = std::visit([](auto of) { return of.sigil; }, kind);
    if (!sigil)
      return std::nullopt;
    out.id = *sigil + name;
    if (std::visit([](auto of) { return of.names_event; }, uri_part_of(part())))
      out.event = "$" + percent_decoded(part());
  } else if (url.starts_with("xmpp:")) {
    std::string_view path = url.substr(5);
    split_query(path);
    out.id = percent_decoded(path);
    out.xmpp = true;
  } else {
    return std::nullopt;
  }
  if (out.id.empty() || (!out.xmpp && !std::string_view("@!#").contains(out.id.front())))
    return std::nullopt;
  return out;
}

void app::open_chat(const mux::conversation_id& which, const std::optional<std::string>& event) {
  auto& screen = root().main();
  screen.current = which.account;
  this->apply(request::choose{which});
  if (event)
    screen.jump_to(*event);
}

auto app::chat_of(const link_target& where) const -> std::optional<mux::conversation_id> {
  for (const auto& [account, one] : model->accounts())
    for (const auto& [key, chat] : one.conversations) {
      const bool matrix = mux::is_matrix(account.speaks);
      if (where.xmpp != !matrix)
        continue;
      if (chat.id.id == where.id || (chat.alias && *chat.alias == where.id))
        return chat.id;
    }
  return std::nullopt;
}

void app::go_to(const link_target& where) {
  auto& screen = root().main();
  if (!where.xmpp && where.id.front() == '@') {
    // In the chat being read: their page. Elsewhere: a chat with them.
    if (const mux::conversation* here = screen.chosen ? model->find(*screen.chosen) : nullptr;
        here && std::ranges::contains(here->members, where.id, &mux::member::id)) {
      this->apply(request::open_member_info{where.id});
      return;
    }
    for (const auto& [account, one] : model->accounts())
      for (const auto& [key, chat] : one.conversations)
        if (mux::one_to_one(chat.kind) &&
            std::ranges::contains(chat.members, where.id, &mux::member::id)) {
          this->open_chat(chat.id, std::nullopt);
          return;
        }
    root().show_message("No chat yet", std::format("There is no chat with {} yet.", where.id));
    return;
  }
  if (const auto found = this->chat_of(where)) {
    this->open_chat(*found, where.event);
    return;
  }
  if (where.xmpp) {
    root().show_message("Not joined", std::format("{} is not in your list.", where.id));
    return;
  }
  // A Matrix room not joined: joined through the account in view, or the
  // first Matrix one, and opened when it comes.
  std::optional<mux::account_id> by;
  if (screen.current && mux::is_matrix(screen.current->speaks))
    by = screen.current;
  for (const auto& [account, one] : model->accounts())
    if (!by && mux::is_matrix(account.speaks))
      by = account;
  if (!by) {
    root().show_message("No Matrix account", "A Matrix account is needed to open that room.");
    return;
  }
  pending_link = where;
  net->join(*by, where.id, where.via);
}

void app::apply(const request::cancel_compose&) {
  composing = compose::plain{};
  root().main().line.show_context(std::nullopt);
}

void app::apply(const request::load_older& one) {
  if (ask.demo)
    return;
  const mux::conversation* chat = model->find(one.in);
  const auto before = chat && !chat->timeline.empty() ? chat->timeline.front().at
                                                      : message_store::time_point::max();
  if (auto kept = store.older(one.in, before, 100); !kept.empty()) {
    for (auto it = kept.rbegin(); it != kept.rend(); ++it)
      model->apply(mux::change_t{mux::change::message_added{.message = std::move(*it), .history = true}});
    // The window may ask again: there may be more on the disk.
    root().main().history_asked.reset();
    this->refresh();
    return;
  }
  net->load_older(one.in, one.from);
}

void app::apply(const request::submit_message& one) { this->send_message(one.text); }

void app::apply(const request::resize_sidebar& one) { root().main().resize_sidebar(one.x); }

void app::apply(const request::message_person& one) {
  if (model->find(one.who) != nullptr)
    this->apply(request::choose{one.who});
  else
    root().show_notice("Starting a new chat");
}

void app::apply(const request::jump_to_message& one) { root().main().jump_to(one.id); }

}  // namespace mux::app

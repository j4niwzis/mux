// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:message -- A message's row: its bubble placed, its face, readers, reactions and menu.
export module mux.ui.messages:message;

import std;
import chevron.escape;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.compose;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.icon;
import skiff.nodes.image;
import skiff.nodes.text;
import skiff.widgets.loader;
import skiff.widgets.pill;
import mux.platform.audio;
import mux.platform.video;
import mux.core;
import mux.config;
import mux.logic.links;
import mux.protocols;
import mux.plugins.types;
import mux.ui.base;
import mux.plugins.ui;
import mux.ui.proto;


export import :message_body;

export namespace mux::ui {

// Who has read a message: those whose receipt is for it or for one after
// it, by their names in the chat -- its sender and the user aside.
[[nodiscard]] inline std::vector<seen_reader> seen_by(const conversation& chat, const std::string& id, const std::string& sender) {
  const auto place = [&](const std::string& event) -> std::optional<std::size_t> {
    const auto at = std::ranges::find(chat.timeline, event, &message::id);
    return at == chat.timeline.end() ? std::nullopt : std::optional(static_cast<std::size_t>(at - chat.timeline.begin()));
  };
  const auto mine = place(id);
  if (!mine)
    return {};
  const auto when = chat.timeline[*mine].at;
  auto out = chat.read_by | std::views::filter([&](const auto& read) {
               const auto& [user, event] = read;
               if (user == sender || user == chat.id.account.address)
                 return false;
               // Read up to a message here at or after it; else, the receipt
               // pointing at what is not among these -- a reaction, a state
               // event -- read at or after it was sent.
               if (const auto theirs = place(event))
                 return *theirs >= *mine;
               const auto read_at = chat.receipt_times.find(user);
               return read_at != chat.receipt_times.end() && read_at->second >= when;
             }) | std::views::transform([&](const auto& read) {
               const auto read_at = chat.receipt_times.find(read.first);
               return seen_reader{read.first, sender_name(chat, read.first),
                                  read_at == chat.receipt_times.end() ? std::nullopt : std::optional(read_at->second)};
             }) | std::ranges::to<std::vector>();
  std::ranges::sort(out, {}, &seen_reader::name);
  return out;
}

// A message's menu, for a right press on its row wherever it is shown -- the
// timeline, a thread: what it offers, from the message, its chat as the
// model has it now, what is selected in its text and the link pressed.
struct row_menu {
  bubble_needs needs;
  conversation_id in;
  message said;
  bool version = false;
  std::optional<menu_facts> operator()(auto& row, float x, float y) const {
    const bubble_words_t* text = words_in(row);
    const conversation* chat = needs.now ? needs.now->find(in) : nullptr;
    menu_facts facts;
    facts.id = said.id;
    if (chat && !facts.id.empty() && !held_message(*chat, facts.id))
      facts.event = said;
    facts.own = said.outgoing;
    facts.text = said.body.plain;
    facts.selection = text && text->hasSelection();
    if (facts.selection)
      facts.copied_fragment = text->selectedFragment();
    facts.copied = facts.copied_fragment ? facts.copied_fragment->text : said.body.plain;
    if (const auto& kept = said.attachment) {
      facts.media = kept->video.value_or(kept->source);
      facts.media_name = kept->name;
      facts.moving = moves(kept->kind);
      if (is_picture(kept->kind) || kept->video)
        facts.picture = kept->source;
      facts.captioned = is_picture(kept->kind) && !kept->video;
    }
    if (chat) {
      const message* held = held_message(*chat, said.id);
      const message& current = held ? *held : said;
      facts.pinned = std::ranges::contains(chat->pinned, said.id);
      const protocol_state_t now = protocol_state_of(*needs.shared, chat->id.account);
      // What its account does, offered only where anything may be done now.
      if (proto::available(now))
        facts.can = ops_of(*needs.shared, chat->id.account);
      facts.can.react = facts.can.react && proto::may_react(now, *chat, current);
      facts.pinnable = proto::can_pin(now, said.id);
      // Edited as its protocol's rule allows: any of one's own, or the last.
      facts.editable = !said.service && !said.reaction && proto::may_edit(now, *chat, said);
      facts.history = !said.versions.empty();
      // Delete as the protocol allows it -- Matrix: as the room's power levels do.
      facts.deletable = proto::may_delete(now, *chat, said.outgoing);
      facts.reaction_events = !current.reaction_events.empty();
      facts.reaction_count = std::ranges::fold_left(current.reactions | std::views::values | std::views::transform([](const auto& who) { return who.size(); }),
                                                    std::size_t{0}, std::plus{});
      // The message's link, where its protocol has one.
      if (auto link = proto::message_link(now, *chat, said.id))
        facts.link = std::move(*link);
      facts.seen = seen_by(*chat, said.id, said.sender);
    }
    // The link the press was on: one in the text -- its text asked a menu
    // of its own with it, which this one is in place of -- or the preview's,
    // where the pointer is on it.
    if (auto pressed = skiff::scene::pressedLink())
      facts.pressed_link = std::move(*pressed);
    else if (const previewed_link_t* preview = skiff::scene::findNode<previewed_link_t>(row); preview && preview->bounds().contains(x, y))
      facts.pressed_link = preview->fTag.url;
    facts.emoji = copied_emoji_at(facts.pressed_link);
    // A sticker: what sending it again takes.
    if (said.sticker && said.attachment)
      facts.sticker = emote{.shortcode = said.attachment->name,
                            .url = said.attachment->source,
                            .body = said.attachment->name,
                            .w = said.attachment->width > 0 ? std::optional<std::int64_t>(said.attachment->width) : std::nullopt,
                            .h = said.attachment->height > 0 ? std::optional<std::int64_t>(said.attachment->height) : std::nullopt,
                            .mimetype = said.attachment->mimetype.empty() ? std::nullopt : std::optional<std::string>(said.attachment->mimetype)};
    // A reaction shown as a line: what it is on, and with what.
    if (!said.reaction_key.empty() && said.replies_to)
      facts.reaction = menu_facts::reaction_facts{*said.replies_to, said.reaction_key};
    // A row of what is not held as an event of its own -- a reaction kept
    // without its id: nothing it offers can be aimed at it.
    if (facts.id.empty()) {
      facts.can.react = false;
      facts.deletable = false;
    }
    if (version) {
      facts.editable = false;
      facts.history = false;
    }
    facts.x = x;
    facts.y = y;
    return facts;
  }
};
// A reaction shown as a line: pressed anywhere, to what it is on.
struct reaction_line_press {
  std::optional<std::string> on;
  std::string id;
  std::optional<request::jump_to_message> operator()(auto&, float, float) const {
    return on ? std::optional(request::jump_to_message{*on, std::nullopt, id}) : std::nullopt;
  }
};
[[nodiscard]] inline bool is_reaction_line(const message& said) {
  return said.service && said.replies_to &&
         spl::visit(spl::overloaded{[](room_event::reactions) { return true; }, [](room_event::unreactions) { return true; },
                                    [](const auto&) { return false; }},
                    said.event_kind);
}


// A message's row: the avatar's room and the bubble -- at the right where it
// is one's own, in the middle where it is something done -- the arrow a
// swipe shows, the unread bar over it, who has read it under it. Swiped
// left, it is answered; washed while it is selected, flashed where a jump
// went to it; coming in moving where it has just come (arrives).
auto message_row(const bubble_needs& n, const conversation& in, const bubble_facts& f, const model* now, bool arrives) {
  namespace c = skiff::compose;
  const palette& colours = *n.colours;
  const looks_shown& looks = *n.looks;
  const message& said = f.said;
  const bool group = is_group(in);
  const bool as_lines = spl::visit(spl::overloaded{[](proto::part::style::lines) { return true; }, [](proto::part::style::bubbles) { return false; }},
                                   proto::message_style(protocol_state_of(*n.shared, in.id.account)));
  // As tdesktop: a sender's messages one under the other nearly touch; where
  // the sender changes, a gap. The avatar hangs at the row's bottom left, out
  // of its flow, the row keeping its width on the left.
  const bool with_face = group && !said.outgoing && !said.service;
  const bool face_shown = with_face && f.last;
  auto face = c::visible(with_face, c::styled({.place = scene::anchor::kBottomLeft, .x = -(kBubbleAvatar + 8.0f), .y = -1.0f,
                                               .alpha = face_shown ? static_cast<float>(element_opacity_of(looks.bubbles, &config::element_opacity::avatars)) / 100.0f
                                                                   : 0.0f},
                                              c::onPress(open_face{said.sender, face_shown},
                                                         avatar_mark(avatar_of::person{}, said.sender, sender_name(in, said.sender), kBubbleAvatar),
                                                         sender_name(in, said.sender))));
  const nodes::Justify justify = said.service                    ? nodes::Justify{nodes::justify::middle{}}
                                 : said.outgoing && !as_lines ? nodes::Justify{nodes::justify::end{}}
                                                              : nodes::Justify{nodes::justify::start{}};
  auto inner = c::onClickAt(reaction_line_press{is_reaction_line(said) ? said.replies_to : std::nullopt, said.id},
                            c::row(c::justified(c::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY,
                                                               .padding = {f.first ? 8.0f : 1.0f, 0.0f, 1.0f, with_face ? kBubbleAvatar + 8.0f : 0.0f}}),
                                                justify),
                                   std::move(face), message_body(n, in, f, now)));
  auto mark = c::styled({.place = scene::anchor::kCentreRight, .x = -6.0f, .width = 28.0f, .height = 28.0f, .cornerRadius = 14.0f,
                         .background = colours.tile, .alpha = 0.0f},
                        nodes::Icon(shape_of(icon::back{}), colours.dim));
  // Over the first unread message of a chat opened: tdesktop's bar, across
  // the whole list -- held out past the row's sides and the list's.
  std::optional<decltype(unread_bar(colours))> unread;
  if (f.unread_start) {
    unread.emplace(unread_bar(colours));
    unread->apply({.margin = {0.0f, -kListSide, 0.0f, -kListSide}});
  }
  std::optional<readers_row_t> readers;
  if (!f.readers.empty()) {
    readers.emplace(readers_row(colours, in, f.readers));
    readers->apply({.place = scene::anchor::kBottomRight, .x = -4.0f, .y = kReadersHeight + 1.0f});
  }
  // Drawn once and played back until something in it changes -- not one
  // with a picture or a file (a loader turns in it while it comes, a picture
  // may move), nor a frosted one (its backdrop is where it is on the screen).
  const bool frosted = frosts(looks.bubbles);
  auto row = c::onRightPressAt(row_menu{n, in.id, said, f.version}, c::column(c::vbox(0.0f, {.fillX = true, .autoSize = scene::axes::kY,
                                      .padding = {f.unread_start ? kUnreadBarHeight + 6.0f : 0.0f, 0.0f,
                                                  f.readers.empty() ? 0.0f : kReadersHeight + 2.0f, 0.0f}}),
                       c::swiped(kSwipeToReply, 120.0f, sends<request::reply_to>{{said.id, said.body.plain}}, swipe_shown{&colours},
                                 std::move(mark), std::move(inner)),
                       std::move(unread), std::move(readers)));
  skiff::compose::state_of(row).setRecorded(!said.attachment && said.album.empty() && !frosted);
  // Not shown where the chat's settings say so: kept, and out of the flow.
  row.setVisible(!said.service || f.events_shown);
  return c::washed_for<message_marks>(colours.accent, 56.0f, 80.0f,
      [id = said.id](const message_marks& marks) {
        return skiff::compose::Wash{.held = marks.previewed ? marks.previewed->contains(id) : marks.selected.contains(id),
                                    .flash = marks.flashed == id ? marks.flash : 0};
      },
      c::appearing(arrives,
                   // Messages being selected: a press on one is the list's --
                   // it selects it or lets it go, or a drag spans several --
                   // not what is pressed in it.
                   c::held_off_for<message_marks>([](const message_marks& marks) { return !marks.selected.empty(); }, std::move(row))));
}
using message_row_t = decltype(message_row(std::declval<const bubble_needs&>(), std::declval<const conversation&>(),
                                           std::declval<const bubble_facts&>(), nullptr, false));
// A message shown alone, as the chat shows it -- in a list of mentions, an
// edit history, who reacted: its row, first and last of its run as said.
message_row_t message_alone(const ui_needs& n, const conversation& in, const message& said, bool first, bool last,
                                   const model* now, bool version = false) {
  return message_row(bubble_needs{.colours = n.colours, .looks = n.looks, .shared = n.shared, .now = now}, in,
                     bubble_facts{.said = said, .first = first, .last = last, .version = version}, now, false);
}

}  // namespace mux::ui

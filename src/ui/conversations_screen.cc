// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:conversations_screen -- The conversations screen: the list, the chat, its info.
export module mux.ui:conversations_screen;

import std;
import mux.logic.text;
import mux.logic.emoji;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.bind;
import skiff.compose;
import skiff.model;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.button;
import skiff.widgets.pill;
import skiff.widgets.textarea;
import mux.core;
import mux.protocols;
import mux.config;
import mux.kept_root;
import skiff.model;
import mux.plugins.types;
import mux.ui.base;
import mux.plugins.ui.top;
import mux.ui.proto;
import :chat_list;
import mux.ui.messages;
import :header;
import :info;

export import :conversations_side;

export namespace mux::ui {

namespace head_view_defaults {
constexpr proto::sticker_view_list<> head_views(const auto&) {
  return {};
}
constexpr std::nullopt_t make_head_view(const auto&, const conversation&) {
  return std::nullopt;
}
}  // namespace head_view_defaults
template <class State>
constexpr auto head_views_for(const State& state) {
  using head_view_defaults::head_views;
  return head_views(state);
}
namespace composer_view_defaults {
constexpr proto::sticker_view_list<> composer_views(const auto&) {
  return {};
}
constexpr std::nullopt_t make_composer_view(const auto&, const conversation&) {
  return std::nullopt;
}
}  // namespace composer_view_defaults
template <class State>
constexpr auto composer_views_for(const State& state) {
  using composer_view_defaults::composer_views;
  return composer_views(state);
}

// A row of a list over the field -- the @ list's, the emoji list's: what
// it shows at its start, its text beside it; pressed to pick.
template <class Pick, class Face>
auto suggestion_row(const palette& colours, Pick pick, Face face, std::string first, std::string second) {
  namespace c = skiff::compose;
  auto label = first;
  return c::onPress(std::move(pick), c::row(
      c::hbox(10.0f, {.fillX = true, .height = 44.0f, .padding = {0.0f, 14.0f, 0.0f, 14.0f},
                     .hoverBackground = colours.chosen, .selectedBackground = colours.chosen}),
      std::move(face), two_lines(colours, std::move(first), std::move(second), 14.0f, 1.0f)), std::move(label));
}
// No account at all: what to do about it.
inline auto no_accounts_yet(const palette& colours) {
  using add_button = button_for<sends<::mux::ui::request::open_new_account>>;
  return skiff::compose::column(
      skiff::compose::vbox(12.0f, {.fillX = true, .autoSize = scene::axes::kY, .padding = {120.0f, 48.0f, 0.0f, 48.0f}}),
      nodes::Text("No accounts yet", 22.0f, colours.text, true),
      skiff::compose::styled({.fillX = true},
                             wrapped(nodes::Text("Add an XMPP or a Matrix account, and its chats will be here.", 14.0f, colours.dim))),
      skiff::compose::styled({.width = 140.0f, .height = 36.0f}, primary(add_button(colours.widgets, "Add account", {}))));
}
// No chat chosen: the wallpaper, and in its middle a small pill saying what
// to do, as tdesktop's (its service message look).
inline auto select_a_chat() {
  return skiff::compose::column(
      skiff::compose::justified(skiff::compose::vbox(0.0f, {.fillX = true, .grow = scene::axes::kY}), nodes::justify::middle{}),
      skiff::compose::styled({.alignSelf = scene::align::kMiddle},
                             widgets::Pill("Select a chat to start messaging",
                                           {.plate = skia::colorSetARGB(0x66, 0, 0, 0), .size = 13.0f, .height = 26.0f, .padX = 12.0f})));
}

struct conversations_screen : skiff::compose::Stacked {
  // What its keys and its swipes ask for.
  using Answer = std::variant<::mux::ui::request::toggle_threads, ::mux::ui::request::toggle_info, ::mux::ui::request::close_chat, ::mux::ui::request::choose, ::mux::ui::request::open_search, ::mux::ui::request::ask_link, ::mux::ui::request::reply_step, ::mux::ui::request::edit_last, ::mux::ui::request::selection_cancel, ::mux::ui::request::selection_copy, ::mux::ui::request::selection_delete, ::mux::ui::request::close_search, ::mux::ui::request::cancel_compose, ::mux::ui::request::jump_to_end, ::mux::ui::request::open_drawer, ::mux::ui::request::toggle_mute_of>;
  // What it was handed, for the rows it makes.
  ui_needs needs_;
  std::optional<conversation_id> chosen;
  // The account whose chats are listed.
  std::optional<account_id> current;
  // Dismissed warnings stay hidden for this chat until their text changes.
  std::map<conversation_id, std::string> dismissed_banners;
  // Which of the chat's pins the bar shows, by its place among them: the
  // one above the view (the newest until that is worked out).
  std::size_t pinned_step = 0;
  std::optional<conversation_id> pinned_of;
  // Which room events a chat shows, kind by kind, as the program's settings
  // say: set by the program before it shows the model; a chat not in it
  // shows them all.
  std::map<conversation_id, room_event_filter> event_filters;
  // Whether a chat's view has a message: said, or a room event its filter
  // shows -- what the reader can see, and so answer.
  [[nodiscard]] bool shown_in(const conversation_id& chat, const message& said) const {
    if (!said.service)
      return true;
    const auto found = event_filters.find(chat);
    return found == event_filters.end() || found->second.shows(said.event_kind);
  }
  // Chats listed in another account's list than their own: each
  // account's, the chats moved out of their own, and the strip each shows
  // where it is listed elsewhere -- none where it is off. As the program
  // keeps them.
  std::map<account_id, std::vector<conversation_id>> listed_in;
  std::set<conversation_id> moved_out;
  std::map<conversation_id, skia::SkColor> strips;
  // The chats that show who has read up to where, as faces.
  std::set<conversation_id> receipts_in;
  // The chats that show no link previews.
  std::set<conversation_id> previews_off;
  // The @ list: the chat's members matching what follows an @ at the end of
  // what is written, as Telegram's; who was picked from it, to be sent as
  // mentions with the message.
  std::vector<member> mention_matches;
  std::size_t mention_lit = 0;
  std::string mention_query;
  struct pick_mention {
    conversations_screen* screen;
    std::size_t index;
    skiff::model::Nothing operator()() const { screen->choose_mention(index); return {}; }
  };
  // The emoji list: the emoji whose names or keywords (CLDR's, English and
  // Russian) have what follows a ':' at the end of what is written -- one at
  // its start or after a space, two letters at least -- as tdesktop's
  // suggestions; the one picked put in their place.
  struct emoji_choice { std::string text, name, picture; };
  std::vector<emoji_choice> emoji_matches;
  std::size_t emoji_lit = 0;
  std::string emoji_query;
  std::optional<conversation_id> emoji_room;
  struct pick_emoji {
    conversations_screen* screen;
    std::size_t index;
    skiff::model::Nothing operator()() const { screen->choose_emoji(index); return {}; }
  };
  // A row of a list over the field -- the @ list's, the emoji list's: what
  // it shows at its start, its text beside it; lit while Up and Down are
  // on it, pressed to pick.

  using emoji_face = spl::variant<nodes::Text, nodes::Image<from_emotes>>;
  using mention_row = decltype(suggestion_row(std::declval<const palette&>(), std::declval<pick_mention>(),
      std::declval<legacy::avatar_mark>(), std::string{}, std::string{}));
  using emoji_row = decltype(suggestion_row(std::declval<const palette&>(), std::declval<pick_emoji>(),
      std::declval<emoji_face>(), std::string{}, std::string{}));
  template <class Row> struct suggestion_list : skiff::compose::Stacked {
    struct parts_t {
      std::vector<Row> rows;
    } parts;
    explicit suggestion_list(const palette &colours)
        : Stacked(skiff::compose::vbox(0.0f, {.fillX = true,
                                              .autoSize = scene::axes::kY,
                                              .background = colours.sidebar})) {
    }
  };
  using mention_list = suggestion_list<mention_row>;
  using emoji_list = suggestion_list<emoji_row>;
  // The account to list once the model has it: the one shown last, kept.
  // Taken the first time it is there; dropped when an account is chosen.
  std::optional<account_id> wanted;
  bool info_open = false;
  // How wide the chat list and the chat's info are: their own, whatever the
  // window's size, until their edges are dragged.
  float side_width = 300.0f;
  float info_width = 340.0f;

  // The folder whose chats are listed.
  folder_t folder = folder::all{};
  // The folders the tabs were made for, and the one chosen then.
  std::vector<std::pair<std::string, folder_t>> shown_folders;
  folder_t shown_folder = folder::all{};
  // Each list keeps its own offset; a short space list must not make Home
  // follow its end when Home's longer contents are restored.
  using list_place = std::tuple<std::optional<account_id>, folder_t, std::optional<std::string>>;
  std::optional<list_place> listed_place;
  std::vector<std::pair<list_place, float>> list_positions;
  std::optional<float> restoring_list_at;
  void restore_list_position() {
    const list_place next{current, folder, forum_open};
    if (listed_place && *listed_place == next)
      return;
    if (listed_place) {
      const auto saved = std::ranges::find(list_positions, *listed_place, &std::pair<list_place, float>::first);
      if (saved != list_positions.end())
        saved->second = list.current();
      else
        list_positions.emplace_back(*listed_place, list.current());
    }
    listed_place = next;
    const auto saved = std::ranges::find(list_positions, next, &std::pair<list_place, float>::first);
    restoring_list_at = saved == list_positions.end() ? 0.0f : saved->second;
    list.setCurrent(*restoring_list_at);
  }

  // The space bars, as the program says: at all, the top one, and where
  // each item is put; and what they were made for, not made again unchanged.
  bool spaces_on = true;
  bool top_bar_on = true;
  // The spaces shown as forums, and the one open in the list -- its rooms
  // listed, as tdesktop lists a forum's topics.
  std::set<conversation_id> forums;
  // Spaces whose rooms Home leaves out, each as it chose.
  std::set<conversation_id> hidden_from_home;
  std::optional<std::string> forum_open;
  // A forum gone to by Alt+Up or Alt+Down: lit as chosen, not yet opened --
  // Alt+Right opens it, as Alt+Left leaves one open.
  std::optional<conversation_id> pointed;
  // A chat shown as a forum: a space the user shows so, or a forum of its
  // protocol's own (proto::native_forum).
  [[nodiscard]] bool shown_as_forum(const conversation& one) const {
    return one.space && (forums.contains(one.id) || proto::native_forum(protocol_state_of(*needs_.shared, one.id.account), one));
  }
  [[nodiscard]] bool is_forum(const conversation_id& id) const {
    const conversation* one = last_model ? last_model->find(id) : nullptr;
    return one && this->shown_as_forum(*one);
  }
  // Home without what spaces hold, but direct messages -- and without those
  // too, where that is chosen as well.
  bool home_hides_spaced = false;
  bool home_hides_direct = false;
  // The messages a search in the chat found, in the chats' place while it is
  // asked; none, the chats again.
  void show_search_results(std::vector<search_result> results, std::optional<std::size_t> count) {
    auto& rows = std::get<0>(std::get<0>(side.found.fChildren).fChildren);
    rows.clear();
    rows.reserve(results.size());
    for (const search_result& one : results)
      rows.emplace_back(*needs_.colours, one);
    const bool on = count.has_value();
    side.found_title.setText(!count ? std::string() : *count == 0 ? std::string("No messages found")
                                    : *count == 1                ? std::string("1 message found")
                                                                 : std::format("{} messages found", *count));
    side.found_title.setVisible(on);
    side.found.setVisible(on);
    side.list.setVisible(!on);
    side.found.scrollToStart();
    side.invalidateLayout();
    side.markDamaged();
  }
  // The server's answers for what is searched, where nothing joined matched
  // it: kept while they are for what is typed now.
  void found_rooms_elsewhere(const std::string& query, const std::vector<directory_room>& rooms) {
    if (query.empty() || query != asked_elsewhere)
      return;
    rooms_elsewhere = rooms;
    rooms_came = true;
    this->show_elsewhere();
  }
  void found_people_elsewhere(const std::string& query, const std::vector<found_person>& people) {
    if (query.empty() || query != asked_elsewhere)
      return;
    people_elsewhere = people;
    people_came = true;
    for (const found_person& one : people)
      if (one.avatar && !one.avatar->empty())
        listed_avatars().emplace_back(one.id, *one.avatar);
    this->show_elsewhere();
  }
  void show_elsewhere() {
    auto rooms = rooms_elsewhere;
    if (current && last_model)
      std::erase_if(rooms, [&](const auto& room) { return last_model->find(conversation_id{*current, room.id}); });
    if (const auto address = room_address_for(current, asked_elsewhere))
      if (std::ranges::none_of(rooms, [&](const auto& room) { return room.id == *address || room.alias == *address; }) &&
          (!last_model || !last_model->find(conversation_id{*current, *address})))
        rooms.insert(rooms.begin(), directory_room{.id = *address, .name = "Join room by address", .alias = *address});
    std::get<0>(side.elsewhere.fChildren).read(std::optional(server_search_facts{
        std::move(rooms), people_elsewhere, current, !rooms_came || !people_came}));
    side.elsewhere.scrollToStart();
    side.elsewhere.invalidateLayout();
    side.invalidateLayout();
    side.markDamaged();
  }
  void open_forum(std::string room) {
    forum_open = std::move(room);
    this->slide_list(1.0f);
    if (last_model)
      this->show(*last_model, false);
  }
  void close_forum() {
    forum_open.reset();
    this->slide_list(-1.0f);
    if (last_model)
      this->show(*last_model, false);
  }
  // The list sliding in as what it lists changes -- another space, a forum
  // opened or left: from the side it comes from, faded in; only drawn
  // moved, nothing laid out again for it.
  skiff::paint::Tween list_in{1.0f, 200.0f, skiff::paint::movement::subtle{}};
  // How many of the chats listed are made into rows: the first few
  // screens, more as the reader scrolls near the last made. A space of
  // hundreds of chats was hundreds of rows made in the frame it was chosen
  // in -- the slide waited on them.
  static constexpr std::size_t kChatsFirst = 40, kChatsStep = 40;
  // The chats listed made into rows: from chats_from to chats_made, those
  // a screen above the view to two below it, in steps of kChatsStep -- as
  // Qt's list view with uniform rows, the rest stand-ins of their height
  // (the flow's padding above and below), not rows reconciled at every
  // refresh. Made rows grew without end as the list was scrolled down.
  std::size_t chats_from = 0;
  std::size_t chats_made = kChatsFirst;
  std::size_t chats_listed = 0;
  // Every chat listed, in order; and where each one's row begins, from
  // their heights, known without making them -- one more: where the last
  // ends.
  std::vector<conversation_id> order;
  std::vector<float> tops;
  // The chats to make for where the list is now.
  [[nodiscard]] std::pair<std::size_t, std::size_t> chats_window() const {
    const std::size_t count = order.size();
    if (tops.size() != count + 1 || count == 0)
      return {0, std::min(kChatsFirst, count)};
    const float view = std::max(list.bounds().height(), 400.0f);
    const float at = restoring_list_at.value_or(list.current());
    const auto index_at = [&](float y) {
      const auto past = std::ranges::upper_bound(tops, y);
      return std::min(count, static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, past - tops.begin() - 1)));
    };
    std::size_t from = index_at(at - view);
    from -= from % kChatsStep;
    std::size_t to = index_at(at + 2.0f * view) + 1;
    to += (kChatsStep - to % kChatsStep) % kChatsStep;
    return {from, std::min(count, std::max(to, from + kChatsFirst))};
  }
  // What the list shows, as the model had it at the last show: read again
  // from the model, not kept up by hand.
  chat_listing listing;
  [[nodiscard]] chat_listing listing_of(const model& now) const;
  // The rows for where the list is now, from the listing.
  void show_rows(const model& now);
  float list_from = 1.0f;
  // Begun two frames on: the frame the list is made in -- its rows made,
  // laid out, their avatars scaled -- took its time out of the slide's
  // first frames, and the slide jumped, then went on.
  int slide_wait = 0;
  void slide_list(float from) {
    chats_from = 0;
    chats_made = kChatsFirst;
    list_from = from;
    list_in.jump(0.0f);
    slide_wait = 2;
    this->place_list();
    scene::work::mark(fState.fId);
  }
  // Slid only, not faded: a faded list played each row back through a layer
  // of its own, at every frame of the slide -- it crawled.
  void place_list() {
    const float value = list_in.value();
    for (scene::Node* each : std::initializer_list<scene::Node*>{&side.list, &side.forum_head})
      each->apply({.shiftX = (1.0f - value) * 32.0f * list_from});
  }
  std::vector<config::space_placed> space_places;
  std::vector<std::string> shown_bars;
  // A phone's way (the user's, #13548): a window narrow and taller than it is
  // wide shows one thing at a time, all of it across -- the chats, a chat,
  // or its info or threads -- not side by side; and how wide it is.
  bool single = false;
  float single_width = 0.0f;
  // Where a swipe across the chat began, single: left to right, back to
  // the chats.
  std::optional<skia::SkPoint> swipe_from;
  // The row a swipe across the chats began on: right to left, muted or not,
  // as Telegram's quick action.
  std::optional<conversation_id> swipe_row;
  // The spaces along the top bigger, for a finger: a long press on them.
  bool big_spaces = false;
  // The panel docked under the field, as high as it is: the chat standing
  // over it, its field just above it.
  float docked_applied = 0.0f;
  void follow_docked() {
    if (needs_.shared->docked_panel_height == docked_applied)
      return;
    docked_applied = needs_.shared->docked_panel_height;
    // Zero too when the panel goes, or the window grows out of a phone's:
    // the space under the field given back.
    chat.apply({.padding = {0.0f, 0.0f, docked_applied, 0.0f}});
    this->markDamaged();
  }
  // Single, the one shown -- the chats (0), the chat (1), its info or
  // threads (2) -- and it sliding in as it changes, as a phone's: from the
  // right going in, from the left coming back (the user's, #13662).
  int pane_shown = -1;
  float pane_from = 0.0f;
  skiff::paint::Tween pane_in{1.0f, 240.0f, skiff::paint::movement::sweeping{}};
  void place_pane() {
    const float shift = single ? (1.0f - pane_in.value()) * single_width * pane_from : 0.0f;
    for (scene::Node* each : std::initializer_list<scene::Node*>{&side, &chat, &info, &parts.threads})
      each->apply({.shiftX = shift});
  }
  struct pick_folder {
    conversations_screen* screen;
    void operator()(const folder_t& which) const { screen->choose_folder(which); }
  };
  // The @ list, as what is written now asks: what follows the last @ at
  // its end -- one at its start or after a space, with no space after it --
  // matched against the chat's members, by name or ID, as Telegram does.
  void find_mentions() {
    const std::string& text = composer_text(chat.line);
    std::optional<std::string> query;
    if (const auto at = text.rfind('@'); at != std::string::npos && (at == 0 || text[at - 1] == ' ' || text[at - 1] == '\n')) {
      const std::string_view after = std::string_view(text).substr(at + 1);
      if (after.find_first_of(" \n") == std::string_view::npos)
        query = std::string(after);
    }
    const conversation* in = chosen && last_model ? last_model->find(*chosen) : nullptr;
    if (!query || in == nullptr || !is_group(*in)) {
      if (chat.parts.mentions.visible()) {
        chat.parts.mentions.setVisible(false);
        mention_matches.clear();
        fState.relayoutQuietly();
      }
      mention_query.clear();
      return;
    }
    if (*query == mention_query && chat.parts.mentions.visible())
      return;
    mention_query = *query;
    constexpr auto lower = mux::logic::folded;
    const std::string wanted = lower(*query);
    mention_matches.clear();
    for (const member& one : in->members) {
      if (chosen && one.id == chosen->account.address)
        continue;
      if (lower(one.name).find(wanted) != std::string::npos || lower(one.id).find(wanted) != std::string::npos)
        mention_matches.push_back(one);
      if (mention_matches.size() == 6)
        break;
    }
    auto& rows = chat.parts.mentions.parts.rows;
    rows.clear();
    for (std::size_t i = 0; i < mention_matches.size(); ++i)
      rows.push_back(suggestion_row(*needs_.colours, pick_mention{this, i},
                        legacy::avatar_mark(avatar_of::person{}, mention_matches[i].id,
                                    mention_matches[i].name.empty() ? mention_matches[i].id : mention_matches[i].name, 28.0f),
                        mention_matches[i].name.empty() ? mention_matches[i].id : mention_matches[i].name, mention_matches[i].id));
    mention_lit = 0;
    if (!rows.empty())
      rows.front().fState.apply({.selected = true});
    chat.parts.mentions.setVisible(!rows.empty());
    fState.relayoutQuietly();
  }
  // One picked: the @ and what follows it made their name, and they kept
  // to be mentioned when it is sent.
  void choose_mention(std::size_t index) {
    if (index >= mention_matches.size())
      return;
    const member one = mention_matches[index];
    const std::string& text = composer_text(chat.line);
    const auto at = text.rfind('@');
    if (at == std::string::npos)
      return;
    put_mention(chat.line, at, one.name.empty() ? one.id : one.name, one.id);
    mention_query.clear();
    mention_matches.clear();
    chat.parts.mentions.parts.rows.clear();
    chat.parts.mentions.setVisible(false);
    fState.relayoutQuietly();
  }
  // The emoji list, as what is written now asks.
  void find_emoji() {
    const std::string& text = composer_text(chat.line);
    std::optional<std::string> query;
    if (const auto colon = text.rfind(':');
        colon != std::string::npos && (colon == 0 || text[colon - 1] == ' ' || text[colon - 1] == '\n')) {
      const std::string_view after = std::string_view(text).substr(colon + 1);
      if (after.size() >= 2 && after.find_first_of(" \n:") == std::string_view::npos)
        query = std::string(after);
    }
    auto& list = chat.parts.emojis;
    if (!query) {
      if (list.visible()) {
        list.setVisible(false);
        emoji_matches.clear();
        fState.relayoutQuietly();
      }
      emoji_query.clear();
      return;
    }
    // The same as before: as it was -- closed, where Esc closed it.
    if (*query == emoji_query && emoji_room == chosen)
      return;
    emoji_query = *query;
    emoji_room = chosen;
    emoji_matches.clear();
    const auto* room = chosen && last_model ? last_model->find(*chosen) : nullptr;
    const auto wanted = mux::logic::folded(*query);
    if (room && proto::offers(protocol_state_of(*needs_.shared, room->id.account), proto::feature::sticker_packs{}))
      for (const auto& emoji : room->emotes) {
        if (!mux::logic::folded(emoji.shortcode).contains(wanted) && !mux::logic::folded(emoji.body).contains(wanted)) continue;
        const auto label = ":" + emoji.shortcode + ":";
        if (std::ranges::any_of(emoji_matches, [&](const auto& old) { return old.picture == emoji.url && old.text == label; })) continue;
        emoji_matches.push_back({label, emoji.shortcode, emoji.url});
        if (emoji_matches.size() == 6) break;
      }
    for (const auto* emoji : mux::logic::emoji_found(*query, 6 - emoji_matches.size()))
      emoji_matches.push_back({std::string(emoji->text), std::string(emoji->name), {}});
    auto& rows = list.parts.rows;
    rows.clear();
    for (std::size_t i = 0; i < emoji_matches.size(); ++i) {
      const auto& emoji = emoji_matches[i];
      emoji_face face(std::in_place_index<0>, nodes::Text(emoji.text, 22.0f, needs_.colours->text));
      if (!emoji.picture.empty()) {
        face.template emplace<1>(from_emotes{emoji.picture});
        auto& picture = spl::get<1>(face);
        picture.apply({.width = 28.0f, .height = 28.0f, .alignSelf = scene::align::kMiddle});
      }
      rows.push_back(suggestion_row(*needs_.colours, pick_emoji{this, i}, std::move(face), emoji.name, emoji.picture.empty() ? "" : emoji.text));
    }
    emoji_lit = 0;
    if (!rows.empty())
      rows.front().fState.apply({.selected = true});
    list.setVisible(!rows.empty());
    fState.relayoutQuietly();
  }
  // One picked: the ':' and what follows it made that emoji.
  void choose_emoji(std::size_t index) {
    if (index >= emoji_matches.size())
      return;
    const auto emoji = emoji_matches[index];
    const auto colon = composer_text(chat.line).rfind(':');
    if (colon == std::string::npos)
      return;
    if (emoji.picture.empty()) put_over_end(chat.line, colon, emoji.text);
    else {
      composer_field(chat.line).select(colon, composer_text(chat.line).size());
      composer_field(chat.line).insertAtom("\u2003", emoji.picture, emoji.text, true);
    }
    emoji_query.clear();
    emoji_matches.clear();
    chat.parts.emojis.parts.rows.clear();
    chat.parts.emojis.setVisible(false);
    fState.relayoutQuietly();
  }
  // Up and Down through a list over the field, Enter or Tab picks, Esc
  // closes it: true where the key was the list's.
  template <class List, class Choose>
  bool list_keys(List& list, std::size_t& lit, Choose choose, const scene::key::down& press) {
    namespace keys = scene::keys;
    auto& rows = list.parts.rows;
    // Modified arrows belong to the screen's reply/chat shortcuts, even
    // while mention or emoji suggestions are visible.
    if (!list.visible() || rows.empty() ||
        press.modifiers.template has<scene::modifier::control>() ||
        press.modifiers.template has<scene::modifier::alt>())
      return false;
    if (press.key == keys::kUp || press.key == keys::kDown) {
      rows[lit].fState.apply({.selected = false});
      const std::size_t n = rows.size();
      lit = press.key == keys::kUp ? (lit + n - 1) % n : (lit + 1) % n;
      rows[lit].fState.apply({.selected = true});
      return true;
    }
    if (press.key == keys::kEnter || press.key == keys::kTab) {
      choose(lit);
      return true;
    }
    if (press.key == keys::kEscape) {
      list.setVisible(false);
      fState.relayoutQuietly();
      return true;
    }
    return false;
  }
  // The keys, while the list is up: Up and Down through it, Enter picks,
  // Esc closes it -- before the input reads Enter as sending.
  std::optional<Answer> onKey(scene::phase::capture, const scene::key::down& press, scene::Reply& reply) {
    namespace keys = scene::keys;
    // Selection shortcuts precede the composer and its suggestion lists.
    if (chat.parts.selection.visible()) {
      const bool control = press.modifiers.template has<scene::modifier::control>();
      const bool other = press.modifiers.template has<scene::modifier::shift>() || press.modifiers.template has<scene::modifier::alt>();
      if (press.key == keys::kC && control && !other) {
        reply.handle();
        return request::selection_copy{};
      }
      if (press.key == keys::kDelete && !control && !other) {
        reply.handle();
        return request::selection_delete{};
      }
    }
    // Alt+Right: into the forum gone to; Alt+Left: out of the one open, to
    // its row. Taken before the field: it moves its caret on Left and Right
    // whatever the modifiers, and took Alt+Left from under this.
    if (press.modifiers.template has<scene::modifier::alt>() && press.key == keys::kRight && pointed) {
      reply.handle();
      return ::mux::ui::request::choose{*std::exchange(pointed, std::nullopt)};
    }
    if (press.modifiers.template has<scene::modifier::alt>() && press.key == keys::kLeft && forum_open && current) {
      pointed = conversation_id{*current, *forum_open};
      this->close_forum();
      reply.handle();
      return std::nullopt;
    }
    if (this->list_keys(chat.parts.mentions, mention_lit, [this](std::size_t i) { this->choose_mention(i); }, press) ||
        this->list_keys(chat.parts.emojis, emoji_lit, [this](std::size_t i) { this->choose_emoji(i); }, press))
      reply.handle();
    return std::nullopt;
  }
  void choose_folder(const folder_t& which) {
    if (which != folder)
      this->slide_list(1.0f);
    folder = which;
    if (last_model)
      this->show(*last_model, false);
  }

  // The chat list: the drawer's button and the name, then the chats.
  // The side: its space bars' view and its column, picking folders here.
  using icons_t = space_icons<pick_folder>;
  using top_view = ui::top_view<pick_folder>;
  using side_column = ui::side_column<pick_folder>;
  // The chat: its header, its messages, and where one writes; or, with no
  // account at all, what to do about it.
  // The pinned bar pressed: to the pinned message -- the bar then shows the
  // one pinned above it, as it always shows the one above the view.
  struct pinned_press {
    using Answer = ::mux::ui::request::jump_to_message;
    conversations_screen* screen;
    std::string id;
    ::mux::ui::request::jump_to_message operator()() const { return ::mux::ui::request::jump_to_message{id}; }
  };
  // A banner's button pressed: its protocol's request, asked.
  struct banner_press {
    using Answer = ::mux::ui::request::banner_pressed;
    const conversations_screen* screen;
    Answer operator()() const { return {*screen->chosen}; }
  };
  struct banner_close {
    using Answer = ::mux::ui::request::dismiss_banner;
    const conversations_screen* screen;
    Answer operator()() const { return {*screen->chosen}; }
  };
  struct composer_banner : skiff::compose::Stacked {
    struct parts_t {
      nodes::Text text;
      std::optional<widgets::Button<banner_press>> verify;
      legacy::icon_button<banner_close> close;
    } parts;
    composer_banner(const palette& colours, conversations_screen* screen)
        : Stacked(skiff::compose::hbox(8.0f, {.fillX = true, .autoSize = scene::axes::kY,
                                             .padding = {10.0f, 12.0f, 10.0f, 12.0f}})),
          parts{.text = skiff::compose::styled(
                    {.grow = scene::axes::kX, .alignSelf = scene::align::kMiddle},
                    wrapped(nodes::Text("", 13.0f, colours.text))),
                .close = legacy::icon_button<banner_close>(colours, icon::close{}, {screen})} {
      parts.close.apply({.alignSelf = scene::align::kMiddle});
      this->setVisible(false);
    }
  };
  // A node of the chat's protocol's own over the composer (a Telegram bot's
  // keyboard): listed by composer_views(state), made for
  // a chat by make_composer_view, found by ADL; none by default.
  template <class List>
  struct view_nodes;
  template <class... Vs>
  struct view_nodes<proto::sticker_view_list<Vs...>> {
    using type = type_list<Vs...>;
  };
  template <class>
  struct protocol_composer_nodes;
  template <class... Tags>
  struct protocol_composer_nodes<protocol_list<Tags...>> {
    using type = typename joined<
        type_list<>, typename view_nodes<decltype(composer_views_for(::mux::state_of<Tags>{}))>::type...>::type;
  };
  using composer_view_t = typename variant_of_types<
      typename joined<type_list<nodes::Text>, typename protocol_composer_nodes<protocols>::type>::type>::type;
  // And under the chat's header, over its messages (an IRC channel's topic
  // and modes, a Telegram channel's join button): head_views(state,
  // type_tag), made by make_head_view.
  template <class>
  struct protocol_head_nodes;
  template <class... Tags>
  struct protocol_head_nodes<protocol_list<Tags...>> {
    using type = typename joined<
        type_list<>, typename view_nodes<decltype(head_views_for(::mux::state_of<Tags>{}))>::type...>::type;
  };
  using head_view_t = typename variant_of_types<
      typename joined<type_list<nodes::Text>, typename protocol_head_nodes<protocols>::type>::type>::type;
  using head_view_holder = skiff::compose::Holding<head_view_t>;
  using composer_view_holder = skiff::compose::Holding<composer_view_t>;
  // What plugins put in the chat's column under its head
  // (contract::chat_column), in their order.
  template <class List>
  struct column_parts;
  template <class... Features>
  struct column_parts<plugin_list<Features...>> {
    template <class Feature>
    static auto made_of(const ui_needs& n) -> decltype(extend(contract::chat_column{}, type_tag<Feature>{}, n)) {
      static_assert(requires { extend(contract::chat_column{}, type_tag<Feature>{}, n); },
                    composition::broken<contract::chat_column, Feature>());
      return extend(contract::chat_column{}, type_tag<Feature>{}, n);
    }
    using nodes_t = std::tuple<decltype(made_of<Features>(std::declval<const ui_needs&>()))...>;
    static nodes_t made(const ui_needs& n) { return nodes_t{made_of<Features>(n)...}; }
  };
  using column_parts_t = column_parts<composition::providers<contract::chat_column, enabled_plugins>>;
  struct chat_column : skiff::compose::Stacked {
    // Child references and handlers require a fixed address.
    chat_column(const chat_column&) = delete;
    chat_column& operator=(const chat_column&) = delete;
    chat_column(chat_column&&) = delete;
    chat_column& operator=(chat_column&&) = delete;

    using header_t = chat_head;
    using pinned_t = nodes::Memo<pinned_view, pinned_bar<pinned_press>>;
    using empty_state = decltype(no_accounts_yet(std::declval<const palette&>()));
    using select_hint = decltype(select_a_chat());
    struct parts_t {
      // The head, as a function of the chat shown.
      header_t header;
      connection_bar connection;
      search_bar search;
      selection_bar_t selection;
      // The pinned message, under the head, where the chat has any.
      pinned_t pinned;
      // What plugins put under the head (contract::chat_column): a call in
      // this chat, as Element's call view.
      typename column_parts_t::nodes_t plugins;
      // Its protocol's own node under the head (make_head_view).
      std::optional<head_view_holder> their_head;
      chat_area_t area;
      mention_list mentions;
      emoji_list emojis;
      // Over the composer: what the chat's protocol says there -- Matrix's
      // warning that the other is not verified, or was reset.
      composer_banner banner;
      std::optional<composer_view_holder> their_view;
      composer_bar_t<> line;
      empty_state empty;
      select_hint hint;
    } parts;
    // Its parts by their names, for what reads them: it is never moved.
    header_t& header = parts.header;
    search_bar& search = parts.search;
    chat_area_t& area = parts.area;
    composer_bar_t<>& line = parts.line;
    empty_state& empty = parts.empty;
    select_hint& hint = parts.hint;
    chat_column(const ui_needs &n, conversations_screen* screen)
        : Stacked(skiff::compose::vbox(
              0.0f, {.fillY = true,
                     .grow = scene::axes::kX,
                     .background = n.looks->window.behind ? skia::SkColor{0}
                                                          : n.colours->chat})),
          parts{
              .connection = connection_bar(*n.colours),
              .search = search_bar(n),
              .selection = selection_bar(n),
              .plugins = column_parts_t::made(n),
              .area = chat_area(n),
              .mentions =
                  skiff::compose::visible(false, mention_list(*n.colours)),
              .emojis = skiff::compose::visible(false, emoji_list(*n.colours)),
              .banner = composer_banner(*n.colours, screen),
              .line = composer_bar<>(n),
              .empty = no_accounts_yet(*n.colours),
              .hint = select_a_chat()} {
      header.apply({.fillX = true, .height = chat_header::kHeight});
      parts.pinned.apply({.fillX = true, .height = pinned_bar<pinned_press>::kHeight});
      parts.pinned.setVisible(false);
      header.needs = n;
      header.show({}, [&n](const auto& shown) { return chat_header(n, shown); });
      // A plain colour: the wallpaper is the messages' own -- the timeline's
      // Wallpaper -- not behind Select a chat, where Telegram has none.
      // Nothing, where the background is behind the whole window.
      area.apply({.fillX = true, .grow = scene::axes::kY});
    }
  };
  using side_edge = decltype(drag_edge(std::declval<const palette&>(), resize_sidebar_to{}));
  using info_edge_t = decltype(drag_edge(std::declval<const palette&>(), resize_info_to{}, false));
  struct parts_t {
    side_column side;
    side_edge edge;
    chat_column chat;
    info_edge_t info_edge;
    info_panel info;
    // Threads, in the info's place while they are open.
    threads_panel threads;
  } parts;
  // Its parts by their names, for what reads them: the screen is never moved.
  side_column& side = parts.side;
  side_edge& edge = parts.edge;
  chat_column& chat = parts.chat;
  info_edge_t& info_edge = parts.info_edge;
  info_panel& info = parts.info;

  // The old names, for what is kept in the parts.
  nodes::ScrollContainer<chat_rows>& list = side.list;
  nodes::Text& no_chats = side.no_chats;
  chat_head& header = chat.header;
  search_bar& search = chat.search;
  // The keys of a chat, as tdesktop's -- what the input leaves to it:
  // Ctrl+F finds in it; Up in an empty input edits the last message sent;
  // Ctrl+Up answers the last message, and each Ctrl+Up after it the one
  // above, Ctrl+Down back down; Ctrl+C copies what is selected in the
  // messages; Esc lets an answer or an edit go.
  using Node::onKey;
  std::optional<Answer> onKey(scene::phase::bubble, const scene::key::down& press, scene::Reply& reply);
  // Messages selected: the selection bar in place of the head, and the
  // messages marked; none, the head back.
  void show_selection(const std::set<std::string>& ids) {
    header.setVisible(ids.empty() && !search.visible());
    parts.threads.parts.answers.set_selected(ids);
    this->fState.relayoutQuietly();
  }
  // The search bar in place of the head, or the head back.
  void show_search(bool shown) {
    search.setVisible(shown);
    header.setVisible(!shown);
    if (!shown) {
      search.parts.field.setText({});
    }
    this->invalidateLayout();
  }
  // The chat's messages: the list in its area (chat_area), told by the
  // program; read here for the pinned bar and the page keys.
  [[nodiscard]] auto& messages() { return std::get<1>(chat.area.fParts).list(); }
  // The chat last shown: its thread let go where another is.
  std::optional<conversation_id> shown_chat;
  // The chat's wallpaper, as its levels say, behind its messages: not
  // there where the window's background is behind the whole window, and
  // else at the window's opacity, as the panels are.
  void show_wallpaper() {
    auto& wall = std::get<0>(chat.area.fParts);
    wall.setVisible(!needs_.looks->window.behind);
    wall.setOpacity(static_cast<float>(needs_.looks->window.opacity) / 100.0f);
    show_wallpaper_on(wall, wallpaper, *needs_.colours, *needs_.looks);
  }
  composer_bar_t<>& line = chat.line;

  // Its child references and handlers point into this screen. Construct it
  // in its final place; moving it would leave them pointing at the old one.
  conversations_screen(const conversations_screen&) = delete;
  conversations_screen& operator=(const conversations_screen&) = delete;
  conversations_screen(conversations_screen&&) = delete;
  conversations_screen& operator=(conversations_screen&&) = delete;

  conversations_screen(const ui_needs &n)
      : Stacked(skiff::compose::hbox(0.0f, {.fill = true})), needs_(n),
        parts{.side = side_column(*n.colours),
              .edge = drag_edge(*n.colours, resize_sidebar_to{}),
              .chat = chat_column(n, this),
              .info_edge =
                  drag_edge(*n.colours, resize_info_to{}, false),
              .info = info_panel(*n.colours, *n.shared),
              .threads = threads_panel(n)} {
    needs_.shared->docked_panel_watcher = fState.fId;
    // The edges take a pixel between the columns, their line, and are
    // wider than that over them to be caught.
    edge.apply({.fillY = true, .width = 7.0f, .margin = {0.0f, -3.0f, 0.0f, -3.0f}});
    info_edge.apply({.fillY = true, .width = 7.0f, .margin = {0.0f, -3.0f, 0.0f, -3.0f}});
    info.apply({.fillY = true, .width = info_width});
    this->show_info();
  }

  // The space bars of the account shown: Home, Direct messages, and its
  // spaces, each in the bars it is put in -- none said, the side one --
  // in the order put there, the rest after them as the model has them.
  void show_space_bars(const model& now);

  // The space menu closed by any press off it, wherever on the screen: at
  // once, where the press is not on it; one on it chooses first.
  using Node::onPointer;
  void onPointer(scene::phase::capture, const scene::pointer::down& press, scene::PointerReply& reply);
  // The swipe let go: far enough to the right, and more across than down --
  // out of the chat, to the chats.
  std::optional<Answer> onPointer(scene::phase::capture, const scene::pointer::up& lift, scene::PointerReply& reply);
  // Single, a step back: from the threads or the info to the chat, from
  // the chat to the chats -- a swipe across, or Esc.
  Answer step_back() {
    if (threads_open)
      return ::mux::ui::request::toggle_threads{};
    else if (info_open)
      return ::mux::ui::request::toggle_info{};
    else
      return ::mux::ui::request::close_chat{};
  }
  // Esc too.
  bool close_space_menu() {
    if (!side.menu_up())
      return false;
    side.close_menu();
    return true;
  }
  // The menu the program acted on, closed at the next frame.
  void close_space_menu_soon() { side.close_menu_soon(); }

  // The chat list as wide as `x`, where its edge was dragged to.
  void resize_sidebar(float x) {
    side_width = x - fState.contentBox().fLeft;
    side.wanted = side_width;
    side.invalidateLayout();
  }

  // The chat's info as wide as from `x` to the window's right.
  void resize_info(float x) {
    info_width = std::clamp(fState.contentBox().fRight - x, 260.0f,
                            std::max(260.0f, fState.contentBox().width() - side_width - 300.0f));
    info.apply({.width = info_width});
    parts.threads.apply({.width = info_width});
  }

  // The chat's info beside it where it is open and a chat is chosen -- or
  // its threads in the info's place, where they are.
  void show_info() {
    const bool shown = (info_open || threads_open) && chosen.has_value();
    info.setVisible(shown && !threads_open);
    parts.threads.setVisible(shown && threads_open);
    info_edge.setVisible(shown && !single);
    side.wanted = side_width;
    // Single: the chats with none chosen; the chat; or its info in its place.
    side.whole = single;
    side.setVisible(!single || !chosen.has_value());
    edge.setVisible(!single);
    chat.setVisible(!single || (chosen.has_value() && !shown));
    const float across = single ? single_width : info_width;
    info.apply({.width = across});
    parts.threads.apply({.width = across});
    const int now_pane = !single ? -1 : !chosen.has_value() ? 0 : shown ? 2 : 1;
    if (single && pane_shown >= 0 && now_pane != pane_shown) {
      pane_from = now_pane > pane_shown ? 1.0f : -1.0f;
      pane_in.jump(0.0f);
      pane_in.setTarget(1.0f);
      scene::work::mark(fState.fId);
    }
    pane_shown = now_pane;
    this->place_pane();
  }
  // Single or not as the window is now: looked at as it is laid out.
  void layoutChildren() {
    // Its id as it is now, for the docked panel to tell it by.
    needs_.shared->docked_panel_watcher = fState.fId;
    const skia::SkRect box = fState.contentBox();
    const bool now = box.width() < 600.0f && box.height() > box.width();
    if (now != single || (now && box.width() != single_width)) {
      if (now != single)
        header.go_back(now);
      single = now;
      single_width = box.width();
      if (last_model) this->show_connections(*last_model);
      this->show_info();
    }
    this->nodes::Stack::layoutChildren();
  }
  bool threads_open = false;
  // The chosen chat's background, as the program resolves it.
  config::wallpaper_t wallpaper = config::wallpaper::theme{};
  // The threads' panel open or shut, and the thread open in it: an answer
  // being written let go where the thread changes.
  void set_threads(bool on, std::optional<std::string> root) {
    if (parts.threads.open != root)
      parts.threads.stop_answering();
    threads_open = on;
    parts.threads.open = std::move(root);
    this->show_info();
  }
  // The thread open, where the panel shows one.
  [[nodiscard]] std::optional<std::string> thread_open() const {
    return threads_open ? parts.threads.open : std::nullopt;
  }
  // What is written goes into the thread open: its field has the keys.
  [[nodiscard]] bool writing_in_thread() const { return thread_open() && composer_field(parts.threads.parts.line).focused(); }
  // An answer in the thread open answered there.
  [[nodiscard]] bool answer_in_thread(const std::string& root, std::string id, compose_context said) {
    if (!threads_open || parts.threads.open != root)
      return false;
    parts.threads.answer(std::move(id), std::move(said));
    return true;
  }

  void toggle_info() { this->set_info_open(!info_open); }
  void set_info_open(bool on) {
    info_open = on;
    this->show_info();
    // Not kept up while shut: shown as it is now.
    if (last_model && chosen)
      info.choose(*last_model, chosen, muted.contains(*chosen));
  }

  // The model as it is now: the current account's chats, newest first, and
  // the chosen one.
  // The chats muted, as the program keeps them.
  std::set<conversation_id> muted;
  // What is left written in each chat, as the program keeps it.
  std::map<conversation_id, std::string> drafts;
  [[nodiscard]] std::string draft_of(const conversation_id& id) const {
    const auto found = drafts.find(id);
    return found == drafts.end() ? std::string() : found->second;
  }
  // Frames wanted while a jump goes on: it is carried out a step a frame --
  // made, paged back to, fetched around, aimed at -- and skiff draws the next
  // frame only for what is still settling. And while the list slides in --
  // another space, a forum -- and the panels ease to a chat's opacity: only
  // ticked (wantsTick), they moved a step at a frame something else asked
  // for, and the slide went by in jerks unless the mouse moved.
  [[nodiscard]] bool settling() const {
    return slide_wait > 0 || list_in.moving() ||
           needs_.paint->ease.t.moving() || pane_in.moving();
  }
  // What was searched for last, and the model last shown: typing into the
  // search filters the list again.
  std::string searched;
  // What the server was last asked for where nothing joined matched, and
  // what came of it.
  std::string asked_elsewhere;
  std::vector<directory_room> rooms_elsewhere;
  std::vector<found_person> people_elsewhere;
  bool rooms_came = false;
  bool people_came = false;
  const model* last_model = nullptr;
  // What it wants of the program, as its state comes to want it.
  wants_model* wants_ = nullptr;

  bool was_typing = false;
  std::string typed_last;
  // The pinned bar: of the chat's pins, the one `pinned_step` says -- the
  // newest where none was worked out yet.
  void show_pinned(const conversation* one) {
    {
      auto& bar = chat.parts.pinned;
      if (!one || one->pinned.empty()) {
        bar.setVisible(false);
      } else {
        const std::size_t count = one->pinned.size();
        const std::size_t at = std::min(pinned_step, count - 1);
        const std::string& id = one->pinned[at];
        pinned_view shown{id, count == 1 ? std::string("Pinned message")
                                         : std::format("Pinned message #{} of {}", at + 1, count),
                          "A message"};
        if (const message* said = held_message(*one, id)) {
          shown.line = said->body.plain.empty() && said->attachment ? std::string("Photo") : said->body.plain;
          std::ranges::replace(shown.line, '\n', ' ');
        }
        bar.setVisible(true);
        bar.show(shown, [this](const pinned_view& view) { return pinned_bar<pinned_press>(*needs_.colours, {this, view.id}, view); });
      }
    }
  }
  // The pin the bar shows, as Telegram's: the newest pinned above the view
  // -- before the first message seen -- else the oldest. A pin not loaded
  // is taken as older than all that is.
  [[nodiscard]] std::size_t pin_above(const conversation& one) {
    auto& list = this->messages();
    const skia::SkRect view = list.bounds();
    const auto& rows = list.rows().fRows;
    const auto first = std::ranges::find_if(rows, [&](const auto& row) {
      const skia::SkRect box = list.toView(row.bounds());
      return !box.isEmpty() && box.fBottom > view.fTop + 8.0f;
    });
    const auto place = [&](const std::string& id) -> std::ptrdiff_t {
      const auto found = std::ranges::find(one.timeline, id, &message::id);
      return found == one.timeline.end() ? -1 : found - one.timeline.begin();
    };
    const std::ptrdiff_t top = first == rows.end() ? std::numeric_limits<std::ptrdiff_t>::max() : place(first->fKey);
    std::optional<std::size_t> best;
    std::ptrdiff_t best_place = -2;
    for (std::size_t i = 0; i < one.pinned.size(); ++i)
      if (const std::ptrdiff_t at = place(one.pinned[i]); at < top && at >= best_place) {
        best = i;
        best_place = at;
      }
    return best.value_or(0);
  }
  // What the pin above the view was last worked out from.
  struct pin_inputs {
    conversation_id chat;
    float at = 0.0f;
    std::size_t held = 0;
    std::size_t pins = 0;
    std::string first;
    friend bool operator==(const pin_inputs&, const pin_inputs&) = default;
  };
  std::optional<pin_inputs> pin_worked_out;
  // Ticked while something moves: the list sliding in, the panels' ease,
  // the docked panel's height. What the list does is its own.
  [[nodiscard]] bool wantsTick() const {
    return slide_wait > 0 || list_in.moving() || pane_in.moving() || needs_.shared->docked_panel_height != docked_applied ||
           needs_.paint->ease.t.moving();
  }
  void update(double now_ms);

  // The screen as the model is: the list, and the chat shown -- or, where
  // only what the list lists changed (another space, a forum, the search),
  // the list alone: the chat's messages were reconciled again for nothing.
  void show(const model& now, bool with_chat = true);
  // Read by the window's binding: what the settings say of the chats --
  // which are muted, which shown as forums or out of Home -- and how the
  // spaces show; the screen shown again where any of it moved.
  // What is kept, as the window's binding last read it: what each chat's
  // settings come to is read from it, with the chats, at every show.
  const kept_root* kept_ = nullptr;
  // Each chat's settings as they come to now -- its own, its space's, its
  // account's, every chat's: which room events it shows (in an encrypted
  // room, who joins and who is invited always: each is given the room's
  // key, and the server could put anyone there -- the one thing to see
  // before writing), whether it shows receipts and link previews, how far
  // a jump's search pages back.
  void read_chat_settings(const model& now) {
    if (kept_ == nullptr)
      return;
    const auto space = space_above_of(now.accounts());
    const chat_settings reads{kept_, &space};
    const auto all = [&] {
      return now.accounts().values() | std::views::transform([](const account& one) { return one.conversations.values(); }) | std::views::join;
    };
    event_filters = all() | std::views::transform([&](const conversation& one) {
                      room_event_filter filter = reads.room_event_filter_of(one.id);
                      if (one.encrypted)
                        std::ranges::for_each(std::array{room_event_t{room_event::joins{}}, room_event_t{room_event::invites{}}},
                                              [&](const room_event_t& kind) { filter.shown[kind.index()] = true; });
                      return std::pair{one.id, filter};
                    }) |
                    std::ranges::to<std::map>();
    receipts_in = all() | std::views::filter([&](const conversation& one) { return reads.receipts_shown(one.id); }) |
                  std::views::transform(&conversation::id) | std::ranges::to<std::set>();
    previews_off = all() | std::views::filter([&](const conversation& one) { return !reads.previews_shown(one.id); }) |
                   std::views::transform(&conversation::id) | std::ranges::to<std::set>();
    // The chosen chat's background, as its levels say.
    wallpaper = chosen ? reads.wallpaper_of(*chosen) : config::wallpaper_t{config::wallpaper::theme{}};
  }
  template <class Reactions>
  void refresh(const skiff::model::Model<kept_root, Reactions>& kept) {
    this->refresh_kept(kept.root());
  }
  // What is kept read: by its root, whatever the model's reactions.
  void refresh_kept(const kept_root& root) {
    kept_ = &root;
    const auto& looks = root.looks.fValue;
    side.theme_now = looks.theme;
    const config::account_t* own = current ? account_settings(root, current->address) : nullptr;
    const auto own_or = [&](const std::optional<bool>& theirs, bool everyone) { return theirs.value_or(everyone); };
    // The chats listed in other accounts' lists, each with its strip: its
    // own colour, else its account's; shown as it says, else as its account.
    const auto& placements = root.placements.fValue;
    const auto chat_of = [](const config::chat_placement& one) {
      return conversation_id{{protocol_of(one.account), one.account}, one.conversation};
    };
    const auto strip_shown = [&](const config::chat_placement& one) {
      const config::account_t* theirs = account_settings(root, one.account);
      return one.strip.value_or(theirs == nullptr || config::strip_of(*theirs));
    };
    const auto strip_of = [&](const config::chat_placement& one) {
      const config::account_t* theirs = account_settings(root, one.account);
      const config::accent_t colour = one.strip_colour ? config::accent_of(one.strip_colour)
                                      : theirs != nullptr ? config::colour_of(*theirs)
                                                          : config::default_colour_of(one.account);
      return std::pair{chat_of(one), colour_of(colour, looks.theme)};
    };
    auto listed_now = std::ranges::fold_left(placements, std::map<account_id, std::vector<conversation_id>>{},
                                             [&](auto by_list, const config::chat_placement& one) {
                                               by_list[account_id{protocol_of(one.listed_in), one.listed_in}].push_back(chat_of(one));
                                               return by_list;
                                             });
    auto moved_now = placements | std::views::filter(&config::chat_placement::moved) | std::views::transform(chat_of) |
                     std::ranges::to<std::set>();
    // The last placement of a chat says its strip, as it was written last.
    auto strips_now = placements | std::views::reverse | std::views::filter(strip_shown) | std::views::transform(strip_of) |
                      std::ranges::to<std::map>();
    auto now = std::tuple{std::move(listed_now), std::move(moved_now), std::move(strips_now), chats_where<&config::chat_choices::muted>(root), chats_where<&config::chat_choices::forum>(root),
                          chats_where<&config::chat_choices::hidden_from_home>(root), looks.spaces, looks.top_bar, looks.space_places,
                          own ? own_or(config::home_hides_of(*own), looks.home_hides_spaced) : looks.home_hides_spaced,
                          own ? own_or(config::home_direct_of(*own), looks.home_hides_direct) : looks.home_hides_direct};
    auto was = std::tie(listed_in, moved_out, strips, muted, forums, hidden_from_home, spaces_on, top_bar_on, space_places, home_hides_spaced, home_hides_direct);
    // The chats' own settings may have moved with anything kept: shown
    // again whenever the binding reads it.
    was = std::move(now);
    if (last_model)
      this->show(*last_model);
  }
  // Read by the chats binding: the screen shown again from the model it was
  // last given, as the chats move.
  void refresh(const chats_model&) {
    if (last_model)
      this->show(*last_model);
  }
  // What the runtime binding walks call it by (outside a release build):
  // its refresh, made where it is made, not where a table of it is.
  skiff::bind::Refreshes<2> fRefreshes = skiff::bind::refreshes(
      skiff::bind::refreshFrom<conversations_screen, chats_model>(),
      skiff::bind::refreshFromRoot<conversations_screen, kept_root, &conversations_screen::refresh_kept>());

  // What the chat's protocol says over the composer (proto::composer_banners):
  // Matrix's warning where the other is not verified, for one. Its banners
  // share a dismissible bar in the first's tone.
  void place_head_view(std::nullopt_t) {}
  template <class View>
  void place_head_view(std::optional<View> made) {
    if (made)
      chat.parts.their_head.emplace(std::move(*made));
  }
  void place_composer_view(std::nullopt_t) {}
  template <class View>
  void place_composer_view(std::optional<View> made) {
    if (made)
      chat.parts.their_view.emplace(std::move(*made));
  }
  void dismiss_banner(const conversation_id& in) {
    if (chosen != in || !chat.parts.banner.visible())
      return;
    dismissed_banners.insert_or_assign(in, chat.parts.banner.parts.text.text());
    chat.parts.banner.setVisible(false);
    chat.invalidateLayout();
  }
  void show_connections(const model& now) {
    side.parts.connection.show(current ? now.accounts().find(*current) : nullptr);
    chat.parts.connection.show(chosen ? now.accounts().find(chosen->account) : nullptr,
                                single || (chosen && current != chosen->account));
  }
  void show_banners(const conversation* one, const model& now);
  void show_conversation(const model& now);
};

}  // namespace mux::ui

// Outside a release build each child is walked through its table, and a
// stack lays itself out through its own (Stack::layoutChildren): so a unit
// that lays out or walks one of these would make its table, and the walks
// of everything under it, there -- the chats screen's took this unit past
// ten minutes. Their tables are made once, each in a walks unit of this
// module (src/ui/walks_*.cc); here, and wherever they are walked, a call.
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::conversations_screen> = true;  // walks_chats.cc
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::conversations_screen>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::conversations_screen::side_column> = true;  // walks_side.cc
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::conversations_screen::side_column>() noexcept;
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::ui::conversations_screen::chat_column> = true;  // walks_column.cc
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::ui::conversations_screen::chat_column>() noexcept;

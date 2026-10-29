// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.commands.app -- The commands' part of the program: each carried
// out with the actions plugins script with (mux.app.commands' script).
export module mux.plugin.commands.app;

import std;
import mux.composition;
import mux.core;
import mux.ui;
import mux.app.services;
import mux.app.commands;
import mux.app.network;
import mux.plugin.commands.types;

export namespace mux::plugins::commands {

// A hue, from 0 (red) round to 1, at full saturation and half lightness, as
// 0xRRGGBB.
[[nodiscard]] inline std::uint32_t hue_rgb(double hue) {
  const double h = hue * 6.0;
  const double x = 1.0 - std::abs(std::fmod(h, 2.0) - 1.0);
  const std::array<std::array<double, 3>, 6> sextants{{{1, x, 0}, {x, 1, 0}, {0, 1, x}, {0, x, 1}, {x, 0, 1}, {1, 0, x}}};
  const auto& rgb = sextants[std::min<std::size_t>(static_cast<std::size_t>(h), 5)];
  const auto byte = [](double v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
  return (byte(rgb[0]) << 16) | (byte(rgb[1]) << 8) | byte(rgb[2]);
}
// The time asked, read once: "2026-10-28", "2026-10-28 14:30" (UTC), a
// number and a unit ago -- m, h, d, w -- or yesterday or today (their start,
// UTC).
[[nodiscard]] inline std::optional<std::chrono::sys_time<std::chrono::milliseconds>> time_of(std::string_view said,
                                                                                         std::chrono::system_clock::time_point now) {
  using namespace std::chrono;
  const auto ms = [](auto t) { return time_point_cast<milliseconds>(t); };
  const auto today = floor<days>(now);
  static constexpr std::array<std::pair<std::string_view, int>, 2> kDays{{{"today", 0}, {"yesterday", 1}}};
  if (const auto found = std::ranges::find(kDays, said, &std::pair<std::string_view, int>::first); found != kDays.end())
    return ms(today - days(found->second));
  // A number and a unit: ago.
  static constexpr std::array<std::pair<char, minutes>, 4> kUnits{{{'m', minutes(1)}, {'h', hours(1)}, {'d', days(1)}, {'w', weeks(1)}}};
  const std::string_view count = said.starts_with('-') ? said.substr(1) : said;
  if (!count.empty()) {
    int n = 0;
    const auto [end, failed] = std::from_chars(count.data(), count.data() + count.size(), n);
    if (failed == std::errc{} && end + 1 == count.data() + count.size())
      if (const auto unit = std::ranges::find(kUnits, *end, &std::pair<char, minutes>::first); unit != kUnits.end())
        return ms(now - unit->second * n);
  }
  // A date, and a time where one is given.
  int y = 0, mo = 0, d = 0, hh = 0, mi = 0;
  const char* at = said.data();
  const char* const stop = said.data() + said.size();
  const auto number = [&](int& out, char then) {
    const auto [end, failed] = std::from_chars(at, stop, out);
    if (failed != std::errc{})
      return false;
    at = end;
    if (then == '\0')
      return at == stop;
    if (at == stop || *at != then)
      return false;
    ++at;
    return true;
  };
  if (!number(y, '-') || !number(mo, '-'))
    return std::nullopt;
  const bool timed = said.find_first_of(" T") != std::string_view::npos;
  if (!number(d, timed ? (said.find(' ') != std::string_view::npos ? ' ' : 'T') : '\0'))
    return std::nullopt;
  if (timed && (!number(hh, ':') || !number(mi, '\0')))
    return std::nullopt;
  const year_month_day date{year(y), month(static_cast<unsigned>(mo)), day(static_cast<unsigned>(d))};
  if (!date.ok() || hh > 23 || mi > 59)
    return std::nullopt;
  return ms(sys_days(date) + hours(hh) + minutes(mi));
}

struct part {
  app::services* s_ = nullptr;

  [[nodiscard]] app::script act() const { return app::script{s_}; }

  void apply(const shrug& one) {
    if (const auto chat = this->act().chat())
      this->act().send(*chat, one.arguments.empty() ? std::string("¯\\_(ツ)_/¯") : one.arguments + " ¯\\_(ツ)_/¯");
  }
  template <class Asked>
  void apply(const open& one, const Asked& asked) {
    const auto found = this->act().chats_named(one.arguments);
    if (found.empty()) {
      this->act().notice("/open", std::format("No chat has \"{}\" in its name.", one.arguments));
      return;
    }
    this->act().open(found.front(), asked);
  }
  void apply(const join& one) {
    const auto chat = this->act().chat();
    if (!chat || one.arguments.empty()) {
      this->act().notice("/join", "Give a room's address or ID, with a chat of the account open.");
      return;
    }
    this->act().join(chat->account, one.arguments);
  }
  void apply(const react& one) {
    const auto chat = this->act().chat();
    if (!chat || one.arguments.empty())
      return;
    const auto messages = this->act().messages(*chat);
    if (messages.empty())
      return;
    this->act().react(*chat, messages.back().id, one.arguments);
  }
  // Each character its colour, round the hues from red, as Element's
  // /rainbow: a run per character, by its UTF-8 bytes.
  void apply(const rainbow& one) {
    const auto chat = this->act().chat();
    if (!chat || one.arguments.empty())
      return;
    const std::string& text = one.arguments;
    const auto starts = std::views::iota(std::size_t{0}, text.size()) | std::views::filter([&](std::size_t at) {
                          return (static_cast<unsigned char>(text[at]) & 0xC0u) != 0x80u;
                        }) | std::ranges::to<std::vector>();
    const std::size_t count = starts.size();
    auto runs = std::views::iota(std::size_t{0}, count) | std::views::transform([&](std::size_t i) {
                  const std::size_t last = i + 1 < count ? starts[i + 1] : text.size();
                  return styled_run{starts[i], last, run_style::colour{hue_rgb(static_cast<double>(i) / static_cast<double>(count))}};
                }) | std::ranges::to<std::vector>();
    this->act().send_styled(*chat, text, std::move(runs));
  }
  // Sent as typed: one run over all of it that keeps it from being read as
  // Markdown.
  void apply(const raw& one) {
    if (const auto chat = this->act().chat(); chat && !one.arguments.empty())
      this->act().send_styled(*chat, one.arguments, {styled_run{0, one.arguments.size(), run_style::verbatim{}}});
  }
  // The chat's first message from the time asked -- a date, a date and a
  // time (UTC), or a time ago -- of those loaded; earlier than them, the
  // oldest loaded, and a word that there is more above.
  template <class Asked>
  void apply(const jump_to_date& one, const Asked& asked) {
    const auto chat = this->act().chat();
    if (!chat)
      return;
    const auto when = time_of(one.arguments, std::chrono::system_clock::now());
    if (!when) {
      this->act().notice("/jump-to-date", "Give a date (2026-10-28), a date and a time (2026-10-28 14:30, UTC), or a time ago: 3d, 2h, 30m, 1w, yesterday, today.");
      return;
    }
    const auto messages = this->act().messages(*chat);
    const auto from = std::ranges::find_if(messages, [&](const message& each) { return each.at >= *when && !each.id.empty(); });
    if (from == messages.end()) {
      this->act().notice("/jump-to-date", "Nothing from then yet: that is after the newest message.");
      return;
    }
    // Among what is loaded: gone to.
    // Earlier than what is loaded: its account asked for the first message
    // from then (find_message_at -- its server, its archive, or its history
    // paged back on its own fiber), gone to when it says; the oldest loaded
    // meanwhile.
    if (from == messages.begin() && messages.front().at > *when) {
      sought_ = chat;
      s_->net->find_message_at(*chat, when->time_since_epoch().count());
    }
    this->act().jump(from->id, asked);
  }
  // The account's answer: its message gone to, where the chat asked of is
  // still open.
  void take(const change::message_found_at& one) {
    if (sought_ != one.in)
      return;
    sought_.reset();
    if (!one.id) {
      this->act().notice("/jump-to-date", "Nothing found from then: here is the oldest message loaded.");
      return;
    }
    if (s_->chosen() == one.in)
      s_->jump_due = ui::request::jump_to_message{*one.id};
  }
  std::optional<conversation_id> sought_;
  void apply(const help&) {
    this->act().notice("Commands", app::kCommands | std::views::transform([](const app::command_said& one) {
                                     return std::format("{} -- {}", one.usage, one.summary);
                                   }) | std::views::join_with(std::string_view("\n")) |
                                       std::ranges::to<std::string>());
  }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::commands

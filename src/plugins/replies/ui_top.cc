// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.replies.ui_top -- What a reply shows of the message it answers,
// above its text (contract::message_heads), as tdesktop's reply
// (history_view_reply.cpp): who wrote it, a line of it -- or a picture's
// thumbnail and caption, a file's name -- pressed to go to it. A reply whose
// text opens with its only quote, of the message it answers: that quote in
// the head, marked "quoted", as the part answered, and not again in the text
// -- only where that message says all of it.
export module mux.plugin.replies.ui_top;

import std;
import skia;
import skiff.paint;
import skiff.compose;
import mux.core;
import mux.composition;
import mux.ui.base;
import mux.ui.messages;
import mux.plugin.replies.types;

export namespace mux::plugins::replies {

inline std::optional<ui::message_head> head_of(contract::message_heads, type_tag<feature>, ui::head_facts& f) {
  namespace c = skiff::compose;
  if (!f.said.replies_to)
    return std::nullopt;
  const message* found = ui::held_message(f.in, *f.said.replies_to);
  const bool known = found != nullptr;
  // Its opening quote, where it is of the message answered: taken out of
  // the text, into the head.
  std::optional<std::string> header_quote;
  {
    ui::mentioned taken = f.shown;
    if (const auto quote = ui::take_opening_quote(taken);
        quote && found && ui::squeezed(ui::quote_line_of(*found, f.in, f.now)).contains(ui::squeezed(*quote))) {
      header_quote = quote;
      f.shown = std::move(taken);
    }
  }
  // A picture's: its thumbnail, and its caption or "Photo"; a file's: its
  // name; else its text.
  std::optional<std::string> pictured;
  std::string line = header_quote ? *header_quote : known ? ui::quote_line_of(*found, f.in, f.now) : std::string("not loaded");
  if (known && found->attachment) {
    if (is_picture(found->attachment->kind))
      pictured = found->attachment->source;
    if (line.empty())
      line = is_picture(found->attachment->kind) ? std::string("Photo") : found->attachment->name;
  }
  if (!header_quote)
    std::ranges::replace(line, '\n', ' ');
  const bool with_picture = pictured.has_value();
  const std::string from = known ? (found->outgoing ? std::string("You") : ui::sender_name(f.in, found->sender)) : std::string("A message");
  // The bubble at least as wide as the head asks -- its name, "quoted"
  // after it, and its line up to maxSignatureSize -- so that a short answer
  // to a long message does not stretch its bubble to the full width.
  float least = 160.0f;
  if (skia::SkFont* font = skiff::paint::defaultFont()) {
    skiff::paint::Painter measure(nullptr, *font);
    const float who = measure.measure(from, 13.0f) + (header_quote ? 8.0f + std::ceil(measure.measure("quoted", 11.0f)) : 0.0f);
    const float words_wide = std::max(who, std::min(measure.measure(line, 13.0f), ui::kReplyLineMax));
    const float around = (with_picture && !header_quote ? 7.0f + 32.0f + 4.0f : 11.0f) + 6.0f + 2.0f * ui::kBubblePadX;
    least = std::min(std::ceil(words_wide) + around, ui::kBubbleMaxWidth + 2.0f * ui::kBubblePadX);
  }
  return ui::message_head{c::onClick(ui::request::jump_to_message{*f.said.replies_to, header_quote, f.said.id},
                                     ui::quote_row(f.colours, known ? ui::avatar_colour(found->sender) : f.colours.accent, from, std::move(line),
                                                   header_quote ? std::nullopt : std::move(pictured), header_quote.has_value(),
                                                   known && found->sticker),
                                     "Go to the message answered"),
                          least};
}

}  // namespace mux::plugins::replies

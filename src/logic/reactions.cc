// SPDX-License-Identifier: AGPL-3.0-only
// Reaction input: readable labels beside protocol keys, with copied image
// identities taking precedence over a room's possibly identical shortcodes.
export module mux.logic.reactions;

import std;
import mux.core;

export namespace mux::logic {
struct reaction_image {
  std::size_t first, last;
  std::string source, label;
};
struct reaction_text {
  std::string key, label;
  bool custom = false;
};
[[nodiscard]] inline reaction_text reaction_text_of(std::string_view text, const std::vector<emote>& emotes,
                                                   const std::vector<reaction_image>& images = {}) {
  reaction_text out;
  for (std::size_t at = 0; at < text.size();) {
    std::string source, label;
    std::size_t end = at;
    const auto copied = std::ranges::find(images, at, &reaction_image::first);
    if (copied != images.end() && copied->last > at && copied->last <= text.size() && copied->source.starts_with("mxc://")) {
      source = copied->source;
      label = copied->label;
      end = copied->last;
    } else if (text.substr(at).starts_with("mxc://")) {
      end = text.find_first_of(" \t\r\n", at);
      if (end == std::string_view::npos) end = text.size();
      source = text.substr(at, end - at);
      const auto known = std::ranges::find(emotes, source, &emote::url);
      label = known == emotes.end() ? source : ":" + known->shortcode + ":";
    } else if (text[at] == ':') {
      const auto close = text.find(':', at + 1);
      const auto finish = close == std::string_view::npos ? text.size() : close;
      const auto code = text.substr(at + 1, finish - at - 1);
      const auto known = std::ranges::find(emotes, code, &emote::shortcode);
      if (known != emotes.end() && known->url.starts_with("mxc://")) {
        source = known->url;
        label = ":" + known->shortcode + ":";
        end = close == std::string_view::npos ? finish : close + 1;
      }
    }
    if (source.empty()) {
      out.key += text[at];
      out.label += text[at++];
      continue;
    }
    // Mixed reaction keys use whitespace-delimited media URIs. Keep nearby
    // words/punctuation outside the media ID while preserving the label.
    const auto space = [](char c) { return std::string_view(" \t\r\n").contains(c); };
    if (!out.key.empty() && !space(out.key.back())) out.key += ' ';
    out.key += source;
    if (end < text.size() && !space(text[end])) out.key += ' ';
    out.label += label;
    out.custom = true;
    at = end;
  }
  return out;
}
}  // namespace mux::logic

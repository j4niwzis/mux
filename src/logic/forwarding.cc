// SPDX-License-Identifier: AGPL-3.0-only
export module mux.logic.forwarding;

import std;
import splice;
import chevron.parser;
import mux.core;

export namespace mux::logic {

// Decode attribution at the HTML boundary, including entities and inline markup.
inline std::optional<forward_info> forward_attribution(std::string_view html) {
  chevron::parser reader({}, chevron::dialect::html{});
  reader.feed(html);
  reader.finish();
  std::vector<std::pair<std::string, std::string>> links;
  bool anchor = false;
  for (;;) {
    auto next = reader.next();
    if (!next) return std::nullopt;
    if (!*next) break;
    spl::visit(spl::overloaded{
      [&](const chevron::start_element& tag) {
        if (tag.name.local != "a") return;
        for (const auto& attr : tag.attributes)
          if (attr.name.local == "href") {
            links.emplace_back(attr.value, "");
            anchor = true;
            break;
          }
      },
      [&](const chevron::end_element& tag) { if (tag.name.local == "a") anchor = false; },
      [&](const chevron::text& text) { if (anchor) links.back().second += text.content; }
    }, **next);
  }
  if (links.empty()) return std::nullopt;
  constexpr std::string_view prefix = "https://matrix.to/#/";
  return forward_info{.from = links.front().first.starts_with(prefix) ? links.front().first.substr(prefix.size()) : links.front().first,
                      .name = links.front().second,
                      .link = links.size() > 1 ? links[1].first : std::string()};
}

struct forwarded_body {
  forward_info origin;
  std::string html;
};

// Peel only a complete forward wrapper. Keep the quoted body's original HTML:
// its own quotes, links and custom emoji must survive unchanged.
inline std::optional<forwarded_body> unwrap_forward(std::string_view html, bool marked = false) {
  if (html.find("Forwarded from ") == std::string_view::npos) return std::nullopt;
  chevron::parser reader({}, chevron::dialect::html{});
  reader.feed(html);
  reader.finish();
  std::string heading;
  std::size_t start = 0, end = 0;
  int depth = 0;
  bool closed = false, trailing = false;
  std::optional<forward_info> origin;
  const auto whitespace = [](std::string_view text) { return text.find_first_not_of(" \t\r\n") == std::string_view::npos; };
  for (;;) {
    auto next = reader.next();
    if (!next) return std::nullopt;
    if (!*next) break;
    const auto consumed = html.size() - reader.unread().size();
    spl::visit(spl::overloaded{
      [&](const chevron::start_element& tag) {
        if (closed) { trailing = true; return; }
        if (tag.name.local != "blockquote") return;
        if (depth++ == 0) {
          const auto first = heading.find_first_not_of(" \t\r\n");
          if (first == std::string::npos || !std::string_view(heading).substr(first).starts_with("Forwarded from ")) return;
          const auto opening = html.rfind('<', consumed - 1);
          origin = forward_attribution(html.substr(0, opening));
          start = consumed;
        }
      },
      [&](const chevron::end_element& tag) {
        if (tag.name.local == "blockquote" && depth > 0 && --depth == 0) {
          end = html.rfind('<', consumed - 1);
          closed = true;
        }
      },
      [&](const chevron::text& text) {
        if (closed) trailing |= !whitespace(text.content);
        else if (!depth) heading += text.content;
      }
    }, **next);
  }
  // An unmarked user quote must not be mistaken for an attribution.
  if (!origin || (!marked && !origin->from.starts_with('@')) || !closed || trailing || end < start) return std::nullopt;
  return forwarded_body{std::move(*origin), std::string(html.substr(start, end - start))};
}
}  // namespace mux::logic

// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:html -- A message's HTML read into text and links.
export module mux.ui:html;

import std;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.text;
import mux.core;
import mux.config;
import chevron;
import :base;

export namespace mux::ui {

// A message's HTML (Matrix's org.matrix.custom.html) read into what is
// drawn: its text -- tags gone, line breaks and paragraphs as newlines,
// list items bulleted, quotes marked, the common entities decoded -- and
// its links, each a place to open.
struct html_disclosure {
  std::size_t first = 0, last = 0;
  std::size_t summary_first = 0, summary_last = 0;
  std::optional<std::size_t> parent;
  bool open = false;
  std::string group;
};
struct formatted {
  std::string text;
  std::vector<html_disclosure> disclosures;
  std::vector<std::pair<std::string, std::string>> links;  // what it says, where it goes
  std::vector<nodes::Text::Link> spans;                    // where in the text each is
  std::vector<nodes::Text::Styled> styles;                 // what is strong, slanted, code, quoted
};
// What a tag makes of the text inside it.
namespace text_style {
struct strong {};    // <b>, <strong>
struct emphasis {};  // <i>, <em>
struct struck {};    // <del>, <s>, <strike>
struct code {};      // <code>
struct quote {};     // <blockquote>
struct underline {}; // <u>, <ins>
// <span>: a spoiler where it says so (data-mx-spoiler), else nothing -- a
// span all the same, so that its end closes it and not another.
struct span {
  bool spoiler = false;
};
}  // namespace text_style
using text_style_t = spl::variant<text_style::strong, text_style::emphasis, text_style::struck, text_style::code,
                                  text_style::quote, text_style::underline, text_style::span>;
[[nodiscard]] inline nodes::Text::Styled styled(text_style::strong, std::size_t a, std::size_t b) {
  return {.first = a, .last = b, .strong = true};
}
[[nodiscard]] inline nodes::Text::Styled styled(text_style::emphasis, std::size_t a, std::size_t b) {
  return {.first = a, .last = b, .emphasis = true};
}
[[nodiscard]] inline nodes::Text::Styled styled(text_style::struck, std::size_t a, std::size_t b) {
  return {.first = a, .last = b, .struck = true};
}
[[nodiscard]] inline nodes::Text::Styled styled(text_style::code, std::size_t a, std::size_t b) {
  return {.first = a, .last = b, .code = true};
}
[[nodiscard]] inline nodes::Text::Styled styled(text_style::quote, std::size_t a, std::size_t b) {
  return {.first = a, .last = b, .quote = true};
}
[[nodiscard]] inline nodes::Text::Styled styled(text_style::underline, std::size_t a, std::size_t b) {
  return {.first = a, .last = b, .underline = true};
}
[[nodiscard]] inline nodes::Text::Styled styled(text_style::span one, std::size_t a, std::size_t b) {
  return {.first = a, .last = b, .spoiler = one.spoiler};
}
// An HTML tag, as read: what it does to the text, told by its type. Its
// name is looked up once, where it is read (start_of, end_of); what follows works on
// the variant.
namespace html_tag {
struct line_break {};  // <br>
struct block_end {};   // </p>, </div>, </li>, </blockquote>, </h1>..</h3>, </pre>
struct list_item { std::optional<int> value; };   // <li>
struct list_open { bool ordered = false; int start = 1; };
struct list_close {};
struct paragraph {};
struct heading_open {};
struct heading_close {};
struct cell {};
struct table_row {};
struct rule {};
struct details_open { bool open = false; std::string group; };
struct details_close {};
struct summary_open {};
struct summary_close {};
struct quote {};       // <blockquote>
struct reply {};       // <mx-reply>: the quoted message a reply carries
struct link_open {     // <a href="...">
  std::string href;
};
struct link_close {};  // </a>
struct style_open {    // <b>, <em>, <code>, <blockquote>, ...
  text_style_t style;
};
struct style_close {   // </b>, </em>, </code>, </blockquote>, ...
  text_style_t style;
};
struct image {         // <img src="mxc://..." alt="..."> -- a custom emoji, in Matrix
  std::string src;
  std::string alt;
};
struct code_open {     // <code class="language-...">: code; in a block, its language
  std::string language;
};
struct block_open {};   // <pre>: a block of code
struct block_close {};  // </pre>
struct other {};  // anything else: dropped
}  // namespace html_tag
using html_tag_t = spl::variant<html_tag::line_break, html_tag::block_end, html_tag::list_item, html_tag::quote,
                                html_tag::reply, html_tag::link_open, html_tag::link_close, html_tag::image,
                                html_tag::style_open, html_tag::style_close, html_tag::code_open, html_tag::block_open,
                                html_tag::block_close, html_tag::list_open, html_tag::list_close, html_tag::paragraph,
                                html_tag::heading_open, html_tag::heading_close, html_tag::cell, html_tag::table_row, html_tag::rule,
                                html_tag::details_open, html_tag::details_close, html_tag::summary_open,
                                html_tag::summary_close, html_tag::other>;

// An element's start, as chevron read it (dialect::html), read into what it
// does: its name looked up once, here -- what follows works on the variant.
[[nodiscard]] inline html_tag_t start_of(const chevron::start_element& one) {
  const auto attribute = [&](std::string_view name) -> std::string {
    const auto found = std::ranges::find(one.attributes, name, [](const chevron::attribute& each) { return each.name.local; });
    return found == one.attributes.end() ? std::string() : std::string(found->value);
  };
  static const std::unordered_map<std::string_view, html_tag_t> known = {
      {"br", html_tag::line_break{}},
      {"p", html_tag::paragraph{}}, {"div", html_tag::paragraph{}},
      {"section", html_tag::paragraph{}}, {"article", html_tag::paragraph{}},
      {"header", html_tag::paragraph{}}, {"footer", html_tag::paragraph{}},
      {"h1", html_tag::heading_open{}}, {"h2", html_tag::heading_open{}},
      {"h3", html_tag::heading_open{}}, {"h4", html_tag::heading_open{}},
      {"h5", html_tag::heading_open{}}, {"h6", html_tag::heading_open{}},
      {"ol", html_tag::list_open{true}}, {"ul", html_tag::list_open{}},
      {"dl", html_tag::paragraph{}}, {"dt", html_tag::heading_open{}}, {"dd", html_tag::paragraph{}},
      {"table", html_tag::table_row{}}, {"caption", html_tag::heading_open{}},
      {"tr", html_tag::table_row{}}, {"td", html_tag::cell{}}, {"th", html_tag::cell{}},
      {"hr", html_tag::rule{}},
      {"details", html_tag::details_open{}}, {"summary", html_tag::summary_open{}},
      {"script", html_tag::reply{}}, {"style", html_tag::reply{}},
      {"kbd", html_tag::code_open{}}, {"samp", html_tag::code_open{}},
      {"li", html_tag::list_item{}},
      {"mx-reply", html_tag::reply{}},
      {"b", html_tag::style_open{text_style::strong{}}},
      {"strong", html_tag::style_open{text_style::strong{}}},
      {"i", html_tag::style_open{text_style::emphasis{}}},
      {"em", html_tag::style_open{text_style::emphasis{}}},
      {"del", html_tag::style_open{text_style::struck{}}},
      {"s", html_tag::style_open{text_style::struck{}}},
      {"strike", html_tag::style_open{text_style::struck{}}},
      {"u", html_tag::style_open{text_style::underline{}}},
      {"ins", html_tag::style_open{text_style::underline{}}},
      {"span", html_tag::style_open{text_style::span{}}},
      {"code", html_tag::code_open{}},
      {"pre", html_tag::block_open{}},
      {"blockquote", html_tag::style_open{text_style::quote{}}},
      {"a", html_tag::link_open{}},
      {"img", html_tag::image{}},
  };
  const auto found = known.find(one.name.local);
  if (found == known.end())
    return html_tag::other{};
  const auto integer = [&](std::string_view name) -> std::optional<int> {
    const std::string value = attribute(name);
    int number = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() ? std::optional{number} : std::nullopt;
  };
  return spl::visit(spl::overloaded{
                                          [&](html_tag::details_open) -> html_tag_t {
                                            return html_tag::details_open{std::ranges::any_of(one.attributes,
                                                [](const auto& a) { return a.name.local == "open"; }), attribute("name")};
                                          },
                                          [&](html_tag::list_open list) -> html_tag_t {
                                            list.start = integer("start").value_or(1);
                                            return list;
                                          },
                                          [&](html_tag::list_item) -> html_tag_t { return html_tag::list_item{integer("value")}; },
                                          [&](html_tag::link_open) -> html_tag_t {
                                            const std::string href = attribute("href");
                                            return href.empty() ? html_tag_t{html_tag::other{}} : html_tag_t{html_tag::link_open{href}};
                                          },
                                          // Code says its language as a class, language-...
                                          [&](html_tag::code_open) -> html_tag_t {
                                            const std::string classes = attribute("class");
                                            constexpr std::string_view prefix = "language-";
                                            const auto at = classes.find(prefix);
                                            if (at == std::string::npos)
                                              return html_tag::code_open{};
                                            const auto end = classes.find(' ', at);
                                            return html_tag::code_open{classes.substr(
                                                at + prefix.size(), end == std::string::npos ? std::string::npos : end - at - prefix.size())};
                                          },
                                          // A span: a spoiler where it has data-mx-spoiler, a
                                          // value or none.
                                          [&](html_tag::style_open open) -> html_tag_t {
                                            const bool spoiler = std::ranges::any_of(one.attributes, [](const chevron::attribute& each) {
                                              return each.name.local == "data-mx-spoiler";
                                            });
                                            spl::visit(spl::overloaded{[&](text_style::span& span) { span.spoiler = spoiler; },
                                                                       [](const auto&) {}},
                                                       open.style);
                                            return open;
                                          },
                                          [&](html_tag::image) -> html_tag_t {
                                            return html_tag::image{attribute("src"), attribute("alt")};
                                          },
                                          [](const auto& as_found) -> html_tag_t { return as_found; }},
                       found->second);
}
// An element's end, read into what it does. The void elements' -- <br>,
// <img> -- do nothing: their start did it.
[[nodiscard]] inline html_tag_t end_of(const chevron::end_element& one) {
  static const std::unordered_map<std::string_view, html_tag_t> known = {
      {"section", html_tag::block_end{}}, {"article", html_tag::block_end{}},
      {"header", html_tag::block_end{}}, {"footer", html_tag::block_end{}},
      {"h4", html_tag::heading_close{}}, {"h5", html_tag::heading_close{}}, {"h6", html_tag::heading_close{}},
      {"ol", html_tag::list_close{}}, {"ul", html_tag::list_close{}},
      {"dl", html_tag::block_end{}}, {"dt", html_tag::heading_close{}}, {"dd", html_tag::block_end{}},
      {"table", html_tag::block_end{}}, {"caption", html_tag::heading_close{}}, {"tr", html_tag::block_end{}},
      {"kbd", html_tag::style_close{text_style::code{}}}, {"samp", html_tag::style_close{text_style::code{}}},
      {"details", html_tag::details_close{}}, {"summary", html_tag::summary_close{}},
      {"p", html_tag::block_end{}},
      {"div", html_tag::block_end{}},
      {"li", html_tag::block_end{}},
      {"h1", html_tag::heading_close{}},
      {"h2", html_tag::heading_close{}},
      {"h3", html_tag::heading_close{}},
      {"b", html_tag::style_close{text_style::strong{}}},
      {"strong", html_tag::style_close{text_style::strong{}}},
      {"i", html_tag::style_close{text_style::emphasis{}}},
      {"em", html_tag::style_close{text_style::emphasis{}}},
      {"del", html_tag::style_close{text_style::struck{}}},
      {"s", html_tag::style_close{text_style::struck{}}},
      {"strike", html_tag::style_close{text_style::struck{}}},
      {"u", html_tag::style_close{text_style::underline{}}},
      {"ins", html_tag::style_close{text_style::underline{}}},
      {"span", html_tag::style_close{text_style::span{}}},
      {"code", html_tag::style_close{text_style::code{}}},
      {"pre", html_tag::block_close{}},
      {"blockquote", html_tag::style_close{text_style::quote{}}},
      {"a", html_tag::link_close{}},
  };
  const auto found = known.find(one.name.local);
  return found == known.end() ? html_tag_t{html_tag::other{}} : found->second;
}

// Resolve custom emoji wherever they occur in plain reaction text. Preserve
// unknown shortcodes and words, and keep the protocol's original key intact.
[[nodiscard]] inline formatted emoji_text_of(std::string_view text, const std::vector<emote>& emotes) {
  formatted out;
  for (std::size_t at = 0; at < text.size();) {
    std::string source, label;
    std::size_t end = at;
    if (text.substr(at).starts_with("mxc://")) {
      end = text.find_first_of(" \t\r\n", at);
      if (end == std::string_view::npos) end = text.size();
      source = text.substr(at, end - at);
      const auto found = std::ranges::find(emotes, source, &emote::url);
      label = found == emotes.end() ? ":emoji:" : ":" + found->shortcode + ":";
    } else if (text[at] == ':') {
      const auto close = text.find(':', at + 1);
      if (close != std::string_view::npos) {
        const auto code = text.substr(at + 1, close - at - 1);
        const auto found = std::ranges::find(emotes, code, &emote::shortcode);
        if (found != emotes.end()) {
          source = found->url; label = text.substr(at, close - at + 1); end = close + 1;
        }
      }
    }
    if (source.empty()) { out.text += text[at++]; continue; }
    const auto first = out.text.size();
    out.text += "\u2003";
    out.spans.push_back({first, out.text.size(), std::move(source), false, true, std::move(label)});
    at = end;
  }
  return out;
}

// Reaction labels may mix text, pack shortcodes and MSC4027 media URLs.
// Build the same inline images for panel bubbles and timeline event rows.
[[nodiscard]] inline mux::body reaction_body_of(std::string_view key, const std::vector<emote>& emotes,
                                               const std::optional<std::string>& shortcode = std::nullopt) {
  auto words = emoji_text_of(key, emotes);
  if (words.spans.empty()) return {std::string(key), std::nullopt};
  if (shortcode && words.spans.size() == 1 && words.spans.front().first == 0 && words.spans.front().last == words.text.size())
    words.spans.front().plain = *shortcode;
  std::string plain, html;
  std::size_t from = 0;
  for (const auto& image : words.spans) {
    const auto before = std::string_view(words.text).substr(from, image.first - from);
    plain += before;
    html += chevron::escaped(before);
    plain += image.plain;
    html += std::format(R"(<img data-mx-emoticon src="{}" alt="{}" height="32">)",
                        chevron::escaped(image.target), chevron::escaped(image.plain));
    from = image.last;
  }
  const auto tail = std::string_view(words.text).substr(from);
  plain += tail;
  html += chevron::escaped(tail);
  return {std::move(plain), std::move(html)};
}

[[nodiscard]] inline std::vector<nodes::Text::Link> link_spans_in(std::string_view text);

[[nodiscard]] inline formatted read_html(std::string_view html) {
  formatted out;
  struct disclosure_frame { std::size_t index; int depth; bool summary_seen = false; std::optional<int> summary_depth; };
  std::vector<disclosure_frame> disclosures;
  std::vector<html_tag::list_open> lists;
  int depth = 0;
  bool cell_started = false;
  // Where each open style began, by its kind: closed, a stretch.
  std::vector<std::pair<text_style_t, std::size_t>> opened;
  std::string open_href;
  std::size_t link_start = 0;
  // A block of code open: where it began, and its language -- its lines
  // kept as they are, drawn as a block of their own.
  std::optional<std::size_t> block_from;
  std::string block_language;
  // A line ended: once, as a browser's blocks are -- never an empty line
  // between two, nor one at the start. Inside code, every line kept.
  const auto in_code = [&] {
    return block_from.has_value() || std::ranges::any_of(opened, [](const auto& one) {
      return spl::visit(spl::overloaded{[](text_style::code) { return true; }, [](const auto&) { return false; }}, one.first);
    });
  };
  const auto end_line = [&] {
    if (!out.text.empty() && out.text.back() != '\n')
      out.text += '\n';
  };
  // A tag read: what it does to the text.
  // Inside the quoted message a reply carries (<mx-reply>): how deep, its
  // elements passed over -- not shown twice.
  int skipping = 0;
  const auto apply = [&](html_tag_t read) {
      spl::visit(spl::overloaded{[&](html_tag::line_break) { out.text += '\n'; },
                            // A block of code: on lines of its own, its language as its
                            // code says, a stretch of the text marked as one.
                            [&](html_tag::block_open) {
                              end_line();
                              block_from = out.text.size();
                              block_language.clear();
                            },
                            [&](html_tag::block_close) {
                              if (!block_from)
                                return;
                              if (out.text.size() > *block_from && out.text.back() == '\n')
                                out.text.pop_back();
                              if (out.text.size() > *block_from)
                                out.styles.push_back({.first = *block_from, .last = out.text.size(), .code = true,
                                                      .block = true, .language = std::move(block_language)});
                              block_from.reset();
                              end_line();
                            },
                            // Code: in a block, it says the block's language; else it
                            // is code within a line.
                            [&](html_tag::code_open& open) {
                              if (block_from) {
                                if (!open.language.empty())
                                  block_language = std::move(open.language);
                                return;
                              }
                              opened.emplace_back(text_style::code{}, out.text.size());
                            },
                            [&](html_tag::block_end) { end_line(); },
                            [&](html_tag::paragraph) { end_line(); },
                            [&](html_tag::table_row) { end_line(); cell_started = false; },
                            [&](html_tag::heading_open) { end_line(); opened.emplace_back(text_style::strong{}, out.text.size()); },
                            [&](html_tag::heading_close) {
                              for (auto it = opened.rbegin(); it != opened.rend(); ++it)
                                if (spl::visit(spl::overloaded{[](text_style::strong) { return true; }, [](const auto&) { return false; }}, it->first)) {
                                  if (out.text.size() > it->second)
                                    out.styles.push_back(styled(text_style::strong{}, it->second, out.text.size()));
                                  opened.erase(std::next(it).base());
                                  break;
                                }
                              end_line();
                            },
                            [&](html_tag::list_open list) { end_line(); lists.push_back(list); },
                            [&](html_tag::list_close) { end_line(); if (!lists.empty()) lists.pop_back(); },
                            [&](html_tag::list_item item) {
                              end_line();
                              if (lists.size() > 1) out.text.append((lists.size() - 1) * 2, ' ');
                              if (!lists.empty() && lists.back().ordered) {
                                if (item.value) lists.back().start = *item.value;
                                out.text += std::to_string(lists.back().start) + ". ";
                                if (lists.back().start < std::numeric_limits<int>::max()) ++lists.back().start;
                              } else out.text += "\u2022 ";
                            },
                            [&](html_tag::cell) { if (cell_started) out.text += " | "; cell_started = true; },
                            [&](html_tag::rule) { end_line(); out.text += "────────\n"; },
                            [&](html_tag::details_open detail) {
                              end_line();
                              const auto at = out.text.size();
                              const auto parent = disclosures.empty() ? std::optional<std::size_t>{} : std::optional{disclosures.back().index};
                              out.disclosures.push_back({at, at, at, at, parent, detail.open, std::move(detail.group)});
                              disclosures.push_back({out.disclosures.size() - 1, depth});
                            },
                            [&](html_tag::details_close) {
                              if (disclosures.empty()) return;
                              out.disclosures[disclosures.back().index].last = out.text.size();
                              disclosures.pop_back();
                              end_line();
                            },
                            [&](html_tag::summary_open) {
                              if (disclosures.empty() || disclosures.back().summary_seen || depth != disclosures.back().depth + 1) {
                                end_line(); return;
                              }
                              end_line();
                              auto& frame = disclosures.back();
                              frame.summary_seen = true;
                              frame.summary_depth = depth;
                              out.disclosures[frame.index].summary_first = out.text.size();
                            },
                            [&](html_tag::summary_close) {
                              if (!disclosures.empty() && disclosures.back().summary_depth == depth) {
                                out.disclosures[disclosures.back().index].summary_last = out.text.size();
                                disclosures.back().summary_depth.reset();
                              }
                              end_line();
                            },
                            [&](html_tag::quote) {},
                            [&](html_tag::style_open& open) {
                              // A quote and a block of code start on a line of their own.
                              spl::visit(spl::overloaded{[&](text_style::quote) { end_line(); },
                                                    [](const auto&) {}},
                                         open.style);
                              opened.emplace_back(open.style, out.text.size());
                            },
                            [&](html_tag::style_close& close) {
                              for (auto it = opened.rbegin(); it != opened.rend(); ++it)
                                if (it->first.index() == close.style.index()) {
                                  const std::size_t from = it->second;
                                  if (out.text.size() > from)
                                    out.styles.push_back(spl::visit(
                                        [&](auto kind) { return styled(kind, from, out.text.size()); }, close.style));
                                  opened.erase(std::next(it).base());
                                  break;
                                }
                              spl::visit(spl::overloaded{[&](text_style::quote) { end_line(); }, [](const auto&) {}},
                                         close.style);
                            },
                            [&](html_tag::reply) { skipping = 1; },
                            [&](html_tag::link_open& link) {
                              open_href = std::move(link.href);
                              link_start = out.text.size();
                            },
                            [&](html_tag::link_close) {
                              if (open_href.empty())
                                return;
                              out.links.emplace_back(out.text.substr(link_start), open_href);
                              if (out.text.size() > link_start)
                                out.spans.push_back({link_start, out.text.size(), open_href});
                              open_href.clear();
                            },
                            // A picture from the server -- a custom emoji -- in the
                            // line, in the room of an em space; anything else by
                            // what it says it is.
                            [&](html_tag::image& picture) {
                              if (proto::is_media(picture.src)) {
                                const std::size_t first = out.text.size();
                                out.text += "\u2003";
                                out.spans.push_back({first, out.text.size(), std::move(picture.src), false, true, std::move(picture.alt)});
                              } else {
                                out.text += picture.alt;
                              }
                            },
                            [](html_tag::other) {}},
                 read);
  };
  // What is said: as it is in code; elsewhere its line ends as one, a
  // no-break space as a space.
  const auto say = [&](std::string_view said) {
    for (std::size_t at = 0; at < said.size();) {
      if (said[at] == '\n' && !in_code()) {
        end_line();
        ++at;
      } else if (said.substr(at).starts_with("\u00A0")) {
        out.text += ' ';
        at += std::string_view("\u00A0").size();
      } else {
        out.text += said[at];
        ++at;
      }
    }
  };
  // Read by chevron as HTML, as a message carries it: its events, in turn.
  chevron::parser reader(chevron::limits{}, chevron::dialect::html{});
  reader.feed(html);
  reader.finish();
  for (auto next = reader.next(); next && *next; next = reader.next())
    spl::visit(spl::overloaded{[&](const chevron::start_element& one) {
                                       if (skipping > 0) {
                                         ++skipping;
                                         return;
                                       }
                                       ++depth;
                                       apply(start_of(one));
                                     },
                                     [&](const chevron::end_element& one) {
                                       if (skipping > 0) {
                                         --skipping;
                                         if (skipping == 0) --depth;
                                         return;
                                       }
                                       apply(end_of(one));
                                       --depth;
                                     },
                                     [&](const chevron::text& one) {
                                       if (skipping == 0)
                                         say(one.content);
                                     }},
                  **next);
  while (!out.text.empty() && out.text.back() == '\n')
    out.text.pop_back();
  // Every stretch within the text left: one closed after a line break that
  // is gone ran past its end (review 5).
  const std::size_t size = out.text.size();
  for (auto& detail : out.disclosures) {
    detail.last = std::min(detail.last, size);
    detail.summary_first = std::min(detail.summary_first, detail.last);
    detail.summary_last = std::clamp(detail.summary_last, detail.summary_first, detail.last);
  }
  for (auto& span : out.spans)
    span.last = std::min(span.last, size);
  for (auto& style : out.styles)
    style.last = std::min(style.last, size);
  std::erase_if(out.spans, [](const auto& span) { return span.first >= span.last; });
  std::erase_if(out.styles, [](const auto& style) { return style.first >= style.last; });
  // And the addresses written in it bare, as in a plain text: a message's
  // HTML has an <a> only where its client made one, and an edited message
  // comes with HTML -- a link typed into it was not a link.
  for (const auto& bare : link_spans_in(out.text)) {
    const bool inside = std::ranges::any_of(out.spans, [&](const auto& span) {
      return bare.first < span.last && bare.last > span.first;
    });
    if (inside)
      continue;
    out.links.emplace_back(out.text.substr(bare.first, bare.last - bare.first), bare.target);
    out.spans.push_back(bare);
  }
  std::ranges::sort(out.spans, {}, &nodes::Text::Link::first);
  return out;
}

// Where the links of a plain text are: what starts with http:// or
// https://, up to a space.
[[nodiscard]] inline std::vector<nodes::Text::Link> link_spans_in(std::string_view text) {
  std::vector<nodes::Text::Link> out;
  for (std::size_t at = 0; at < text.size();) {
    const auto found = std::min(text.find("https://", at), text.find("http://", at));
    if (found == std::string_view::npos)
      break;
    auto end = text.find_first_of(" \n\t", found);
    if (end == std::string_view::npos)
      end = text.size();
    // A sentence's end is not the link's.
    while (end > found && std::string_view(".,;:!?)\"'").contains(text[end - 1]))
      --end;
    out.push_back({found, end, std::string(text.substr(found, end - found))});
    at = std::max(end, found + 1);
  }
  return out;
}

// The links of a plain text, each with its words: where link_spans_in finds
// them, the URL its own words.
[[nodiscard]] inline std::vector<std::pair<std::string, std::string>> links_in(std::string_view text) {
  return std::ranges::to<std::vector>(std::views::transform(link_spans_in(text), [](const nodes::Text::Link& one) { return std::pair{one.target, one.target}; }));
}

// A message's HTML as the message field edits it: its text and its runs,
// where it has only runs -- bold, links and the like. Not where it has a
// quote, a block of code or a list, which the field writes as marks of
// their own, nor a mention's pill or a custom emoji, which are atoms there:
// those are edited from the text as it is.
[[nodiscard]] inline std::optional<std::pair<std::string, std::vector<mux::styled_run>>> editable_runs(std::string_view html) {
  formatted read = read_html(html);
  if (std::ranges::any_of(read.styles, [](const nodes::Text::Styled& one) { return one.quote || one.block; }) ||
      std::ranges::any_of(read.spans, [](const nodes::Text::Link& one) { return one.pill || one.picture; }) ||
      read.text.contains("\u2022 "))
    return std::nullopt;
  std::vector<mux::styled_run> runs;
  for (const nodes::Text::Styled& one : read.styles) {
    const auto put = [&](bool on, mux::run_style_t style) {
      if (on)
        runs.push_back({one.first, one.last, std::move(style)});
    };
    put(one.strong, run_style::bold{});
    put(one.emphasis, run_style::italic{});
    put(one.underline, run_style::underline{});
    put(one.struck, run_style::strike{});
    put(one.code, run_style::code{});
    put(one.spoiler, run_style::spoiler{});
  }
  for (const nodes::Text::Link& one : read.spans)
    if (!one.target.empty())
      runs.push_back({one.first, one.last, run_style::link{one.target}});
  return std::pair{std::move(read.text), std::move(runs)};
}

}  // namespace mux::ui

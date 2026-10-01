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
struct formatted {
  std::string text;
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
}  // namespace text_style
using text_style_t =
    splice::variant<text_style::strong, text_style::emphasis, text_style::struck, text_style::code, text_style::quote>;
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
// An HTML tag, as read: what it does to the text, told by its type. Its
// name is looked up once, where it is read (start_of, end_of); what follows works on
// the variant.
namespace html_tag {
struct line_break {};  // <br>
struct block_end {};   // </p>, </div>, </li>, </blockquote>, </h1>..</h3>, </pre>
struct list_item {};   // <li>
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
using html_tag_t = splice::variant<html_tag::line_break, html_tag::block_end, html_tag::list_item, html_tag::quote,
                                html_tag::reply, html_tag::link_open, html_tag::link_close, html_tag::image,
                                html_tag::style_open, html_tag::style_close, html_tag::code_open, html_tag::block_open,
                                html_tag::block_close, html_tag::other>;

// An element's start, as chevron read it (dialect::html), read into what it
// does: its name looked up once, here -- what follows works on the variant.
[[nodiscard]] inline html_tag_t start_of(const chevron::start_element& one) {
  const auto attribute = [&](std::string_view name) -> std::string {
    const auto found = std::ranges::find(one.attributes, name, [](const chevron::attribute& each) { return each.name.local; });
    return found == one.attributes.end() ? std::string() : std::string(found->value);
  };
  static const std::unordered_map<std::string_view, html_tag_t> known = {
      {"br", html_tag::line_break{}},
      {"li", html_tag::list_item{}},
      {"mx-reply", html_tag::reply{}},
      {"b", html_tag::style_open{text_style::strong{}}},
      {"strong", html_tag::style_open{text_style::strong{}}},
      {"i", html_tag::style_open{text_style::emphasis{}}},
      {"em", html_tag::style_open{text_style::emphasis{}}},
      {"del", html_tag::style_open{text_style::struck{}}},
      {"s", html_tag::style_open{text_style::struck{}}},
      {"strike", html_tag::style_open{text_style::struck{}}},
      {"code", html_tag::code_open{}},
      {"pre", html_tag::block_open{}},
      {"blockquote", html_tag::style_open{text_style::quote{}}},
      {"a", html_tag::link_open{}},
      {"img", html_tag::image{}},
  };
  const auto found = known.find(one.name.local);
  if (found == known.end())
    return html_tag::other{};
  return splice::visit(splice::overloaded{[&](html_tag::link_open) -> html_tag_t {
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
      {"p", html_tag::block_end{}},
      {"div", html_tag::block_end{}},
      {"li", html_tag::block_end{}},
      {"h1", html_tag::block_end{}},
      {"h2", html_tag::block_end{}},
      {"h3", html_tag::block_end{}},
      {"b", html_tag::style_close{text_style::strong{}}},
      {"strong", html_tag::style_close{text_style::strong{}}},
      {"i", html_tag::style_close{text_style::emphasis{}}},
      {"em", html_tag::style_close{text_style::emphasis{}}},
      {"del", html_tag::style_close{text_style::struck{}}},
      {"s", html_tag::style_close{text_style::struck{}}},
      {"strike", html_tag::style_close{text_style::struck{}}},
      {"code", html_tag::style_close{text_style::code{}}},
      {"pre", html_tag::block_close{}},
      {"blockquote", html_tag::style_close{text_style::quote{}}},
      {"a", html_tag::link_close{}},
  };
  const auto found = known.find(one.name.local);
  return found == known.end() ? html_tag_t{html_tag::other{}} : found->second;
}

[[nodiscard]] inline std::vector<nodes::Text::Link> link_spans_in(std::string_view text);

[[nodiscard]] inline formatted read_html(std::string_view html) {
  formatted out;
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
      return splice::visit(splice::overloaded{[](text_style::code) { return true; }, [](const auto&) { return false; }}, one.first);
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
      splice::visit(splice::overloaded{[&](html_tag::line_break) { out.text += '\n'; },
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
                            [&](html_tag::list_item) { out.text += "\u2022 "; },
                            [&](html_tag::quote) {},
                            [&](html_tag::style_open& open) {
                              // A quote and a block of code start on a line of their own.
                              splice::visit(splice::overloaded{[&](text_style::quote) { end_line(); },
                                                    [](const auto&) {}},
                                         open.style);
                              opened.emplace_back(open.style, out.text.size());
                            },
                            [&](html_tag::style_close& close) {
                              for (auto it = opened.rbegin(); it != opened.rend(); ++it)
                                if (it->first.index() == close.style.index()) {
                                  const std::size_t from = it->second;
                                  if (out.text.size() > from)
                                    out.styles.push_back(splice::visit(
                                        [&](auto kind) { return styled(kind, from, out.text.size()); }, close.style));
                                  opened.erase(std::next(it).base());
                                  break;
                                }
                              splice::visit(splice::overloaded{[&](text_style::quote) { end_line(); }, [](const auto&) {}},
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
                              if (picture.src.starts_with("mxc://")) {
                                const std::size_t first = out.text.size();
                                out.text += "\u2003";
                                out.spans.push_back({first, out.text.size(), std::move(picture.src), false, true});
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
    splice::visit(splice::overloaded{[&](const chevron::start_element& one) {
                                       if (skipping > 0) {
                                         ++skipping;
                                         return;
                                       }
                                       apply(start_of(one));
                                     },
                                     [&](const chevron::end_element& one) {
                                       if (skipping > 0) {
                                         --skipping;
                                         return;
                                       }
                                       apply(end_of(one));
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

// The links of a plain text: what starts with http:// or https://, up to a
// space.
[[nodiscard]] inline std::vector<std::pair<std::string, std::string>> links_in(std::string_view text) {
  std::vector<std::pair<std::string, std::string>> out;
  for (std::size_t at = 0; at < text.size();) {
    const auto found = std::min(text.find("https://", at), text.find("http://", at));
    if (found == std::string_view::npos)
      break;
    auto end = text.find_first_of(" \n\t", found);
    if (end == std::string_view::npos)
      end = text.size();
    const std::string url(text.substr(found, end - found));
    out.emplace_back(url, url);
    at = end;
  }
  return out;
}

}  // namespace mux::ui

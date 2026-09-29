// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:html -- A message's HTML read into text and links.
export module mux.ui:html;

import std;
import mux.variant;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.text;
import mux.core;
import mux.config;
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
struct code {};      // <code>, <pre>
struct quote {};     // <blockquote>
}  // namespace text_style
using text_style_t =
    mux::variant<text_style::strong, text_style::emphasis, text_style::struck, text_style::code, text_style::quote>;
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
// name is looked up once, where it is read (tag_of); what follows works on
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
struct other {};  // anything else: dropped
}  // namespace html_tag
using html_tag_t = mux::variant<html_tag::line_break, html_tag::block_end, html_tag::list_item, html_tag::quote,
                                html_tag::reply, html_tag::link_open, html_tag::link_close, html_tag::image,
                                html_tag::style_open, html_tag::style_close, html_tag::other>;

// An attribute's value in a tag's inside, quoted either way.
[[nodiscard]] inline std::optional<std::string> attribute_of(std::string_view inside, std::string_view name) {
  for (std::size_t at = inside.find(name); at != std::string_view::npos; at = inside.find(name, at + 1)) {
    const bool starts = at == 0 || inside[at - 1] == ' ';
    const std::size_t eq = at + name.size();
    if (!starts || eq + 1 >= inside.size() || inside[eq] != '=')
      continue;
    const char quote = inside[eq + 1];
    if (quote != '"' && quote != '\'')
      continue;
    const auto stop = inside.find(quote, eq + 2);
    return std::string(inside.substr(eq + 2, stop == std::string_view::npos ? std::string_view::npos : stop - eq - 2));
  }
  return std::nullopt;
}

// A tag's inside (between < and >) read into what it is.
[[nodiscard]] inline html_tag_t tag_of(std::string_view inside) {
  std::string name;
  for (const char t : inside) {
    if (t == ' ' || t == '/' && !name.empty())
      break;
    name += static_cast<char>(std::tolower(static_cast<unsigned char>(t)));
  }
  static const std::unordered_map<std::string_view, html_tag_t> known = {
      {"br", html_tag::line_break{}},   {"/p", html_tag::block_end{}},    {"/div", html_tag::block_end{}},
      {"/li", html_tag::block_end{}}, {"/h1", html_tag::block_end{}},
      {"/h2", html_tag::block_end{}},   {"/h3", html_tag::block_end{}},
      {"li", html_tag::list_item{}},    {"mx-reply", html_tag::reply{}},
      {"b", html_tag::style_open{text_style::strong{}}},       {"/b", html_tag::style_close{text_style::strong{}}},
      {"strong", html_tag::style_open{text_style::strong{}}},  {"/strong", html_tag::style_close{text_style::strong{}}},
      {"i", html_tag::style_open{text_style::emphasis{}}},     {"/i", html_tag::style_close{text_style::emphasis{}}},
      {"em", html_tag::style_open{text_style::emphasis{}}},    {"/em", html_tag::style_close{text_style::emphasis{}}},
      {"del", html_tag::style_open{text_style::struck{}}},     {"/del", html_tag::style_close{text_style::struck{}}},
      {"s", html_tag::style_open{text_style::struck{}}},       {"/s", html_tag::style_close{text_style::struck{}}},
      {"strike", html_tag::style_open{text_style::struck{}}},  {"/strike", html_tag::style_close{text_style::struck{}}},
      {"code", html_tag::style_open{text_style::code{}}},      {"/code", html_tag::style_close{text_style::code{}}},
      {"pre", html_tag::style_open{text_style::code{}}},       {"/pre", html_tag::style_close{text_style::code{}}},
      {"blockquote", html_tag::style_open{text_style::quote{}}}, {"/blockquote", html_tag::style_close{text_style::quote{}}},
      {"a", html_tag::link_open{}},     {"/a", html_tag::link_close{}},  {"img", html_tag::image{}},
  };
  const auto found = known.find(name);
  if (found == known.end())
    return html_tag::other{};
  // A link takes where it goes from its href; the rest are as found.
  return mux::visit(overloaded{[&](html_tag::link_open) -> html_tag_t {
                                 const auto href = inside.find("href=");
                                 if (href == std::string_view::npos || href + 6 >= inside.size())
                                   return html_tag::other{};
                                 const char quote = inside[href + 5];
                                 const auto stop = inside.find(quote, href + 6);
                                 return html_tag::link_open{std::string(inside.substr(
                                     href + 6, stop == std::string_view::npos ? std::string_view::npos : stop - href - 6))};
                               },
                               [&](html_tag::image) -> html_tag_t {
                                 return html_tag::image{attribute_of(inside, "src").value_or(""),
                                                        attribute_of(inside, "alt").value_or("")};
                               },
                               [](const auto& as_found) -> html_tag_t { return as_found; }},
                    found->second);
}

// An entity's name (between & and ;) read into what it stands for; one not
// known is kept as it was written.
[[nodiscard]] inline std::string entity_of(std::string_view name) {
  static const std::unordered_map<std::string_view, std::string_view> known = {
      {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"}, {"#39", "'"}, {"nbsp", " "},
  };
  if (const auto found = known.find(name); found != known.end())
    return std::string(found->second);
  return "&" + std::string(name) + ";";
}

[[nodiscard]] inline std::vector<nodes::Text::Link> link_spans_in(std::string_view text);

[[nodiscard]] inline formatted read_html(std::string_view html) {
  formatted out;
  // Where each open style began, by its kind: closed, a stretch.
  std::vector<std::pair<text_style_t, std::size_t>> opened;
  std::string open_href;
  std::size_t link_start = 0;
  std::size_t at = 0;
  // A line ended: once, as a browser's blocks are -- never an empty line
  // between two, nor one at the start. Inside code, every line kept.
  const auto in_code = [&] {
    return std::ranges::any_of(opened, [](const auto& one) {
      return mux::visit(overloaded{[](text_style::code) { return true; }, [](const auto&) { return false; }}, one.first);
    });
  };
  const auto end_line = [&] {
    if (!out.text.empty() && out.text.back() != '\n')
      out.text += '\n';
  };
  while (at < html.size()) {
    const char c = html[at];
    if (c == '<') {
      const auto end = html.find('>', at);
      if (end == std::string_view::npos)
        break;
      html_tag_t read = tag_of(html.substr(at + 1, end - at - 1));
      at = end + 1;
      mux::visit(overloaded{[&](html_tag::line_break) { out.text += '\n'; },
                            [&](html_tag::block_end) { end_line(); },
                            [&](html_tag::list_item) { out.text += "• "; },
                            [&](html_tag::quote) {},
                            [&](html_tag::style_open& open) {
                              // A quote and a block of code start on a line of their own.
                              mux::visit(overloaded{[&](text_style::quote) { end_line(); },
                                                    [](const auto&) {}},
                                         open.style);
                              opened.emplace_back(open.style, out.text.size());
                            },
                            [&](html_tag::style_close& close) {
                              for (auto it = opened.rbegin(); it != opened.rend(); ++it)
                                if (it->first.index() == close.style.index()) {
                                  const std::size_t from = it->second;
                                  if (out.text.size() > from)
                                    out.styles.push_back(mux::visit(
                                        [&](auto kind) { return styled(kind, from, out.text.size()); }, close.style));
                                  opened.erase(std::next(it).base());
                                  break;
                                }
                              mux::visit(overloaded{[&](text_style::quote) { end_line(); }, [](const auto&) {}},
                                         close.style);
                            },
                            [&](html_tag::reply) {
                              // The quoted message: not shown twice.
                              const auto close = html.find("</mx-reply>", end);
                              at = close == std::string_view::npos ? html.size() : close + 11;
                            },
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
    } else if (c == '&') {
      const auto end = html.find(';', at);
      if (end == std::string_view::npos || end - at > 8) {
        out.text += c;
        ++at;
        continue;
      }
      out.text += entity_of(html.substr(at + 1, end - at - 1));
      at = end + 1;
    } else if (c == '\n' && !in_code()) {
      end_line();
      ++at;
    } else {
      out.text += c;
      ++at;
    }
  }
  while (!out.text.empty() && out.text.back() == '\n')
    out.text.pop_back();
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

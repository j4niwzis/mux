// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:html -- A message's HTML read into text and links.
export module mux.ui:html;

import std;
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
};
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
struct other {};       // anything else: dropped
}  // namespace html_tag
using html_tag_t = std::variant<html_tag::line_break, html_tag::block_end, html_tag::list_item, html_tag::quote,
                                html_tag::reply, html_tag::link_open, html_tag::link_close, html_tag::other>;

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
      {"/blockquote", html_tag::block_end{}}, {"/li", html_tag::block_end{}}, {"/h1", html_tag::block_end{}},
      {"/h2", html_tag::block_end{}},   {"/h3", html_tag::block_end{}},   {"/pre", html_tag::block_end{}},
      {"li", html_tag::list_item{}},    {"blockquote", html_tag::quote{}}, {"mx-reply", html_tag::reply{}},
      {"a", html_tag::link_open{}},     {"/a", html_tag::link_close{}},
  };
  const auto found = known.find(name);
  if (found == known.end())
    return html_tag::other{};
  // A link takes where it goes from its href; the rest are as found.
  return std::visit(overloaded{[&](html_tag::link_open) -> html_tag_t {
                                 const auto href = inside.find("href=");
                                 if (href == std::string_view::npos || href + 6 >= inside.size())
                                   return html_tag::other{};
                                 const char quote = inside[href + 5];
                                 const auto stop = inside.find(quote, href + 6);
                                 return html_tag::link_open{std::string(inside.substr(
                                     href + 6, stop == std::string_view::npos ? std::string_view::npos : stop - href - 6))};
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

[[nodiscard]] inline formatted read_html(std::string_view html) {
  formatted out;
  std::string open_href;
  std::size_t link_start = 0;
  std::size_t at = 0;
  while (at < html.size()) {
    const char c = html[at];
    if (c == '<') {
      const auto end = html.find('>', at);
      if (end == std::string_view::npos)
        break;
      html_tag_t read = tag_of(html.substr(at + 1, end - at - 1));
      at = end + 1;
      std::visit(overloaded{[&](html_tag::line_break) { out.text += '\n'; },
                            [&](html_tag::block_end) { out.text += '\n'; },
                            [&](html_tag::list_item) { out.text += "• "; },
                            [&](html_tag::quote) { out.text += "│ "; },
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
    } else {
      out.text += c;
      ++at;
    }
  }
  while (!out.text.empty() && out.text.back() == '\n')
    out.text.pop_back();
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

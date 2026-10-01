// SPDX-License-Identifier: AGPL-3.0-only
// mux.preview -- A link's preview fetched from its site itself, not through
// any server: the page asked for over HTTPS, through the chat's
// account's proxy, its Open Graph tags read; and the picture they name.
//
// Off unless chosen: the site then sees the address the request comes from
// (the proxy's, where there is one), and no server learns the link.
export module mux.preview;

import std;
import mux.core;
import mux.net;
import mux.http;

export namespace mux::preview {

// Where a link points, as a request is made of it: the site, and the
// target on it (its path and query; never its fragment).
struct page {
  http::url site;
  std::string target;
};

// A link as a page to ask: https as it is, http asked over https (mux speaks
// no plain HTTP, and the link stays off the wire in the clear); anything
// else, none.
[[nodiscard]] inline std::optional<page> page_of(std::string_view link) {
  if (link.starts_with("https://"))
    link.remove_prefix(8);
  else if (link.starts_with("http://"))
    link.remove_prefix(7);
  else
    return std::nullopt;
  link = link.substr(0, link.find('#'));
  const auto end = link.find_first_of("/?");
  const std::string_view authority = link.substr(0, end);
  if (authority.empty() || authority.find('@') != std::string_view::npos)
    return std::nullopt;  // no user names: never sent anywhere
  auto site = http::url::parse(authority);
  if (!site)
    return std::nullopt;
  std::string target(end == std::string_view::npos ? std::string_view("/") : link.substr(end));
  if (target.starts_with('?'))
    target.insert(0, "/");
  return page{std::move(*site), std::move(target)};
}

// Where a redirect or a page's picture points, from the page it was on:
// a whole link, one without its scheme, or a path on the same site.
[[nodiscard]] inline std::optional<std::string> resolve(std::string_view from, std::string_view to) {
  if (to.starts_with("https://") || to.starts_with("http://"))
    return std::string(to);
  const auto at = page_of(from);
  if (!at)
    return std::nullopt;
  const std::string origin = "https://" + at->site.host + (at->site.port == 443 ? "" : ":" + std::to_string(at->site.port));
  if (to.starts_with("//"))
    return "https:" + std::string(to);
  if (to.starts_with('/'))
    return origin + std::string(to);
  const std::string_view path = std::string_view(at->target).substr(0, at->target.find('?'));
  return origin + std::string(path.substr(0, path.rfind('/') + 1)) + std::string(to);
}

// HTML's character references, the ones pages write in their tags.
[[nodiscard]] inline std::string unescaped(std::string_view text) {
  constexpr std::array<std::pair<std::string_view, std::string_view>, 6> named{
      {{"amp;", "&"}, {"lt;", "<"}, {"gt;", ">"}, {"quot;", "\""}, {"apos;", "'"}, {"nbsp;", " "}}};
  const auto utf8 = [](std::uint32_t c) {
    std::string out;
    if (c < 0x80) {
      out += static_cast<char>(c);
    } else if (c < 0x800) {
      out += static_cast<char>(0xC0 | (c >> 6));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      out += static_cast<char>(0xE0 | (c >> 12));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x110000) {
      out += static_cast<char>(0xF0 | (c >> 18));
      out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    }
    return out;
  };
  std::string out;
  out.reserve(text.size());
  for (std::size_t at = 0; at < text.size();) {
    if (text[at] != '&') {
      out += text[at++];
      continue;
    }
    const std::string_view rest = text.substr(at + 1);
    if (const auto found = std::ranges::find_if(named, [&](const auto& one) { return rest.starts_with(one.first); });
        found != named.end()) {
      out += found->second;
      at += 1 + found->first.size();
      continue;
    }
    if (rest.starts_with('#')) {
      const bool hex = rest.size() > 1 && (rest[1] == 'x' || rest[1] == 'X');
      const std::string_view digits = rest.substr(hex ? 2 : 1);
      std::uint32_t code = 0;
      const auto [end, bad] = std::from_chars(digits.data(), digits.data() + digits.size(), code, hex ? 16 : 10);
      if (bad == std::errc{} && end < digits.data() + digits.size() && *end == ';' && code != 0) {
        out += utf8(code);
        at = static_cast<std::size_t>(end - text.data()) + 1;
        continue;
      }
    }
    out += text[at++];
  }
  return out;
}

// One attribute's value in a tag, by its name: quoted either way, or bare.
// `lower` is the tag lower-cased, for the names; the value comes from `tag`.
[[nodiscard]] inline std::optional<std::string> attribute(std::string_view tag, std::string_view lower, std::string_view name) {
  for (std::size_t from = 0;;) {
    const auto found = lower.find(name, from);
    if (found == std::string_view::npos)
      return std::nullopt;
    from = found + name.size();
    const bool starts = found > 0 && (std::isspace(static_cast<unsigned char>(lower[found - 1])) != 0);
    std::size_t at = from;
    while (at < lower.size() && std::isspace(static_cast<unsigned char>(lower[at])) != 0)
      ++at;
    if (!starts || at >= lower.size() || lower[at] != '=')
      continue;
    ++at;
    while (at < lower.size() && std::isspace(static_cast<unsigned char>(lower[at])) != 0)
      ++at;
    if (at >= tag.size())
      return std::nullopt;
    if (tag[at] == '"' || tag[at] == '\'') {
      const auto close = tag.find(tag[at], at + 1);
      if (close == std::string_view::npos)
        return std::nullopt;
      return unescaped(tag.substr(at + 1, close - at - 1));
    }
    const auto end = tag.find_first_of(" \t\r\n/>", at);
    return unescaped(tag.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at));
  }
}

// What a preview is read from: the page's own tags, Open Graph's first,
// its plain description and <title> where it has none.
struct facts {
  std::string site, title, description, image, plain_description, page_title;
};
// The tags read, by their names -- where the names are read, once, into
// the fields they fill.
inline constexpr std::array<std::pair<std::string_view, std::string facts::*>, 6> kTags{{
    {"og:site_name", &facts::site},
    {"og:title", &facts::title},
    {"og:description", &facts::description},
    {"og:image", &facts::image},
    {"og:image:url", &facts::image},
    {"description", &facts::plain_description},
}};

// The page's head read for its preview: none where it says nothing.
[[nodiscard]] inline std::optional<link_preview> read_page(std::string_view html, std::string_view at) {
  const std::string lowered = html | std::views::transform([](char c) {
                                return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                              }) |
                              std::ranges::to<std::string>();
  const std::string_view lower = lowered;
  const std::size_t head_end = std::min(lower.find("</head"), lower.size());
  facts read;
  for (std::size_t from = 0;;) {
    const auto open = lower.find("<meta", from);
    if (open == std::string_view::npos || open >= head_end)
      break;
    const auto close = lower.find('>', open);
    if (close == std::string_view::npos)
      break;
    from = close + 1;
    const std::string_view tag = html.substr(open, close - open);
    const std::string_view tag_lower = lower.substr(open, close - open);
    const auto name = attribute(tag, tag_lower, "property").or_else([&] { return attribute(tag, tag_lower, "name"); });
    const auto content = attribute(tag, tag_lower, "content");
    if (!name || !content)
      continue;
    const std::string key = *name | std::views::transform([](char c) {
                              return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                            }) |
                            std::ranges::to<std::string>();
    if (const auto field = std::ranges::find(kTags, key, [](const auto& one) { return one.first; });
        field != kTags.end() && (read.*(field->second)).empty())
      read.*(field->second) = *content;
  }
  if (const auto title = lower.find("<title"); title != std::string_view::npos && title < head_end)
    if (const auto start = lower.find('>', title); start != std::string_view::npos)
      if (const auto end = lower.find("</title", start); end != std::string_view::npos)
        read.page_title = unescaped(html.substr(start + 1, end - start - 1));
  link_preview made{.site = read.site,
                    .title = read.title.empty() ? read.page_title : read.title,
                    .description = read.description.empty() ? read.plain_description : read.description,
                    .from_site = true};
  if (!read.image.empty())
    if (auto image = resolve(at, read.image); image && image->starts_with("https://"))
      made.image = std::move(*image);
  if (made.title.empty() && made.description.empty())
    return std::nullopt;
  return made;
}

// A link's bytes, fetched from its site: its redirects followed, a few at
// most, each over HTTPS, through `via`. The page's final address with it.
struct fetched {
  std::string at;
  http::response answer;
};
[[nodiscard]] inline std::optional<fetched> fetch(net::loop& loop, net::tls& tls, const std::optional<net::proxy>& via,
                                                  std::string link, std::string_view accept) {
  for (int hop = 0; hop < 5; ++hop) {
    const auto where = page_of(link);
    if (!where)
      return std::nullopt;
    http::connection one(loop, tls, where->site, via);
    try {
      auto got = one.request("GET", where->target, {}, std::nullopt, std::chrono::seconds(20), {}, static_cast<const http::no_progress*>(nullptr), accept);
      if (got.status >= 300 && got.status < 400 && got.location) {
        auto next = resolve(link, *got.location);
        if (!next)
          return std::nullopt;
        link = std::move(*next);
        continue;
      }
      if (got.status != 200)
        return std::nullopt;
      return fetched{std::move(link), std::move(got)};
    } catch (const net::failure&) {
      return std::nullopt;
    }
  }
  return std::nullopt;
}

// A link's preview, from its page.
[[nodiscard]] inline std::optional<link_preview> fetch_preview(net::loop& loop, net::tls& tls,
                                                               const std::optional<net::proxy>& via, std::string link) {
  auto got = fetch(loop, tls, via, std::move(link), "text/html,application/xhtml+xml");
  if (!got)
    return std::nullopt;
  return read_page(got->answer.body, got->at);
}
// A preview's picture's bytes.
[[nodiscard]] inline std::optional<std::string> fetch_picture(net::loop& loop, net::tls& tls,
                                                              const std::optional<net::proxy>& via, std::string link) {
  auto got = fetch(loop, tls, via, std::move(link), "image/*");
  if (!got || got->answer.body.empty())
    return std::nullopt;
  return std::move(got->answer.body);
}

}  // namespace mux::preview

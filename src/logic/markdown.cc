// SPDX-License-Identifier: AGPL-3.0-only
// mux.logic.markdown -- What is typed, as Element sends it: the Markdown in
// it -- quotes, code, emphasis, strong, strike, links, lists -- made the HTML
// Matrix carries beside the text (org.matrix.custom.html). Text with none of
// it has no HTML: it is sent as it is.
export module mux.logic.markdown;

import std;

export namespace mux::logic {

// Text made safe inside HTML.
[[nodiscard]] inline std::string escaped(std::string_view text) {
  std::string out;
  for (const char c : text) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

// A line's inline Markdown: `code` first (nothing inside it is Markdown),
// then [text](url), **strong**, ~~strike~~, *emphasis* and _emphasis_.
// `marked` is set where any of it was found.
[[nodiscard]] inline std::string inline_html(std::string_view line, bool& marked) {
  std::string out;
  std::size_t at = 0;
  // A run closed by `close` on this line, from just after its opening.
  const auto closing = [&](std::size_t from, std::string_view close) {
    const auto end = line.find(close, from);
    return end == std::string_view::npos || end == from ? std::string_view::npos : end;
  };
  while (at < line.size()) {
    const std::string_view rest = line.substr(at);
    if (rest.starts_with("`")) {
      if (const auto end = closing(at + 1, "`"); end != std::string_view::npos) {
        out += "<code>" + escaped(line.substr(at + 1, end - at - 1)) + "</code>";
        at = end + 1;
        marked = true;
        continue;
      }
    }
    if (rest.starts_with("[")) {
      const auto text_end = line.find("](", at + 1);
      const auto url_end = text_end == std::string_view::npos ? text_end : line.find(')', text_end + 2);
      if (text_end != std::string_view::npos && url_end != std::string_view::npos) {
        bool inner = false;
        out += "<a href=\"" + escaped(line.substr(text_end + 2, url_end - text_end - 2)) + "\">" +
               inline_html(line.substr(at + 1, text_end - at - 1), inner) + "</a>";
        at = url_end + 1;
        marked = true;
        continue;
      }
    }
    struct pair_mark {
      std::string_view mark;
      std::string_view tag;
    };
    bool matched = false;
    for (const pair_mark& one : {pair_mark{"**", "strong"}, pair_mark{"__", "strong"}, pair_mark{"~~", "del"},
                                  pair_mark{"*", "em"}, pair_mark{"_", "em"}}) {
      if (!rest.starts_with(one.mark))
        continue;
      // A word's own underscore (snake_case) is not emphasis.
      if (one.mark == "_" && at > 0 && std::isalnum(static_cast<unsigned char>(line[at - 1])))
        continue;
      const auto end = closing(at + one.mark.size(), one.mark);
      if (end == std::string_view::npos)
        continue;
      bool inner = false;
      out += std::format("<{}>{}</{}>", one.tag, inline_html(line.substr(at + one.mark.size(), end - at - one.mark.size()), inner),
                         one.tag);
      at = end + one.mark.size();
      marked = matched = true;
      break;
    }
    if (matched)
      continue;
    out += escaped(line.substr(at, 1));
    ++at;
  }
  return out;
}

// The whole of what is typed, as HTML where it has Markdown; nothing where
// it has none.
[[nodiscard]] inline std::optional<std::string> markdown_html(std::string_view text) {
  std::vector<std::string_view> lines;
  for (std::size_t at = 0; at <= text.size();) {
    const auto end = text.find('\n', at);
    lines.push_back(text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at));
    if (end == std::string_view::npos)
      break;
    at = end + 1;
  }
  std::string html;
  bool marked = false;
  // The block a line is in, as it is closed when the next one opens.
  enum class block { none, quote, bullets, numbers };
  block open = block::none;
  const auto close = [&] {
    html += open == block::quote ? "</blockquote>" : open == block::bullets ? "</ul>" : open == block::numbers ? "</ol>" : "";
    open = block::none;
  };
  bool first_line = true;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    const std::string_view line = lines[i];
    // A fenced block of code: kept as it is, up to its closing fence.
    if (line.starts_with("```")) {
      close();
      std::string code;
      std::size_t j = i + 1;
      for (; j < lines.size() && !lines[j].starts_with("```"); ++j) {
        code += escaped(lines[j]);
        code += '\n';
      }
      const std::string_view language = line.substr(3);
      html += language.empty() ? "<pre><code>" : "<pre><code class=\"language-" + escaped(language) + "\">";
      html += code + "</code></pre>";
      marked = true;
      first_line = true;
      i = j;
      continue;
    }
    const auto begin = [&](block which, std::string_view tag) {
      if (open != which) {
        close();
        html += tag;
        open = which;
        first_line = true;
      }
      marked = true;
    };
    if (line.starts_with("> ") || line == ">") {
      begin(block::quote, "<blockquote>");
      if (!first_line)
        html += "<br>";
      html += inline_html(line.substr(std::min<std::size_t>(2, line.size())), marked);
      first_line = false;
      continue;
    }
    if (line.starts_with("- ") || line.starts_with("* ") || line.starts_with("+ ")) {
      begin(block::bullets, "<ul>");
      html += "<li>" + inline_html(line.substr(2), marked) + "</li>";
      continue;
    }
    if (const auto dot = line.find(". "); dot != std::string_view::npos && dot > 0 && dot < 4 &&
                                          std::ranges::all_of(line.substr(0, dot), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
      begin(block::numbers, "<ol>");
      html += "<li>" + inline_html(line.substr(dot + 2), marked) + "</li>";
      continue;
    }
    if (open != block::none) {
      close();
      first_line = true;
    }
    if (!first_line)
      html += "<br>";
    html += inline_html(line, marked);
    first_line = false;
  }
  close();
  if (!marked)
    return std::nullopt;
  return html;
}

}  // namespace mux::logic

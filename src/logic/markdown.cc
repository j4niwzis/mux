// SPDX-License-Identifier: AGPL-3.0-only
// mux.logic.markdown -- What is typed, as Element sends it: the Markdown in
// it -- quotes, code, emphasis, strong, strike, links, lists -- made the HTML
// Matrix carries beside the text (org.matrix.custom.html). Text with none of
// it has no HTML: it is sent as it is.
export module mux.logic.markdown;

import std;
import chevron.escape;

export namespace mux::logic {


// A run of the text formatted in the field -- bold, a link -- by byte
// offsets in the whole text, and the tags it is written with.
struct html_run {
  std::size_t first = 0;
  std::size_t last = 0;
  std::string open;
  std::string close;
};
// The runs a line is written with: they, and where the whole text they
// count in starts -- the line a view into it.
struct runs_in {
  const char* origin = nullptr;
  const std::vector<html_run>* runs = nullptr;
};

// A line's inline Markdown: `code` first (nothing inside it is Markdown),
// then [text](url), **strong**, ~~strike~~, *emphasis* and _emphasis_.
// `marked` is set where any of it was found. With the field's runs: each
// written with its tags where it is -- all those open closed and the ones
// there opened again at each place one starts or ends, so that they nest
// -- and Markdown read only where no run starts or ends inside it.
[[nodiscard]] inline std::string inline_html(std::string_view line, bool& marked, runs_in in = {}) {
  std::string out;
  std::size_t at = 0;
  const std::vector<html_run> none;
  const std::vector<html_run>& runs = in.runs != nullptr ? *in.runs : none;
  const std::size_t base = in.origin != nullptr ? static_cast<std::size_t>(line.data() - in.origin) : 0;
  std::vector<std::size_t> open_runs;
  // The runs at `here`, opened, and those that ended closed.
  const auto sync = [&](std::size_t here) {
    std::vector<std::size_t> now = std::ranges::to<std::vector<std::size_t>>(std::views::filter(
        std::views::iota(std::size_t{0}, runs.size()), [&](std::size_t i) { return runs[i].first <= here && here < runs[i].last; }));
    if (now == open_runs)
      return;
    for (const std::size_t i : std::views::reverse(open_runs))
      out += runs[i].close;
    for (const std::size_t i : now)
      out += runs[i].open;
    open_runs = std::move(now);
    marked = true;
  };
  // Whether no run starts or ends inside [from, to) of the line, past its
  // start: Markdown there may be read as one piece.
  const auto whole = [&](std::size_t from, std::size_t to) {
    return std::ranges::none_of(runs, [&](const html_run& run) {
      return (run.first > base + from && run.first < base + to) || (run.last > base + from && run.last < base + to);
    });
  };
  // A run closed by `close` on this line, from just after its opening.
  const auto closing = [&](std::size_t from, std::string_view close) {
    const auto end = line.find(close, from);
    return end == std::string_view::npos || end == from ? std::string_view::npos : end;
  };
  while (at < line.size()) {
    sync(base + at);
    const std::string_view rest = line.substr(at);
    if (rest.starts_with("`")) {
      if (const auto end = closing(at + 1, "`"); end != std::string_view::npos && whole(at, end + 1)) {
        out += "<code>" + chevron::escaped(line.substr(at + 1, end - at - 1)) + "</code>";
        at = end + 1;
        marked = true;
        continue;
      }
    }
    if (rest.starts_with("[")) {
      const auto text_end = line.find("](", at + 1);
      const auto url_end = text_end == std::string_view::npos ? text_end : line.find(')', text_end + 2);
      if (text_end != std::string_view::npos && url_end != std::string_view::npos && whole(at, url_end + 1)) {
        bool inner = false;
        out += "<a href=\"" + chevron::escaped(line.substr(text_end + 2, url_end - text_end - 2)) + "\">" +
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
      if (end == std::string_view::npos || !whole(at, end + one.mark.size()))
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
    out += chevron::escaped(line.substr(at, 1));
    ++at;
  }
  for (const std::size_t i : std::views::reverse(open_runs))
    out += runs[i].close;
  return out;
}

// The whole of what is typed, as HTML where it has Markdown; nothing where
// it has none.
[[nodiscard]] inline std::optional<std::string> markdown_html(std::string_view text, const std::vector<html_run>& runs = {}) {
  // The field's runs, by where they start (the longer first where two do):
  // opened in that order, they nest.
  std::vector<html_run> ordered = runs;
  std::ranges::sort(ordered, [](const html_run& a, const html_run& b) {
    return a.first != b.first ? a.first < b.first : a.last > b.last;
  });
  const runs_in in{text.data(), &ordered};
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
  // How many quotes are open: a line "> > " is a quote inside a quote.
  int quotes = 0;
  const auto close = [&] {
    for (; quotes > 0; --quotes)
      html += "</blockquote>";
    html += open == block::bullets ? "</ul>" : open == block::numbers ? "</ol>" : "";
    open = block::none;
  };
  // How deep in quotes a line is -- how many "> " it starts with, a bare ">"
  // the last -- and where its text starts after them.
  const auto quote_depth = [](std::string_view line, std::size_t& text) {
    int depth = 0;
    text = 0;
    while (true) {
      const std::string_view rest = line.substr(text);
      if (rest.starts_with("> ")) {
        text += 2;
      } else if (rest == ">") {
        text += 1;
      } else {
        return depth;
      }
      ++depth;
    }
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
        code += chevron::escaped(lines[j]);
        code += '\n';
      }
      const std::string_view language = line.substr(3);
      html += language.empty() ? "<pre><code>" : "<pre><code class=\"language-" + chevron::escaped(language) + "\">";
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
    if (std::size_t text = 0; const int depth = quote_depth(line, text)) {
      // A line of a quote: the quotes it is in opened, those it is out of
      // closed; a line on at the same depth as the one before, after a
      // break.
      const bool on = open == block::quote && quotes == depth;
      if (open != block::quote) {
        close();
        open = block::quote;
      }
      marked = true;
      if (on)
        html += "<br>";
      for (; quotes < depth; ++quotes)
        html += "<blockquote>";
      for (; quotes > depth; --quotes)
        html += "</blockquote>";
      html += inline_html(line.substr(text), marked, in);
      first_line = false;
      continue;
    }
    if (line.starts_with("- ") || line.starts_with("* ") || line.starts_with("+ ")) {
      begin(block::bullets, "<ul>");
      html += "<li>" + inline_html(line.substr(2), marked, in) + "</li>";
      continue;
    }
    if (const auto dot = line.find(". "); dot != std::string_view::npos && dot > 0 && dot < 4 &&
                                          std::ranges::all_of(line.substr(0, dot), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
      begin(block::numbers, "<ol>");
      html += "<li>" + inline_html(line.substr(dot + 2), marked, in) + "</li>";
      continue;
    }
    if (open != block::none) {
      close();
      first_line = true;
    }
    if (!first_line)
      html += "<br>";
    html += inline_html(line, marked, in);
    first_line = false;
  }
  close();
  if (!marked)
    return std::nullopt;
  return html;
}

}  // namespace mux::logic

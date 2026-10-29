// SPDX-License-Identifier: AGPL-3.0-only
// mux.logic.formatting -- What is typed, made the HTML Matrix carries beside
// the text (org.matrix.custom.html): the runs the field formatted -- bold, a
// link, a custom emoji's picture -- each with its tags. Text with none has no
// HTML: it is sent as it is. (Markdown read in it is the markdown plugin's,
// which writes these runs as it reads it.)
export module mux.logic.formatting;

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
  std::optional<std::string> replacement;
};
// The runs a line is written with: they, and where the whole text they
// count in starts -- the line a view into it.
struct runs_in {
  const char* origin = nullptr;
  const std::vector<html_run>* runs = nullptr;
};

// A line's runs, each written with its tags where it is -- all those open
// closed and the ones there opened again at each place one starts or ends,
// so that they nest -- and what replaces a run (a custom emoji's picture) in
// its place; the rest escaped. `marked` set where any run was.
[[nodiscard]] inline std::string runs_line_html(std::string_view line, bool& marked, runs_in in) {
  std::string out;
  std::size_t at = 0;
  const std::vector<html_run> none;
  const std::vector<html_run>& runs = in.runs != nullptr ? *in.runs : none;
  const std::size_t base = in.origin != nullptr ? static_cast<std::size_t>(line.data() - in.origin) : 0;
  std::vector<std::size_t> open_runs;
  while (at < line.size()) {
    const std::size_t here = base + at;
    std::vector<std::size_t> now = std::views::iota(std::size_t{0}, runs.size()) | std::views::filter([&](std::size_t i) {
                                     return !runs[i].replacement && runs[i].first <= here && here < runs[i].last;
                                   }) | std::ranges::to<std::vector<std::size_t>>();
    if (now != open_runs) {
      for (const std::size_t i : open_runs | std::views::reverse)
        out += runs[i].close;
      for (const std::size_t i : now)
        out += runs[i].open;
      open_runs = std::move(now);
      marked = true;
    }
    if (const auto found = std::ranges::find_if(runs, [&](const html_run& run) {
          return run.replacement && run.first == here && run.last <= base + line.size();
        }); found != runs.end()) {
      out += *found->replacement;
      at = found->last - base;
      marked = true;
      continue;
    }
    out += chevron::escaped(line.substr(at, 1));
    ++at;
  }
  for (const std::size_t i : open_runs | std::views::reverse)
    out += runs[i].close;
  return out;
}

// The whole of what is typed, as HTML where the field formatted any of it:
// its lines, broken where they are; nothing where none of it was.
[[nodiscard]] inline std::optional<std::string> runs_html(std::string_view text, const std::vector<html_run>& runs = {}) {
  // The runs, by where they start (the longer first where two do): opened
  // in that order, they nest.
  std::vector<html_run> ordered = runs;
  std::ranges::sort(ordered, [](const html_run& a, const html_run& b) { return a.first != b.first ? a.first < b.first : a.last > b.last; });
  const runs_in in{text.data(), &ordered};
  bool marked = false;
  const std::string html = text | std::views::split('\n') | std::views::transform([&](auto line) {
                             return runs_line_html(std::string_view(line.begin(), line.end()), marked, in);
                           }) | std::views::join_with(std::string_view("<br>")) | std::ranges::to<std::string>();
  if (!marked)
    return std::nullopt;
  return html;
}

}  // namespace mux::logic

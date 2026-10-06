// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:themes -- The themes: what each sets.
export module mux.ui:themes;

import std;
import splice.bytes;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;
import mux.config;
import :base;

// Telegram's chat pattern, in the binary (telegram_pattern.cc): tdesktop's
// art/background.tgv, a gzipped SVG, as the build fetched it.
extern "C++" {
extern const unsigned char mux_telegram_pattern[];
extern const decltype(sizeof 0) mux_telegram_pattern_size;
}

namespace mux::ui {
namespace pattern_reading {
// DEFLATE (RFC 1951), as puff.c reads it: what gzip keeps the pattern in.
// Read once, at the boundary, into the pattern's steps; a build without
// zlib has it this way.
struct bits {
  std::span<const unsigned char> in;
  std::size_t at = 0;
  std::uint32_t held = 0;
  int count = 0;
  // `n` bits, the first the lowest; -1 past the end.
  int take(int n) {
    while (count < n) {
      if (at >= in.size())
        return -1;
      held |= static_cast<std::uint32_t>(in[at++]) << count;
      count += 8;
    }
    const int value = static_cast<int>(held & ((1u << n) - 1u));
    held >>= n;
    count -= n;
    return value;
  }
};
struct huffman {
  std::array<short, 16> counts{};
  std::array<short, 320> symbols{};
};
inline void build(huffman& table, std::span<const short> lengths) {
  table.counts.fill(0);
  for (const short length : lengths)
    ++table.counts[static_cast<std::size_t>(length)];
  table.counts[0] = 0;
  std::array<short, 16> offsets{};
  for (std::size_t length = 1; length < 16; ++length)
    offsets[length] = static_cast<short>(offsets[length - 1] + table.counts[length - 1]);
  for (std::size_t symbol = 0; symbol < lengths.size(); ++symbol)
    if (lengths[symbol] != 0)
      table.symbols[static_cast<std::size_t>(offsets[static_cast<std::size_t>(lengths[symbol])]++)] =
          static_cast<short>(symbol);
}
inline int decode(bits& from, const huffman& table) {
  int code = 0, first = 0, index = 0;
  for (std::size_t length = 1; length < 16; ++length) {
    const int bit = from.take(1);
    if (bit < 0)
      return -1;
    code |= bit;
    const int count = table.counts[length];
    if (code - count < first)
      return table.symbols[static_cast<std::size_t>(index + (code - first))];
    index += count;
    first += count;
    first <<= 1;
    code <<= 1;
  }
  return -1;
}
inline constexpr std::array<short, 29> kLengthBase{3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
inline constexpr std::array<short, 29> kLengthExtra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
inline constexpr std::array<short, 30> kDistanceBase{1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                                     33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                                     1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
inline constexpr std::array<short, 30> kDistanceExtra{0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                      6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
// A block's codes, into what is written out.
inline bool codes(bits& from, std::string& out, const huffman& literals, const huffman& distances) {
  while (true) {
    const int symbol = decode(from, literals);
    if (symbol < 0)
      return false;
    if (symbol < 256) {
      out += static_cast<char>(symbol);
      continue;
    }
    if (symbol == 256)
      return true;
    const std::size_t which = static_cast<std::size_t>(symbol - 257);
    if (which >= kLengthBase.size())
      return false;
    const int extra = from.take(kLengthExtra[which]);
    const int distance_symbol = decode(from, distances);
    if (extra < 0 || distance_symbol < 0 || static_cast<std::size_t>(distance_symbol) >= kDistanceBase.size())
      return false;
    const int far_extra = from.take(kDistanceExtra[static_cast<std::size_t>(distance_symbol)]);
    if (far_extra < 0)
      return false;
    const std::size_t length = static_cast<std::size_t>(kLengthBase[which] + extra);
    const std::size_t distance = static_cast<std::size_t>(kDistanceBase[static_cast<std::size_t>(distance_symbol)] + far_extra);
    if (distance > out.size())
      return false;
    for (std::size_t i = 0; i < length; ++i)
      out += out[out.size() - distance];
  }
}
// A gzip file (RFC 1952) inflated; nothing where it is not one.
[[nodiscard]] inline std::optional<std::string> gunzip(std::span<const unsigned char> file) {
  if (file.size() < 18 || file[0] != 0x1f || file[1] != 0x8b || file[2] != 8)
    return std::nullopt;
  const unsigned flags = file[3];
  std::size_t at = 10;
  if (flags & 4u)
    at += 2 + (file[at] | (static_cast<std::size_t>(file[at + 1]) << 8));
  if (flags & 8u)
    while (at < file.size() && file[at++] != 0) {
    }
  if (flags & 16u)
    while (at < file.size() && file[at++] != 0) {
    }
  if (flags & 2u)
    at += 2;
  if (at >= file.size())
    return std::nullopt;
  bits from{file.subspan(at)};
  std::string out;
  while (true) {
    const int last = from.take(1);
    const int type = from.take(2);
    if (last < 0 || type < 0)
      return std::nullopt;
    if (type == 0) {
      // Stored: from the next byte, its length and that length's complement.
      from.held = 0;
      from.count = 0;
      if (from.at + 4 > from.in.size())
        return std::nullopt;
      const std::size_t length = from.in[from.at] | (static_cast<std::size_t>(from.in[from.at + 1]) << 8);
      from.at += 4;
      if (from.at + length > from.in.size())
        return std::nullopt;
      out.append(spl::bytes::text_of(from.in.subspan(from.at, length)));
      from.at += length;
    } else if (type == 1) {
      std::array<short, 288> lengths{};
      std::fill(lengths.begin(), lengths.begin() + 144, short{8});
      std::fill(lengths.begin() + 144, lengths.begin() + 256, short{9});
      std::fill(lengths.begin() + 256, lengths.begin() + 280, short{7});
      std::fill(lengths.begin() + 280, lengths.end(), short{8});
      std::array<short, 30> far{};
      far.fill(5);
      huffman literals, distances;
      build(literals, lengths);
      build(distances, far);
      if (!codes(from, out, literals, distances))
        return std::nullopt;
    } else if (type == 2) {
      const int literal_count = from.take(5) + 257;
      const int distance_count = from.take(5) + 1;
      const int length_count = from.take(4) + 4;
      if (literal_count > 286 || distance_count > 30)
        return std::nullopt;
      static constexpr std::array<std::size_t, 19> kOrder{16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
      std::array<short, 19> code_lengths{};
      for (int i = 0; i < length_count; ++i) {
        const int length = from.take(3);
        if (length < 0)
          return std::nullopt;
        code_lengths[kOrder[static_cast<std::size_t>(i)]] = static_cast<short>(length);
      }
      huffman lengths_code;
      build(lengths_code, code_lengths);
      std::array<short, 320> lengths{};
      const int total = literal_count + distance_count;
      for (int i = 0; i < total;) {
        const int symbol = decode(from, lengths_code);
        if (symbol < 0)
          return std::nullopt;
        if (symbol < 16) {
          lengths[static_cast<std::size_t>(i++)] = static_cast<short>(symbol);
          continue;
        }
        short repeated = 0;
        int times = 0;
        if (symbol == 16) {
          if (i == 0)
            return std::nullopt;
          repeated = lengths[static_cast<std::size_t>(i - 1)];
          times = 3 + from.take(2);
        } else if (symbol == 17) {
          times = 3 + from.take(3);
        } else {
          times = 11 + from.take(7);
        }
        if (i + times > total)
          return std::nullopt;
        for (; times > 0; --times)
          lengths[static_cast<std::size_t>(i++)] = repeated;
      }
      huffman literals, distances;
      build(literals, std::span<const short>(lengths.data(), static_cast<std::size_t>(literal_count)));
      build(distances, std::span<const short>(lengths.data() + literal_count, static_cast<std::size_t>(distance_count)));
      if (!codes(from, out, literals, distances))
        return std::nullopt;
    } else {
      return std::nullopt;
    }
    if (last == 1)
      return out;
  }
}

// An SVG's path data (its M, L, H, V, C, S and Z, each also relative) read
// into steps in absolute points; what else it may say is not in this one.
inline void read_path(std::string_view data, std::vector<widgets::PatternStep>& steps) {
  std::size_t at = 0;
  const auto skip = [&] {
    while (at < data.size() && (data[at] == ' ' || data[at] == ',' || data[at] == '\n' || data[at] == '\t' || data[at] == '\r'))
      ++at;
  };
  const auto number = [&](float& value) {
    skip();
    const std::size_t start = at;
    if (at < data.size() && (data[at] == '-' || data[at] == '+'))
      ++at;
    bool dot = false;
    while (at < data.size() && (std::isdigit(static_cast<unsigned char>(data[at])) || (data[at] == '.' && !dot))) {
      dot = dot || data[at] == '.';
      ++at;
    }
    if (at < data.size() && (data[at] == 'e' || data[at] == 'E')) {
      ++at;
      if (at < data.size() && (data[at] == '-' || data[at] == '+'))
        ++at;
      while (at < data.size() && std::isdigit(static_cast<unsigned char>(data[at])))
        ++at;
    }
    if (at == start)
      return false;
    return std::from_chars(data.data() + start, data.data() + at, value).ec == std::errc{};
  };
  float x = 0.0f, y = 0.0f, start_x = 0.0f, start_y = 0.0f, control_x = 0.0f, control_y = 0.0f;
  bool smooth = false;  // the last step a cubic: its second control reflected by an S
  char command = 0;
  while (true) {
    skip();
    if (at >= data.size())
      return;
    if (std::isalpha(static_cast<unsigned char>(data[at])))
      command = data[at++];
    const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
    const float ox = relative ? x : 0.0f, oy = relative ? y : 0.0f;
    switch (std::tolower(static_cast<unsigned char>(command))) {
      case 'm': {
        float px = 0.0f, py = 0.0f;
        if (!number(px) || !number(py))
          return;
        x = start_x = ox + px;
        y = start_y = oy + py;
        steps.push_back(widgets::pattern_step::move{x, y});
        command = relative ? 'l' : 'L';  // pairs after it are lines
        smooth = false;
        break;
      }
      case 'l': {
        float px = 0.0f, py = 0.0f;
        if (!number(px) || !number(py))
          return;
        x = ox + px;
        y = oy + py;
        steps.push_back(widgets::pattern_step::line{x, y});
        smooth = false;
        break;
      }
      case 'h': {
        float px = 0.0f;
        if (!number(px))
          return;
        x = ox + px;
        steps.push_back(widgets::pattern_step::line{x, y});
        smooth = false;
        break;
      }
      case 'v': {
        float py = 0.0f;
        if (!number(py))
          return;
        y = oy + py;
        steps.push_back(widgets::pattern_step::line{x, y});
        smooth = false;
        break;
      }
      case 'c': {
        float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f, px = 0.0f, py = 0.0f;
        if (!number(x1) || !number(y1) || !number(x2) || !number(y2) || !number(px) || !number(py))
          return;
        steps.push_back(widgets::pattern_step::cubic{ox + x1, oy + y1, ox + x2, oy + y2, ox + px, oy + py});
        control_x = ox + x2;
        control_y = oy + y2;
        x = ox + px;
        y = oy + py;
        smooth = true;
        break;
      }
      case 's': {
        float x2 = 0.0f, y2 = 0.0f, px = 0.0f, py = 0.0f;
        if (!number(x2) || !number(y2) || !number(px) || !number(py))
          return;
        const float x1 = smooth ? 2.0f * x - control_x : x, y1 = smooth ? 2.0f * y - control_y : y;
        steps.push_back(widgets::pattern_step::cubic{x1, y1, ox + x2, oy + y2, ox + px, oy + py});
        control_x = ox + x2;
        control_y = oy + y2;
        x = ox + px;
        y = oy + py;
        smooth = true;
        break;
      }
      case 'z':
        steps.push_back(widgets::pattern_step::close{});
        x = start_x;
        y = start_y;
        smooth = false;
        break;
      default:
        return;  // one it does not read: the rest of this path left out
    }
  }
}
// The SVG's size, from its viewBox, and every path's data in it.
[[nodiscard]] inline widgets::Pattern pattern_of(std::string_view svg) {
  widgets::Pattern out;
  if (const auto box = svg.find("viewBox=\""); box != std::string_view::npos) {
    std::array<float, 4> numbers{};
    std::size_t at = box + 9;
    for (float& one : numbers) {
      while (at < svg.size() && (svg[at] == ' ' || svg[at] == ','))
        ++at;
      const auto [end, failed] = std::from_chars(svg.data() + at, svg.data() + svg.size(), one);
      if (failed != std::errc{})
        break;
      at = static_cast<std::size_t>(end - svg.data());
    }
    out.width = numbers[2];
    out.height = numbers[3];
  }
  for (std::size_t at = svg.find(" d=\""); at != std::string_view::npos; at = svg.find(" d=\"", at)) {
    at += 4;
    const std::size_t end = svg.find('"', at);
    if (end == std::string_view::npos)
      break;
    read_path(svg.substr(at, end - at), out.steps);
    at = end;
  }
  return out;
}
}  // namespace pattern_reading
}  // namespace mux::ui

export namespace mux::ui {
// Telegram's chat pattern, read once from what is in the binary: drawn over
// the theme's gradient behind the messages, in the theme's pattern colour.
[[nodiscard]] inline std::shared_ptr<const widgets::Pattern> telegram_pattern() {
  static const std::shared_ptr<const widgets::Pattern> read = [] {
    const auto svg = pattern_reading::gunzip(std::span<const unsigned char>(mux_telegram_pattern, mux_telegram_pattern_size));
    return svg ? std::make_shared<const widgets::Pattern>(pattern_reading::pattern_of(*svg)) : nullptr;
  }();
  return read;
}
}  // namespace mux::ui

export namespace mux::ui {

// ---- the conversations ------------------------------------------------------

// A control that does nothing when pressed: the page already up.
struct nothing {
  void operator()() const {}
};

// A control whose action is still to come: it says so.
template <class Actions>
struct not_yet {
  Actions* actions = nullptr;
  std::string_view what;
  void operator()() const { actions->not_implemented(std::string(what)); }
};

// Between the sections of a panel: just darker than the panel.

// The colours of a theme, "dark" or "light", put in place: mux.ui's and
// skiff-widgets'. What is made takes its colours then: the window is made
// again after it (window::rebuild).
// The themes, as Telegram Desktop's: their colours are its palettes' own,
// key for key (tools: tdesktop's Resources and lib_ui's colors.palette).
// Classic: tdesktop's base palette.
inline palette palette_of(config::theme::classic) {
  palette out;
  out.background = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  out.sidebar = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  out.chosen = skia::colorSetARGB(255, 241, 241, 241);  // #f1f1f1
  out.text = skia::colorSetARGB(255, 0, 0, 0);  // #000000
  out.dim = skia::colorSetARGB(255, 153, 153, 153);  // #999999
  out.accent = skia::colorSetARGB(255, 64, 167, 227);  // #40a7e3
  out.error = skia::colorSetARGB(255, 209, 78, 78);  // #d14e4e
  out.selected = skia::colorSetARGB(255, 65, 159, 217);  // #419fd9
  out.selected_text = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  out.band = skia::colorSetARGB(255, 231, 231, 231);  // #e7e7e7
  out.section = skia::colorSetARGB(255, 241, 241, 241);  // #f1f1f1
  out.tile = skia::colorSetARGB(255, 241, 241, 241);  // #f1f1f1
  out.bubble = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  out.out_bubble = skia::colorSetARGB(255, 239, 253, 222);  // #effdde
  out.sent_time = skia::colorSetARGB(255, 109, 181, 102);  // #6db566
  out.chat = skia::colorSetARGB(255, 136, 184, 132);      // #88b884, Telegram's default wallpaper
  out.chat_top = skia::colorSetARGB(255, 213, 216, 141);  // #d5d88d, down to it
  out.pattern = skia::colorSetARGB(36, 0, 0, 0);
  out.on_accent = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  return out;
}
inline widgets::Theme widget_theme_of(config::theme::classic) {
  widgets::Theme widget{};
  widget.fSurface = skia::colorSetARGB(255, 241, 241, 241);
  widget.fSurfaceHover = skia::colorSetARGB(255, 241, 241, 241);
  widget.fSurfaceActive = skia::colorSetARGB(255, 229, 229, 229);
  widget.fText = skia::colorSetARGB(255, 0, 0, 0);
  widget.fLabel = skia::colorSetARGB(255, 0, 0, 0);
  widget.fTextDim = skia::colorSetARGB(255, 153, 153, 153);
  widget.fTextFaint = skia::colorSetARGB(255, 153, 153, 153);
  widget.fAccent = skia::colorSetARGB(255, 64, 167, 227);
  widget.fOnAccent = skia::colorSetARGB(255, 255, 255, 255);
  return widget;
}
// Day: tdesktop's day-blue.tdesktop-theme.
inline palette palette_of(config::theme::day) {
  palette out;
  out.background = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  out.sidebar = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  out.chosen = skia::colorSetARGB(255, 241, 241, 241);  // #f1f1f1
  out.text = skia::colorSetARGB(255, 0, 0, 0);  // #000000
  out.dim = skia::colorSetARGB(255, 153, 153, 153);  // #999999
  out.accent = skia::colorSetARGB(255, 64, 167, 227);  // #40a7e3
  out.error = skia::colorSetARGB(255, 209, 78, 78);  // #d14e4e
  out.selected = skia::colorSetARGB(255, 65, 159, 217);  // #419fd9
  out.selected_text = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  out.band = skia::colorSetARGB(255, 231, 231, 231);  // #e7e7e7
  out.section = skia::colorSetARGB(255, 241, 241, 241);  // #f1f1f1
  out.tile = skia::colorSetARGB(255, 241, 241, 241);  // #f1f1f1
  out.bubble = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  out.out_bubble = skia::colorSetARGB(255, 222, 241, 253);  // #def1fd
  out.sent_time = skia::colorSetARGB(255, 134, 168, 194);  // #86a8c2
  out.chat = skia::colorSetARGB(255, 92, 159, 214);       // #5c9fd6
  out.chat_top = skia::colorSetARGB(255, 166, 211, 240);  // #a6d3f0, down to it
  out.pattern = skia::colorSetARGB(36, 0, 0, 0);
  out.on_accent = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  return out;
}
inline widgets::Theme widget_theme_of(config::theme::day) {
  widgets::Theme widget{};
  widget.fSurface = skia::colorSetARGB(255, 241, 241, 241);
  widget.fSurfaceHover = skia::colorSetARGB(255, 241, 241, 241);
  widget.fSurfaceActive = skia::colorSetARGB(255, 229, 229, 229);
  widget.fText = skia::colorSetARGB(255, 0, 0, 0);
  widget.fLabel = skia::colorSetARGB(255, 0, 0, 0);
  widget.fTextDim = skia::colorSetARGB(255, 153, 153, 153);
  widget.fTextFaint = skia::colorSetARGB(255, 153, 153, 153);
  widget.fAccent = skia::colorSetARGB(255, 64, 167, 227);
  widget.fOnAccent = skia::colorSetARGB(255, 255, 255, 255);
  return widget;
}
// Tinted: tdesktop's night.tdesktop-theme.
inline palette palette_of(config::theme::tinted) {
  palette out;
  out.background = skia::colorSetARGB(255, 23, 33, 43);  // #17212b
  out.sidebar = skia::colorSetARGB(255, 23, 33, 43);  // #17212b
  out.chosen = skia::colorSetARGB(255, 32, 43, 54);  // #202b36
  out.text = skia::colorSetARGB(255, 245, 245, 245);  // #f5f5f5
  out.dim = skia::colorSetARGB(255, 112, 132, 153);  // #708499
  out.accent = skia::colorSetARGB(255, 82, 136, 193);  // #5288c1
  out.error = skia::colorSetARGB(255, 236, 57, 66);  // #ec3942
  out.selected = skia::colorSetARGB(255, 43, 82, 120);  // #2b5278
  out.selected_text = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  out.band = skia::colorSetARGB(255, 36, 48, 61);  // #24303d
  out.section = skia::colorSetARGB(255, 35, 46, 60);  // #232e3c
  out.tile = skia::colorSetARGB(255, 36, 47, 61);  // #242f3d
  out.bubble = skia::colorSetARGB(255, 24, 37, 51);  // #182533
  out.out_bubble = skia::colorSetARGB(255, 43, 82, 120);  // #2b5278
  out.sent_time = skia::colorSetARGB(255, 125, 168, 211);  // #7da8d3
  out.chat = skia::colorSetARGB(255, 14, 22, 33);      // #0e1621
  out.chat_top = skia::colorSetARGB(255, 25, 44, 66);  // #192c42, down to it
  out.pattern = skia::colorSetARGB(20, 255, 255, 255);
  out.on_accent = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  return out;
}
inline widgets::Theme widget_theme_of(config::theme::tinted) {
  widgets::Theme widget{};
  widget.fSurface = skia::colorSetARGB(255, 36, 47, 61);
  widget.fSurfaceHover = skia::colorSetARGB(255, 35, 46, 60);
  widget.fSurfaceActive = skia::colorSetARGB(255, 36, 48, 61);
  widget.fText = skia::colorSetARGB(255, 245, 245, 245);
  widget.fLabel = skia::colorSetARGB(255, 245, 245, 245);
  widget.fTextDim = skia::colorSetARGB(255, 112, 132, 153);
  widget.fTextFaint = skia::colorSetARGB(255, 112, 132, 153);
  widget.fAccent = skia::colorSetARGB(255, 82, 136, 193);
  widget.fOnAccent = skia::colorSetARGB(255, 255, 255, 255);
  return widget;
}
// Night: tdesktop's night-green.tdesktop-theme.
inline palette palette_of(config::theme::night) {
  palette out;
  out.background = skia::colorSetARGB(255, 40, 46, 51);  // #282e33
  out.sidebar = skia::colorSetARGB(255, 40, 46, 51);  // #282e33
  out.chosen = skia::colorSetARGB(255, 53, 60, 67);  // #353c43
  out.text = skia::colorSetARGB(255, 245, 245, 245);  // #f5f5f5
  out.dim = skia::colorSetARGB(255, 130, 134, 138);  // #82868a
  out.accent = skia::colorSetARGB(255, 63, 193, 176);  // #3fc1b0
  out.error = skia::colorSetARGB(255, 245, 116, 116);  // #f57474
  out.selected = skia::colorSetARGB(255, 0, 150, 135);  // #009687
  out.selected_text = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  out.band = skia::colorSetARGB(255, 63, 72, 80);  // #3f4850
  out.section = skia::colorSetARGB(255, 49, 59, 67);  // #313b43
  out.tile = skia::colorSetARGB(255, 61, 68, 75);  // #3d444b
  out.bubble = skia::colorSetARGB(255, 51, 57, 63);  // #33393f
  out.out_bubble = skia::colorSetARGB(255, 42, 47, 51);  // #2a2f33
  out.sent_time = skia::colorSetARGB(255, 115, 127, 135);  // #737f87
  out.chat = skia::colorSetARGB(255, 24, 25, 29);      // #18191d
  out.chat_top = skia::colorSetARGB(255, 32, 46, 40);  // #202e28, down to it
  out.pattern = skia::colorSetARGB(20, 255, 255, 255);
  out.on_accent = skia::colorSetARGB(255, 255, 255, 255);  // #ffffff
  return out;
}
inline widgets::Theme widget_theme_of(config::theme::night) {
  widgets::Theme widget{};
  widget.fSurface = skia::colorSetARGB(255, 61, 68, 75);
  widget.fSurfaceHover = skia::colorSetARGB(255, 49, 59, 67);
  widget.fSurfaceActive = skia::colorSetARGB(255, 63, 72, 80);
  widget.fText = skia::colorSetARGB(255, 245, 245, 245);
  widget.fLabel = skia::colorSetARGB(255, 245, 245, 245);
  widget.fTextDim = skia::colorSetARGB(255, 130, 134, 138);
  widget.fTextFaint = skia::colorSetARGB(255, 130, 134, 138);
  widget.fAccent = skia::colorSetARGB(255, 63, 193, 176);
  widget.fOnAccent = skia::colorSetARGB(255, 255, 255, 255);
  return widget;
}
// An accent's colour in a theme: Telegram's circles, a shade of their own
// in each (window_themes_embedded.cpp); the theme's own where none is chosen.
[[nodiscard]] inline skia::SkColor colour_of(const config::accent_t& one, const config::theme_t& in) {
  const std::size_t theme = in.index();
  static constexpr std::array<std::array<skia::SkColor, 9>, 4> shades{{
      {skia::colorSetARGB(255, 64, 167, 227), skia::colorSetARGB(255, 69, 188, 231), skia::colorSetARGB(255, 82, 180, 64), skia::colorSetARGB(255, 212, 108, 153), skia::colorSetARGB(255, 223, 138, 73), skia::colorSetARGB(255, 153, 120, 200), skia::colorSetARGB(255, 197, 82, 69), skia::colorSetARGB(255, 104, 123, 152), skia::colorSetARGB(255, 222, 169, 34)},  // classic
      {skia::colorSetARGB(255, 64, 167, 227), skia::colorSetARGB(255, 69, 188, 231), skia::colorSetARGB(255, 82, 180, 64), skia::colorSetARGB(255, 212, 108, 153), skia::colorSetARGB(255, 223, 138, 73), skia::colorSetARGB(255, 153, 120, 200), skia::colorSetARGB(255, 197, 82, 69), skia::colorSetARGB(255, 104, 123, 152), skia::colorSetARGB(255, 222, 169, 34)},  // day
      {skia::colorSetARGB(255, 82, 136, 193), skia::colorSetARGB(255, 88, 191, 232), skia::colorSetARGB(255, 70, 111, 66), skia::colorSetARGB(255, 170, 96, 132), skia::colorSetARGB(255, 164, 109, 60), skia::colorSetARGB(255, 145, 123, 189), skia::colorSetARGB(255, 171, 81, 73), skia::colorSetARGB(255, 105, 123, 151), skia::colorSetARGB(255, 155, 131, 75)},  // tinted
      {skia::colorSetARGB(255, 63, 193, 176), skia::colorSetARGB(255, 96, 168, 231), skia::colorSetARGB(255, 78, 156, 87), skia::colorSetARGB(255, 202, 120, 150), skia::colorSetARGB(255, 204, 146, 92), skia::colorSetARGB(255, 165, 142, 210), skia::colorSetARGB(255, 210, 117, 112), skia::colorSetARGB(255, 123, 135, 153), skia::colorSetARGB(255, 203, 172, 103)},  // night
  }};
  return shades[theme][one.index()];
}
// A theme, and the accent over it: its palette. In a see-through window
// (`opacity` under 100), its panels at that opacity, and under them nothing
// -- the desktop, or the background behind all of it: their colours taken
// once here, not blended at each frame.
[[nodiscard]] inline palette palette_of(const config::theme_t& chosen, const config::accent_t& accent, int opacity) {
  palette out = spl::visit([](auto one) { return palette_of(one); }, chosen);
  out.accent = colour_of(accent, chosen);
  out.widgets = spl::visit([](auto one) { return widget_theme_of(one); }, chosen);
  out.widgets.fAccent = out.accent;
  if (opacity < 100) {
    out.background = at_opacity(out.background, 0);
    for (skia::SkColor* each : {&out.sidebar, &out.chosen, &out.chat, &out.chat_top})
      *each = at_opacity(*each, opacity);
  }
  return out;
}
// skiff's scroll bars, as the theme has them.
inline void use_scroll_bars(const config::theme_t& chosen) {
  const bool light = spl::visit([](auto one) { return one.light; }, chosen);
  nodes::scrollBarColours() = light ? nodes::ScrollBarColours{skia::colorSetARGB(0x53, 0, 0, 0), skia::colorSetARGB(0x7a, 0, 0, 0)}
                                    : nodes::ScrollBarColours{skia::colorSetARGB(0x53, 255, 255, 255),
                                                              skia::colorSetARGB(0x7a, 255, 255, 255)};
}

// The panels' look put in place, for skiff to paint them in: only over the
// background behind the whole window. Whether it changed -- the window to
// be repainted.
inline bool show_panels(mux_paint& paint, const config::bubble_look& look, const window_look_t& window, const palette& colours) {
  const bool kinded = window.behind && spl::visit(spl::overloaded{[](config::bubbles::solid) { return false; },
                                                                              [](const auto&) { return true; }},
                                                           look.kind);
  panel_look_t next{
      .active = kinded,
      .opacity = static_cast<float>(look.opacity) / 100.0f,
      .frosted = spl::visit(spl::overloaded{[](config::bubbles::frosted) { return true; }, [](const auto&) { return false; }}, look.kind),
      .blur = blur_of(look, window),
      .edge = spl::visit(spl::overloaded{[](config::bubbles::glass) { return true; }, [](const auto&) { return false; }}, look.kind),
      .panels = {colours.sidebar},
      // Tinted at the opacity, never left out as a panel fill again: what is
      // chosen or hovered, a tab lit, a menu.
      .tints = {colours.chosen, colours.tile, colours.popup()}};
  if (!kinded)
    next = {};
  if (next == paint.panel)
    return false;
  // Only the opacity another: eased to it, from where it is now.
  panel_look_t& now = paint.panel;
  const bool same_but_opacity = now.active && next.active && now.frosted == next.frosted && now.blur == next.blur &&
                                now.edge == next.edge &&
                                now.panels == next.panels && now.tints == next.tints;
  if (same_but_opacity) {
    auto& ease = paint.ease;
    if (ease.t.moving() && ease.to == next.opacity)
      return false;
    ease.from = now.opacity;
    ease.to = next.opacity;
    ease.t.jump(0.0f);
    ease.t.setTarget(1.0f);
    return true;
  }
  paint.ease.t.jump(1.0f);
  now = std::move(next);
  return true;
}

// A protocol's part's tone, in a palette's colours.
[[nodiscard]] inline skia::SkColor tone_colour(const palette& colours, const proto::part::tone_t& tone) {
  return spl::visit(spl::overloaded{[&](proto::part::tone::plain) { return colours.dim; },
                                          [&](proto::part::tone::accent) { return colours.accent; },
                                          [&](proto::part::tone::danger) { return colours.error; }},
                       tone);
}

}  // namespace mux::ui

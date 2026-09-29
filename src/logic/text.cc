// SPDX-License-Identifier: AGPL-3.0-only
// mux.logic.text: text as the program reads it, with nothing else --
// folded for finding in any case, and an address's account.
export module mux.logic.text;

import std;
import mux.core;
import mux.config;

export namespace mux::logic {

// A text with its case folded, for finding words in any case: Latin, Greek
// and Cyrillic capitals made small; the rest as it is.
[[nodiscard]] inline std::string folded(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  const auto put = [&](char32_t c) {
    if (c < 0x80) {
      out += static_cast<char>(c);
    } else if (c < 0x800) {
      out += static_cast<char>(0xC0 | (c >> 6));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      out += static_cast<char>(0xE0 | (c >> 12));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (c >> 18));
      out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    }
  };
  std::size_t at = 0;
  while (at < text.size()) {
    const auto lead = static_cast<unsigned char>(text[at]);
    const std::size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
    if (at + length > text.size() || (length > 1 && lead < 0xC0)) {
      out += text[at++];
      continue;
    }
    char32_t c = length == 1 ? lead : lead & (0xFF >> (length + 1));
    for (std::size_t i = 1; i < length; ++i)
      c = (c << 6) | (static_cast<unsigned char>(text[at + i]) & 0x3F);
    at += length;
    if (c >= U'A' && c <= U'Z')
      c += 0x20;
    else if (c >= 0x0410 && c <= 0x042F)  // А..Я
      c += 0x20;
    else if (c >= 0x0400 && c <= 0x040F)  // Ѐ..Џ, Ё among them
      c += 0x50;
    else if (c >= 0x0391 && c <= 0x03A9 && c != 0x03A2)  // Α..Ω
      c += 0x20;
    else if (c >= 0x00C0 && c <= 0x00DE && c != 0x00D7)  // À..Þ
      c += 0x20;
    put(c);
  }
  return out;
}

// The protocol an address speaks: a Matrix user ID starts with '@', and a
// JID cannot; and the account it names.
[[nodiscard]] inline protocol_t protocol_of(std::string_view address) {
  return config::is_matrix(address) ? protocol_t{protocol::matrix{}} : protocol_t{protocol::xmpp{}};
}
[[nodiscard]] inline account_id account_of(std::string_view address) {
  return account_id{protocol_of(address), std::string(address)};
}

}  // namespace mux::logic

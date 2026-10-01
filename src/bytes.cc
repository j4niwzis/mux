// SPDX-License-Identifier: AGPL-3.0-only
// mux.bytes -- Text as bytes, and bytes as text: copied a byte at a time,
// each converted by its value (std::bit_cast of one char or one byte). One
// type's storage is never looked at as another's -- no reinterpret_cast, no
// std::as_bytes.
export module mux.bytes;

import std;

export namespace mux::bytes {

// A text's bytes.
[[nodiscard]] constexpr std::vector<std::uint8_t> of(std::string_view text) {
  return text | std::views::transform([](char c) { return std::bit_cast<std::uint8_t>(c); }) |
         std::ranges::to<std::vector<std::uint8_t>>();
}

// Bytes as a text.
[[nodiscard]] constexpr std::string text_of(std::span<const std::uint8_t> bytes) {
  return bytes | std::views::transform([](std::uint8_t b) { return std::bit_cast<char>(b); }) |
         std::ranges::to<std::string>();
}

// A C string of bytes -- as OpenGL gives its names -- up to its zero, as a
// text; empty for none.
[[nodiscard]] inline std::string text_of_terminated(const std::uint8_t* bytes) {
  if (bytes == nullptr)
    return {};
  return std::ranges::subrange(bytes, std::unreachable_sentinel) |
         std::views::take_while([](std::uint8_t b) { return b != 0; }) |
         std::views::transform([](std::uint8_t b) { return std::bit_cast<char>(b); }) | std::ranges::to<std::string>();
}

}  // namespace mux::bytes

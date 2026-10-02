// SPDX-License-Identifier: AGPL-3.0-only
// mux.bytes -- Text and bytes, each seen as the other lazily: a view that
// turns each char into its byte by value (std::bit_cast of one char), or the
// other way. One type's storage is never looked at as another's -- no
// reinterpret_cast, no std::as_bytes -- and nothing is copied until the end
// that needs it does (a C API that keeps a pointer, a string a change carries).
export module mux.bytes;

import std;

export namespace mux::bytes {

inline constexpr auto to_byte = [](char c) { return std::bit_cast<std::uint8_t>(c); };
inline constexpr auto to_char = [](std::uint8_t b) { return std::bit_cast<char>(b); };

// Any characters -- a string, a view, knot's lazy JSON -- as bytes.
template <std::ranges::viewable_range Chars>
  requires std::same_as<std::ranges::range_value_t<Chars>, char>
[[nodiscard]] constexpr auto of(Chars&& chars) {
  return std::views::all(std::forward<Chars>(chars)) | std::views::transform(to_byte);
}
[[nodiscard]] constexpr auto of(std::string_view text) { return text | std::views::transform(to_byte); }

// Any bytes as characters.
template <std::ranges::viewable_range Bytes>
  requires std::same_as<std::ranges::range_value_t<Bytes>, std::uint8_t>
[[nodiscard]] constexpr auto chars(Bytes&& bytes) {
  return std::views::all(std::forward<Bytes>(bytes)) | std::views::transform(to_char);
}

// Made whole, where something keeps them: a string, a byte buffer.
template <std::ranges::input_range Bytes>
[[nodiscard]] constexpr std::string text_of(Bytes&& bytes) {
  return chars(std::forward<Bytes>(bytes)) | std::ranges::to<std::string>();
}
template <std::ranges::input_range Bytes>
[[nodiscard]] constexpr std::vector<std::uint8_t> buffer_of(Bytes&& bytes) {
  return std::forward<Bytes>(bytes) | std::ranges::to<std::vector<std::uint8_t>>();
}

// Bytes handed to something that takes them a piece at a time -- a hash, a
// cipher, a MAC -- through a buffer on the stack: never made whole.
template <std::ranges::input_range Bytes, class Take>
constexpr void in_pieces(Bytes&& bytes, Take&& take) {
  std::array<std::uint8_t, 4096> buffer{};
  std::size_t filled = 0;
  for (const std::uint8_t byte : bytes) {
    buffer[filled++] = byte;
    if (filled == buffer.size()) {
      take(std::span<const std::uint8_t>(buffer.data(), filled));
      filled = 0;
    }
  }
  if (filled > 0)
    take(std::span<const std::uint8_t>(buffer.data(), filled));
}

// Exactly N bytes, as a key or a nonce is: none where there are not.
template <std::size_t N, std::ranges::input_range Bytes>
[[nodiscard]] constexpr std::optional<std::array<std::uint8_t, N>> exactly(Bytes&& bytes) {
  std::array<std::uint8_t, N> out{};
  std::size_t at = 0;
  for (const std::uint8_t byte : bytes) {
    if (at == N)
      return std::nullopt;
    out[at++] = byte;
  }
  if (at != N)
    return std::nullopt;
  return out;
}

// Base64 of any bytes, lazily: three bytes at a time, four characters each,
// nothing allocated -- fewer at the end, unpadded, as Matrix writes it, or
// filled with '=', padded, as OpenSSL reads it.
namespace detail {
inline constexpr std::string_view kBase64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
template <bool Padded>
inline constexpr auto base64_group = [](auto&& group) {
  std::array<std::uint8_t, 3> in{};
  std::size_t taken = 0;
  for (const std::uint8_t byte : group)
    in[taken++] = byte;
  const std::uint32_t joined = (std::uint32_t{in[0]} << 16) | (std::uint32_t{in[1]} << 8) | std::uint32_t{in[2]};
  const std::array<char, 4> out{kBase64[(joined >> 18) & 63], kBase64[(joined >> 12) & 63],
                                taken > 1 ? kBase64[(joined >> 6) & 63] : '=', taken > 2 ? kBase64[joined & 63] : '='};
  return std::views::take(out, static_cast<std::ptrdiff_t>(Padded ? 4 : taken + 1));
};
}  // namespace detail
template <std::ranges::viewable_range Bytes>
[[nodiscard]] constexpr auto base64(Bytes&& bytes) {
  return std::views::all(std::forward<Bytes>(bytes)) | std::views::chunk(3) | std::views::transform(detail::base64_group<false>) |
         std::views::join;
}
template <std::ranges::viewable_range Bytes>
[[nodiscard]] constexpr auto base64_padded(Bytes&& bytes) {
  return std::views::all(std::forward<Bytes>(bytes)) | std::views::chunk(3) | std::views::transform(detail::base64_group<true>) |
         std::views::join;
}
// Made whole, where a field keeps it.
template <std::ranges::viewable_range Bytes>
[[nodiscard]] constexpr std::string base64_text(Bytes&& bytes) {
  return base64(std::forward<Bytes>(bytes)) | std::ranges::to<std::string>();
}
template <std::ranges::viewable_range Bytes>
[[nodiscard]] constexpr std::string base64_padded_text(Bytes&& bytes) {
  return base64_padded(std::forward<Bytes>(bytes)) | std::ranges::to<std::string>();
}

// A C string of bytes -- as OpenGL gives its names -- up to its zero, as a
// text; empty for none.
[[nodiscard]] inline std::string text_of_terminated(const std::uint8_t* bytes) {
  if (bytes == nullptr)
    return {};
  return std::ranges::subrange(bytes, std::unreachable_sentinel) |
         std::views::take_while([](std::uint8_t b) { return b != 0; }) | std::views::transform(to_char) |
         std::ranges::to<std::string>();
}

}  // namespace mux::bytes

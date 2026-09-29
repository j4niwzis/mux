// SPDX-License-Identifier: AGPL-3.0-only
// mux.logic.blurhash: a picture's blurhash (https://blurha.sh), as a Matrix
// sender attaches one to a picture (xyz.amorgan.blurhash), decoded into the
// pixels of a small blurred picture of it: what is shown until the picture
// itself comes.
export module mux.logic.blurhash;

import std;

export namespace mux::logic {

namespace detail {
// Base 83, as blurhash writes its numbers.
inline std::optional<std::uint32_t> base83(std::string_view digits) {
  static constexpr std::string_view alphabet =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#$%*+,-.:;=?@[]^_{|}~";
  std::uint32_t value = 0;
  for (const char c : digits) {
    const auto at = alphabet.find(c);
    if (at == std::string_view::npos)
      return std::nullopt;
    value = value * 83 + static_cast<std::uint32_t>(at);
  }
  return value;
}
inline float srgb_to_linear(std::uint32_t value) {
  const float v = static_cast<float>(value) / 255.0f;
  return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}
inline std::uint8_t linear_to_srgb(float value) {
  const float v = std::clamp(value, 0.0f, 1.0f);
  const float s = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
  return static_cast<std::uint8_t>(std::lround(s * 255.0f));
}
inline float signed_pow(float value, float exponent) {
  return std::copysign(std::pow(std::abs(value), exponent), value);
}
}  // namespace detail

// The pixels -- red, green, blue, alpha, four bytes each, row after row --
// of a picture `width` by `height` from its blurhash; none for a hash that
// is not one.
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>> blurhash_pixels(std::string_view hash, int width, int height) {
  if (hash.size() < 6 || width <= 0 || height <= 0)
    return std::nullopt;
  const auto size = detail::base83(hash.substr(0, 1));
  if (!size)
    return std::nullopt;
  const int nx = static_cast<int>(*size % 9) + 1, ny = static_cast<int>(*size / 9) + 1;
  if (hash.size() != static_cast<std::size_t>(4 + 2 * nx * ny))
    return std::nullopt;
  const auto quantised = detail::base83(hash.substr(1, 1));
  if (!quantised)
    return std::nullopt;
  const float maximum = static_cast<float>(*quantised + 1) / 166.0f;
  std::vector<std::array<float, 3>> colours(static_cast<std::size_t>(nx * ny));
  const auto dc = detail::base83(hash.substr(2, 4));
  if (!dc)
    return std::nullopt;
  colours[0] = {detail::srgb_to_linear(*dc >> 16), detail::srgb_to_linear((*dc >> 8) & 255),
                detail::srgb_to_linear(*dc & 255)};
  for (int i = 1; i < nx * ny; ++i) {
    const auto ac = detail::base83(hash.substr(4 + static_cast<std::size_t>(i) * 2, 2));
    if (!ac)
      return std::nullopt;
    const auto channel = [&](std::uint32_t q) {
      return detail::signed_pow((static_cast<float>(q) - 9.0f) / 9.0f, 2.0f) * maximum;
    };
    colours[static_cast<std::size_t>(i)] = {channel(*ac / (19 * 19)), channel((*ac / 19) % 19), channel(*ac % 19)};
  }
  std::vector<std::uint8_t> out(static_cast<std::size_t>(width * height * 4));
  constexpr float pi = std::numbers::pi_v<float>;
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      std::array<float, 3> pixel{};
      for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
          const float basis = std::cos(pi * static_cast<float>(x * i) / static_cast<float>(width)) *
                              std::cos(pi * static_cast<float>(y * j) / static_cast<float>(height));
          const auto& c = colours[static_cast<std::size_t>(i + j * nx)];
          for (int k = 0; k < 3; ++k)
            pixel[static_cast<std::size_t>(k)] += c[static_cast<std::size_t>(k)] * basis;
        }
      const std::size_t at = static_cast<std::size_t>((y * width + x) * 4);
      out[at] = detail::linear_to_srgb(pixel[0]);
      out[at + 1] = detail::linear_to_srgb(pixel[1]);
      out[at + 2] = detail::linear_to_srgb(pixel[2]);
      out[at + 3] = 255;
    }
  return out;
}

}  // namespace mux::logic

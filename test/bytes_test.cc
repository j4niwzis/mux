// SPDX-License-Identifier: GPL-3.0-only
// Instantiate the exported helpers here, across the module boundary.
import std;
import splice.bytes;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

constexpr bool round_trip() {
  const std::array<std::uint8_t, 5> original{0, 65, 127, 128, 255};
  const auto text = spl::bytes::text_of(original);
  const auto bytes = spl::bytes::buffer_of(spl::bytes::of(std::string_view(text)));
  return std::ranges::equal(original, bytes);
}
static_assert(round_trip());

TEST(bytes, materializes_temporary_and_borrowed_views) {
  EXPECT_TRUE(round_trip());
  auto borrowed = spl::bytes::of(std::string_view("hello"));
  const auto bytes = spl::bytes::buffer_of(borrowed);
  EXPECT_EQ(spl::bytes::text_of(bytes), "hello");
  EXPECT_TRUE(spl::bytes::buffer_of(spl::bytes::of(std::string_view{})).empty());
  EXPECT_TRUE(spl::bytes::text_of(std::span<const std::uint8_t>{}).empty());
}

TEST(bytes, materializes_a_single_pass_range) {
  std::istringstream input("ABC");
  auto characters = std::ranges::istream_view<char>(input);
  const auto bytes = spl::bytes::buffer_of(spl::bytes::of(characters));
  EXPECT_EQ(bytes, (std::vector<std::uint8_t>{65, 66, 67}));
  EXPECT_TRUE(input.eof());
}

TEST(bytes, text_conversions_keep_their_contents) {
  EXPECT_EQ(spl::bytes::lower_text("AbC-19"), "abc-19");
  EXPECT_EQ(spl::bytes::key_text("AbC !19"), "abc__19");
  EXPECT_EQ(std::ranges::to<std::string>(spl::bytes::every(std::string_view("abcdef"), 2, '-')),
            "ab-cd-ef");
  const std::array<std::uint8_t, 1> one{102};
  EXPECT_EQ(spl::bytes::base64_text(one), "Zg");
  EXPECT_EQ(spl::bytes::base64_padded_text(one), "Zg==");
  const std::array<std::uint8_t, 3> terminated{65, 255, 0};
  const auto text = spl::bytes::text_of_terminated(terminated.data());
  EXPECT_EQ(spl::bytes::buffer_of(spl::bytes::of(text)),
            (std::vector<std::uint8_t>{65, 255}));
  EXPECT_TRUE(spl::bytes::text_of_terminated(nullptr).empty());
}

}  // namespace

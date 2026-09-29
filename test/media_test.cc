// SPDX-License-Identifier: AGPL-3.0-only
// mux.media: metadata cut out of pictures' files, their pictures left alone.
import std;
import mux.media;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

std::string segment(std::uint8_t marker, std::string_view payload) {
  const std::size_t length = payload.size() + 2;
  std::string out{'\xFF', static_cast<char>(marker), static_cast<char>(length >> 8), static_cast<char>(length & 0xFF)};
  out.append(payload);
  return out;
}
std::string chunk(std::string_view type, std::string_view data) {
  std::string out(4, '\0');
  const auto n = static_cast<std::uint32_t>(data.size());
  out[0] = static_cast<char>(n >> 24); out[1] = static_cast<char>(n >> 16);
  out[2] = static_cast<char>(n >> 8); out[3] = static_cast<char>(n);
  out.append(type);
  out.append(data);
  out.append("CRC!");
  return out;
}

TEST(Media, JpegLosesExifAndCommentsKeepsJfifProfileAndScan) {
  const std::string scan = std::string("\xFF\xDA", 2) + std::string("\x00\x08\x01\x01\x00\x00\x3F\x00", 8) +
                           "pixels\xFF\xD9";
  const std::string jpeg = std::string("\xFF\xD8", 2) + segment(0xE0, std::string("JFIF\0\x01\x01", 7)) +
                           segment(0xE1, std::string_view("Exif\0\0GPS here", 15)) + segment(0xE2, "ICC_PROFILE\0colours") +
                           segment(0xED, "Photoshop 3.0 IPTC") + segment(0xFE, "a comment") +
                           segment(0xDB, "quantisation") + scan;
  const std::string out = mux::media::without_metadata(jpeg);
  EXPECT_EQ(out.find("GPS here"), std::string::npos);
  EXPECT_EQ(out.find("IPTC"), std::string::npos);
  EXPECT_EQ(out.find("a comment"), std::string::npos);
  EXPECT_NE(out.find("JFIF"), std::string::npos);
  EXPECT_NE(out.find("ICC_PROFILE"), std::string::npos);
  EXPECT_NE(out.find("quantisation"), std::string::npos);
  EXPECT_TRUE(out.ends_with(scan)) << "the picture's bytes are as they were";
}

TEST(Media, PngLosesTextAndTimeKeepsTheRest) {
  const std::string png = std::string("\x89PNG\r\n\x1A\n", 8) + chunk("IHDR", "0123456789abc") +
                          chunk("tEXt", "Author\0me") + chunk("iCCP", "profile") + chunk("tIME", "1234567") +
                          chunk("eXIf", "gps") + chunk("IDAT", "pixels") + chunk("IEND", "");
  const std::string out = mux::media::without_metadata(png);
  EXPECT_EQ(out.find("Author"), std::string::npos);
  EXPECT_EQ(out.find("tIME"), std::string::npos);
  EXPECT_EQ(out.find("eXIf"), std::string::npos);
  EXPECT_NE(out.find("iCCP"), std::string::npos);
  EXPECT_NE(out.find("pixels"), std::string::npos);
  EXPECT_TRUE(out.ends_with(chunk("IEND", "")));
}

TEST(Media, OtherFilesAreLeftAlone) {
  EXPECT_EQ(mux::media::without_metadata("just some text"), "just some text");
  EXPECT_FALSE(mux::media::picture_of("just some text"));
}

}  // namespace

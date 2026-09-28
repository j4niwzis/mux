// SPDX-License-Identifier: AGPL-3.0-only
// mux.config: what is kept, where, and who can read it.
import std;
import mux.config;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

namespace fs = std::filesystem;
using mux::config::saved_account;

// A directory of the test's own, removed after it.
struct scratch {
  fs::path dir;
  scratch() {
    dir = fs::temp_directory_path() / std::format("mux-config-test-{}", std::random_device{}());
    fs::remove_all(dir);
  }
  ~scratch() {
    std::error_code ignored;
    fs::remove_all(dir, ignored);
  }
};

TEST(Config, NoFileIsNoAccounts) {
  scratch here;
  const auto got = mux::config::load(here.dir / "mux" / "accounts.json");
  ASSERT_TRUE(got.has_value());
  EXPECT_TRUE(got->accounts.empty());
}

TEST(Config, WhatIsSavedIsWhatIsLoaded) {
  scratch here;
  const fs::path where = here.dir / "mux" / "accounts.json";
  mux::config::file kept;
  kept.accounts.push_back({.address = "alice@example.com", .password = "p\"ss\\word", .host = "xmpp.example.com", .port = 5222});
  kept.accounts.push_back({.address = "@bob:example.org", .password = "секрет", .enabled = false,
                           .homeserver = "https://matrix.example.org"});
  ASSERT_TRUE(mux::config::save(where, kept).has_value());
  const auto got = mux::config::load(where);
  ASSERT_TRUE(got.has_value()) << got.error();
  EXPECT_EQ(*got, kept);
}

TEST(Config, OnlyItsOwnerCanReadIt) {
  scratch here;
  const fs::path where = here.dir / "mux" / "accounts.json";
  ASSERT_TRUE(mux::config::save(where, {.accounts = {{.address = "a@b.c", .password = "x"}}}).has_value());
  EXPECT_EQ(fs::status(where).permissions(), fs::perms::owner_read | fs::perms::owner_write);
  EXPECT_EQ(fs::status(where.parent_path()).permissions(), fs::perms::owner_all);
  EXPECT_FALSE(fs::exists(fs::path(where) += ".new"));
}

TEST(Config, SavingAgainReplacesTheFile) {
  scratch here;
  const fs::path where = here.dir / "accounts.json";
  ASSERT_TRUE(mux::config::save(where, {.accounts = {{.address = "a@b.c", .password = "x"}}}).has_value());
  ASSERT_TRUE(mux::config::save(where, {}).has_value());
  const auto got = mux::config::load(where);
  ASSERT_TRUE(got.has_value());
  EXPECT_TRUE(got->accounts.empty());
}

TEST(Config, ABrokenFileSaysSo) {
  scratch here;
  fs::create_directories(here.dir);
  const fs::path where = here.dir / "accounts.json";
  std::ofstream(where) << "{\"accounts\": [ {\"address\": }";
  const auto got = mux::config::load(where);
  ASSERT_FALSE(got.has_value());
  EXPECT_NE(got.error().find("is not an accounts file"), std::string::npos);
}

TEST(Config, TheAddressSaysTheProtocol) {
  EXPECT_TRUE(mux::config::is_matrix("@bob:example.org"));
  EXPECT_FALSE(mux::config::is_matrix("alice@example.com"));
}

TEST(Config, WhatIsWrongWithAnAccount) {
  using mux::config::check;
  EXPECT_EQ(check({.address = "alice@example.com", .password = "x"}), std::nullopt);
  EXPECT_EQ(check({.address = "@bob:example.org", .password = "x", .homeserver = "https://m.example.org"}),
            std::nullopt);
  EXPECT_TRUE(check({.address = "", .password = "x"}));
  EXPECT_TRUE(check({.address = "alice", .password = "x"}));
  EXPECT_TRUE(check({.address = "a@b@c", .password = "x"}));
  EXPECT_TRUE(check({.address = "@bob", .password = "x"}));
  EXPECT_TRUE(check({.address = "alice@example.com", .password = ""}));
  EXPECT_TRUE(check({.address = "alice@example.com", .password = "x", .port = 70000}));
  EXPECT_TRUE(check({.address = "alice@example.com", .password = "x", .homeserver = "https://x"}));
  EXPECT_TRUE(check({.address = "@bob:example.org", .password = "x", .host = "h"}));
  EXPECT_TRUE(check({.address = "@bob:example.org", .password = "x", .homeserver = "matrix.example.org"}));
}

}  // namespace

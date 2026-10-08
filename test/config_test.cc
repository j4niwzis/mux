// SPDX-License-Identifier: AGPL-3.0-only
// mux.config: what is kept, where, and who can read it.
import std;
import mux.config;
import mux.vault;
import mux.proto.kept;
import gtest;

#include "gtest/gtest-macros.h"

namespace {

namespace fs = std::filesystem;
// What each protocol keeps of its own.
using matrix_account = mux::proto::matrix::kept;
using xmpp_account = mux::proto::xmpp::kept;

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
  mux::vault::vault vault;
  scratch here;
  const auto got = mux::config::load(here.dir / "mux" / "accounts.json", vault);
  ASSERT_TRUE(got.has_value());
  EXPECT_TRUE(mux::config::accounts_of(*got).empty());
}

TEST(Config, WhatIsSavedIsWhatIsLoaded) {
  mux::vault::vault vault;
  scratch here;
  const fs::path where = here.dir / "mux" / "accounts.json";
  const std::vector<mux::config::account_t> accounts{
      {.own = mux::config::kept_t{xmpp_account{.address = "alice@example.com", .password = "p\"ss\\word", .resource = "laptop",
                                               .host = "xmpp.example.com", .port = 5222, .plain_without_tls = true}},
       .shared = {.colour = mux::config::accent_said_t{mux::config::accent::pink{}}}},
      {.own = mux::config::kept_t{matrix_account{.user_id = "@bob:example.org", .password = "секрет",
                                                 .homeserver = "https://matrix.example.org", .device_name = "desk"}},
       .shared = {.enabled = false}}};
  const mux::config::file kept = mux::config::file_of(accounts);
  ASSERT_TRUE(mux::config::save(where, kept, vault).has_value());
  const auto got = mux::config::load(where, vault);
  ASSERT_TRUE(got.has_value()) << got.error();
  EXPECT_EQ(*got, kept);
}

TEST(Config, OnlyItsOwnerCanReadIt) {
  mux::vault::vault vault;
  scratch here;
  const fs::path where = here.dir / "mux" / "accounts.json";
  ASSERT_TRUE(mux::config::save(where, mux::config::file_of(std::vector{mux::config::account_from("a@b.c", "x")}), vault).has_value());
  EXPECT_EQ(fs::status(where).permissions(), fs::perms::owner_read | fs::perms::owner_write);
  EXPECT_EQ(fs::status(where.parent_path()).permissions(), fs::perms::owner_all);
  EXPECT_FALSE(fs::exists(fs::path(where) += ".new"));
}

TEST(Config, SavingAgainReplacesTheFile) {
  mux::vault::vault vault;
  scratch here;
  const fs::path where = here.dir / "accounts.json";
  ASSERT_TRUE(mux::config::save(where, mux::config::file_of(std::vector{mux::config::account_from("a@b.c", "x")}), vault).has_value());
  ASSERT_TRUE(mux::config::save(where, {}, vault).has_value());
  const auto got = mux::config::load(where, vault);
  ASSERT_TRUE(got.has_value());
  EXPECT_TRUE(mux::config::accounts_of(*got).empty());
}

TEST(Config, ABrokenFileSaysSo) {
  mux::vault::vault vault;
  scratch here;
  fs::create_directories(here.dir);
  const fs::path where = here.dir / "accounts.json";
  std::ofstream(where) << "{\"xmpp\": [ {\"address\": }";
  const auto got = mux::config::load(where, vault);
  ASSERT_FALSE(got.has_value());
  EXPECT_NE(got.error().find("is not an accounts file"), std::string::npos);
}

// The choices' words, read once into the choices: each its own, the old
// ones as what they meant, an unknown one as the default.
TEST(Config, TheChoicesWordsAreReadAsChoices) {
  mux::vault::vault vault;
  scratch here;
  fs::create_directories(here.dir);
  const fs::path where = here.dir / "accounts.json";
  std::ofstream(where) << R"({"theme": "dark", "accent": "cyan", "renderer": "software", "motion": "spinning"})";
  const auto got = mux::config::load(where, vault);
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(mux::config::theme_of(got->theme), mux::config::theme_t{mux::config::theme::night{}});
  EXPECT_EQ(mux::config::accent_of(got->accent), mux::config::accent_t{mux::config::accent::blue{}});
  EXPECT_EQ(mux::config::renderer_of(got->renderer), mux::config::renderer_t{mux::config::renderer::software{}});
  EXPECT_EQ(mux::config::motion_of(got->motion), mux::config::motion_t{mux::config::motion::full{}});
}

TEST(Config, AnOldFileIsNotReadAsEmpty) {
  mux::vault::vault vault;
  scratch here;
  fs::create_directories(here.dir);
  const fs::path where = here.dir / "accounts.json";
  std::ofstream(where) << R"({"accounts": [{"address": "a@b.c", "password": "x", "enabled": true}]})";
  EXPECT_FALSE(mux::config::load(where, vault).has_value());
}

// A file of before -- one list a protocol, every setting flat in each --
// read as it is now: the account's own, and its settings kept.
TEST(Config, AnOldFilesAccountsAreReadAsTheyAreNow) {
  mux::vault::vault vault;
  scratch here;
  fs::create_directories(here.dir);
  const fs::path where = here.dir / "accounts.json";
  std::ofstream(where) << R"({"xmpp": [{"address": "a@b.c", "password": "x", "resource": "mux", "plain_without_tls": false, "enabled": true, "colour": "red"}],
                              "matrix": [{"user_id": "@d:e.f", "password": "y", "device_name": "mux", "enabled": false, "access_token": "t"}]})";
  const auto got = mux::config::load(where, vault);
  ASSERT_TRUE(got.has_value()) << got.error();
  const auto all = mux::config::accounts_of(*got);
  ASSERT_EQ(all.size(), 2u);
  EXPECT_EQ(mux::config::address_of(all[0]), "a@b.c");
  EXPECT_EQ(mux::config::protocol_name(all[0]), "XMPP");
  EXPECT_EQ(all[0].shared.colour, std::optional<std::string>("red"));
  EXPECT_EQ(mux::config::address_of(all[1]), "@d:e.f");
  EXPECT_EQ(mux::config::protocol_name(all[1]), "Matrix");
  EXPECT_FALSE(mux::config::enabled_of(all[1]));
  // Written as it is now, and read back the same.
  ASSERT_TRUE(mux::config::save(where, mux::config::file_of(all), vault).has_value());
  const auto again = mux::config::load(where, vault);
  ASSERT_TRUE(again.has_value()) << again.error();
  EXPECT_EQ(mux::config::accounts_of(*again), all);
}

// An account of a protocol this build does not have -- a newer mux's -- is
// kept as it was and written back so: never lost.
TEST(Config, AnAccountOfAnotherProtocolIsKept) {
  mux::vault::vault vault;
  scratch here;
  fs::create_directories(here.dir);
  const fs::path where = here.dir / "accounts.json";
  std::ofstream(where) << R"({"accounts": [{"protocol": "irc", "own": {"nick": "alice", "server": "irc.libera.chat"}, "shared": {"enabled": true}},
                                           {"protocol": "xmpp", "own": {"address": "a@b.c", "password": "x", "resource": "mux", "plain_without_tls": false}, "shared": {"enabled": true}}]})";
  const auto got = mux::config::load(where, vault);
  ASSERT_TRUE(got.has_value()) << got.error();
  const auto all = mux::config::accounts_of(*got);
  ASSERT_EQ(all.size(), 1u);
  EXPECT_EQ(mux::config::address_of(all[0]), "a@b.c");
  const auto foreign = mux::config::foreign_of(*got);
  ASSERT_EQ(foreign.size(), 1u);
  EXPECT_EQ(foreign[0].protocol, "irc");
  ASSERT_TRUE(mux::config::save(where, mux::config::file_of(all, foreign), vault).has_value());
  const auto again = mux::config::load(where, vault);
  ASSERT_TRUE(again.has_value()) << again.error();
  EXPECT_EQ(mux::config::foreign_of(*again), foreign);
  EXPECT_EQ(mux::config::accounts_of(*again), all);
}

TEST(Config, TheAddressSaysTheProtocol) {
  mux::vault::vault vault;
  EXPECT_TRUE(mux::config::is_matrix("@bob:example.org"));
  EXPECT_FALSE(mux::config::is_matrix("alice@example.com"));
  EXPECT_EQ(mux::config::protocol_name(mux::config::account_from("@bob:example.org", "x")), "Matrix");
  EXPECT_EQ(mux::config::protocol_name(mux::config::account_from("alice@example.com", "x")), "XMPP");
}

TEST(Config, WhatIsWrongWithAnXmppAccount) {
  mux::vault::vault vault;
  using mux::config::check;
  EXPECT_EQ(check(xmpp_account{.address = "alice@example.com", .password = "x"}), std::nullopt);
  EXPECT_TRUE(check(xmpp_account{.address = "", .password = "x"}));
  EXPECT_TRUE(check(xmpp_account{.address = "alice", .password = "x"}));
  EXPECT_TRUE(check(xmpp_account{.address = "a@b@c", .password = "x"}));
  EXPECT_TRUE(check(xmpp_account{.address = "@bob:example.org", .password = "x"}));
  EXPECT_TRUE(check(xmpp_account{.address = "alice@example.com", .password = ""}));
  EXPECT_TRUE(check(xmpp_account{.address = "alice@example.com", .password = "x", .resource = ""}));
  EXPECT_TRUE(check(xmpp_account{.address = "alice@example.com", .password = "x", .port = 70000}));
}

TEST(Config, WhatIsWrongWithAMatrixAccount) {
  mux::vault::vault vault;
  using mux::config::check;
  EXPECT_EQ(check(matrix_account{.user_id = "@bob:example.org", .password = "x"}), std::nullopt);
  EXPECT_EQ(check(matrix_account{.user_id = "@bob:example.org", .password = "x", .homeserver = "https://m.example.org"}),
            std::nullopt);
  EXPECT_TRUE(check(matrix_account{.user_id = "bob:example.org", .password = "x"}));
  EXPECT_TRUE(check(matrix_account{.user_id = "@bob", .password = "x"}));
  EXPECT_TRUE(check(matrix_account{.user_id = "@bob:example.org", .password = ""}));
  EXPECT_TRUE(check(matrix_account{.user_id = "@bob:example.org", .password = "x", .homeserver = "matrix.example.org"}));
  EXPECT_TRUE(check(matrix_account{.user_id = "@bob:example.org", .password = "x", .device_name = ""}));
}

// What an account keeps of itself -- its proxy's name, its receipts -- and
// the program's proxy profiles, saved and read back as they were.
TEST(Config, ProxiesAndTheirAccountsAreKept) {
  mux::vault::vault vault;
  scratch here;
  const fs::path where = here.dir / "accounts.json";
  auto account = mux::config::account_from("alice@example.com", "x");
  account.shared.read_receipts = false;
  account.shared.proxy = "tor";
  mux::config::file kept = mux::config::file_of(std::vector{account});
  kept.proxies = std::vector<mux::config::proxy_settings>{
      {.name = "tor", .kind = mux::config::proxy_kind::socks5{}, .host = "127.0.0.1", .port = 9050}};
  ASSERT_TRUE(mux::config::save(where, kept, vault).has_value());
  const auto got = mux::config::load(where, vault);
  ASSERT_TRUE(got.has_value()) << got.error();
  EXPECT_EQ(*got, kept);
  const auto all = mux::config::accounts_of(*got);
  ASSERT_EQ(all.size(), 1u);
  EXPECT_EQ(mux::config::proxy_of(all.front()), std::optional<std::string>("tor"));
}

}  // namespace

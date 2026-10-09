// SPDX-License-Identifier: AGPL-3.0-only
import std;
import mux.core;
import mux.config;
import mux.vault;
import mux.app.store;
import gtest;
#include "gtest/gtest-macros.h"

TEST(Store, DirectMessageReadMarkersAndReceiptTimesSurviveReopening) {
  const mux::account_id account{mux::protocol::matrix{},
      std::format("@read-test-{}:example.org", std::chrono::steady_clock::now().time_since_epoch().count())};
  const mux::conversation_id room{account, "!dm:example.org"};
  const auto directory = mux::config::state_path("messages") / mux::config::file_name_of(account.address);
  struct clean {
    std::filesystem::path directory;
    ~clean() { std::error_code ignored; std::filesystem::remove_all(directory, ignored); }
  } cleanup{directory};
  const auto path = directory / (mux::config::file_name_of(room.id) + ".reads.json");
  mux::vault::vault vault;
  mux::conversation chat;
  chat.id = room;
  chat.read_up_to = "$latest";
  chat.read_by.emplace("@other:example.org", "$other-latest");
  const auto time = std::chrono::sys_time<std::chrono::milliseconds>{std::chrono::milliseconds{123456}};
  chat.receipt_times.emplace("@other:example.org", time);
  {
    mux::app::message_store first;
    first.vault = &vault;
    first.keep_reads(room, chat);
  }
  ASSERT_TRUE(std::filesystem::is_regular_file(path));
  mux::app::message_store reopened;
  reopened.vault = &vault;
  const auto saved = reopened.read_reads(room);
  EXPECT_EQ(saved.me, chat.read_up_to);
  EXPECT_EQ(saved.read_by, chat.read_by);
  EXPECT_EQ(saved.times, chat.receipt_times);
  // Old installations have users/me but no receipt timestamps.
  ASSERT_TRUE(vault.write_file(path, R"({"users":{"@other:example.org":"$old"},"me":"$old"})"));
  const auto legacy = reopened.read_reads(room);
  EXPECT_EQ(legacy.me, std::optional<std::string>("$old"));
  EXPECT_TRUE(legacy.times.empty());
}

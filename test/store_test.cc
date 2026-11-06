// SPDX-License-Identifier: AGPL-3.0-only
import std;
import mux.core;
import mux.config;
import mux.vault;
import mux.app.store;
import gtest;
#include "gtest/gtest-macros.h"

TEST(Store, ReadMarkersAndPluginPartsSurviveReopening) {
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
  const std::string part = R"({"users":{"@other:example.org":"$other-latest"},"times":{"@other:example.org":123456}})";
  {
    mux::app::message_store first;
    first.vault = &vault;
    first.keep_reads(room, chat);
    first.keep_part(room, "receipts", part);
  }
  ASSERT_TRUE(std::filesystem::is_regular_file(path));
  mux::app::message_store reopened;
  reopened.vault = &vault;
  EXPECT_EQ(reopened.read_reads(room).me, chat.read_up_to);
  EXPECT_EQ(reopened.read_part(room, "receipts"), std::optional<std::string>(part));
  EXPECT_EQ(reopened.read_part(room, "other"), std::nullopt);
  // Old installations kept others' receipts in the reads file too: the
  // user's position still read from it.
  ASSERT_TRUE(vault.write_file(path, R"({"users":{"@other:example.org":"$old"},"me":"$old"})"));
  EXPECT_EQ(reopened.read_reads(room).me, std::optional<std::string>("$old"));
}

TEST(Store, EditEventIdsSurviveReopening) {
  const mux::account_id account{mux::protocol::matrix{},
      std::format("@edit-test-{}:example.org", std::chrono::steady_clock::now().time_since_epoch().count())};
  const mux::conversation_id room{account, "!room:example.org"};
  const auto directory = mux::config::state_path("messages") / mux::config::file_name_of(account.address);
  struct clean { std::filesystem::path directory; ~clean() {
    std::error_code ignored; std::filesystem::remove_all(directory, ignored);
  } } cleanup{directory};
  mux::vault::vault vault;
  mux::message original{.in = room, .id = "$original", .sender = account.address,
      .body = {"latest", std::nullopt}, .edited = true};
  original.versions = {{{"first", std::nullopt}, {}, "$original"}, {{"second", std::nullopt}, {}, "$edit1"}};
  original.latest_edit_event = "$edit2";
  {
    mux::app::message_store store;
    store.vault = &vault;
    store.record(original);
  }
  mux::app::message_store reopened;
  reopened.vault = &vault;
  const auto saved = reopened.everything(room);
  ASSERT_TRUE(saved.contains("$original"));
  const auto& restored = saved.at("$original");
  ASSERT_EQ(restored.versions.size(), 2u);
  EXPECT_EQ(restored.versions[0].event, "$original");
  EXPECT_EQ(restored.versions[1].event, "$edit1");
  EXPECT_EQ(restored.latest_edit_event, "$edit2");
}

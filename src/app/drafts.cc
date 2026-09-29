// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.drafts: what was being written in a chat, kept when the reader
// goes to another and when mux quits -- in the window's field for each
// chat, and in a small file under the state directory.
export module mux.app.drafts;

import std;
import knot;
import mux.core;
import mux.config;
import mux.ui;
import mux.app.services;
import mux.logic.drafts;

export namespace mux::app {

class drafts_part {
 public:
  explicit drafts_part(services& shared) : s_(&shared) {}

  // What is in a chat's field, kept as its draft -- blank, none -- and the
  // file written where it changed.
  void keep(const conversation_id& in, const std::string& text) {
    auto& drafts = s_->root().main().drafts;
    if (!logic::keep_draft(drafts, in, text) || s_->demo())
      return;
    const auto where = mux::config::state_path("drafts.json");
    std::error_code failed;
    std::filesystem::create_directories(where.parent_path(), failed);
    std::ofstream(where, std::ios::binary | std::ios::trunc) << logic::drafts_text(drafts);
  }

  // The drafts kept, back in the window: at the start.
  void load() {
    std::ifstream file(mux::config::state_path("drafts.json"), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    for (auto& [in, draft] : logic::drafts_from(text))
      s_->root().main().drafts.insert_or_assign(in, std::move(draft));
  }

 private:
  services* s_;
};

}  // namespace mux::app

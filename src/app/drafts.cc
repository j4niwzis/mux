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

export namespace mux::app {

class drafts_part {
 public:
  explicit drafts_part(services& shared) : s_(&shared) {}

  // What is in a chat's field, kept as its draft: blank, none.
  void keep(const conversation_id& in, const std::string& text) {
    auto& drafts = s_->root().main().drafts;
    const bool blank = std::ranges::all_of(text, [](unsigned char c) { return std::isspace(c) != 0; });
    const auto found = drafts.find(in);
    if (blank ? found == drafts.end() : (found != drafts.end() && found->second == text))
      return;
    if (blank)
      drafts.erase(in);
    else
      drafts.insert_or_assign(in, text);
    if (s_->demo())
      return;
    knot::value::object all;
    for (const auto& [id, draft] : drafts)
      all.emplace(id.account.address + "\n" + id.id, knot::value(draft));
    const auto where = mux::config::state_path("drafts.json");
    std::error_code failed;
    std::filesystem::create_directories(where.parent_path(), failed);
    std::ofstream(where, std::ios::binary | std::ios::trunc) << knot::to_json_string(knot::value(std::move(all)));
  }

  // The drafts kept, back in the window: at the start.
  void load() {
    std::ifstream file(mux::config::state_path("drafts.json"), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto parsed = knot::try_read<knot::value>(std::string_view(text));
    if (!parsed || !parsed->is<knot::value::object>())
      return;
    for (const auto& [key, draft] : parsed->as<knot::value::object>()) {
      const auto cut = key.find('\n');
      if (cut == std::string::npos || !draft.is<std::string>())
        continue;
      const std::string address = key.substr(0, cut);
      s_->root().main().drafts.insert_or_assign(
          conversation_id{account_id{mux::ui::protocol_of(address), address}, key.substr(cut + 1)},
          draft.as<std::string>());
    }
  }

 private:
  services* s_;
};

}  // namespace mux::app

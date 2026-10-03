// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.local_data: local data's encryption -- the unlock screen at the
// start, where it is on; turned on, off, or under another passphrase, from
// Storage; and everything kept sealed again as it changes. A part of the
// program: it owns what the start waits with while it is locked, and
// reaches the vault, the settings it is given and the rest through the
// services.
export module mux.app.local_data;

import std;
import splice;
import mux.vault;
import mux.core;
import mux.config;
import mux.ui;
import mux.ui.proto;
import mux.app.kept;
import mux.app.services;
import mux.app.requests;

export namespace mux::app {

class local_data_part {
 public:
  local_data_part(services& shared, kept_settings& kept) : s_(&shared), k_(&kept) {}
  local_data_part(const local_data_part&) = delete;
  local_data_part& operator=(const local_data_part&) = delete;

  // Local data encrypted and locked: the unlock screen, and what the start
  // would have done kept until it is opened.
  void lock(std::vector<mux::config::account_t> extra, bool demo) {
    waiting_extra_ = std::move(extra);
    waiting_demo_ = demo;
    s_->root().ask_passphrase(mux::config::passphrase_for::unlock{});
  }
  // What the start does once local data is open: the settings read, and
  // what it waited with.
  struct opened {
    mux::config::file saved;
    std::vector<mux::config::account_t> extra;
    bool demo = false;
    std::optional<std::string> error;
  };
  // The passphrase at the start: local data opened -- a re-seal cut short
  // last time finished first -- and what the start does then; nothing where
  // it is not the passphrase.
  [[nodiscard]] std::optional<opened> unlock(const request::give_passphrase& one) {
    auto& vault = *s_->vault;
    if (!vault.unlock(one.current)) {
      s_->root().passphrase_refused("That is not the passphrase.");
      return std::nullopt;
    }
    if (vault.resealing() && !this->reseal([](mux::vault::vault&) {}))
      s_->root().show_message("Local data", "Re-sealing what is kept, begun before, could not be finished. It is "
                                            "tried again at the next start; everything stays readable.");
    s_->root().close_passphrase();
    opened out{.extra = std::move(waiting_extra_), .demo = waiting_demo_};
    if (auto loaded = mux::config::load(k_->config_path, vault))
      out.saved = std::move(*loaded);
    else
      out.error = loaded.error();
    return out;
  }
  // Turned on, under a new passphrase: pictures are not kept on disk while
  // it is on, and those there go.
  void encrypt(const request::give_passphrase& one) {
    if (s_->vault->on())
      return this->done();
    if (auto refused = mux::config::new_passphrase_refused(one.fresh, one.again))
      return s_->root().passphrase_refused(*refused);
    if (!this->resealed([&](mux::vault::vault& v) { v.begin_encrypt(one.fresh); }))
      return;
    std::error_code ignored;
    std::filesystem::remove_all(mux::config::cache_path("").parent_path(), ignored);
    this->done();
  }
  // Under another passphrase, the one now given first.
  void change(const request::give_passphrase& one) {
    if (!s_->vault->matches(one.current))
      return s_->root().passphrase_refused("That is not the passphrase now.");
    if (auto refused = mux::config::new_passphrase_refused(one.fresh, one.again))
      return s_->root().passphrase_refused(*refused);
    if (this->resealed([&](mux::vault::vault& v) { v.begin_change(one.fresh); }))
      this->done();
  }
  // Turned off, the passphrase given first.
  void decrypt(const request::give_passphrase& one) {
    if (!s_->vault->matches(one.current))
      return s_->root().passphrase_refused("That is not the passphrase.");
    if (this->resealed([](mux::vault::vault& v) { v.begin_decrypt(); }))
      this->done();
  }
  // From Storage: on asks for a new passphrase, off for the one now.
  void apply(const request::flip_local_encryption&) {
    if (s_->vault->on())
      s_->root().ask_passphrase(mux::config::passphrase_for::decrypt{});
    else
      s_->root().ask_passphrase(mux::config::passphrase_for::encrypt{});
  }
  void apply(const request::change_passphrase&) { s_->root().ask_passphrase(mux::config::passphrase_for::change{}); }

 private:
  // The passphrase asked for done: its dialog closed, Storage showing it.
  void done() {
    s_->root().close_passphrase();
    if (auto* up = s_->root().settings_up())
      if (auto* page = up->storage())
        page->show_sealed(s_->vault->on());
  }
  // Re-sealed, or why not said in the dialog: whether it was.
  template <class Turn>
  [[nodiscard]] bool resealed(Turn turn) {
    if (this->reseal(std::move(turn)))
      return true;
    s_->root().passphrase_refused(std::string(s_->vault->resealing() ? kCutShort : kUnread));
    return false;
  }
  // Everything kept sealed again as the vault is after `turn` -- on, off, or
  // under another passphrase. Read first, as the vault is; then its journal
  // begun, everything written again, and the journal ended. Cut short after
  // the turn -- an error, a crash -- the journal stays: every file is still
  // opened, by either key or plain, and the re-seal is finished at the next
  // start. False, and nothing changed, where it could not all be read.
  template <class Turn>
  [[nodiscard]] bool reseal(Turn turn) {
    auto& vault = *s_->vault;
    // All of it with every other read and write of the vault waiting: the
    // network's saves and the store's lines come after, under the new key.
    return vault.exclusive([&] {
      const auto all = vault.read_all(this->sealed_files());
      if (!all)
        return false;
      turn(vault);
      if (!vault.write_all(*all))
        return false;
      vault.finish();
      return true;
    });
  }
  // The files sealed: the settings, and every file in the state directory
  // that is read through the vault -- a chat's messages and its deleted ones
  // a line at a time, the rest whole. Not the wallpapers, read as pictures.
  [[nodiscard]] mux::vault::vault::kept_files sealed_files() const {
    mux::vault::vault::kept_files out;
    out.whole.push_back(k_->config_path);
    const auto root = mux::config::state_path("").parent_path();
    std::error_code failed;
    if (!std::filesystem::exists(root, failed))
      return out;
    for (auto walk = std::filesystem::recursive_directory_iterator(root, failed);
         !failed && walk != std::filesystem::recursive_directory_iterator(); walk.increment(failed)) {
      const auto& path = walk->path();
      if (walk->is_directory() && path.filename() == "wallpapers") {
        walk.disable_recursion_pending();
        continue;
      }
      if (!walk->is_regular_file())
        continue;
      const auto name = path.filename().string();
      if (name.ends_with(".new") || name.ends_with(".unreadable"))
        continue;
      (path.extension() == ".jsonl" ? out.lines : out.whole).push_back(path);
    }
    return out;
  }
  // Why a re-seal did not happen, or was not all done.
  static constexpr std::string_view kUnread = "Something kept could not be read, so nothing was changed.";
  static constexpr std::string_view kCutShort =
      "Not everything could be written. Everything stays readable, and it is finished at the next start.";

  services* s_;
  kept_settings* k_;
  // What the start waits with while local data is locked.
  std::vector<mux::config::account_t> waiting_extra_;
  bool waiting_demo_ = false;
};

}  // namespace mux::app

// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.gifs.app -- The GIFs' part of the program: the saved ones read
// from where they are kept, for their page, each decoded on a worker into
// the frames it plays; and one kept among them, from a message's menu.
export module mux.plugin.gifs.app;

import std;
import mux.composition;
import skia;
import mux.config;
import mux.platform.files;
import mux.ui;
import mux.app.services;
import mux.app.workers;
import mux.plugin.gifs.types;
import mux.plugin.emoji_picker.ui;

export namespace mux::plugins::gifs {

struct part {
  app::services* s_ = nullptr;
  // What is being decoded now: not asked twice.
  std::set<std::string> decoding_;
  // Where the saved GIFs are kept: with the program's state, not its cache,
  // which is pruned.
  static std::filesystem::path saved() { return config::state_path("gifs"); }

  // One kept among them, by its source: named by it, so saving it twice keeps
  // one; written anew, so it comes first. The pictures part has its whole.
  template <class Asked>
  void apply(const save& one, const Asked& asked) {
    ui::show<ui::menu_facts>(*s_->showing, std::nullopt);
    asked(ui::request::keep_whole{one.source, (saved() / config::file_name_of(one.source)).string(), "GIFs",
                                  "The GIF has not loaded yet. Save it once it plays.", "Saved to your GIFs."});
  }

  // The saved GIFs, newest first, to their page; each decoded on a worker
  // into the frames it plays, where it is not already.
  // Its page chosen in the media panel: its GIFs shown, read again.
  void apply(const page&) { this->apply(show{}); }
  void apply(const show&) {
    const auto chosen = s_->chosen();
    if (!chosen || !ui::may_send_files(s_->ui, chosen->account)) return;
    std::error_code failed;
    auto kept = std::filesystem::directory_iterator(saved(), failed) | std::views::filter([&](const auto& entry) { return entry.is_regular_file(failed); }) | std::views::transform([&](const auto& entry) { return std::pair{entry.last_write_time(failed), entry.path().string()}; }) | std::ranges::to<std::vector>();
    std::ranges::sort(kept, std::greater{});
    auto paths = kept | std::views::values | std::ranges::to<std::vector>();
    auto* scene = s_->scene;
    for (const std::string& path : paths) {
      const std::string key = "gif:" + path;
      if (ui::animations().has(key) || ui::whole_pictures().has(key) || !decoding_.insert(key).second)
        continue;
      s_->work->run([path, key, scene]() -> app::workers::done_t {
        std::string bytes = platform::files::read(path).value_or(std::string());
        auto frames = skia::decodeFrames(bytes.data(), bytes.size());
        return [frames = std::move(frames), key, scene]() mutable {
          if (frames.size() > 1)
            ui::animations().put(key, std::move(frames));
          else if (!frames.empty())
            ui::whole_pictures().put(key, std::move(frames.front().image));
          scene->state().markDamaged();
        };
      });
    }
    ui::change_shown<std::optional<emoji_picker::emoji_facts>>(*s_->showing, [&](auto& now) {
      if (now)
        std::get<shown>(now->pages).paths = std::move(paths);
    });
  }
};

// Its part, among the program's (mux.app.program).
inline part extend(contract::program_parts, type_tag<feature>, app::services& shared) { return part{&shared}; }

}  // namespace mux::plugins::gifs

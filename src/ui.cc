// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui: the screen, as skiff's scene -- the conversations of every account
// down the side, the one chosen beside them with its timeline, and a line
// to write in. Built from mux.core's model, and brought up to date from it
// after each batch of changes the network sends.
export module mux.ui;

import std;
import skia;
import skiff.scene;
import skiff.nodes;
import skiff.widgets;
import mux.core;

export namespace mux::ui {

namespace scene = skiff::scene;
namespace nodes = skiff::nodes;
namespace widgets = skiff::widgets;

inline const skia::SkColor background = skia::colorSetARGB(255, 24, 27, 30);
inline const skia::SkColor sidebar_colour = skia::colorSetARGB(255, 32, 36, 40);
inline const skia::SkColor chosen_colour = skia::colorSetARGB(255, 52, 60, 66);
inline const skia::SkColor text_colour = skia::colorSetARGB(255, 235, 240, 243);
inline const skia::SkColor dim_colour = skia::colorSetARGB(255, 150, 162, 170);
inline const skia::SkColor accent_colour = skia::colorSetARGB(255, 102, 204, 255);

// What the screen asks of the program: a message to send. `Sender` is
// called on the UI's thread, with where and what.
template <class Sender>
class screen;

// The line to write in: Enter sends what is in it.
template <class Sender>
class composer : public scene::TypedDrawable<composer<Sender>, widgets::TextBox> {
 public:
  explicit composer(screen<Sender>* owner)
      : scene::TypedDrawable<composer<Sender>, widgets::TextBox>("Write a message…"), owner_(owner) {}

 protected:
  void onKeyEvent(scene::KeyEvent& event) override {
    if (event.fPhase == scene::EventPhase::kTarget && event.fPressed && event.fKey == scene::Key::kEnter) {
      owner_->submit(this->text());
      this->setText({});
      event.handle();
      return;
    }
    widgets::TextBox::onKeyEvent(event);
  }

 private:
  screen<Sender>* owner_;
};

template <class Sender>
class screen {
 public:
  explicit screen(Sender send) : send_(std::move(send)) {
    root_ = scene::make<nodes::Box>({.fill = true}, background);
    auto* columns = root_->add<nodes::FillFlow>({.fill = true}, nodes::FillFlow::Direction::kHorizontal, 0.0f, 0.0f);

    auto* side = columns->add<nodes::Box>({.fillY = true, .width = 280.0f}, sidebar_colour);
    auto* side_column = side->add<nodes::FillFlow>({.fill = true, .padding = scene::Margin::all(8.0f)},
                                                   nodes::FillFlow::Direction::kVertical, 0.0f, 8.0f);
    status_ = side_column->add<nodes::Text>({.fillX = true}, "mux", 13.0f, dim_colour);
    auto* list_scroll = side_column->add<nodes::ScrollContainer>({.fill = true});
    list_ = list_scroll->add<nodes::FillFlow>({.fillX = true, .autoSize = scene::Axes::kY},
                                              nodes::FillFlow::Direction::kVertical, 0.0f, 2.0f);

    auto* main = columns->add<nodes::FillFlow>({.fillY = true, .grow = scene::Axes::kX,
                                                .padding = scene::Margin::all(12.0f)},
                                               nodes::FillFlow::Direction::kVertical, 0.0f, 8.0f);
    title_ = main->add<nodes::Text>({.fillX = true}, "", 20.0f, text_colour, true);
    topic_ = main->add<nodes::Text>({.fillX = true}, "", 13.0f, dim_colour);
    timeline_scroll_ = main->add<nodes::ScrollContainer>({.fillX = true, .grow = scene::Axes::kY});
    timeline_ = timeline_scroll_->add<nodes::FillFlow>({.fillX = true, .autoSize = scene::Axes::kY},
                                                       nodes::FillFlow::Direction::kVertical, 0.0f, 10.0f);
    main->add<composer<Sender>>({.fillX = true}, this);
  }

  scene::Drawable& root() { return *root_; }

  // The model as it is now: the list, and the chosen conversation.
  void show(const model& now) {
    model_ = &now;
    rebuild_list();
    rebuild_conversation();
  }

  void choose(const conversation_id& which) {
    chosen_ = which;
    if (model_) {
      rebuild_list();
      rebuild_conversation();
    }
  }

  void submit(std::string_view text) {
    if (!chosen_ || text.empty())
      return;
    send_(*chosen_, std::string(text));
  }

 private:
  void rebuild_list() {
    list_->clear();
    std::size_t online = 0, accounts = 0;
    for (const auto& [id, kept] : model_->accounts()) {
      ++accounts;
      if (kept.state == connection::online)
        ++online;
      list_->add<nodes::Text>({.fillX = true}, id.address, 12.0f, dim_colour, true);
      for (const auto& [key, one] : kept.conversations) {
        const bool is_chosen = chosen_ && *chosen_ == one.id;
        auto* row = list_->add<nodes::Clickable>({.fillX = true, .height = 44.0f},
                                                 [this, which = one.id] { this->choose(which); }, one.name);
        row->add<nodes::Box>({.fill = true, .cornerRadius = 6.0f}, is_chosen ? chosen_colour : sidebar_colour);
        auto* line = row->add<nodes::FillFlow>({.fill = true, .padding = scene::Margin::horizontal(10.0f)},
                                               nodes::FillFlow::Direction::kHorizontal, 8.0f, 0.0f);
        line->setCrossAlign(nodes::FillFlow::Align::kCentre);
        auto* name = line->add<nodes::Text>({.grow = scene::Axes::kX}, one.name.empty() ? one.id.id : one.name,
                                            15.0f, text_colour, one.unread > 0);
        name->setElided(true);
        if (one.unread > 0)
          line->add<nodes::Text>({}, std::to_string(one.unread), 13.0f, accent_colour, true);
      }
    }
    status_->setText(std::format("{} of {} account{} online", online, accounts, accounts == 1 ? "" : "s"));
  }

  void rebuild_conversation() {
    timeline_->clear();
    const conversation* one = chosen_ ? model_->find(*chosen_) : nullptr;
    if (!one) {
      title_->setText("Choose a conversation");
      topic_->setText("");
      return;
    }
    title_->setText(one->name.empty() ? one->id.id : one->name);
    std::string about = one->topic.value_or("");
    if (one->encrypted)
      about = about.empty() ? "encrypted" : about + " · encrypted";
    if (!one->typing.empty())
      about += (about.empty() ? "" : " · ") + std::to_string(one->typing.size()) + " typing";
    topic_->setText(about);
    for (const message& said : one->timeline) {
      auto* entry = timeline_->add<nodes::FillFlow>({.fillX = true, .autoSize = scene::Axes::kY},
                                                    nodes::FillFlow::Direction::kVertical, 0.0f, 2.0f);
      std::string who = said.sender;
      if (said.delivery == delivery::sending)
        who += " · sending";
      else if (said.delivery == delivery::failed)
        who += " · not sent";
      entry->add<nodes::Text>({.fillX = true}, who, 12.0f, said.outgoing ? accent_colour : dim_colour, true);
      std::string body = said.redacted ? "(removed)" : said.body.plain;
      if (said.edited)
        body += " (edited)";
      auto* text = entry->add<nodes::Text>({.fillX = true}, body, 15.0f, text_colour);
      text->setWrapped(true);
      if (!said.reactions.empty()) {
        std::string reactions;
        for (const auto& [key, who_reacted] : said.reactions)
          reactions += std::format("{} {}  ", key, who_reacted.size());
        entry->add<nodes::Text>({.fillX = true}, reactions, 13.0f, dim_colour);
      }
    }
    // The newest is at the bottom, and that is where the reader is.
    timeline_scroll_->scrollTo(std::numeric_limits<float>::max());
  }

  Sender send_;
  std::unique_ptr<nodes::Box> root_;
  nodes::Text* status_ = nullptr;
  nodes::FillFlow* list_ = nullptr;
  nodes::Text* title_ = nullptr;
  nodes::Text* topic_ = nullptr;
  nodes::ScrollContainer* timeline_scroll_ = nullptr;
  nodes::FillFlow* timeline_ = nullptr;
  const model* model_ = nullptr;
  std::optional<conversation_id> chosen_;
};

}  // namespace mux::ui

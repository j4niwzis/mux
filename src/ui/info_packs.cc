// SPDX-License-Identifier: AGPL-3.0-only
// mux.ui:info_packs -- Emojis & Stickers: a room's packs, or one's own.
export module mux.ui:info_packs;

import std;
import splice.bytes;
import splice;
import skia;
import skiff.paint;
import skiff.scene;
import skiff.nodes.box;
import skiff.nodes.flow;
import skiff.nodes.icon;
import skiff.nodes.image;
import skiff.nodes.scroll;
import skiff.nodes.text;
import skiff.widgets.pill;
import skiff.widgets.button;
import skiff.widgets.sliderbar;
import skiff.widgets.textbox;
import skiff.widgets.textarea;
import mux.core;
import mux.config;
import mux.logic.links;
import mux.protocols;
import :base;
import :icons;
import :avatars;
import :controls;
import :themes;
import :names;
import :forms;
import :composer;
import :message;
import :html;
import :timeline;  // a message's menu, for the reactions list's bubbles
import :info_common;
import :info_cards;
import :info_reactions;
import :info_new_chats;
import :info_looks;
import :info_threads;

export namespace mux::ui {
// Emojis & Stickers, as Cinny edits them (MSC2545): the packs of a room --
// or one's own pack -- listed; a pack opened: its name, attribution and use
// (as emoji, as stickers), and its images, each with its shortcode and use,
// removable; images added from files, uploaded as they are chosen; saved as
// the room's state, or one's account data.
template <class Actions>
struct packs_box : nodes::Stack {
  // The dialog it is shown in.
  [[nodiscard]] static dialog_look look_of_dialog() { return {.size = dialog_size::fixed{620.0f, 600.0f}}; }
  Actions* actions = nullptr;
  // The colours it is made in, for its parts and the rows it makes later.
  const palette* colours_ = nullptr;
  // What the window's parts tell the program: the pictures shown.
  ui_shared* shared_ = nullptr;
  std::optional<std::string> room;  // the room's packs, or one's own
  bool may_edit = true;
  std::vector<emote_pack> packs;  // as last listed
  emote_pack draft;               // the pack open, as it is edited
  bool open = false;              // a pack open, not the list
  bool new_pack = false;          // the one open not yet saved
  struct close_it {
    Actions* actions;
    void operator()() const { actions->close_packs(); }
  };
  struct back_press {
    packs_box* box;
    void operator()() const { box->show_list(); }
  };
  struct create_press {
    packs_box* box;
    void operator()() const { box->open_pack(std::nullopt); }
  };
  struct add_press {
    Actions* actions;
    void operator()() const { actions->pick_pack_images(); }
  };
  struct save_press {
    packs_box* box;
    void operator()() const { box->save(); }
  };
  struct delete_press {
    packs_box* box;
    void operator()() const { box->remove_pack(); }
  };
  struct flip_emoji {
    packs_box* box;
    void operator()() const {
      box->draft.emoji = !box->draft.emoji || !box->draft.sticker;
      box->show_use();
    }
  };
  struct flip_sticker {
    packs_box* box;
    void operator()() const {
      box->draft.sticker = !box->draft.sticker || !box->draft.emoji;
      box->show_use();
    }
  };
  // A pack in the list: its picture, its name, how many images and what
  // for; pressed, opened.
  struct pack_row : nodes::Stack {
    packs_box* box;
    std::size_t index;
    struct lines_t : two_lines {
      lines_t(const palette& colours, const emote_pack& one)
          : two_lines(colours, one.name.empty() ? std::string("Unnamed pack") : one.name,
                      std::format("{} image{} · {}", one.pictures.size(), one.pictures.size() == 1 ? "" : "s",
                                  one.emoji && one.sticker ? "Emoji and stickers"
                                  : one.emoji              ? "Emoji"
                                                           : "Stickers"),
                      14.0f, 2.0f) {}
    };
    struct parts_t {
      nodes::Image<from_avatars> face;
      lines_t lines;
    } parts;
    pack_row(packs_box* b, std::size_t i, const emote_pack& one)
        : box(b), index(i),
          parts{.face = nodes::Image<from_avatars>(
                    {one.avatar.value_or(one.pictures.empty() ? std::string() : one.pictures.front().url)}),
                .lines = lines_t(*b->colours_, one)} {
      this->setHorizontal();
      this->setGap(12.0f);
      fState.apply({.fillX = true, .height = 56.0f, .padding = {8.0f, 14.0f, 8.0f, 14.0f}, .cornerRadius = 8.0f,
                    .hoverBackground = b->colours_->chosen});
      parts.face.apply({.width = 40.0f, .height = 40.0f, .alignSelf = scene::align::kMiddle, .cornerRadius = 8.0f,
                        .background = b->colours_->tile});
      parts.face.keepBox();
    }
    [[nodiscard]] bool acceptsInput() const { return true; }
    [[nodiscard]] bool hoverChangesAppearance() const { return true; }
    [[nodiscard]] bool onClick(float, float) {
      box->open_pack(index);
      return true;
    }
  };
  // An image of the pack open: its picture, its shortcode to edit, its use,
  // and × to take it out.
  struct picture_row : nodes::Stack {
    struct renamed {
      packs_box* box;
      std::size_t index;
      void operator()(std::string_view text) const {
        if (index < box->draft.pictures.size())
          box->draft.pictures[index].shortcode = std::string(text);
      }
    };
    struct flip_its_emoji {
      packs_box* box;
      std::size_t index;
      void operator()() const { box->flip_picture(index, true); }
    };
    struct flip_its_sticker {
      packs_box* box;
      std::size_t index;
      void operator()() const { box->flip_picture(index, false); }
    };
    struct remove_it {
      packs_box* box;
      std::size_t index;
      void operator()() const { box->remove_picture(index); }
    };
    struct parts_t {
      nodes::Image<from_avatars> face;
      widgets::TextBox<renamed> shortcode;
      nodes::Text emoji_label;
      widgets::Toggle<flip_its_emoji> emoji;
      nodes::Text sticker_label;
      widgets::Toggle<flip_its_sticker> sticker;
      icon_button<remove_it> remove;
    } parts;
    picture_row(packs_box* box, std::size_t index, const pack_picture& one)
        : parts{.face = nodes::Image<from_avatars>({one.url}),
                .shortcode = widgets::TextBox<renamed>(box->colours_->widgets, "shortcode", {box, index}),
                .emoji_label = nodes::Text("Emoji", 12.0f, box->colours_->dim),
                .emoji = widgets::Toggle<flip_its_emoji>(box->colours_->widgets, {box, index}),
                .sticker_label = nodes::Text("Sticker", 12.0f, box->colours_->dim),
                .sticker = widgets::Toggle<flip_its_sticker>(box->colours_->widgets, {box, index}),
                .remove = icon_button<remove_it>(*box->colours_, icon::close{}, {box, index})} {
      this->setHorizontal();
      this->setGap(8.0f);
      fState.apply({.fillX = true, .height = 52.0f, .padding = {6.0f, 10.0f, 6.0f, 10.0f}});
      parts.face.apply({.width = 40.0f, .height = 40.0f, .alignSelf = scene::align::kMiddle, .cornerRadius = 6.0f,
                        .background = box->colours_->tile});
      parts.face.keepBox();
      parts.shortcode.setText(one.shortcode);
      parts.shortcode.apply({.height = 32.0f, .relativeSize = scene::axes::kNone, .grow = scene::axes::kX,
                             .alignSelf = scene::align::kMiddle});
      parts.emoji.setOnNow(one.emoji);
      parts.sticker.setOnNow(one.sticker);
      for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.emoji_label, &parts.emoji, &parts.sticker_label,
                                                                   &parts.sticker, &parts.remove})
        each->apply({.alignSelf = scene::align::kMiddle});
    }
  };
  struct use_row : nodes::Stack {
    struct parts_t {
      nodes::Text emoji_label;
      widgets::Toggle<flip_emoji> emoji;
      nodes::Text sticker_label;
      widgets::Toggle<flip_sticker> sticker;
    } parts;
    explicit use_row(packs_box* box)
        : parts{.emoji_label = nodes::Text("Use as emoji", 13.0f, box->colours_->text),
                .emoji = widgets::Toggle<flip_emoji>(box->colours_->widgets, {box}),
                .sticker_label = nodes::Text("Use as stickers", 13.0f, box->colours_->text),
                .sticker = widgets::Toggle<flip_sticker>(box->colours_->widgets, {box})} {
      this->setHorizontal();
      this->setGap(10.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {4.0f, 10.0f, 4.0f, 10.0f}});
      for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.emoji_label, &parts.emoji, &parts.sticker_label,
                                                                   &parts.sticker})
        each->apply({.alignSelf = scene::align::kMiddle});
      parts.sticker_label.apply({.margin = {0.0f, 0.0f, 0.0f, 14.0f}});
    }
  };
  struct list_buttons : nodes::Stack {
    struct parts_t {
      widgets::Button<create_press> create;
    } parts;
    explicit list_buttons(packs_box* box) : parts{.create = widgets::Button<create_press>(box->colours_->widgets, "Create pack", {box})} {
      this->setHorizontal();
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {6.0f, 10.0f, 0.0f, 10.0f}});
      parts.create.setPrimary(true);
      parts.create.apply({.width = 140.0f, .height = 34.0f});
    }
  };
  struct edit_buttons : nodes::Stack {
    struct parts_t {
      widgets::Button<add_press> add;
      nodes::Box<> gap{skia::colorSetARGB(0, 0, 0, 0)};
      widgets::Button<delete_press> remove;
      widgets::Button<back_press> back;
      widgets::Button<save_press> save;
    } parts;
    edit_buttons(Actions* a, packs_box* box)
        : parts{.add = widgets::Button<add_press>(box->colours_->widgets, "Add images", {a}),
                .remove = widgets::Button<delete_press>(box->colours_->widgets, "Delete pack", {box}),
                .back = widgets::Button<back_press>(box->colours_->widgets, "Back", {box}),
                .save = widgets::Button<save_press>(box->colours_->widgets, "Save", {box})} {
      this->setHorizontal();
      this->setGap(8.0f);
      fState.apply({.fillX = true, .autoSize = scene::axes::kY, .padding = {6.0f, 10.0f, 0.0f, 10.0f}});
      parts.gap.apply({.height = 1.0f, .grow = scene::axes::kX});
      parts.save.setPrimary(true);
      for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.add, &parts.remove, &parts.back, &parts.save})
        each->apply({.width = 110.0f, .height = 34.0f});
    }
  };
  using header_t = page_header<no_back, close_it>;
  using packs_t = nodes::Flow<std::vector<pack_row>>;
  using pictures_t = nodes::Flow<std::vector<picture_row>>;
  struct parts_t {
    header_t header;
    nodes::Text note;
    // The list.
    nodes::ScrollContainer<packs_t> list{packs_t({.spacingY = 0.0f, .wrap = false}, {})};
    list_buttons list_actions;
    // A pack open.
    field name;
    field attribution;
    use_row use;
    nodes::Text images_heading;
    nodes::ScrollContainer<pictures_t> pictures{pictures_t({.spacingY = 0.0f, .wrap = false}, {})};
    edit_buttons edit_actions;
  } parts;
  packs_box(Actions* a, const palette& colours, ui_shared& shared, std::optional<std::string> in, bool editable)
      : actions(a), colours_(&colours), shared_(&shared), room(std::move(in)), may_edit(editable),
        parts{.header = header_t(colours, "Emojis & Stickers", {}, {a}, false, true),
              .note = nodes::Text("", 13.0f, colours.dim),
              .list_actions = list_buttons(this),
              .name = field(colours, "Name", "Pack name"),
              .attribution = field(colours, "Attribution (optional)", "Where its images are from"),
              .use = use_row(this),
              .images_heading = nodes::Text("Images", 13.0f, colours.dim, true),
              .edit_actions = edit_buttons(a, this)} {
    this->setGap(8.0f);
    fState.apply({.fillX = true, .height = 600.0f, .padding = {0.0f, 12.0f, 16.0f, 12.0f}});
    parts.note.setWrapped(true);
    parts.note.apply({.fillX = true, .margin = {0.0f, 10.0f, 4.0f, 10.0f}});
    parts.list.apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(parts.list.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    parts.images_heading.apply({.margin = {6.0f, 10.0f, 0.0f, 10.0f}});
    parts.pictures.apply({.fillX = true, .grow = scene::axes::kY});
    std::get<0>(parts.pictures.fChildren).apply({.fillX = true, .autoSize = scene::axes::kY});
    parts.note.setText("Loading…");
    this->show_list();
  }
  // What the account listed: the packs, the list shown -- one's own pack
  // opened at once, as there is one.
  void show_packs(std::vector<emote_pack> listed) {
    packs = std::move(listed);
    if (!room && !packs.empty()) {
      this->open_pack(0);
      return;
    }
    this->show_list();
  }
  void show_list() {
    open = false;
    parts.header.parts.title.setText("Emojis & Stickers");
    parts.note.setText(room ? (packs.empty() ? std::string("This room has no packs yet.")
                                             : std::string("The packs of this room: their emoji and stickers are "
                                                           "there for everyone in it."))
                            : std::string("Your own pack: its emoji and stickers are yours in every chat."));
    auto& rows = std::get<0>(std::get<0>(parts.list.fChildren).fChildren);
    rows.clear();
    for (std::size_t i = 0; i < packs.size(); ++i)
      rows.emplace_back(this, i, packs[i]);
    this->show_page();
  }
  // A pack opened -- a new one where none is named -- to edit.
  void open_pack(std::optional<std::size_t> index) {
    new_pack = !index;
    draft = index && *index < packs.size() ? packs[*index] : emote_pack{.chat = room, .emoji = true, .sticker = true};
    open = true;
    parts.header.parts.title.setText(new_pack ? "New pack" : draft.name.empty() ? "Pack" : draft.name);
    parts.name.parts.box.setText(draft.name);
    parts.attribution.parts.box.setText(draft.attribution);
    parts.note.setText(may_edit ? std::string("Shortcodes are what the emoji are typed as, :like_this:.")
                                : std::string("You may not change this room's packs."));
    this->show_use();
    this->show_pictures();
    this->show_page();
  }
  void show_use() {
    parts.use.parts.emoji.setOn(draft.emoji);
    parts.use.parts.sticker.setOn(draft.sticker);
  }
  void show_pictures() {
    auto& rows = std::get<0>(std::get<0>(parts.pictures.fChildren).fChildren);
    rows.clear();
    for (std::size_t i = 0; i < draft.pictures.size(); ++i)
      rows.emplace_back(this, i, draft.pictures[i]);
    parts.pictures.invalidateLayout();
    shared_->pack_pictures_shown = std::ranges::to<std::vector>(std::views::transform(draft.pictures, &pack_picture::url));
  }
  // What shows: the list, or the pack open.
  void show_page() {
    parts.list.setVisible(!open);
    parts.list_actions.setVisible(!open && room.has_value() && may_edit);
    for (scene::Node* each : std::initializer_list<scene::Node*>{&parts.name, &parts.attribution, &parts.use,
                                                                 &parts.images_heading, &parts.pictures, &parts.edit_actions})
      each->setVisible(open);
    parts.edit_actions.parts.add.setVisible(may_edit);
    parts.edit_actions.parts.save.setVisible(may_edit);
    parts.edit_actions.parts.remove.setVisible(may_edit && room.has_value() && !new_pack);
    parts.edit_actions.parts.back.setVisible(room.has_value());
    this->invalidateLayout();
  }
  void flip_picture(std::size_t index, bool emoji) {
    if (index >= draft.pictures.size())
      return;
    pack_picture& one = draft.pictures[index];
    // Never neither: the one flipped off turns the other on.
    if (emoji)
      one.emoji = !one.emoji || !one.sticker;
    else
      one.sticker = !one.sticker || !one.emoji;
    auto& rows = std::get<0>(std::get<0>(parts.pictures.fChildren).fChildren);
    if (index < rows.size()) {
      rows[index].parts.emoji.setOn(one.emoji);
      rows[index].parts.sticker.setOn(one.sticker);
    }
  }
  void remove_picture(std::size_t index) {
    if (index >= draft.pictures.size())
      return;
    draft.pictures.erase(draft.pictures.begin() + static_cast<std::ptrdiff_t>(index));
    removing = true;
    scene::work::mark(fState.fId);
  }
  // Rows let go at the next frame, not from inside one of their buttons.
  bool removing = false;
  [[nodiscard]] bool wantsTick() const { return removing; }
  void update(double) {
    if (!removing)
      return;
    removing = false;
    this->show_pictures();
  }
  // An image uploaded for the pack open: in it, its shortcode from its
  // file's name.
  void add_picture(pack_picture one) {
    if (!open)
      return;
    std::string code = spl::bytes::key_text(one.shortcode);
    if (code.empty())
      code = "image";
    std::string unique = code;
    for (int n = 2; std::ranges::contains(draft.pictures, unique, &pack_picture::shortcode); ++n)
      unique = std::format("{}_{}", code, n);
    one.shortcode = unique;
    one.emoji = draft.emoji;
    one.sticker = draft.sticker;
    draft.pictures.push_back(std::move(one));
    this->show_pictures();
  }
  void save() {
    draft.name = parts.name.text();
    draft.attribution = parts.attribution.text();
    if (!draft.avatar && !draft.pictures.empty())
      draft.avatar = draft.pictures.front().url;
    std::erase_if(draft.pictures, [](const pack_picture& one) { return one.shortcode.empty() || one.url.empty(); });
    actions->save_pack(draft);
    parts.note.setText("Saving…");
  }
  // As its protocol takes a pack away; a new one, not saved yet, is nothing.
  void remove_pack() {
    if (draft.key.empty())
      return;
    actions->delete_pack(draft);
    parts.note.setText("Deleting…");
  }
};

}  // namespace mux::ui

// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.store: The messages, reads and drafts kept on disk.
export module mux.app.store;

import std;
import splice.bytes;
import knot;
import mux.vault;
import mux.core;
import mux.logic.room_events;
import mux.config;

export namespace mux::app {

// The store's files, as their lines say them: read and written by knot, each
// straight into its type.
namespace store_file {

// An attachment as a message's line keeps it.
struct attachment_line {
  bool image = false;
  std::optional<bool> moves;
  std::string source, name, mimetype;
  std::int64_t size = 0;
  std::optional<std::string> blurhash;
  std::int64_t w = 0, h = 0;
  std::optional<std::string> video;
  std::optional<std::int64_t> duration;
  friend consteval auto json_schema(knot::type<attachment_line>) { return knot::schema<attachment_line>(); }
};

// Where a forwarded message is from, as its line keeps it.
struct forward_line {
  std::string from;
  std::string name;
  std::string link;
  friend consteval auto json_schema(knot::type<forward_line>) { return knot::schema<forward_line>(); }
};

// A line of a chat's messages: a message, or only its id and that it is
// deleted or gone.
struct message_line {
  std::string id;
  std::optional<bool> gone;
  std::optional<bool> deleted;
  std::optional<std::string> sender;
  std::optional<std::int64_t> at;
  std::optional<std::string> plain;
  std::optional<std::string> html;
  std::optional<std::string> reply;
  std::optional<std::string> thread;
  std::optional<bool> edited;
  // Its edit history: what it said before each edit, and until when.
  struct version_line {
    std::optional<std::string> plain;
    std::optional<std::string> html;
    std::optional<std::int64_t> until;
    friend consteval auto json_schema(knot::type<version_line>) { return knot::schema<version_line>(); }
  };
  std::optional<std::vector<version_line>> versions;
  std::optional<bool> redacted;
  std::optional<bool> out;
  std::optional<bool> service;
  std::optional<std::string> kind;
  std::optional<attachment_line> attachment;
  std::optional<std::vector<attachment_line>> album;
  // Forwarded: who from, kept -- read back without it, a forward lost its
  // "Forwarded from" while its text stayed stripped of it.
  std::optional<forward_line> forwarded;
  std::optional<bool> sticker;
  // Came end-to-end encrypted: kept, or read back it said "not encrypted".
  std::optional<bool> encrypted;
  // And from a device its sender did not cross-sign.
  std::optional<bool> unverified;
  std::optional<bool> came_plain;
  std::optional<bool> unauthenticated;
  // Its reactions: each by its key and who sent it, and its own event and
  // when, where known -- read back from the disk, a message had none.
  struct reaction_line {
    std::string key;
    std::string who;
    std::optional<std::string> event;
    std::optional<std::int64_t> at;
    friend consteval auto json_schema(knot::type<reaction_line>) { return knot::schema<reaction_line>(); }
  };
  std::optional<std::vector<reaction_line>> reactions;
  friend consteval auto json_schema(knot::type<message_line>) { return knot::schema<message_line>(); }
};

// Who has read up to where in a chat, and the user.
struct reads_file {
  std::map<std::string, std::string> users;
  std::optional<std::string> me;
  friend consteval auto json_schema(knot::type<reads_file>) { return knot::schema<reads_file>(); }
};

// A flag as a line keeps it: there only when set.
constexpr std::optional<bool> flag(bool on) { return on ? std::optional<bool>(true) : std::nullopt; }

}  // namespace store_file

// ---- the messages kept on disk ---------------------------------------------
// Every message of every chat, a line of JSON each, appended as it comes, is
// sent, edited or goes: the last line for an id is what it is, a "gone" one
// that it is not. Memory holds the newest of the chats read lately -- the
// model's LRU, small -- and this holds all of them: what is scrolled back to
// comes from here before the server is asked. A file whose lines are mostly
// old versions of each other is written again with one line each.
class message_store {
 public:
  // What the files are read and written through: the program's.
  mux::vault::vault* vault = nullptr;
  using time_point = std::chrono::sys_time<std::chrono::milliseconds>;

  void record(const mux::message& one) {
    if (!one.id.empty())
      append(one.in, line_of(one));
  }
  // Who has read up to where in a chat, the user among them: one small file
  // beside its messages, written anew when it changes, read when the chat
  // is opened.
  void keep_reads(const mux::conversation_id& in, const mux::conversation& chat) {
    const store_file::reads_file all{.users = chat.read_by, .me = chat.read_up_to};
    const auto where = reads_file_of(in);
    std::error_code failed;
    std::filesystem::create_directories(where.parent_path(), failed);
    (void)vault->write_file(where, knot::to_json_string(all));
  }
  struct reads {
    std::map<std::string, std::string> read_by;
    std::optional<std::string> me;
  };
  [[nodiscard]] reads read_reads(const mux::conversation_id& in) const {
    reads out;
    const std::string text = vault->read_file(reads_file_of(in)).value_or(std::string());
    auto parsed = knot::try_read<store_file::reads_file>(std::string_view(text));
    if (!parsed)
      return out;
    out.read_by = std::move(parsed->users);
    out.me = std::move(parsed->me);
    return out;
  }
  // A message deleted that is held in memory: marked where it is kept, and
  // kept whole, as it was, in the archive of deleted messages, which the
  // cache's limit does not reach.
  void mark_deleted(const mux::conversation_id& in, const std::string& id, mux::message whole) {
    append(in, knot::to_json_string(store_file::message_line{.id = id, .deleted = true}));
    whole.redacted = true;
    const auto where = deleted_file_of(in);
    std::error_code failed;
    std::filesystem::create_directories(where.parent_path(), failed);
    {
      std::lock_guard held(file_lock());
      (void)vault->append_line(where, line_of(whole));
    }
    prune(mux::config::state_path("deleted"), deleted_budget, where);
  }
  // Messages deleted that are not held in memory -- older than what is, or
  // told again from the sync kept at every start: each found in the chat's
  // file, marked there and kept whole in the archive, the file and the
  // archive read once for all of them. What is marked and archived already
  // is passed over, and what the file does not have is not marked. From any
  // thread: a worker's -- read on the UI's, once for each deletion, the
  // whole file of a big room was parsed again and again, and a start with
  // many deletions in what came stood still for minutes.
  void mark_deleted_on_disk(const mux::conversation_id& in, const std::vector<std::string>& ids) {
    std::map<std::string, mux::message> kept;
    read_lines(file_of(in), in, kept);
    std::map<std::string, mux::message> archived;
    read_lines(deleted_file_of(in), in, archived);
    const auto where = deleted_file_of(in);
    bool archiving = false;
    for (const std::string& id : ids) {
      const auto found = kept.find(id);
      if (found == kept.end())
        continue;
      if (!found->second.redacted) {
        append(in, knot::to_json_string(store_file::message_line{.id = id, .deleted = true}));
        found->second.redacted = true;
      }
      if (archived.contains(id))
        continue;
      if (!archiving) {
        std::error_code failed;
        std::filesystem::create_directories(where.parent_path(), failed);
        archiving = true;
      }
      std::lock_guard held(file_lock());
      (void)vault->append_line(where, line_of(found->second));
      archived.insert_or_assign(id, found->second);
    }
    if (archiving)
      prune(mux::config::state_path("deleted"), deleted_budget, where);
  }
  void forget(const mux::conversation_id& in, const std::string& id) {
    append(in, knot::to_json_string(store_file::message_line{.id = id, .gone = true}));
  }

  // Where a chat's history on disk has gaps: before each message named
  // here, the older history is not on disk (or not known to be) -- what
  // to ask the server for it with, or the room's beginning. A message not
  // named follows the one before it on disk with nothing missing between.
  struct gap_mark {
    std::optional<std::string> token;  // the server's token, to page back from
    bool start = false;                // the room's beginning: nothing older
    friend consteval auto json_schema(knot::type<gap_mark>) { return knot::schema<gap_mark>(); }
  };
  using gaps_t = std::map<std::string, gap_mark>;
  static std::filesystem::path gaps_file_of(const mux::conversation_id& in) { return kept_of("messages", in, ".gaps.json"); }
  // Nothing where no file was kept: the history before gaps were kept is
  // not known to be whole.
  std::optional<gaps_t> gaps(const mux::conversation_id& in) const {
    auto text_read = spl::bytes::file_text(gaps_file_of(in));
    if (!text_read)
      return std::nullopt;
    const std::string text = std::move(*text_read);
    auto read = knot::try_read<gaps_t>(std::string_view(text));
    return read ? std::optional<gaps_t>(std::move(*read)) : std::optional<gaps_t>(gaps_t{});
  }
  void keep_gaps(const mux::conversation_id& in, const gaps_t& all) const {
    const auto where = gaps_file_of(in);
    std::error_code failed;
    std::filesystem::create_directories(where.parent_path(), failed);
    std::lock_guard held(file_lock());
    std::ofstream(where, std::ios::binary | std::ios::trunc) << knot::to_json_string(all);
  }

  // A marked message -- one mentioning this user, one of theirs reacted to --
  // kept apart as the mark is made, and never pruned with the history: the
  // list of marks shows it whatever else was let go, and needs nothing from
  // the server. From any thread.
  void keep_marked(const mux::conversation_id& in, const mux::message& one) const {
    const auto where = kept_of("marked", in, ".jsonl");
    std::error_code failed;
    std::filesystem::create_directories(where.parent_path(), failed);
    std::lock_guard held(file_lock());
    std::ofstream(where, std::ios::binary | std::ios::app) << line_of(one) << '\n';
  }
  std::map<std::string, mux::message> marked(const mux::conversation_id& in) const {
    std::map<std::string, mux::message> all;
    read_lines(kept_of("marked", in, ".jsonl"), in, all);
    return all;
  }

  // All of a chat's messages kept, by id. From any thread: a worker's.
  std::map<std::string, mux::message> everything(const mux::conversation_id& in) const {
    auto all = read(in);
    return {std::make_move_iterator(all.begin()), std::make_move_iterator(all.end())};
  }

  // Up to `count` of a chat's messages from before `before`, oldest first.
  // Reading a chat's file counts as using it. From any thread: a worker's.
  std::vector<mux::message> older(const mux::conversation_id& in, time_point before, std::size_t count) const {
    std::error_code failed;
    std::filesystem::last_write_time(file_of(in), std::filesystem::file_time_type::clock::now(), failed);
    auto all = read(in);
    std::vector<mux::message> out;
    for (auto& [id, one] : all)
      if (one.at < before)
        out.push_back(std::move(one));
    std::ranges::sort(out, {}, &mux::message::at);
    if (out.size() > count)
      out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(count));
    return out;
  }

 private:
  // A chat's file in a folder: named one to one by its account and its id
  // -- what was kept under the names before moved there.
  static std::filesystem::path kept_of(std::string_view folder, const mux::conversation_id& in, std::string_view ending) {
    const auto root = mux::config::state_path(std::string(folder));
    return mux::config::moved_from(
        root / mux::config::file_name_of(in.account.address) / (mux::config::file_name_of(in.id) + std::string(ending)),
        root / mux::config::old_file_name_of(in.account.address) / (mux::config::old_file_name_of(in.id) + std::string(ending)));
  }
  static std::filesystem::path reads_file_of(const mux::conversation_id& in) { return kept_of("messages", in, ".reads.json"); }
  static std::filesystem::path deleted_file_of(const mux::conversation_id& in) { return kept_of("deleted", in, ".jsonl"); }
  static std::filesystem::path file_of(const mux::conversation_id& in) { return kept_of("messages", in, ".jsonl"); }
  // The files are read on workers and written on the UI's thread: a line
  // written, and a file written again whole, hold this.
  std::mutex& file_lock() const { return lock_; }
  mutable std::mutex lock_;
  void append(const mux::conversation_id& in, const std::string& line) {
    const auto where = file_of(in);
    std::error_code failed;
    std::filesystem::create_directories(where.parent_path(), failed);
    {
      std::lock_guard held(file_lock());
      (void)vault->append_line(where, line);
    }
    if (++appended_ % 500 == 1)
      prune(mux::config::state_path("messages"), budget, where);
  }

 public:
  // The files held to this size in all; set from Storage.
  std::uintmax_t budget = 512u << 20;
  // The deleted messages' archive held to this size; set from Storage.
  std::uintmax_t deleted_budget = 256u << 20;

 private:
  // What is under `dir` held to `cap`: the chats used longest ago -- read
  // or written -- go first, whole; `keep`, just written, never does.
  void prune(const std::filesystem::path& dir, std::uintmax_t cap, const std::filesystem::path& keep) {
    const std::uintmax_t kDiskBudget = cap;
    std::error_code failed;
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> files;
    std::uintmax_t total = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, failed)) {
      if (!entry.is_regular_file(failed))
        continue;
      total += entry.file_size(failed);
      files.emplace_back(entry.last_write_time(failed), entry.path());
    }
    if (total <= kDiskBudget)
      return;
    std::ranges::sort(files);
    for (const auto& [when, path] : files) {
      if (total <= kDiskBudget)
        break;
      if (path == keep)
        continue;
      const auto size = std::filesystem::file_size(path, failed);
      if (std::filesystem::remove(path, failed))
        total -= size;
    }
  }
  std::atomic<std::size_t> appended_ = 0;
  // The chat's messages as its file says, each as its last line says; the
  // file written again with one line each where most of its lines were old.
  // The chat's messages as its file says, each as its last line says -- the
  // file written again with one line each where most of its lines were old
  // -- and what was deleted in it, from the archive apart.
  std::map<std::string, mux::message> read(const mux::conversation_id& in) const {
    std::map<std::string, mux::message> all;
    const auto where = file_of(in);
    std::error_code sized;
    const auto size_read = std::filesystem::file_size(where, sized);
    const std::size_t lines = read_lines(where, in, all);
    if (lines > 2 * all.size() + 64) {
      // Mostly old versions: one line each, oldest first.
      std::vector<const mux::message*> order;
      for (const auto& [id, one] : all)
        order.push_back(&one);
      std::ranges::sort(order, {}, [](const mux::message* one) { return one->at; });
      const auto fresh = std::filesystem::path(where.string() + ".new");
      // Written and put in its place with the vault's other reads and writes
      // waiting (the file's lock first, as an append takes them): a re-seal
      // meanwhile would leave it under a key gone.
      std::lock_guard held(file_lock());
      vault->exclusive([&] {
        {
          std::ofstream out(fresh, std::ios::binary | std::ios::trunc);
          for (const mux::message* one : order)
            out << vault->line_of(line_of(*one), where) << '\n';
        }
        // Put in its place only where nothing was written to it meanwhile:
        // a line written since the read would be lost.
        std::error_code failed;
        if (std::filesystem::file_size(where, failed) == size_read && !sized && !failed)
          std::filesystem::rename(fresh, where, failed);
        else
          std::filesystem::remove(fresh, failed);
      });
    }
    read_lines(deleted_file_of(in), in, all);
    return all;
  }
  // One file's lines, into `all`: how many there were.
  std::size_t read_lines(const std::filesystem::path& where, const mux::conversation_id& in,
                                std::map<std::string, mux::message>& all) const {
    std::ifstream file(where, std::ios::binary);
    std::string text;
    std::size_t lines = 0;
    while (std::getline(file, text)) {
      ++lines;
      // Sealed where local data is encrypted: a line that cannot be opened
      // is passed over, as one that cannot be read is.
      const auto opened = vault->open_line(text, where);
      if (!opened)
        continue;
      auto parsed = knot::try_read<store_file::message_line>(std::string_view(*opened));
      if (!parsed)
        continue;
      auto& o = *parsed;
      if (o.gone.value_or(false)) {
        all.erase(o.id);
        continue;
      }
      if (o.deleted.value_or(false)) {
        if (const auto found = all.find(o.id); found != all.end())
          found->second.redacted = true;
        continue;
      }
      mux::message one;
      one.in = in;
      one.id = o.id;
      one.sender = o.sender.value_or("");
      if (o.at)
        one.at = time_point(std::chrono::milliseconds(*o.at));
      one.body.plain = o.plain.value_or("");
      one.body.html = std::move(o.html);
      one.replies_to = std::move(o.reply);
      one.thread = std::move(o.thread);
      one.edited = o.edited.value_or(false);
      one.versions = std::ranges::to<std::vector>(std::views::transform(o.versions.value_or(std::vector<store_file::message_line::version_line>{}), [&](const store_file::message_line::version_line& v) {
                       return mux::message::version{mux::body{v.plain.value_or(""), v.html},
                                                    time_point(std::chrono::milliseconds(v.until.value_or(0)))};
                     }));
      one.encrypted = o.encrypted.value_or(false);
      one.unverified = o.unverified.value_or(false);
      one.came_plain = o.came_plain.value_or(false);
      one.unauthenticated = o.unauthenticated.value_or(false);
      one.redacted = o.redacted.value_or(false);
      one.outgoing = o.out.value_or(false);
      one.service = o.service.value_or(false);
      if (o.kind)
        one.event_kind = mux::logic::room_event_of(*o.kind);
      if (o.attachment)
        one.attachment = attachment_of(*o.attachment);
      one.sticker = o.sticker.value_or(false);
      for (const auto& reaction : o.reactions.value_or(std::vector<store_file::message_line::reaction_line>{})) {
        one.reactions[reaction.key].insert(reaction.who);
        if (reaction.event)
          one.reaction_events.push_back({*reaction.event, reaction.key, reaction.who,
                                         std::chrono::sys_time<std::chrono::milliseconds>(
                                             std::chrono::milliseconds(reaction.at.value_or(0)))});
      }
      if (o.forwarded)
        one.forwarded = mux::forward_info{.from = std::move(o.forwarded->from), .name = std::move(o.forwarded->name),
                                          .link = std::move(o.forwarded->link)};
      // A gallery's pictures, each as an attachment is.
      if (o.album)
        for (const auto& each : *o.album)
          one.album.push_back(attachment_of(each));
      all.insert_or_assign(o.id, std::move(one));
    }
    return lines;
  }
  // An attachment as a line keeps it, and read back.
  static mux::attachment attachment_of(const store_file::attachment_line& c) {
    mux::attachment a;
    if (c.image)
      a.kind = mux::attachment_kind::image{.moves = c.moves.value_or(false)};
    a.source = c.source;
    a.name = c.name;
    a.mimetype = c.mimetype;
    a.blurhash = c.blurhash;
    a.size = c.size;
    a.width = static_cast<int>(c.w);
    a.height = static_cast<int>(c.h);
    a.video = c.video;
    a.duration_ms = c.duration.value_or(0);
    return a;
  }
  static store_file::attachment_line line_of(const mux::attachment& a) {
    return {.image = mux::is_picture(a.kind),
            .moves = store_file::flag(mux::moves(a.kind)),
            .source = a.source,
            .name = a.name,
            .mimetype = a.mimetype,
            .size = a.size,
            .blurhash = a.blurhash,
            .w = a.width,
            .h = a.height,
            .video = a.video,
            .duration = a.video ? std::optional<std::int64_t>(a.duration_ms) : std::nullopt};
  }
  static std::string line_of(const mux::message& one) {
    store_file::message_line line{
        .id = one.id,
        .sender = one.sender,
        .at = static_cast<std::int64_t>(one.at.time_since_epoch().count()),
        .plain = one.body.plain,
        .html = one.body.html,
        .reply = one.replies_to,
        .thread = one.thread,
        .edited = store_file::flag(one.edited),
        .versions = one.versions.empty()
                        ? std::nullopt
                        : std::optional(std::ranges::to<std::vector>(std::views::transform(one.versions, [](const mux::message::version& v) {
                                          return store_file::message_line::version_line{
                                              v.body.plain, v.body.html, static_cast<std::int64_t>(v.until.time_since_epoch().count())};
                                        }))),
        .redacted = store_file::flag(one.redacted),
        .out = store_file::flag(one.outgoing),
        // Something done, not said: read back as a line of its own again,
        // with which kind of room event, for which are shown.
        .service = store_file::flag(one.service),
        .kind = one.service ? std::optional<std::string>(mux::logic::word_of(one.event_kind)) : std::nullopt,
        .attachment = one.attachment ? std::optional(line_of(*one.attachment)) : std::nullopt,
        .forwarded = one.forwarded ? std::optional(store_file::forward_line{one.forwarded->from, one.forwarded->name,
                                                                              one.forwarded->link})
                                   : std::nullopt,
        .sticker = store_file::flag(one.sticker),
        .encrypted = store_file::flag(one.encrypted),
        .unverified = store_file::flag(one.unverified),
        .came_plain = store_file::flag(one.came_plain),
        .unauthenticated = store_file::flag(one.unauthenticated),
    };
    if (!one.album.empty()) {
      line.album.emplace();
      for (const mux::attachment& each : one.album)
        line.album->push_back(line_of(each));
    }
    // Every reaction: who, under which key -- with its event where it is
    // known, which is what taking it back or a mark on it needs.
    for (const auto& [key, who] : one.reactions)
      for (const std::string& person : who) {
        const auto known = std::ranges::find_if(one.reaction_events, [&](const mux::message::reaction_event& each) {
          return each.key == key && each.who == person;
        });
        auto& kept = line.reactions ? *line.reactions : line.reactions.emplace();
        kept.push_back({.key = key,
                        .who = person,
                        .event = known != one.reaction_events.end() ? std::optional(known->event) : std::nullopt,
                        .at = known != one.reaction_events.end()
                                  ? std::optional(static_cast<std::int64_t>(known->at.time_since_epoch().count()))
                                  : std::nullopt});
      }
    return knot::to_json_string(line);
  }
};

}  // namespace mux::app

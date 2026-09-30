// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.store: The messages, reads and drafts kept on disk.
export module mux.app.store;

import std;
import knot;
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
};
consteval auto json_schema(knot::type<attachment_line>) { return knot::schema<attachment_line>(); }

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
  std::optional<bool> edited;
  std::optional<bool> redacted;
  std::optional<bool> out;
  std::optional<bool> service;
  std::optional<std::string> kind;
  std::optional<attachment_line> attachment;
  std::optional<std::vector<attachment_line>> album;
};
consteval auto json_schema(knot::type<message_line>) { return knot::schema<message_line>(); }

// Who has read up to where in a chat, and the user.
struct reads_file {
  std::map<std::string, std::string> users;
  std::optional<std::string> me;
};
consteval auto json_schema(knot::type<reads_file>) { return knot::schema<reads_file>(); }

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
    std::ofstream(where, std::ios::binary | std::ios::trunc) << knot::to_json_string(all);
  }
  struct reads {
    std::map<std::string, std::string> read_by;
    std::optional<std::string> me;
  };
  [[nodiscard]] static reads read_reads(const mux::conversation_id& in) {
    reads out;
    std::ifstream file(reads_file_of(in), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto parsed = knot::try_read<store_file::reads_file>(std::string_view(text));
    if (!parsed)
      return out;
    out.read_by = std::move(parsed->users);
    out.me = std::move(parsed->me);
    return out;
  }
  // A message deleted: marked where it is kept, and kept whole in the
  // archive of deleted messages, which the cache's limit does not reach --
  // as it was where that is given, else as the disk has it.
  void mark_deleted(const mux::conversation_id& in, const std::string& id, std::optional<mux::message> whole) {
    append(in, knot::to_json_string(store_file::message_line{.id = id, .deleted = true}));
    if (!whole) {
      auto all = read(in);
      if (const auto found = all.find(id); found != all.end())
        whole = std::move(found->second);
    }
    if (!whole)
      return;
    whole->redacted = true;
    const auto where = deleted_file_of(in);
    std::error_code failed;
    std::filesystem::create_directories(where.parent_path(), failed);
    {
      std::lock_guard held(file_lock());
      std::ofstream(where, std::ios::binary | std::ios::app) << line_of(*whole) << '\n';
    }
    prune(mux::config::state_path("deleted"), deleted_budget, where);
  }
  void forget(const mux::conversation_id& in, const std::string& id) {
    append(in, knot::to_json_string(store_file::message_line{.id = id, .gone = true}));
  }

  // All of a chat's messages kept, by id. From any thread: a worker's.
  static std::map<std::string, mux::message> everything(const mux::conversation_id& in) {
    auto all = read(in);
    return {std::make_move_iterator(all.begin()), std::make_move_iterator(all.end())};
  }

  // Up to `count` of a chat's messages from before `before`, oldest first.
  // Reading a chat's file counts as using it. From any thread: a worker's.
  static std::vector<mux::message> older(const mux::conversation_id& in, time_point before, std::size_t count) {
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
  static std::string safe(std::string_view name) {
    std::string out;
    for (const char c : name)
      out += std::isalnum(static_cast<unsigned char>(c)) || c == '@' || c == '.' || c == '-' ? c : '_';
    return out;
  }
  static std::filesystem::path reads_file_of(const mux::conversation_id& in) {
    return mux::config::state_path("messages") / safe(in.account.address) / (safe(in.id) + ".reads.json");
  }
  static std::filesystem::path deleted_file_of(const mux::conversation_id& in) {
    return mux::config::state_path("deleted") / safe(in.account.address) / (safe(in.id) + ".jsonl");
  }
  static std::filesystem::path file_of(const mux::conversation_id& in) {
    return mux::config::state_path("messages") / safe(in.account.address) / (safe(in.id) + ".jsonl");
  }
  // The files are read on workers and written on the UI's thread: a line
  // written, and a file written again whole, hold this.
  static std::mutex& file_lock() {
    static std::mutex held;
    return held;
  }
  void append(const mux::conversation_id& in, const std::string& line) {
    const auto where = file_of(in);
    std::error_code failed;
    std::filesystem::create_directories(where.parent_path(), failed);
    {
      std::lock_guard held(file_lock());
      std::ofstream(where, std::ios::binary | std::ios::app) << line << '\n';
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
  static void prune(const std::filesystem::path& dir, std::uintmax_t cap, const std::filesystem::path& keep) {
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
  std::size_t appended_ = 0;
  // The chat's messages as its file says, each as its last line says; the
  // file written again with one line each where most of its lines were old.
  // The chat's messages as its file says, each as its last line says -- the
  // file written again with one line each where most of its lines were old
  // -- and what was deleted in it, from the archive apart.
  static std::map<std::string, mux::message> read(const mux::conversation_id& in) {
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
      {
        std::ofstream out(fresh, std::ios::binary | std::ios::trunc);
        for (const mux::message* one : order)
          out << line_of(*one) << '\n';
      }
      // Put in its place only where nothing was written to it meanwhile:
      // a line written since the read would be lost.
      std::lock_guard held(file_lock());
      std::error_code failed;
      if (std::filesystem::file_size(where, failed) == size_read && !sized && !failed)
        std::filesystem::rename(fresh, where, failed);
      else
        std::filesystem::remove(fresh, failed);
    }
    read_lines(deleted_file_of(in), in, all);
    return all;
  }
  // One file's lines, into `all`: how many there were.
  static std::size_t read_lines(const std::filesystem::path& where, const mux::conversation_id& in,
                                std::map<std::string, mux::message>& all) {
    std::ifstream file(where, std::ios::binary);
    std::string text;
    std::size_t lines = 0;
    while (std::getline(file, text)) {
      ++lines;
      auto parsed = knot::try_read<store_file::message_line>(std::string_view(text));
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
      one.edited = o.edited.value_or(false);
      one.redacted = o.redacted.value_or(false);
      one.outgoing = o.out.value_or(false);
      one.service = o.service.value_or(false);
      if (o.kind)
        one.event_kind = mux::logic::room_event_of(*o.kind);
      if (o.attachment)
        one.attachment = attachment_of(*o.attachment);
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
        .edited = store_file::flag(one.edited),
        .redacted = store_file::flag(one.redacted),
        .out = store_file::flag(one.outgoing),
        // Something done, not said: read back as a line of its own again,
        // with which kind of room event, for which are shown.
        .service = store_file::flag(one.service),
        .kind = one.service ? std::optional<std::string>(mux::logic::word_of(one.event_kind)) : std::nullopt,
        .attachment = one.attachment ? std::optional(line_of(*one.attachment)) : std::nullopt,
    };
    if (!one.album.empty()) {
      line.album.emplace();
      for (const mux::attachment& each : one.album)
        line.album->push_back(line_of(each));
    }
    return knot::to_json_string(line);
  }
};

}  // namespace mux::app

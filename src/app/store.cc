// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.store: The messages, reads and drafts kept on disk.
export module mux.app.store;

import std;
import splice;
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
  // Where it stands, where that is not its time: what came live after what
  // was there, whatever its sender's clock said -- kept, or a restart put it
  // back by its time.
  std::optional<std::int64_t> order;
  std::optional<std::string> plain;
  std::optional<std::string> html;
  std::optional<std::string> reply;
  std::optional<std::string> thread;
  std::optional<bool> edited;
  std::optional<std::string> latest_edit_event;
  // Its edit history: what it said before each edit, and until when.
  struct version_line {
    std::optional<std::string> plain;
    std::optional<std::string> html;
    std::optional<std::int64_t> until;
    std::optional<std::string> event;
    friend consteval auto json_schema(knot::type<version_line>) { return knot::schema<version_line>(); }
  };
  std::optional<std::vector<version_line>> versions;
  std::optional<bool> redacted;
  std::optional<std::string> redacted_by;
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
    std::optional<std::string> shortcode;
    friend consteval auto json_schema(knot::type<reaction_line>) { return knot::schema<reaction_line>(); }
  };
  std::optional<std::vector<reaction_line>> reactions;
  friend consteval auto json_schema(knot::type<message_line>) { return knot::schema<message_line>(); }
};

// Who has read up to where in a chat, and the user.
struct reads_file {
  std::map<std::string, std::string> users;
  std::optional<std::string> me;
  std::optional<std::map<std::string, std::int64_t>> times;
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

  // Its writer started: every line, reads file and compaction written on a
  // thread of its own, in the order asked. The window's thread only asks --
  // writing itself, it waited on the files' and the vault's locks for every
  // message kept, while a worker made a big chat's file compact or the
  // network wrote the encryption store, and Android said the app was not
  // responding, again and again while history was paged back (a tester's).
  message_store() : writer_([this](std::stop_token stop) { this->write_queued(stop); }) {}
  message_store(const message_store&) = delete;
  message_store& operator=(const message_store&) = delete;

  void record(const mux::message& one) {
    if (!one.id.empty())
      append(one.in, line_of(one));
  }
  // Who has read up to where in a chat, the user among them: one small file
  // beside its messages, written anew when it changes, read when the chat
  // is opened.
  void keep_reads(const mux::conversation_id& in, const mux::conversation& chat) {
    std::map<std::string, std::int64_t> times;
    for (const auto& [user, when] : chat.receipt_times)
      times.emplace(user, when.time_since_epoch().count());
    const store_file::reads_file all{.users = chat.read_by, .me = chat.read_up_to, .times = std::move(times)};
    {
      std::lock_guard held(queue_lock_);
      pending_reads_.insert_or_assign(in, knot::to_json_string(all));
    }
    flush_reads(true);
  }
  // The reads waiting written, by the writer: one that failed kept, for a
  // later attempt -- once a second at most, unless `force`.
  void flush_reads(bool force = false) {
    const auto now = std::chrono::steady_clock::now();
    if (!force && now < reads_retry_at_)
      return;
    reads_retry_at_ = now + std::chrono::seconds(1);
    {
      std::lock_guard held(queue_lock_);
      if (pending_reads_.empty())
        return;
    }
    this->queue(queued::reads{});
  }
  struct reads {
    std::map<std::string, std::string> read_by;
    std::optional<std::string> me;
    std::map<std::string, std::chrono::sys_time<std::chrono::milliseconds>> times;
  };
  [[nodiscard]] reads read_reads(const mux::conversation_id& in) const {
    reads out;
    std::optional<std::string> waiting;
    {
      std::lock_guard held(queue_lock_);
      if (const auto pending = pending_reads_.find(in); pending != pending_reads_.end())
        waiting = pending->second;
    }
    const std::string text = waiting ? std::move(*waiting) : vault->read_file(reads_file_of(in)).value_or(std::string());
    auto parsed = knot::try_read<store_file::reads_file>(std::string_view(text));
    if (!parsed)
      return out;
    out.read_by = std::move(parsed->users);
    out.me = std::move(parsed->me);
    if (parsed->times)
      for (const auto& [user, when] : *parsed->times)
        out.times.emplace(user, std::chrono::sys_time<std::chrono::milliseconds>{std::chrono::milliseconds{when}});
    return out;
  }
  // A message deleted that is held in memory: marked where it is kept, and
  // kept whole, as it was, in the archive of deleted messages, which the
  // cache's limit does not reach.
  void mark_deleted(const mux::conversation_id& in, const std::string& id, mux::message whole) {
    append(in, knot::to_json_string(store_file::message_line{.id = id, .deleted = true}));
    whole.redacted = true;
    this->queue(queued::line{.where = deleted_file_of(in), .text = line_of(whole), .archive = true});
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
    this->waiting_into(file_of(in), in, kept);
    std::map<std::string, mux::message> archived;
    read_lines(deleted_file_of(in), in, archived);
    this->waiting_into(deleted_file_of(in), in, archived);
    const auto where = deleted_file_of(in);
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
      this->queue(queued::line{.where = where, .text = line_of(found->second), .archive = true});
      archived.insert_or_assign(id, found->second);
    }
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
    this->queue(queued::line{.where = file_of(in), .text = line, .archive = false});
  }

  // What the writer is asked, in order: a line for a chat's file or for the
  // archive of what was deleted; the reads waiting; a chat's file made
  // compact.
  struct queued {
    struct line {
      std::filesystem::path where;
      std::string text;
      bool archive = false;
    };
    struct reads {};
    struct compact {
      std::filesystem::path where;
      mux::conversation_id in;
    };
    using one = spl::variant<line, reads, compact>;
  };
  void queue(queued::one job) const {
    {
      std::lock_guard held(queue_lock_);
      queue_.push_back(std::move(job));
    }
    queue_ready_.notify_one();
  }
  // The writer: each job done, then let go of -- a line stays in the queue
  // until it is written, so that a read meanwhile finds it there or in the
  // file (waiting_into). Asked to stop, what is queued is written first.
  void write_queued(std::stop_token stop) {
    while (true) {
      std::optional<queued::one> job;
      {
        std::unique_lock held(queue_lock_);
        if (!queue_ready_.wait(held, stop, [&] { return !queue_.empty(); }))
          return;  // asked to stop, and nothing left
        job.emplace(queue_.front());
      }
      spl::visit(spl::overloaded{[&](const queued::line& one) { this->write_line(one); },
                                 [&](const queued::reads&) { this->write_reads(); },
                                 [&](const queued::compact& one) { this->write_compact(one.where, one.in); }},
                 *job);
      {
        std::lock_guard held(queue_lock_);
        queue_.pop_front();
      }
    }
  }
  void write_line(const queued::line& one) {
    if (vault == nullptr)
      return;
    std::error_code failed;
    std::filesystem::create_directories(one.where.parent_path(), failed);
    {
      std::lock_guard held(file_lock());
      (void)vault->append_line(one.where, one.text);
    }
    if (one.archive)
      prune(mux::config::state_path("deleted"), deleted_budget, one.where);
    else if (++appended_ % 500 == 1)
      prune(mux::config::state_path("messages"), budget, one.where);
  }
  void write_reads() {
    if (vault == nullptr)
      return;
    std::map<mux::conversation_id, std::string> now;
    {
      std::lock_guard held(queue_lock_);
      now = pending_reads_;
    }
    for (const auto& [in, text] : now) {
      const auto where = reads_file_of(in);
      std::error_code failed;
      std::filesystem::create_directories(where.parent_path(), failed);
      if (failed || !vault->write_file(where, text))
        continue;
      // Written: let go of, unless a newer one came meanwhile.
      std::lock_guard held(queue_lock_);
      if (const auto still = pending_reads_.find(in); still != pending_reads_.end() && still->second == text)
        pending_reads_.erase(still);
    }
  }
  // A chat's file written again with one line each, oldest first: its lines
  // sealed without the vault's lock held -- a big chat's took seconds, and
  // every read and write of the program waited -- and the file put in its
  // place under it, only where nothing was written to it since and what
  // seals is what it was (vault::generation): else let go.
  void write_compact(const std::filesystem::path& where, const mux::conversation_id& in) {
    {
      std::lock_guard held(queue_lock_);
      compacting_.erase(where);
    }
    if (vault == nullptr)
      return;
    std::error_code sized;
    const auto size_read = std::filesystem::file_size(where, sized);
    const auto sealed_under = vault->generation();
    std::map<std::string, mux::message> all;
    const std::size_t lines = read_lines(where, in, all);
    if (sized || lines <= 2 * all.size() + 64)
      return;
    std::vector<const mux::message*> order;
    for (const auto& [id, one] : all)
      order.push_back(&one);
    std::ranges::sort(order, {}, [](const mux::message* one) { return one->at; });
    const auto fresh = std::filesystem::path(where.string() + ".new");
    {
      std::ofstream out(fresh, std::ios::binary | std::ios::trunc);
      for (const mux::message* one : order)
        out << vault->line_of(line_of(*one), where) << '\n';
    }
    std::lock_guard held(file_lock());
    vault->exclusive([&] {
      std::error_code failed;
      if (std::filesystem::file_size(where, failed) == size_read && !failed && vault->generation() == sealed_under)
        std::filesystem::rename(fresh, where, failed);
      else
        std::filesystem::remove(fresh, failed);
    });
  }
  // The lines for `where` still waiting to be written, read into `all`
  // after the file's: they are newer than what it has.
  void waiting_into(const std::filesystem::path& where, const mux::conversation_id& in, std::map<std::string, mux::message>& all) const {
    std::vector<std::string> waiting;
    {
      std::lock_guard held(queue_lock_);
      for (const queued::one& job : queue_)
        spl::visit(spl::overloaded{[&](const queued::line& one) {
                                     if (one.where == where)
                                       waiting.push_back(one.text);
                                   },
                                   [](const auto&) {}},
                   job);
    }
    for (const std::string& text : waiting)
      this->take_line(text, in, all);
  }

 public:
  // The files held to this size in all; set from Storage.
  std::uintmax_t budget = 512u << 20;
  // The deleted messages' archive held to this size; set from Storage.
  std::uintmax_t deleted_budget = 256u << 20;

 private:
  // History held to `cap`: the chats used longest ago go first, whole.
  // Read markers and gaps survive eviction; `keep`, just written, stays.
  void prune(const std::filesystem::path& dir, std::uintmax_t cap, const std::filesystem::path& keep) {
    const std::uintmax_t kDiskBudget = cap;
    std::error_code failed;
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> files;
    std::uintmax_t total = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, failed)) {
      if (!entry.is_regular_file(failed) || entry.path().extension() != ".jsonl")
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
  // What the writer is asked, and the reads waiting: from any thread.
  mutable std::mutex queue_lock_;
  mutable std::condition_variable_any queue_ready_;
  mutable std::deque<queued::one> queue_;
  mutable std::set<std::filesystem::path> compacting_;
  std::map<mux::conversation_id, std::string> pending_reads_;
  std::chrono::steady_clock::time_point reads_retry_at_{};
  std::atomic<std::size_t> appended_ = 0;
  // The chat's messages as its file says, each as its last line says; the
  // file written again with one line each where most of its lines were old.
  // The chat's messages as its file says, each as its last line says -- the
  // file written again with one line each where most of its lines were old
  // -- and what was deleted in it, from the archive apart.
  std::map<std::string, mux::message> read(const mux::conversation_id& in) const {
    std::map<std::string, mux::message> all;
    const auto where = file_of(in);
    const std::size_t lines = read_lines(where, in, all);
    // Mostly old versions: made compact by the writer, once, not here --
    // a read on the window's thread did it there, every read and write of
    // the program waiting on it.
    if (lines > 2 * all.size() + 64) {
      bool asked = false;
      {
        std::lock_guard held(queue_lock_);
        asked = !compacting_.insert(where).second;
      }
      if (!asked)
        this->queue(queued::compact{.where = where, .in = in});
    }
    this->waiting_into(where, in, all);
    read_lines(deleted_file_of(in), in, all);
    this->waiting_into(deleted_file_of(in), in, all);
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
      this->take_line(*opened, in, all);
    }
    return lines;
  }
  // A line read, into `all`: the message it says, or that it is deleted or
  // gone. One that cannot be read is passed over.
  void take_line(std::string_view text, const mux::conversation_id& in, std::map<std::string, mux::message>& all) const {
    {
      auto parsed = knot::try_read<store_file::message_line>(text);
      if (!parsed)
        return;
      auto& o = *parsed;
      if (o.gone.value_or(false)) {
        all.erase(o.id);
        return;
      }
      if (o.deleted.value_or(false)) {
        if (const auto found = all.find(o.id); found != all.end())
          found->second.redacted = true;
        return;
      }
      mux::message one;
      one.in = in;
      one.id = o.id;
      one.sender = o.sender.value_or("");
      if (o.at)
        one.at = time_point(std::chrono::milliseconds(*o.at));
      if (o.order)
        one.local_order = time_point(std::chrono::milliseconds(*o.order));
      one.body.plain = o.plain.value_or("");
      one.body.html = std::move(o.html);
      one.replies_to = std::move(o.reply);
      one.thread = std::move(o.thread);
      one.edited = o.edited.value_or(false);
      one.latest_edit_event = o.latest_edit_event.value_or("");
      one.versions = o.versions.value_or(std::vector<store_file::message_line::version_line>{}) | std::views::transform([&](const store_file::message_line::version_line& v) {
                       return mux::message::version{mux::body{v.plain.value_or(""), v.html},
                                                    time_point(std::chrono::milliseconds(v.until.value_or(0))), v.event.value_or("")};
                     }) | std::ranges::to<std::vector>();
      one.encrypted = o.encrypted.value_or(false);
      one.unverified = o.unverified.value_or(false);
      one.came_plain = o.came_plain.value_or(false);
      one.unauthenticated = o.unauthenticated.value_or(false);
      one.redacted = o.redacted.value_or(false);
      one.redacted_by = o.redacted_by;
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
                                             std::chrono::milliseconds(reaction.at.value_or(0))), reaction.shortcode});
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
        .order = one.local_order && *one.local_order != one.at
                     ? std::optional(static_cast<std::int64_t>(one.local_order->time_since_epoch().count()))
                     : std::nullopt,
        .plain = one.body.plain,
        .html = one.body.html,
        .reply = one.replies_to,
        .thread = one.thread,
        .edited = store_file::flag(one.edited),
        .latest_edit_event = one.latest_edit_event.empty() ? std::nullopt : std::optional(one.latest_edit_event),
        .versions = one.versions.empty()
                        ? std::nullopt
                        : std::optional(one.versions | std::views::transform([](const mux::message::version& v) {
                                          return store_file::message_line::version_line{
                                              v.body.plain, v.body.html, static_cast<std::int64_t>(v.until.time_since_epoch().count()),
                                              v.event.empty() ? std::nullopt : std::optional(v.event)};
                                        }) | std::ranges::to<std::vector>()),
        .redacted = store_file::flag(one.redacted),
        .redacted_by = one.redacted_by,
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
                                  : std::nullopt,
                        .shortcode = known != one.reaction_events.end() ? known->shortcode : std::nullopt});
      }
    return knot::to_json_string(line);
  }

  // Last: stopped -- what is queued written -- and joined before anything
  // it uses goes.
  std::jthread writer_;
};

}  // namespace mux::app

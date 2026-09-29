// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.store: The messages, reads and drafts kept on disk.
export module mux.app.store;

import std;
import knot;
import mux.core;
import mux.config;

export namespace mux::app {

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
    knot::value::object users;
    for (const auto& [user, event] : chat.read_by)
      users.emplace(user, knot::value(event));
    knot::value::object all;
    all.emplace("users", knot::value(std::move(users)));
    if (chat.read_up_to)
      all.emplace("me", knot::value(*chat.read_up_to));
    const auto where = reads_file_of(in);
    std::error_code failed;
    std::filesystem::create_directories(where.parent_path(), failed);
    std::ofstream(where, std::ios::binary | std::ios::trunc) << knot::to_json_string(knot::value(std::move(all)));
  }
  struct reads {
    std::map<std::string, std::string> read_by;
    std::optional<std::string> me;
  };
  [[nodiscard]] static reads read_reads(const mux::conversation_id& in) {
    reads out;
    std::ifstream file(reads_file_of(in), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto parsed = knot::try_read<knot::value>(std::string_view(text));
    if (!parsed || !parsed->is<knot::value::object>())
      return out;
    const auto& all = parsed->as<knot::value::object>();
    if (const auto users = all.find("users"); users != all.end() && users->second.is<knot::value::object>())
      for (const auto& [user, event] : users->second.as<knot::value::object>())
        if (event.is<std::string>())
          out.read_by.emplace(user, event.as<std::string>());
    if (const auto me = all.find("me"); me != all.end() && me->second.is<std::string>())
      out.me = me->second.as<std::string>();
    return out;
  }
  // A message deleted: marked where it is kept, and kept whole in the
  // archive of deleted messages, which the cache's limit does not reach --
  // as it was where that is given, else as the disk has it.
  void mark_deleted(const mux::conversation_id& in, const std::string& id, std::optional<mux::message> whole) {
    knot::value::object line;
    line.emplace("id", knot::value(id));
    line.emplace("deleted", knot::value(true));
    append(in, knot::to_json_string(knot::value(std::move(line))));
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
    std::ofstream(where, std::ios::binary | std::ios::app) << line_of(*whole) << '\n';
    prune(mux::config::state_path("deleted"), deleted_budget, where);
  }
  void forget(const mux::conversation_id& in, const std::string& id) {
    knot::value::object line;
    line.emplace("id", knot::value(id));
    line.emplace("gone", knot::value(true));
    append(in, knot::to_json_string(knot::value(std::move(line))));
  }

  // All of a chat's messages kept, by id.
  std::map<std::string, mux::message> everything(const mux::conversation_id& in) {
    auto all = read(in);
    return {std::make_move_iterator(all.begin()), std::make_move_iterator(all.end())};
  }

  // Up to `count` of a chat's messages from before `before`, oldest first.
  // Reading a chat's file counts as using it.
  std::vector<mux::message> older(const mux::conversation_id& in, time_point before, std::size_t count) {
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
  void append(const mux::conversation_id& in, const std::string& line) {
    const auto where = file_of(in);
    std::error_code failed;
    std::filesystem::create_directories(where.parent_path(), failed);
    std::ofstream(where, std::ios::binary | std::ios::app) << line << '\n';
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
      std::error_code failed;
      std::filesystem::rename(fresh, where, failed);
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
    const auto text_of = [](const knot::value::object& o, std::string_view key) -> std::optional<std::string> {
      const auto found = o.find(key);
      if (found == o.end() || !found->second.is<std::string>())
        return std::nullopt;
      return found->second.as<std::string>();
    };
    const auto flag_of = [](const knot::value::object& o, std::string_view key) {
      const auto found = o.find(key);
      return found != o.end() && found->second.is<bool>() && found->second.as<bool>();
    };
    while (std::getline(file, text)) {
      ++lines;
      auto parsed = knot::try_read<knot::value>(std::string_view(text));
      if (!parsed || !parsed->is<knot::value::object>())
        continue;
      const auto& o = parsed->as<knot::value::object>();
      const auto id = text_of(o, "id");
      if (!id)
        continue;
      if (flag_of(o, "gone")) {
        all.erase(*id);
        continue;
      }
      if (flag_of(o, "deleted")) {
        if (const auto found = all.find(*id); found != all.end())
          found->second.redacted = true;
        continue;
      }
      mux::message one;
      one.in = in;
      one.id = *id;
      one.sender = text_of(o, "sender").value_or("");
      if (const auto at = o.find("at"); at != o.end() && at->second.is<std::int64_t>())
        one.at = time_point(std::chrono::milliseconds(at->second.as<std::int64_t>()));
      one.body.plain = text_of(o, "plain").value_or("");
      one.body.html = text_of(o, "html");
      one.replies_to = text_of(o, "reply");
      one.edited = flag_of(o, "edited");
      one.redacted = flag_of(o, "redacted");
      one.outgoing = flag_of(o, "out");
      if (const auto carried = o.find("attachment");
          carried != o.end() && carried->second.is<knot::value::object>()) {
        const auto& c = carried->second.as<knot::value::object>();
        const auto number = [&](std::string_view key) -> std::int64_t {
          const auto found = c.find(key);
          return found != c.end() && found->second.is<std::int64_t>() ? found->second.as<std::int64_t>() : 0;
        };
        mux::attachment a;
        if (flag_of(c, "image"))
          a.kind = mux::attachment_kind::image{};
        a.source = text_of(c, "source").value_or("");
        a.name = text_of(c, "name").value_or("");
        a.mimetype = text_of(c, "mimetype").value_or("");
        a.blurhash = text_of(c, "blurhash");
        a.size = number("size");
        a.width = static_cast<int>(number("w"));
        a.height = static_cast<int>(number("h"));
        one.attachment = std::move(a);
      }
      all.insert_or_assign(*id, std::move(one));
    }
    return lines;
  }
  static std::string line_of(const mux::message& one) {
    knot::value::object line;
    line.emplace("id", knot::value(one.id));
    line.emplace("sender", knot::value(one.sender));
    line.emplace("at", knot::value(static_cast<std::int64_t>(one.at.time_since_epoch().count())));
    line.emplace("plain", knot::value(one.body.plain));
    if (one.body.html)
      line.emplace("html", knot::value(*one.body.html));
    if (one.replies_to)
      line.emplace("reply", knot::value(*one.replies_to));
    if (one.edited)
      line.emplace("edited", knot::value(true));
    if (one.redacted)
      line.emplace("redacted", knot::value(true));
    if (one.outgoing)
      line.emplace("out", knot::value(true));
    if (one.attachment) {
      knot::value::object carried;
      carried.emplace("image", knot::value(mux::is_picture(one.attachment->kind)));
      carried.emplace("source", knot::value(one.attachment->source));
      carried.emplace("name", knot::value(one.attachment->name));
      carried.emplace("mimetype", knot::value(one.attachment->mimetype));
      carried.emplace("size", knot::value(one.attachment->size));
      if (one.attachment->blurhash)
        carried.emplace("blurhash", knot::value(*one.attachment->blurhash));
      carried.emplace("w", knot::value(static_cast<std::int64_t>(one.attachment->width)));
      carried.emplace("h", knot::value(static_cast<std::int64_t>(one.attachment->height)));
      line.emplace("attachment", knot::value(std::move(carried)));
    }
    return knot::to_json_string(knot::value(std::move(line)));
  }
};

}  // namespace mux::app

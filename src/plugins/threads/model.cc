// SPDX-License-Identifier: AGPL-3.0-only
// mux.plugin.threads.model -- What threads keep in the model, beside the
// client's own (mux.plugins.model): a message's thread -- its root, where
// it is an answer in one; and, where it is a root, the thread's summary --
// and a chat's threads -- each one's answers, by its root, oldest first,
// kept apart from the timeline as Element keeps them; and the roots its
// server listed. And what the model asks of any plugin's part: whether a
// message is its own to keep aside, and where; the message it is about;
// that one told of it; every list it holds. On std alone.
export module mux.plugin.threads.model;

import std;

export namespace mux::plugins::threads {

// A thread's summary, on its root (m.relations' m.thread, or counted
// here): how many answers, the latest -- who, what, when -- and whether the
// user took part.
struct summary {
  std::int64_t count = 0;
  std::string last_id;
  std::string last_sender;
  std::string last_text;
  std::chrono::sys_time<std::chrono::milliseconds> last_at{};
  bool participated = false;
  friend bool operator==(const summary&, const summary&) = default;
};
struct message_part {
  // In a thread: its root -- kept in the chat's threads, not its timeline.
  std::optional<std::string> root;
  // A thread's root: the thread's summary.
  std::optional<summary> threaded;
  // Sent into a thread: its latest event as it was sent -- what a client
  // that does not know threads shows it as an answer to.
  std::optional<std::string> latest;
  friend bool operator==(const message_part&, const message_part&) = default;
};
template <class Message>
struct chat_part {
  std::map<std::string, std::vector<Message>> answers;
  std::vector<std::string> roots;
  // Each thread's way back, by its root: where its next page of older
  // answers starts -- none where all are loaded.
  std::map<std::string, std::optional<std::string>> pages;
  friend bool operator==(const chat_part&, const chat_part&) = default;
};

// Its part of a message, and of a chat.
template <class Message>
[[nodiscard]] const message_part& part_of(const Message& one) {
  return std::get<message_part>(one.plugins);
}
template <class Message>
[[nodiscard]] message_part& part_of(Message& one) {
  return std::get<message_part>(one.plugins);
}

// What the model asks of its part of a message: the root it is an answer
// in, said, and put back (from the store).
inline const std::string* about_said(const message_part& mine) {
  return mine.root ? &*mine.root : nullptr;
}
inline void about_restored(message_part& mine, const std::string& root) {
  mine.root = root;
}
// What the model asks of its part of a chat (found by its type):
// a message its own to keep aside -- an answer, with its thread's -- and
// where; none for one of the timeline's.
template <class Message>
std::vector<Message>* aside_list(chat_part<Message>& part, const Message& one) {
  const auto& root = part_of(one).root;
  return root ? &part.answers[*root] : nullptr;
}
// The message one kept aside is about: its thread's root.
template <class Message>
const std::string* aside_about(const chat_part<Message>&, const Message& one) {
  const auto& root = part_of(one).root;
  return root ? &*root : nullptr;
}
// That message told of one come: the thread's summary brought up to date --
// one more where it came live, else as many as are held at least.
template <class Message>
void noted(const chat_part<Message>& part, Message& about, const Message& added, bool live) {
  summary now = part_of(about).threaded.value_or(summary{});
  const auto found = part.answers.find(about.id);
  const std::size_t held = found == part.answers.end() ? 0u : found->second.size();
  now.count = live ? now.count + 1 : std::max<std::int64_t>(now.count, static_cast<std::int64_t>(held));
  if (added.at >= now.last_at) {
    now.last_id = added.id;
    now.last_sender = added.sender;
    now.last_text = added.body.plain;
    now.last_at = added.at;
  }
  now.participated = now.participated || added.outgoing;
  part_of(about).threaded = now;
}
// The answers of a thread, by its root.
template <class Message>
const std::vector<Message>* aside_by(const chat_part<Message>& part, const std::string& root) {
  const auto found = part.answers.find(root);
  return found == part.answers.end() ? nullptr : &found->second;
}
// Whether a message is a thread's root: answers counted, or held.
template <class Message>
bool kept_about(const chat_part<Message>& part, const Message& one) {
  const auto& threaded = part_of(one).threaded;
  return (threaded && threaded->count > 0) || part.answers.contains(one.id);
}
// A message to be sent aside about one (an answer into a thread): its part
// says the root, and the thread's latest held answer (the root, none held).
template <class Message, class Parts>
void aside_placed(const chat_part<Message>& part, Parts& parts, const std::string& root) {
  const auto found = part.answers.find(root);
  std::get<message_part>(parts) =
      message_part{.root = root, .latest = found == part.answers.end() || found->second.empty() ? root : found->second.back().id};
}
// Every list it keeps aside, given to f.
template <class Message, class F>
void each_aside(chat_part<Message>& part, F&& f) {
  for (auto& [root, answers] : part.answers)
    f(answers);
}
template <class Message, class F>
void each_aside(const chat_part<Message>& part, F&& f) {
  for (const auto& [root, answers] : part.answers)
    f(answers);
}

}  // namespace mux::plugins::threads

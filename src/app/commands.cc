// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.commands: what plugins script with. Their slash commands
// (contract::slash_commands) -- every one, by name; what is typed in the
// field read once, "/name arguments", into the command it names, asked of
// the program as a request, which its plugin's part takes as it takes any --
// and the actions they script with (script): the chat open and the chats
// there are, their messages, a chat opened, something sent, a reaction put,
// a room joined, a notice shown.
export module mux.app.commands;

import std;
import mux.composition;
import mux.core;
import mux.ui;
import mux.plugins.types;
import mux.app.services;
import mux.app.network;
import mux.app.requests;

export namespace mux::app {

// Every plugin's commands, one list.
template <class... Lists>
struct commands_joined {
  using type = contract::command_list<>;
};
template <class... A>
struct commands_joined<contract::command_list<A...>> {
  using type = contract::command_list<A...>;
};
template <class... A, class... B, class... Rest>
struct commands_joined<contract::command_list<A...>, contract::command_list<B...>, Rest...> : commands_joined<contract::command_list<A..., B...>, Rest...> {};
template <class... Features>
consteval auto commands_of(plugin_list<Features...>) {
  return type_tag<typename commands_joined<decltype(slash_commands(contract::slash_commands{}, type_tag<Features>{}))...>::type>{};
}
using all_commands = typename decltype(commands_of(composition::providers<contract::slash_commands, enabled_plugins>{}))::type;

// What is typed, read as a command where it is one: a '/' first, then its
// name, then -- after a space -- what follows it.
struct command_typed {
  std::string name;
  std::string arguments;
};
[[nodiscard]] inline std::optional<command_typed> command_in(std::string_view text) {
  if (!text.starts_with('/') || text.starts_with("//"))
    return std::nullopt;
  const std::string_view rest = text.substr(1);
  const auto space = rest.find_first_of(" \n");
  const std::string_view name = rest.substr(0, space);
  if (name.empty())
    return std::nullopt;
  return command_typed{std::string(name), space == std::string_view::npos ? std::string() : std::string(rest.substr(space + 1))};
}
// The command a name is -- read here, once, into its type -- made with what
// follows it and asked of the program. Whether there is one of that name.
template <class Asked, class... Commands>
bool ask_command(contract::command_list<Commands...>, const command_typed& typed, const Asked& asked) {
  return ((typed.name == Commands::name && (asked(Commands{typed.arguments}), true)) || ...);
}
// Each command's name, how it is used and what it does: for the list of
// them, over the field as one is typed, and for /help.
struct command_said {
  std::string_view name, usage, summary;
};
template <class... Commands>
consteval auto commands_said(contract::command_list<Commands...>) {
  return std::array<command_said, sizeof...(Commands)>{command_said{Commands::name, Commands::usage, Commands::summary}...};
}
inline constexpr auto kCommands = commands_said(all_commands{});

// What a plugin scripts with: the program's own actions, typed.
struct script {
  services* s = nullptr;

  // The chat open, where one is.
  [[nodiscard]] std::optional<conversation_id> chat() const { return s->chosen(); }
  // A chat, as the model holds it.
  [[nodiscard]] const conversation* find(const conversation_id& id) const { return s->model->find(id); }
  // The chats whose name or address has `wanted` in it, ignoring case: the
  // open chat's account's first.
  [[nodiscard]] std::vector<conversation_id> chats_named(std::string_view wanted) const {
    const auto lower = [](std::string_view text) {
      return text | std::views::transform([](char one) { return static_cast<char>(std::tolower(static_cast<unsigned char>(one))); }) |
             std::ranges::to<std::string>();
    };
    const std::string asked = lower(wanted);
    std::vector<conversation_id> found;
    for (const account& one : s->model->accounts().values())
      for (const conversation& chat : one.conversations.values())
        if (lower(chat.name).contains(asked) || lower(chat.id.id).contains(asked))
          found.push_back(chat.id);
    if (const auto open = this->chat())
      std::ranges::stable_partition(found, [&](const conversation_id& one) { return one.account == open->account; });
    return found;
  }
  // A chat's messages, as the model holds them: oldest first.
  [[nodiscard]] std::span<const message> messages(const conversation_id& id) const {
    const conversation* chat = this->find(id);
    return chat ? std::span<const message>(chat->timeline) : std::span<const message>();
  }
  // A chat opened, as the list opens it.
  template <class Asked>
  void open(const conversation_id& id, const Asked& asked) const {
    asked(request::choose{id});
  }
  // Text sent to a chat -- as an answer to a message, where one is given.
  void send(const conversation_id& to, std::string text, std::optional<std::string> reply_to = std::nullopt) const {
    if (reply_to && !s->demo()) {
      s->net->send(to, std::move(text), std::move(reply_to));
      return;
    }
    s->ask->send(to, std::move(text));
  }
  // Text sent with runs of its own formatting (a colour, "as typed"), not
  // read as the field's: in the demo, its text alone.
  void send_styled(const conversation_id& to, std::string text, std::vector<styled_run> styles) const {
    if (s->demo()) {
      s->ask->send(to, std::move(text));
      return;
    }
    s->net->send(to, std::move(text), std::nullopt, {}, std::move(styles));
  }
  // A message of a chat gone to, as a link or a reply goes to it.
  template <class Asked>
  void jump(std::string message, const Asked& asked) const {
    asked(request::jump_to_message{std::move(message)});
  }
  // A reaction put on a message, or taken back.
  void react(const conversation_id& in, std::string message, std::string key, bool on = true) const {
    s->net->react(in, std::move(message), std::move(key), on);
  }
  // A room joined, by its address or id, by an account.
  void join(const account_id& by, std::string room) const { s->net->join(by, std::move(room), {}); }
  // A notice shown over the window.
  void notice(std::string heading, std::string text) const { s->notice(std::move(heading), std::move(text)); }
};

}  // namespace mux::app

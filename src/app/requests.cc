// SPDX-License-Identifier: AGPL-3.0-only
// mux.app.requests: What the window asks: requests, and the actions that make them.
export module mux.app.requests;

import std;
import splice;
import skiff.scene;
import skiff.bind;
import mux.core;
import mux.config;
import mux.ui;
import mux.ui.proto;
import mux.protocols;
import mux.app.network;
// Where the window is instantiated -- every unit that imports this -- the
// plugins' nodes are found (their extend() overloads, by ADL).
export import mux.plugins.ui;

export namespace mux::app {

// The requests and what the message field is for are the window's own
// (mux.ui:requests): named here as they were.
namespace compose = mux::ui::compose;
using mux::ui::compose_t;
namespace request = mux::ui::request;

// Every request, one of them: a spl::variant, built in time linear in how
// many there are (std::variant's nested union made it quadratic).

// What the program's parts reach of the network for a message sent -- in
// the demo, put in at once, as sent.
struct actions {
  network* net = nullptr;
  // In the demo, a message sent is there at once, as sent.
  bool demo = false;
  mailbox_type* box = nullptr;
  int demo_sent = 0;

  void send(const mux::conversation_id& to, std::string text) {
    if (!demo) {
      net->send(to, std::move(text));
      return;
    }
    mux::message one;
    one.in = to;
    one.id = std::format("demo-sent-{}", demo_sent++);
    one.sender = to.account.address;
    one.at = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
    one.body.plain = std::move(text);
    one.outgoing = true;
    box->push(mux::change_t{mux::change::message_added{.message = std::move(one)}});
  }
};

using window_type = mux::ui::window;


}  // namespace mux::app

// The window's scene is instantiated once, in src/app/scene.cc: its walks
// over the whole tree -- routing, layout, drawing, for every type of node
// in it -- were most of a build, made again in every unit that touched the
// scene. In a named module, members defined in a class are not implicitly
// inline, so this keeps them all out of the other units.
extern template class skiff::scene::Scene<mux::app::window_type>;
// Outside a release build, the window's own table -- and with it the
// tables of every node below it -- made with the scene alone (scene.cc):
// every unit that refreshes the window, or delivers a press to it, takes
// the window through its table, and made the whole tree's tables again.
#ifdef SKIFF_ERASED_WALKS
template <> inline constexpr bool skiff::scene::kOpsElsewhere<mux::app::window_type> = true;
template <> const skiff::scene::AnyNode::Ops& skiff::scene::opsElsewhere<mux::app::window_type>() noexcept;
#endif
// Outside a release build every node's table is made where the window is
// constructed (scene.cc): a stack takes its parts' tables as it is made.
// They were also made in walks_*.cc, each unit importing all of mux.ui and
// making again the tables of what it shares with the others -- the same
// work two or three times over, in a build of one thread.

export namespace mux::app {
// Inline storage with lifetime defined beside Scene's explicit instantiation.
// Clang cannot destroy that extern specialization in an importing unit
// (CODE_STYLE.md, like destroy_account). A union suppresses implicit teardown.
class window_scene {
public:
  explicit window_scene(const mux::ui::ui_needs& needs);
  ~window_scene();
  window_scene(const window_scene&) = delete;
  window_scene& operator=(const window_scene&) = delete;
  skiff::scene::Scene<window_type>& get() noexcept { return fScene; }
private:
  union { skiff::scene::Scene<window_type> fScene; };
};

}

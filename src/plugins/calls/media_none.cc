// SPDX-License-Identifier: AGPL-3.0-only
// mux.calls.media -- Calls not in this build (MUX_CALLS off): the same
// session, which connects nothing -- asked to, it says it failed. Nothing of
// WebRTC is fetched or built for it.
export module mux.calls.media;

import std;
export import mux.calls.types;

export namespace mux::calls {

inline constexpr bool kAvailable = false;

template <class Wake>
class media_session {
 public:
  media_session(const std::vector<ice_server>&, Wake wake) : wake_(std::move(wake)) {}
  media_session(const media_session&) = delete;
  media_session& operator=(const media_session&) = delete;
  void offer() { this->fail(); }
  void answer(const session_description&) { this->fail(); }
  void answered(const session_description&) {}
  void add_candidate(const ice_candidate&) {}
  void mute(bool) {}
  [[nodiscard]] bool muted() const { return false; }
  [[nodiscard]] std::vector<said_t> take() { return std::exchange(said_, {}); }

 private:
  void fail() {
    said_.push_back(said::failed{});
    wake_();
  }
  Wake wake_;
  std::vector<said_t> said_;
};

}  // namespace mux::calls

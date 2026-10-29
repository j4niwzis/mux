// SPDX-License-Identifier: AGPL-3.0-only
// Browser WebRTC owns capture, codecs, ICE and encrypted media transport.
module;
#include <emscripten.h>
export module mux.calls.media;
import std;
import splice;
export import mux.plugin.calls.changes;

// clang-format off
EM_JS(void, mux_web_call_create, (int id, int relay_only), {
  Module.muxCalls ||= new Map();
  const s = {servers: [], candidates: [], closed: false, muted: false, policy: relay_only ? 'relay' : 'all'};
  Module.muxCalls.set(id, s);
  s.tell = (kind, text = "", mid = "", index = -1) => {
    if (!s.closed) Module.ccall('mux_web_call_event', null,
      ['number','number','string','string','number'], [id,kind,text,mid,index]);
  };
  s.fail = e => s.tell(e.muxAudio ? 8 : 6, String(e.message || e));
  s.prepare = async () => {
    if (!s.pc) {
      s.pc = new RTCPeerConnection({iceServers:s.servers, iceTransportPolicy:s.policy});
      s.pc.onicecandidate = e => s.tell(3, e.candidate?.candidate || "", e.candidate?.sdpMid || "", e.candidate?.sdpMLineIndex ?? -1);
      s.pc.onconnectionstatechange = () => {
        const kinds = {connected:4, disconnected:5, failed:6, closed:7};
        const kind = kinds[s.pc.connectionState]; if (kind) s.tell(kind);
      };
      s.pc.ontrack = e => {
        if (!s.audio) {s.audio = document.createElement('audio');s.audio.autoplay = true;document.body.append(s.audio);}
        s.audio.srcObject = e.streams[0] || new MediaStream([e.track]);
        s.audio.play().catch(() => {});
      };
      s.capture = (async () => {
        let timer;
        try {
          const capture = navigator.mediaDevices.getUserMedia({audio:true,video:false});
          capture.then(stream => {if (s.closed || s.closedCapture) stream.getTracks().forEach(t => t.stop());},() => {});
          s.stream = await Promise.race([capture,new Promise((_,reject) => {
            timer = setTimeout(() => {s.closedCapture = true;reject(new Error('Microphone did not start'));},30000);
          })]);
          if (s.closed || s.closedCapture) {s.stream.getTracks().forEach(t => t.stop());return;}
          s.stream.getAudioTracks().forEach(t => {t.enabled = !s.muted;s.pc.addTrack(t,s.stream);});
        } catch (e) {e.muxAudio = true;throw e;}
        finally {clearTimeout(timer);}
      })();
    }
    await s.capture;
    if (s.closed) throw new Error('Call ended');
  };
  s.remote = async description => {
    await s.pc.setRemoteDescription(description);
    for (const c of s.candidates.splice(0)) await s.pc.addIceCandidate(c);
  };
  s.local = async description => {
    await s.pc.setLocalDescription(description);
    s.tell(s.pc.localDescription.type === 'offer' ? 1 : 2,s.pc.localDescription.sdp);
  };
});
EM_JS(void, mux_web_call_server, (int id, const char *url, const char *user, const char *password), {
  Module.muxCalls.get(id).servers.push({urls:UTF8ToString(url),username:UTF8ToString(user),credential:UTF8ToString(password)});
});
EM_JS(void, mux_web_call_offer, (int id), {
  const s = Module.muxCalls.get(id);
  s.prepare().then(() => s.pc.createOffer()).then(d => s.local(d)).catch(s.fail);
});
EM_JS(void, mux_web_call_description, (int id, int offer, const char *sdp), {
  const s = Module.muxCalls.get(id), description = {type:offer ? 'offer':'answer',sdp:UTF8ToString(sdp)};
  (async () => {
    await s.prepare();
    if (s.closed || (!offer && s.pc.signalingState !== 'have-local-offer')) return;
    await s.remote(description);
    if (offer) await s.local(await s.pc.createAnswer());
  })().catch(s.fail);
});
EM_JS(void, mux_web_call_candidate, (int id, const char *line, const char *mid, int index), {
  const s = Module.muxCalls.get(id), text = UTF8ToString(line);
  const candidate = text ? {candidate:text,sdpMid:UTF8ToString(mid)||null,sdpMLineIndex:index < 0 ? null:index} : null;
  if (s.pc?.remoteDescription) s.pc.addIceCandidate(candidate).catch(s.fail);
  else s.candidates.push(candidate);
});
EM_JS(void, mux_web_call_mute, (int id, int muted), {
  const s = Module.muxCalls.get(id);s.muted = !!muted;
  s.stream?.getAudioTracks().forEach(t => {t.enabled = !s.muted;});
});
EM_JS(void, mux_web_call_close, (int id), {
  const s = Module.muxCalls.get(id);if (!s) return;
  s.closed = true;s.pc?.close();s.stream?.getTracks().forEach(t => t.stop());s.audio?.remove();Module.muxCalls.delete(id);
});
// clang-format on

namespace mux::calls::browser {
inline int next = 0;
inline std::unordered_map<int, std::vector<said_t>> boxes;
}
extern "C" EMSCRIPTEN_KEEPALIVE void mux_web_call_event(int id, int kind, const char *text, const char *mid, int index) {
  using namespace mux::calls;
  const auto found = browser::boxes.find(id);
  if (found == browser::boxes.end()) return;
  auto &box = found->second;
  switch (kind) {
    case 1: box.emplace_back(said::description{{sdp_kind::offer{}, text}}); break;
    case 2: box.emplace_back(said::description{{sdp_kind::answer{}, text}}); break;
    case 3: box.emplace_back(said::candidate{{text, mid, index < 0 ? std::nullopt : std::optional<std::int64_t>(index)}}); break;
    case 4: box.emplace_back(said::connected{}); break;
    case 5: box.emplace_back(said::disconnected{}); break;
    case 6: box.emplace_back(said::failed{text, false}); break;
    case 7: box.emplace_back(said::ended{}); break;
    case 8: box.emplace_back(said::failed{text, true}); break;
    default: break;
  }
}
export namespace mux::calls {
inline constexpr bool kAvailable = true;
template <class Wake> class media_session {
public:
  media_session(const std::vector<ice_server> &servers, connect_choice choice, Wake) : id_(++browser::next) {
    browser::boxes.try_emplace(id_);
    mux_web_call_create(id_, choice.relay_only ? 1 : 0);
    for (const auto &server : servers) {
      const auto [scheme, transport] = spl::visit(spl::overloaded{
        [](relay::stun) { return std::pair{"stun:", ""}; },
        [](relay::turn_udp) { return std::pair{"turn:", "?transport=udp"}; },
        [](relay::turn_tcp) { return std::pair{"turn:", "?transport=tcp"}; },
        [](relay::turn_tls) { return std::pair{"turns:", "?transport=tcp"}; }}, server.kind);
      const auto host = server.host.contains(':') ? "[" + server.host + "]" : server.host;
      const auto url = scheme + host + ":" + std::to_string(server.port) + transport;
      mux_web_call_server(id_, url.c_str(), server.username.c_str(), server.password.c_str());
    }
  }
  ~media_session() { browser::boxes.erase(id_); mux_web_call_close(id_); }
  media_session(const media_session &) = delete;
  media_session &operator=(const media_session &) = delete;
  void offer() { mux_web_call_offer(id_); }
  void answer(const session_description &description) { apply(description); }
  void answered(const session_description &description) { apply(description); }
  void renegotiate(const session_description &description) { apply(description); }
  void add_candidate(const ice_candidate &candidate) {
    mux_web_call_candidate(id_, candidate.line.c_str(), candidate.mid.c_str(),
                          candidate.index && *candidate.index >= 0 && *candidate.index <= std::numeric_limits<int>::max()
                            ? static_cast<int>(*candidate.index) : -1);
  }
  void mute(bool on) { muted_ = on; mux_web_call_mute(id_, on); }
  bool muted() const { return muted_; }
  std::vector<said_t> take() { return std::exchange(browser::boxes.at(id_), {}); }
private:
  void apply(const session_description &description) {
    const int offer = spl::visit(spl::overloaded{[](sdp_kind::offer) { return 1; }, [](sdp_kind::answer) { return 0; }}, description.kind);
    mux_web_call_description(id_, offer, description.sdp.c_str());
  }
  int id_;
  bool muted_ = false;
};
}

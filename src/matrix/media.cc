// SPDX-License-Identifier: AGPL-3.0-only
// mux.matrix:media -- Pictures and files: thumbnails and downloads fetched, files uploaded and sent.
export module mux.matrix:media;

import std;
import knot;
import loom.api;
import loom.ev;
import loom.state;
import loom.cs.joining;
import loom.cs.leaving;
import loom.cs.login;
import loom.cs.message_pagination;
import loom.cs.receipts;
import loom.cs.redaction;
import loom.cs.room_send;
import loom.cs.rooms;
import loom.cs.sync;
import loom.cs.typing;
import loom.cs.wellknown;
import mux.config;
import mux.core;
import mux.http;
import mux.net;
import :account;

// The members defined here are declared in :account, and exported there.
namespace mux::matrix {

template <class Sink>
void account<Sink>::fetch_media(std::string source, media_use_t use, int size, bool crop) {
  loop_->spawn([this, source = std::move(source), use = std::move(use), size, crop] {
    if (!api_ || !source.starts_with("mxc://"))
      return;
    const std::string_view rest = std::string_view(source).substr(6);
    const auto slash = rest.find('/');
    if (slash == std::string_view::npos)
      return;
    const std::string server(rest.substr(0, slash));
    const std::string media(rest.substr(slash + 1));
    const std::string query =
        size > 0 ? std::format("?width={0}&height={0}&method={1}", size, crop ? "crop" : "scale") : std::string();
    const auto bases = size > 0 ? std::array<std::string, 2>{"/_matrix/client/v1/media/thumbnail/",
                                                             "/_matrix/media/v3/thumbnail/"}
                                : std::array<std::string, 2>{"/_matrix/client/v1/media/download/",
                                                             "/_matrix/media/v3/download/"};
    for (const std::string& base : bases) {
      try {
        // A download whole says how far it has come, a twentieth at a time.
        int said = -1;
        const auto progress = [&](std::size_t read, std::optional<std::size_t> total) {
          if (!total || *total == 0)
            return;
          const int now = static_cast<int>(20 * read / *total);
          if (now != said) {
            said = now;
            sink_(change::media_progress{source, static_cast<float>(read) / static_cast<float>(*total)});
          }
        };
        const auto got = api_->request("GET", base + server + "/" + media + query, {},
                                       token_ ? std::optional<std::string_view>(*token_) : std::nullopt,
                                       std::chrono::seconds(60), {}, size > 0 ? nullptr : &progress);
        if (got.status == 200 && !got.body.empty()) {
          sink_(change::avatar_loaded{use, source, got.body});
          return;
        }
      } catch (const net::failure&) {
        return;
      }
    }
  });
}

template <class Sink>
void account<Sink>::send_file(std::string room, std::string local, std::string bytes, std::string name, std::string mimetype,
                 bool image, int width, int height, std::string caption, std::optional<std::string> reply_to) {
  loop_->spawn([this, room = std::move(room), local = std::move(local), bytes = std::move(bytes),
                name = std::move(name), mimetype = std::move(mimetype), image, width, height,
                caption = std::move(caption), reply_to = std::move(reply_to)] {
    const conversation_id in{id_, room};
    mux::attachment carried;
    if (image)
      carried.kind = attachment_kind::image{};
    carried.source = "local:" + local;
    carried.name = name;
    carried.mimetype = mimetype;
    carried.size = static_cast<std::int64_t>(bytes.size());
    carried.width = width;
    carried.height = height;
    sink_(change::message_added{message{.in = in,
                                        .id = local,
                                        .sender = id_.address,
                                        .at = std::chrono::time_point_cast<std::chrono::milliseconds>(
                                            std::chrono::system_clock::now()),
                                        .body = {caption, std::nullopt},
                                        .replies_to = reply_to,
                                        .outgoing = true,
                                        .delivery = delivery::sending{},
                                        .attachment = carried}});
    if (!api_) {
      sink_(change::delivery_changed{in, local, delivery::failed{}});
      return;
    }
    std::string target = "/_matrix/media/v3/upload?filename=";
    for (const char c : name)
      target += std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_'
                    ? std::string(1, c)
                    : std::format("%{:02X}", static_cast<unsigned>(static_cast<unsigned char>(c)));
    std::optional<std::string> uri;
    try {
      const auto got = api_->request("POST", target, bytes, token_ ? std::optional<std::string_view>(*token_) : std::nullopt,
                                     std::chrono::seconds(600),
                                     mimetype.empty() ? std::string_view("application/octet-stream") : mimetype);
      if (got.status == 200)
        if (auto answer = knot::try_read<knot::value>(std::string_view(got.body)); answer)
          uri = text(member(*answer, "content_uri"));
      if (!uri)
        log(id_, "upload of {} failed: {} {}", name, got.status, got.body.substr(0, 200));
    } catch (const net::failure& failed) {
      log(id_, "upload of {} failed: {}", name, failed.what());
    }
    if (!uri) {
      sink_(change::delivery_changed{in, local, delivery::failed{}});
      return;
    }
    knot::value::object content;
    content.emplace("msgtype", knot::value(std::string(image ? "m.image" : "m.file")));
    content.emplace("body", knot::value(caption.empty() ? name : caption));
    content.emplace("filename", knot::value(name));
    content.emplace("url", knot::value(*uri));
    knot::value::object info;
    info.emplace("mimetype", knot::value(mimetype));
    info.emplace("size", knot::value(static_cast<std::int64_t>(bytes.size())));
    if (image) {
      info.emplace("w", knot::value(static_cast<std::int64_t>(width)));
      info.emplace("h", knot::value(static_cast<std::int64_t>(height)));
    }
    content.emplace("info", knot::value(std::move(info)));
    // An answer, as any message may be one: a picture or a file too.
    if (reply_to) {
      knot::value::object target;
      target.emplace("event_id", knot::value(*reply_to));
      knot::value::object relates;
      relates.emplace("m.in_reply_to", knot::value(std::move(target)));
      content.emplace("m.relates_to", knot::value(std::move(relates)));
    }
    auto sent = perform(*api_, loom::cs::send_message{.room_id = room,
                                                      .event_type = "m.room.message",
                                                      .txn_id = local,
                                                      .body = knot::value(std::move(content))});
    if (!sent) {
      sink_(change::delivery_changed{in, local, delivery::failed{}});
      return;
    }
    sink_(change::message_acknowledged{in, local, sent->event_id});
  });
}

}  // namespace mux::matrix

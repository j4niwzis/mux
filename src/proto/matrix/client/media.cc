// SPDX-License-Identifier: AGPL-3.0-only
// mux.proto.matrix.client:media -- Pictures and files: thumbnails and downloads fetched, files uploaded and sent.
export module mux.proto.matrix.client:media;

import std;
import loom.media;
import splice.bytes;
import loom.crypto;
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
namespace mux::proto::matrix::client {

// What an upload answers: where the file is kept now.
struct upload_answer {
  std::string content_uri;
  friend consteval auto json_schema(knot::type<upload_answer>) { return knot::schema<upload_answer>(); }
};

template <class Sink>
void account<Sink>::fetch_media(std::string source, media_use_t use, int size, bool crop) {
  this->spawn_guarded([this, source = std::move(source), use = std::move(use), size, crop] {
    // What it names, read once (loom.media): none for anything not a
    // content URI as the specification writes one.
    const auto named = loom::media::mxc_of(source);
    if (!api_ || !named)
      return;
    // Asked again: a stop asked before is let go.
    cancelled_.erase(source);
    // Encrypted: whole, for the server cannot make a thumbnail of
    // ciphertext; and opened here.
    const auto sealed = encrypted_media_.find(source);
    const int asked = sealed != encrypted_media_.end() ? 0 : size;
    // Thumbnail generation may fail even though the original is available.
    // Bound that wait, then download the original for local downsampling.
    // Encrypted media always takes only the original/decryption path.
    bool cancelled = false;
    for (int pass = 0; pass < (asked > 0 ? 2 : 1); ++pass) {
      const bool small = asked > 0 && pass == 0;
      const auto paths = small ? loom::media::paths_of(*named, loom::media::thumbnail{.size = asked, .crop = crop})
                               : loom::media::paths_of(*named);
      for (const std::string& path : paths) {
        if (stopping_ || cancelled || cancelled_.erase(source))
          return;
        try {
          // A download whole says how far it has come, a twentieth at a time.
          int said = -1;
          // Going on as long as it is not stopped.
          const auto progress = [&](std::size_t read, std::optional<std::size_t> total) {
            if (stopping_ || cancelled_.erase(source)) {
              cancelled = true;
              return false;
            }
            if (small || !total || *total == 0)
              return true;
            const int now = static_cast<int>(20 * read / *total);
            if (now != said) {
              said = now;
              sink_(change::media_progress{source, static_cast<float>(read) / static_cast<float>(*total)});
            }
            return true;
          };
          const auto got = api_->request("GET", path, {},
                                         token_ ? std::optional<std::string_view>(*token_) : std::nullopt,
                                         std::chrono::seconds(small ? 15 : 60), {}, &progress);
          if (stopping_ || cancelled || cancelled_.erase(source))
            return;
          if (got.status == 200 && !got.body.empty()) {
            if (sealed == encrypted_media_.end()) {
              sink_(change::avatar_loaded{use, source, got.body});
              return;
            }
            const auto opened = crypto::open_file(spl::bytes::of(got.body), sealed->second);
            if (!opened) {
              log(id_, "{} is not what its event says: not shown", source);
              return;
            }
            sink_(change::avatar_loaded{use, source, spl::bytes::text_of(*opened)});
            return;
          }
        } catch (const net::failure&) {
          if (stopping_ || cancelled || cancelled_.erase(source))
            return;
          // A timeout on thumbnail generation must not skip the original.
          if (small)
            break;
          return;
        }
      }
    }
  });
}

// An image uploaded for a pack: its mxc://, said as pack_picture_uploaded.
template <class Sink>
void account<Sink>::upload_pack_picture(pack_picture picture, std::string bytes) {
  this->spawn_guarded([this, picture = std::move(picture), bytes = std::move(bytes)]() mutable {
    std::optional<std::string> uri;
    if (api_) {
      std::string target = "/_matrix/media/v3/upload?filename=";
      target.append_range(percent_encoded(picture.body));
      try {
        const auto got = api_->request("POST", target, bytes, token_ ? std::optional<std::string_view>(*token_) : std::nullopt,
                                       std::chrono::seconds(120),
                                       picture.mimetype.empty() ? std::string_view("application/octet-stream")
                                                                : std::string_view(picture.mimetype));
        if (got.status == 200)
          if (auto answer = knot::try_read<upload_answer>(std::string_view(got.body)))
            uri = std::move(answer->content_uri);
        if (!uri)
          log(id_, "upload of {} failed: {} {}", picture.body, got.status, got.body.substr(0, 200));
      } catch (const net::failure& failed) {
        log(id_, "upload of {} failed: {}", picture.body, failed.what());
      }
    }
    picture.url = uri.value_or("");
    sink_(proto::matrix::pack_picture_uploaded{.by = id_, .picture = std::move(picture), .done = uri.has_value()});
  });
}

template <class Sink>
void account<Sink>::send_file(std::string room, std::string local, std::string bytes, std::string name, std::string mimetype,
                 bool image, int width, int height, std::string caption, std::optional<std::string> reply_to, std::optional<thread_place> thread,
                 std::optional<video_look> video) {
  this->spawn_sending([this, room = std::move(room), local = std::move(local), bytes = std::move(bytes),
                name = std::move(name), mimetype = std::move(mimetype), image, width, height,
                caption = std::move(caption), reply_to = std::move(reply_to), thread = std::move(thread),
                video = std::move(video)] mutable {
    const conversation_id in{id_, room};
    mux::attachment carried;
    // A video: shown here by its first picture, as one that came is by its
    // thumbnail.
    if (image || video)
      carried.kind = attachment_kind::image{};
    if (video)
      carried.duration_ms = video->duration_ms;
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
                                        .attachment = carried,
                                        .thread = thread ? std::optional<std::string>(thread->root) : std::nullopt}});
    // Before the upload: the bytes of a file for an encrypted room never
    // reach the server in the clear (review 4, H2).
    // In an encrypted room, sealed here first: what reaches the server is
    // ciphertext, under no name and no type.
    std::optional<crypto::sealed_file> sealed;
    if (this->encrypted_room(room)) {
      if (!crypto_)
        throw plaintext_refused(in, local, "Not sent: it could not be encrypted -- encryption is not running for this account.");
      sealed = crypto::seal_file(spl::bytes::of(bytes));
    }
    const std::string ciphertext = sealed ? spl::bytes::text_of(sealed->bytes) : std::string();
    const std::string_view uploaded = sealed ? std::string_view(ciphertext) : std::string_view(bytes);
    if (!api_) {
      sink_(change::delivery_changed{in, local, delivery::failed{}});
      return;
    }
    // A video's thumbnail, uploaded first: the message names it. In an
    // encrypted room sealed as the video is -- its first picture is what it
    // shows, and goes to the server no more in the clear than the rest.
    std::optional<std::string> thumbnail_uri;
    std::optional<crypto::sealed_file> sealed_thumbnail;
    if (video && !video->thumbnail.empty()) {
      if (sealed)
        sealed_thumbnail = crypto::seal_file(spl::bytes::of(video->thumbnail));
      const std::string thumbnail_cipher = sealed_thumbnail ? spl::bytes::text_of(sealed_thumbnail->bytes) : std::string();
      try {
        const auto got = api_->request(
            "POST", sealed_thumbnail ? "/_matrix/media/v3/upload" : "/_matrix/media/v3/upload?filename=thumbnail.png",
            sealed_thumbnail ? std::string_view(thumbnail_cipher) : std::string_view(video->thumbnail),
            token_ ? std::optional<std::string_view>(*token_) : std::nullopt, std::chrono::seconds(120),
            sealed_thumbnail ? std::string_view("application/octet-stream") : std::string_view("image/png"));
        if (got.status == 200)
          if (auto answer = knot::try_read<upload_answer>(std::string_view(got.body)))
            thumbnail_uri = std::move(answer->content_uri);
      } catch (const net::failure& failed) {
        log(id_, "upload of the thumbnail of {} failed: {}", name, failed.what());
      }
    }
    std::string target = sealed ? "/_matrix/media/v3/upload" : "/_matrix/media/v3/upload?filename=";
    if (!sealed)
      target.append_range(percent_encoded(name));
    std::optional<std::string> uri;
    try {
      const auto got = api_->request("POST", target, uploaded, token_ ? std::optional<std::string_view>(*token_) : std::nullopt,
                                     std::chrono::seconds(600),
                                     sealed || mimetype.empty() ? std::string_view("application/octet-stream") : mimetype);
      if (got.status == 200)
        if (auto answer = knot::try_read<upload_answer>(std::string_view(got.body)))
          uri = std::move(answer->content_uri);
      if (!uri)
        log(id_, "upload of {} failed: {} {}", name, got.status, got.body.substr(0, 200));
    } catch (const net::failure& failed) {
      log(id_, "upload of {} failed: {}", name, failed.what());
    }
    if (!uri) {
      sink_(change::delivery_changed{in, local, delivery::failed{}});
      return;
    }
    // The message: a picture or a file, its caption its body; an answer, as
    // any message may be one.
    const loom::client::media_said said{.uri = *uri,
                                        .name = name,
                                        .caption = caption,
                                        .mimetype = mimetype,
                                        .size = static_cast<std::int64_t>(bytes.size()),
                                        .reply_to = reply_to,
                                        .thread = thread ? std::optional<std::string>(thread->root) : std::nullopt,
                                        .thread_latest = thread ? std::optional<std::string>(thread->latest) : std::nullopt};
    // Sealed: the file named by what opens it, its URL not in the clear.
    const auto with_file = [&](auto content) {
      if (sealed) {
        sealed->info.url = *uri;
        content.url.reset();
        content.rest = knot::raw{knot::to_json_string(crypto::file_part{sealed->info})};
        encrypted_media_.insert_or_assign(*uri, sealed->info);
      }
      return std::pair{as_body(content), relates_to_of(content)};
    };
    // A video: m.video, its size, length and thumbnail said -- as a file,
    // every client showed it as one. Its thumbnail sealed where the room is
    // encrypted: named by what opens it (thumbnail_file), not by its URL.
    const auto video_message = [&] {
      loom::ev::m_room_message_m_video_content_t content;
      loom::client::detail::fill_media(content, said);
      content.info->w = width;
      content.info->h = height;
      content.info->duration = video->duration_ms;
      if (thumbnail_uri) {
        if (sealed_thumbnail) {
          sealed_thumbnail->info.url = *thumbnail_uri;
          content.info->thumbnail_file = knot::raw{knot::to_json_string(sealed_thumbnail->info)};
          encrypted_media_.insert_or_assign(*thumbnail_uri, sealed_thumbnail->info);
        } else {
          content.info->thumbnail_url = *thumbnail_uri;
        }
        auto& thumb = content.info->thumbnail_info.emplace();
        thumb.w = video->thumbnail_width;
        thumb.h = video->thumbnail_height;
        thumb.mimetype = "image/png";
        thumb.size = static_cast<std::int64_t>(video->thumbnail.size());
      }
      return content;
    };
    auto [message, relates_to] = video   ? with_file(video_message())
                        : image ? with_file(loom::client::picture_message(said, width, height))
                                : with_file(loom::client::file_message(said));
    // Seed the server URI before sending the event: its echo may arrive
    // during send_room_event. The bytes are already here, including the
    // plaintext thumbnail of an encrypted upload.
    if (video && thumbnail_uri)
      sink_(change::avatar_loaded{media_use::thumbnail{}, *thumbnail_uri, std::move(video->thumbnail)});
    else if (image && !video)
      sink_(change::avatar_loaded{media_use::thumbnail{}, *uri, std::move(bytes)});
    auto sent = this->send_room_event(loom::cs::send_message{.room_id = room,
                                                      .event_type = "m.room.message",
                                                      .txn_id = local,
                                                      .body = std::move(message)},
                                      std::move(relates_to));
    if (!sent) {
      sink_(change::delivery_changed{in, local, delivery::failed{}});
      return;
    }
    sink_(change::message_acknowledged{in, local, sent->event_id});
  });
}

}  // namespace mux::proto::matrix::client

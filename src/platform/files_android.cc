// SPDX-License-Identifier: AGPL-3.0-only
module;
#include <SDL3/SDL.h>
#include <jni.h>
export module mux.platform.files;
import std;

export namespace mux::platform::files {
inline std::optional<std::string> read(const std::string& path) {
  // SDL understands content://, including non-seekable document providers.
  std::size_t size = 0;
  void* bytes = SDL_LoadFile(path.c_str(), &size);
  if (!bytes) return std::nullopt;
  std::string result(static_cast<const char*>(bytes), size);
  SDL_free(bytes);
  return result;
}
inline bool write(const std::string& path, std::string_view bytes) {
  auto* out = SDL_IOFromFile(path.c_str(), "wb");
  if (!out) return false;
  bool written = true;
  while (!bytes.empty()) {
    const auto count = SDL_WriteIO(out, bytes.data(), bytes.size());
    if (!count) { written = false; break; }
    bytes.remove_prefix(count);
  }
  return SDL_CloseIO(out) && written;
}
inline std::string name(const std::string& path) {
  if (!path.starts_with("content://")) return std::filesystem::path(path).filename().string();
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  if (!env || env->PushLocalFrame(32) != JNI_OK) return "attachment";
  std::string result = "attachment";
  auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
  auto uri_class = env->FindClass("android/net/Uri");
  // Android URI.toString() is ASCII escaped; arbitrary display names below are UTF-16.
  auto uri = env->CallStaticObjectMethod(uri_class, env->GetStaticMethodID(uri_class, "parse", "(Ljava/lang/String;)Landroid/net/Uri;"), env->NewStringUTF(path.c_str()));
  auto resolver = env->CallObjectMethod(activity, env->GetMethodID(env->GetObjectClass(activity), "getContentResolver", "()Landroid/content/ContentResolver;"));
  auto columns = env->NewObjectArray(1, env->FindClass("java/lang/String"), env->NewStringUTF("_display_name"));
  auto cursor = env->CallObjectMethod(resolver, env->GetMethodID(env->GetObjectClass(resolver), "query",
      "(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Landroid/database/Cursor;"), uri, columns, nullptr, nullptr, nullptr);
  if (!env->ExceptionCheck() && cursor) {
    auto cls = env->FindClass("android/database/Cursor");
    if (env->CallBooleanMethod(cursor, env->GetMethodID(cls, "moveToFirst", "()Z"))) {
      auto value = static_cast<jstring>(env->CallObjectMethod(cursor, env->GetMethodID(cls, "getString", "(I)Ljava/lang/String;"), 0));
      if (value && !env->ExceptionCheck()) {
        auto string_class = env->FindClass("java/lang/String");
        auto bytes = static_cast<jbyteArray>(env->CallObjectMethod(value, env->GetMethodID(string_class, "getBytes", "(Ljava/lang/String;)[B"), env->NewStringUTF("UTF-8")));
        if (bytes && !env->ExceptionCheck()) {
          result.resize(env->GetArrayLength(bytes));
          env->GetByteArrayRegion(bytes, 0, static_cast<jsize>(result.size()), reinterpret_cast<jbyte*>(result.data()));
        }
      }
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->CallVoidMethod(cursor, env->GetMethodID(cls, "close", "()V"));
  }
  if (env->ExceptionCheck()) env->ExceptionClear();
  env->PopLocalFrame(nullptr);
  // A provider supplies a name, never a path to be trusted for local writes.
  result = std::filesystem::path(result).filename().string();
  return result.empty() || result == "." || result == ".." ? "attachment" : result;
}
}

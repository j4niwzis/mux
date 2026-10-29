// SPDX-License-Identifier: AGPL-3.0-only
// UnifiedPush on Android (its connector specification, AND_3): distributors
// are the apps that take the org.unifiedpush.android.distributor.REGISTER
// broadcast; mux registers with the one chosen by a broadcast to it, and
// hears back through the SDL fork's PushReceiver (exported in the manifest
// for the connector's actions), whose intents wait in a queue of the fork's
// (SDL_WaitAndroidPush) -- this thread blocked on it, not polling.
module;
// The SDL fork's: what the distributor said, as "<what>\n<token>\n<value>",
// waited for; and that wait ended from another thread.
extern "C" int SDL_WaitAndroidPush(char* out, int size);
extern "C" void SDL_WakeAndroidPush(void);
#include <SDL3/SDL.h>
#include <jni.h>
export module mux.platform.push.backend;

import std;
import mux.platform.jni;
import mux.platform.push.types;

namespace mux::platform::push::backend {

constexpr const char* kRegister = "org.unifiedpush.android.distributor.REGISTER";

// What is asked of the distributor, each with its action and its extras.
struct registering {
  static constexpr const char* action = kRegister;
  std::string_view description;
};
struct unregistering {
  static constexpr const char* action = "org.unifiedpush.android.distributor.UNREGISTER";
};

jobject activity_of() { return static_cast<jobject>(SDL_GetAndroidActivity()); }

std::string own_package(JNIEnv* env, jobject activity) {
  return text_of(env, static_cast<jstring>(env->CallObjectMethod(
                          activity, env->GetMethodID(env->FindClass("android/content/Context"), "getPackageName", "()Ljava/lang/String;"))));
}

// The distributor apps installed: the packages whose receivers take REGISTER
// (the manifest's <queries> lets mux see them, Android 11 on), mux's own
// left out.
std::vector<std::string> installed_distributors() {
  const jni_frame frame;
  JNIEnv* env = frame.env();
  jobject activity = activity_of();
  if (!env || !activity)
    return {};
  jclass intent_class = env->FindClass("android/content/Intent");
  jobject asked = env->NewObject(intent_class, env->GetMethodID(intent_class, "<init>", "(Ljava/lang/String;)V"), env->NewStringUTF(kRegister));
  jobject packages = env->CallObjectMethod(activity, env->GetMethodID(env->FindClass("android/content/Context"), "getPackageManager",
                                                                     "()Landroid/content/pm/PackageManager;"));
  if (!asked || !packages || frame.threw())
    return {};
  jobject found = env->CallObjectMethod(packages, env->GetMethodID(env->GetObjectClass(packages), "queryBroadcastReceivers",
                                                                   "(Landroid/content/Intent;I)Ljava/util/List;"),
                                        asked, jint{0});
  if (!found || frame.threw())
    return {};
  jclass list_class = env->FindClass("java/util/List");
  const jint count = env->CallIntMethod(found, env->GetMethodID(list_class, "size", "()I"));
  const jmethodID at = env->GetMethodID(list_class, "get", "(I)Ljava/lang/Object;");
  jclass resolve_class = env->FindClass("android/content/pm/ResolveInfo");
  const jfieldID info_field = env->GetFieldID(resolve_class, "activityInfo", "Landroid/content/pm/ActivityInfo;");
  const jfieldID package_field = env->GetFieldID(env->FindClass("android/content/pm/PackageItemInfo"), "packageName", "Ljava/lang/String;");
  const std::string own = own_package(env, activity);
  auto names = std::views::iota(jint{0}, count) | std::views::transform([&](jint i) {
                 jobject resolved = env->CallObjectMethod(found, at, i);
                 jobject info = resolved ? env->GetObjectField(resolved, info_field) : nullptr;
                 std::string name = info ? text_of(env, static_cast<jstring>(env->GetObjectField(info, package_field))) : std::string();
                 if (resolved)
                   env->DeleteLocalRef(resolved);
                 return name;
               }) |
               std::views::filter([&](const std::string& name) { return !name.empty() && name != own; }) | std::ranges::to<std::vector>();
  std::ranges::sort(names);
  const auto [first, last] = std::ranges::unique(names);
  names.erase(first, last);
  return names;
}

// REGISTER's extras: what mux is, and a PendingIntent of its own the
// distributor knows it by, as AND_3 asks. UNREGISTER has only the token.
void put_extras(JNIEnv* env, const jni_frame& frame, jobject activity, jobject intent, const registering& asked) {
  jclass intent_class = env->FindClass("android/content/Intent");
  const jmethodID put = env->GetMethodID(intent_class, "putExtra", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;");
  env->CallObjectMethod(intent, put, env->NewStringUTF("application"), string_of(env, own_package(env, activity)));
  env->CallObjectMethod(intent, put, env->NewStringUTF("message"), string_of(env, asked.description));
  // Of an intent of no meaning, immutable: only whose it is matters.
  jobject mark = env->NewObject(intent_class, env->GetMethodID(intent_class, "<init>", "(Ljava/lang/String;)V"),
                                env->NewStringUTF("org.unifiedpush.dummy_app"));
  jclass pending = env->FindClass("android/app/PendingIntent");
  constexpr jint immutable = 0x04000000;
  jobject pi = env->CallStaticObjectMethod(
      pending, env->GetStaticMethodID(pending, "getBroadcast", "(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;"),
      activity, jint{0}, mark, SDL_GetAndroidSDKVersion() >= 23 ? immutable : jint{0});
  if (pi && !frame.threw())
    env->CallObjectMethod(intent, env->GetMethodID(intent_class, "putExtra", "(Ljava/lang/String;Landroid/os/Parcelable;)Landroid/content/Intent;"),
                          env->NewStringUTF("pi"), pi);
}
void put_extras(JNIEnv*, const jni_frame&, jobject, jobject, const unregistering&) {}

// A broadcast to the distributor, by the connection's token.
template <class Asked>
bool tell_distributor(const Asked& asked, const std::string& distributor, const std::string& token) {
  const jni_frame frame;
  JNIEnv* env = frame.env();
  jobject activity = activity_of();
  if (!env || !activity)
    return false;
  jclass intent_class = env->FindClass("android/content/Intent");
  jobject intent = env->NewObject(intent_class, env->GetMethodID(intent_class, "<init>", "(Ljava/lang/String;)V"), env->NewStringUTF(Asked::action));
  if (!intent || frame.threw())
    return false;
  env->CallObjectMethod(intent, env->GetMethodID(intent_class, "setPackage", "(Ljava/lang/String;)Landroid/content/Intent;"),
                        string_of(env, distributor));
  env->CallObjectMethod(intent, env->GetMethodID(intent_class, "putExtra", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;"),
                        env->NewStringUTF("token"), string_of(env, token));
  put_extras(env, frame, activity, intent, asked);
  env->CallVoidMethod(activity, env->GetMethodID(env->FindClass("android/content/Context"), "sendBroadcast", "(Landroid/content/Intent;)V"),
                      intent);
  return !frame.threw();
}

// What the fork's queue said, read once into an event: by what it names,
// for this connection's token only; the first endpoint also the
// distributor's yes (`heard` kept by the caller).
constexpr std::array<std::string_view, 4> kSaid{"endpoint", "message", "unregistered", "failed"};
std::vector<event> said_of(std::string_view text, std::string_view token, const std::string& distributor, bool& heard) {
  const auto parts = text | std::views::split('\n') | std::views::transform([](auto part) { return std::string_view(part); }) |
                     std::ranges::to<std::vector>();
  if (parts.size() < 3 || parts[1] != token)
    return {};
  const std::string_view value = text.substr(static_cast<std::size_t>(parts[2].data() - text.data()));
  switch (std::ranges::find(kSaid, parts[0]) - kSaid.begin()) {
    case 0:
      if (std::exchange(heard, true))
        return {event{endpoint{std::string(value)}}};
      return {event{registered{distributor}}, event{endpoint{std::string(value)}}};
    case 1:
      return {event{message{}}};
    case 2:
      return {event{unregistered{}}};
    case 3:
      return {event{refused{value.empty() ? "refused by " + distributor : std::string(value)}}};
    default:
      return {};
  }
}

}  // namespace mux::platform::push::backend

export namespace mux::platform::push::backend {

// The distributor chosen -- `wanted` where it is installed, else the first
// -- registered with, and what it says handed to `sink` until `stop`; the
// registration dropped as it stops where `forget` says so.
template <class Sink>
void run(std::stop_token stop, std::string, std::string token, std::string description, std::string wanted,
         std::shared_ptr<const std::atomic<bool>> forget, Sink sink) {
  const std::vector<std::string> names = installed_distributors();
  if (names.empty()) {
    sink(event{no_distributor{}});
    return;
  }
  const std::string chosen = std::ranges::contains(names, wanted) ? wanted : names.front();
  sink(event{distributors{names, chosen}});
  if (!tell_distributor(registering{description}, chosen, token)) {
    sink(event{refused{"could not reach " + chosen}});
    return;
  }
  const std::stop_callback woken(stop, [] { SDL_WakeAndroidPush(); });
  std::array<char, 8192> said{};
  bool heard = false;
  while (!stop.stop_requested()) {
    const int size = SDL_WaitAndroidPush(said.data(), static_cast<int>(said.size()));
    if (size < 0 || size >= static_cast<int>(said.size()))
      continue;  // woken, or more than an endpoint can be
    for (event& got : said_of(std::string_view(said.data(), static_cast<std::size_t>(size)), token, chosen, heard))
      sink(std::move(got));
  }
  if (forget && forget->load())
    (void)tell_distributor(unregistering{}, chosen, token);
}

}  // namespace mux::platform::push::backend

// SPDX-License-Identifier: AGPL-3.0-only
// Android's notifications: a notification posted through NotificationManager,
// in a channel of mux's own (Android 8 and later need one, and list an app
// with none as one that sends nothing), opening mux when it is pressed.
// Through JNI, from whichever thread asks: SDL attaches it to the VM.
module;
// The SDL fork's: what an intent that brought mux up asked of it.
extern "C" int SDL_TakeAndroidRequest(char *out, int size);
#include <SDL3/SDL.h>
#include <jni.h>
export module mux.platform.notifications.backend;

import std;
import mux.platform.jni;

namespace mux::platform::notifications::backend {

// mux's channels, a sound being the channel's from Android 8 on: messages
// with the system's notification sound, and the same without -- where the
// settings say no sound. Made each time -- making one again changes nothing
// the user set in it.
struct channel {
  const char* id;
  const char* name;
  bool sound;
};
constexpr channel kWithSound{"messages", "Messages", true};
constexpr channel kSilent{"messages-silent", "Messages without sound", false};
void make_channel(JNIEnv* env, jobject manager, const channel& made) {
  jclass channel_class = env->FindClass("android/app/NotificationChannel");
  constexpr jint importance_high = 4;
  jobject channel = env->NewObject(channel_class,
                                   env->GetMethodID(channel_class, "<init>", "(Ljava/lang/String;Ljava/lang/CharSequence;I)V"),
                                   env->NewStringUTF(made.id), string_of(env, made.name), importance_high);
  if (!channel)
    return;
  if (!made.sound)
    env->CallVoidMethod(channel, env->GetMethodID(channel_class, "setSound", "(Landroid/net/Uri;Landroid/media/AudioAttributes;)V"),
                        nullptr, nullptr);
  env->CallVoidMethod(manager, env->GetMethodID(env->GetObjectClass(manager), "createNotificationChannel",
                                                "(Landroid/app/NotificationChannel;)V"),
                      channel);
}

// What pressing it does: mux brought up, as its launcher icon would -- and
// its chat opened, where it is a chat's (the intent's request, which the SDL
// fork keeps for take_opened).
jobject opening_mux(JNIEnv* env, jobject activity, int sdk, std::string_view chat) {
  jclass context_class = env->FindClass("android/content/Context");
  jobject packages = env->CallObjectMethod(activity, env->GetMethodID(context_class, "getPackageManager",
                                                                     "()Landroid/content/pm/PackageManager;"));
  jobject name = env->CallObjectMethod(activity, env->GetMethodID(context_class, "getPackageName", "()Ljava/lang/String;"));
  if (!packages || !name)
    return nullptr;
  jobject intent = env->CallObjectMethod(packages, env->GetMethodID(env->GetObjectClass(packages), "getLaunchIntentForPackage",
                                                                    "(Ljava/lang/String;)Landroid/content/Intent;"),
                                         name);
  if (!intent)
    return nullptr;
  if (!chat.empty())
    env->CallObjectMethod(intent, env->GetMethodID(env->GetObjectClass(intent), "putExtra",
                                                   "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;"),
                          env->NewStringUTF("org.libsdl.app.request"), string_of(env, std::format("open\n{}", chat)));
  // FLAG_IMMUTABLE (API 23), which Android 12 and later require of every
  // PendingIntent; FLAG_UPDATE_CURRENT, its request the newest. One for each
  // chat (its request code): else every notification opened the last one's.
  constexpr jint immutable = 0x04000000, update_current = 0x08000000;
  jclass pending = env->FindClass("android/app/PendingIntent");
  return env->CallStaticObjectMethod(pending, env->GetStaticMethodID(pending, "getActivity",
                                                                     "(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;"),
                                     activity, static_cast<jint>(std::hash<std::string_view>{}(chat)), intent,
                                     (sdk >= 23 ? immutable : 0) | update_current);
}

// An action of a chat's notification: a broadcast to the SDL fork's
// receiver -- mux not brought up -- asking `request` of mux (the action's
// word and the chat, a line each); a reply's with a field to type it in
// (RemoteInput), whose PendingIntent must be mutable for the text to reach
// it. One PendingIntent for each chat and action (its request code).
jobject chat_action(JNIEnv* env, jobject activity, int sdk, std::string_view word, std::string_view chat, std::string_view label,
                    bool reply, jint icon) {
  jclass intent_class = env->FindClass("android/content/Intent");
  jobject intent = env->NewObject(intent_class, env->GetMethodID(intent_class, "<init>", "(Ljava/lang/String;)V"),
                                  env->NewStringUTF("org.libsdl.app.NOTIFICATION_ACTION"));
  jclass context_class = env->FindClass("android/content/Context");
  jobject name = env->CallObjectMethod(activity, env->GetMethodID(context_class, "getPackageName", "()Ljava/lang/String;"));
  if (!intent || !name)
    return nullptr;
  // Its receiver by name: a class of the app's is not found by FindClass on
  // a thread of mux's own.
  env->CallObjectMethod(intent, env->GetMethodID(intent_class, "setClassName", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;"),
                        name, env->NewStringUTF("org.libsdl.nativeapp.Receiver"));
  const std::string request = std::format("{}\n{}", word, chat);
  env->CallObjectMethod(intent, env->GetMethodID(intent_class, "putExtra", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;"),
                        env->NewStringUTF("org.libsdl.app.request"), string_of(env, request));
  constexpr jint immutable = 0x04000000, update_current = 0x08000000, mutable_ = 0x02000000;
  const jint flags = update_current | (reply ? (sdk >= 31 ? mutable_ : 0) : (sdk >= 23 ? immutable : 0));
  jclass pending = env->FindClass("android/app/PendingIntent");
  jobject sent = env->CallStaticObjectMethod(pending, env->GetStaticMethodID(pending, "getBroadcast",
                                                                             "(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;"),
                                             activity, static_cast<jint>(std::hash<std::string_view>{}(request)), intent, flags);
  if (!sent)
    return nullptr;
  jclass builder_class = env->FindClass("android/app/Notification$Action$Builder");
  jobject builder = env->NewObject(builder_class,
                                   env->GetMethodID(builder_class, "<init>", "(ILjava/lang/CharSequence;Landroid/app/PendingIntent;)V"),
                                   icon, string_of(env, label), sent);
  if (!builder)
    return nullptr;
  if (reply) {
    jclass remote_class = env->FindClass("android/app/RemoteInput$Builder");
    jobject remote = env->NewObject(remote_class, env->GetMethodID(remote_class, "<init>", "(Ljava/lang/String;)V"),
                                    env->NewStringUTF("org.libsdl.app.reply"));
    env->CallObjectMethod(remote, env->GetMethodID(remote_class, "setLabel", "(Ljava/lang/CharSequence;)Landroid/app/RemoteInput$Builder;"),
                          string_of(env, label));
    jobject input = env->CallObjectMethod(remote, env->GetMethodID(remote_class, "build", "()Landroid/app/RemoteInput;"));
    env->CallObjectMethod(builder, env->GetMethodID(builder_class, "addRemoteInput",
                                                    "(Landroid/app/RemoteInput;)Landroid/app/Notification$Action$Builder;"),
                          input);
  }
  return env->CallObjectMethod(builder, env->GetMethodID(builder_class, "build", "()Landroid/app/Notification$Action;"));
}
// What a chat's notification is known by: the chat, else its title.
jint notification_id(std::string_view title, std::string_view chat) {
  return static_cast<jint>(std::hash<std::string_view>{}(chat.empty() ? title : chat));
}

// The small icon: the platform's own chat icon, found by name -- mux ships
// no drawable of its own.
jint chat_icon(JNIEnv* env, jobject activity) {
  jobject resources = env->CallObjectMethod(activity, env->GetMethodID(env->FindClass("android/content/Context"), "getResources",
                                                                      "()Landroid/content/res/Resources;"));
  if (!resources)
    return 0;
  return env->CallIntMethod(resources, env->GetMethodID(env->GetObjectClass(resources), "getIdentifier",
                                                        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I"),
                            env->NewStringUTF("stat_notify_chat"), env->NewStringUTF("drawable"), env->NewStringUTF("android"));
}

}  // namespace mux::platform::notifications::backend

export namespace mux::platform::notifications::backend {

// Posted; false where Android could not be asked. One notification for each
// title -- a chat, or a person -- the newest in place of the one before;
// with the system's notification sound, or silent.
[[nodiscard]] inline bool notify_chat(std::string_view title, std::string_view text, bool sound, std::string_view chat) {
  const jni_frame frame;
  JNIEnv* env = frame.env();
  if (!env)
    return false;
  const auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (!activity)
    return false;
  const int sdk = SDL_GetAndroidSDKVersion();
  jobject manager = env->CallObjectMethod(activity, env->GetMethodID(env->FindClass("android/content/Context"), "getSystemService",
                                                                    "(Ljava/lang/String;)Ljava/lang/Object;"),
                                          env->NewStringUTF("notification"));
  if (!manager || frame.threw())
    return false;
  const channel& posted_in = sound ? kWithSound : kSilent;
  if (sdk >= 26)
    make_channel(env, manager, posted_in);
  if (frame.threw())
    return false;
  jclass builder_class = env->FindClass("android/app/Notification$Builder");
  jobject builder = sdk >= 26 ? env->NewObject(builder_class,
                                               env->GetMethodID(builder_class, "<init>", "(Landroid/content/Context;Ljava/lang/String;)V"),
                                               activity, env->NewStringUTF(posted_in.id))
                              : env->NewObject(builder_class, env->GetMethodID(builder_class, "<init>", "(Landroid/content/Context;)V"),
                                               activity);
  if (!builder || frame.threw())
    return false;
  const auto set = [&](const char* method, const char* signature, auto... arguments) {
    env->CallObjectMethod(builder, env->GetMethodID(builder_class, method, signature), arguments...);
  };
  constexpr const char* returns_builder_text = "(Ljava/lang/CharSequence;)Landroid/app/Notification$Builder;";
  set("setSmallIcon", "(I)Landroid/app/Notification$Builder;", chat_icon(env, activity));
  set("setContentTitle", returns_builder_text, string_of(env, title));
  set("setContentText", returns_builder_text, string_of(env, text));
  set("setAutoCancel", "(Z)Landroid/app/Notification$Builder;", JNI_TRUE);
  // Before channels, a notification's own priority: high, shown at once.
  constexpr jint priority_high = 1;
  set("setPriority", "(I)Landroid/app/Notification$Builder;", priority_high);
  // Before channels, the sound is the notification's: the default one.
  constexpr jint default_sound = 1;
  if (sound)
    set("setDefaults", "(I)Landroid/app/Notification$Builder;", default_sound);
  if (jobject opening = opening_mux(env, activity, sdk, chat))
    set("setContentIntent", "(Landroid/app/PendingIntent;)Landroid/app/Notification$Builder;", opening);
  // A chat's: read, or answered, from where it is shown (Android 7's reply
  // field) -- mux not brought up.
  if (!chat.empty() && sdk >= 24) {
    for (const auto& [word, label, reply] : {std::tuple{"read", "Mark as read", false}, std::tuple{"reply", "Reply", true}})
      if (jobject action = chat_action(env, activity, sdk, word, chat, label, reply, chat_icon(env, activity)))
        set("addAction", "(Landroid/app/Notification$Action;)Landroid/app/Notification$Builder;", action);
  }
  if (frame.threw())
    return false;
  jobject notification = env->CallObjectMethod(builder, env->GetMethodID(builder_class, "build", "()Landroid/app/Notification;"));
  if (!notification || frame.threw())
    return false;
  env->CallVoidMethod(manager, env->GetMethodID(env->GetObjectClass(manager), "notify", "(ILandroid/app/Notification;)V"),
                      notification_id(title, chat), notification);
  return !frame.threw();
}
// A chat's notification taken away: read, or answered, from it.
inline void dismiss_chat(std::string_view chat) {
  const jni_frame frame;
  JNIEnv* env = frame.env();
  const auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (!env || !activity)
    return;
  jobject manager = env->CallObjectMethod(activity, env->GetMethodID(env->FindClass("android/content/Context"), "getSystemService",
                                                                    "(Ljava/lang/String;)Ljava/lang/Object;"),
                                          env->NewStringUTF("notification"));
  if (manager && !frame.threw())
    env->CallVoidMethod(manager, env->GetMethodID(env->GetObjectClass(manager), "cancel", "(I)V"), notification_id({}, chat));
}
[[nodiscard]] inline bool notify(std::string_view title, std::string_view text, bool sound) {
  return notify_chat(title, text, sound, {});
}
// The chat a notification pressed asked to open, as notify_chat put it.
[[nodiscard]] inline std::optional<std::string> take_opened() {
  std::array<char, 1024> said{};
  if (SDL_TakeAndroidRequest(said.data(), static_cast<int>(said.size())) <= 0)
    return std::nullopt;
  return std::string(said.data());
}

}  // namespace mux::platform::notifications::backend

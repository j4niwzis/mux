// SPDX-License-Identifier: AGPL-3.0-only
// Android's notifications: a notification posted through NotificationManager,
// in a channel of mux's own (Android 8 and later need one, and list an app
// with none as one that sends nothing), opening mux when it is pressed.
// Through JNI, from whichever thread asks: SDL attaches it to the VM.
module;
#include <SDL3/SDL.h>
#include <jni.h>
export module mux.platform.notifications.backend;

import std;

namespace mux::platform::notifications::backend {

// The JNI environment of this thread, with a frame its local references are
// released with, and whatever exception a call threw cleared as it goes.
class jni_frame {
 public:
  jni_frame() : env_(static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv())) {
    ready_ = env_ && env_->PushLocalFrame(64) == JNI_OK;
  }
  jni_frame(const jni_frame&) = delete;
  jni_frame& operator=(const jni_frame&) = delete;
  ~jni_frame() {
    if (env_ && env_->ExceptionCheck())
      env_->ExceptionClear();
    if (ready_)
      env_->PopLocalFrame(nullptr);
  }
  [[nodiscard]] JNIEnv* env() const { return ready_ ? env_ : nullptr; }
  // Whether the last call threw: cleared, and said.
  [[nodiscard]] bool threw() const {
    if (!env_->ExceptionCheck())
      return false;
    env_->ExceptionClear();
    return true;
  }

 private:
  JNIEnv* env_;
  bool ready_ = false;
};

// UTF-8 as a java.lang.String: through new String(bytes, "UTF-8"), not
// NewStringUTF, whose modified UTF-8 is not UTF-8 for what is past the BMP
// (emoji). The bytes are copied into a Java array -- what JNI reads them from.
jstring string_of(JNIEnv* env, std::string_view text) {
  const auto bytes = std::ranges::to<std::vector>(std::views::transform(text, [](char c) { return static_cast<jbyte>(c); }));
  jbyteArray array = env->NewByteArray(static_cast<jsize>(bytes.size()));
  if (!array)
    return nullptr;
  env->SetByteArrayRegion(array, 0, static_cast<jsize>(bytes.size()), bytes.data());
  jclass string_class = env->FindClass("java/lang/String");
  return static_cast<jstring>(env->NewObject(string_class, env->GetMethodID(string_class, "<init>", "([BLjava/lang/String;)V"),
                                             array, env->NewStringUTF("UTF-8")));
}

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

// What pressing it does: mux brought up, as its launcher icon would.
jobject opening_mux(JNIEnv* env, jobject activity, int sdk) {
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
  // FLAG_IMMUTABLE (API 23), which Android 12 and later require of every
  // PendingIntent.
  constexpr jint immutable = 0x04000000;
  jclass pending = env->FindClass("android/app/PendingIntent");
  return env->CallStaticObjectMethod(pending, env->GetStaticMethodID(pending, "getActivity",
                                                                     "(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;"),
                                     activity, 0, intent, sdk >= 23 ? immutable : 0);
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
[[nodiscard]] inline bool notify(std::string_view title, std::string_view text, bool sound) {
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
  if (jobject opening = opening_mux(env, activity, sdk))
    set("setContentIntent", "(Landroid/app/PendingIntent;)Landroid/app/Notification$Builder;", opening);
  if (frame.threw())
    return false;
  jobject notification = env->CallObjectMethod(builder, env->GetMethodID(builder_class, "build", "()Landroid/app/Notification;"));
  if (!notification || frame.threw())
    return false;
  env->CallVoidMethod(manager, env->GetMethodID(env->GetObjectClass(manager), "notify", "(ILandroid/app/Notification;)V"),
                      static_cast<jint>(std::hash<std::string_view>{}(title)), notification);
  return !frame.threw();
}

}  // namespace mux::platform::notifications::backend

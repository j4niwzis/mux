// SPDX-License-Identifier: AGPL-3.0-only
// mux.platform.permissions -- Android's runtime permissions: RECORD_AUDIO,
// asked of the user the first time a call needs the microphone (Android 6
// and later ask at run time; before, the manifest's word is enough).
module;
#include <SDL3/SDL.h>
#include <jni.h>
export module mux.platform.permissions;

export namespace mux::platform::permissions {
[[nodiscard]] inline bool microphone() {
  if (SDL_GetAndroidSDKVersion() < 23)
    return true;
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  if (!env || env->PushLocalFrame(16) != JNI_OK)
    return false;
  bool granted = false;
  if (auto activity = static_cast<jobject>(SDL_GetAndroidActivity())) {
    auto permission = env->NewStringUTF("android.permission.RECORD_AUDIO");
    constexpr jint kGranted = 0;  // PackageManager.PERMISSION_GRANTED
    granted = env->CallIntMethod(activity, env->GetMethodID(env->FindClass("android/content/Context"), "checkSelfPermission",
                                                            "(Ljava/lang/String;)I"),
                                 permission) == kGranted;
    if (!granted && !env->ExceptionCheck()) {
      auto asked = env->NewObjectArray(1, env->FindClass("java/lang/String"), permission);
      env->CallVoidMethod(activity, env->GetMethodID(env->FindClass("android/app/Activity"), "requestPermissions",
                                                     "([Ljava/lang/String;I)V"),
                          asked, 2);
    }
  }
  if (env->ExceptionCheck())
    env->ExceptionClear();
  env->PopLocalFrame(nullptr);
  return granted;
}
}  // namespace mux::platform::permissions

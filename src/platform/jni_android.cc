// SPDX-License-Identifier: AGPL-3.0-only
// mux.platform.jni -- What every Android backend calls Java through: the
// JNI environment of this thread, and text both ways. From whichever thread
// asks: SDL attaches it to the VM.
module;
#include <SDL3/SDL.h>
#include <jni.h>
export module mux.platform.jni;

import std;

export namespace mux::platform {

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
  const auto bytes = text | std::views::transform([](char c) { return static_cast<jbyte>(c); }) | std::ranges::to<std::vector>();
  jbyteArray array = env->NewByteArray(static_cast<jsize>(bytes.size()));
  if (!array)
    return nullptr;
  env->SetByteArrayRegion(array, 0, static_cast<jsize>(bytes.size()), bytes.data());
  jclass string_class = env->FindClass("java/lang/String");
  return static_cast<jstring>(env->NewObject(string_class, env->GetMethodID(string_class, "<init>", "([BLjava/lang/String;)V"),
                                             array, env->NewStringUTF("UTF-8")));
}

// A java.lang.String as UTF-8: its getBytes("UTF-8"), for the same reason,
// copied out of the Java array element by element; empty for null.
std::string text_of(JNIEnv* env, jstring text) {
  if (!text)
    return {};
  auto array = static_cast<jbyteArray>(env->CallObjectMethod(
      text, env->GetMethodID(env->FindClass("java/lang/String"), "getBytes", "(Ljava/lang/String;)[B"), env->NewStringUTF("UTF-8")));
  if (!array || env->ExceptionCheck())
    return {};
  std::vector<jbyte> bytes(static_cast<std::size_t>(env->GetArrayLength(array)));
  env->GetByteArrayRegion(array, 0, static_cast<jsize>(bytes.size()), bytes.data());
  return bytes | std::views::transform([](jbyte b) { return static_cast<char>(b); }) | std::ranges::to<std::string>();
}

}  // namespace mux::platform

// SPDX-License-Identifier: AGPL-3.0-only
// Platform services needed before mux creates its network or opens its files.
#include "android_runtime.h"
#include <SDL3/SDL.h>
#include <jni.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
struct frame {
  JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  bool ready = env && env->PushLocalFrame(64) == JNI_OK;
  ~frame() {
    if (env && env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
    if (ready) env->PopLocalFrame(nullptr);
  }
};
bool certificates(JNIEnv* env, const std::filesystem::path& destination) {
  auto factory_class = env->FindClass("javax/net/ssl/TrustManagerFactory");
  auto algorithm = env->CallStaticObjectMethod(factory_class, env->GetStaticMethodID(factory_class,
      "getDefaultAlgorithm", "()Ljava/lang/String;"));
  auto factory = env->CallStaticObjectMethod(factory_class, env->GetStaticMethodID(factory_class,
      "getInstance", "(Ljava/lang/String;)Ljavax/net/ssl/TrustManagerFactory;"), algorithm);
  if (!factory || env->ExceptionCheck()) return false;
  env->CallVoidMethod(factory, env->GetMethodID(factory_class, "init", "(Ljava/security/KeyStore;)V"), nullptr);
  if (env->ExceptionCheck()) return false;
  auto managers = static_cast<jobjectArray>(env->CallObjectMethod(factory, env->GetMethodID(factory_class,
      "getTrustManagers", "()[Ljavax/net/ssl/TrustManager;")));
  if (!managers || env->ExceptionCheck()) return false;
  auto trust_class = env->FindClass("javax/net/ssl/X509TrustManager");
  auto cert_class = env->FindClass("java/security/cert/Certificate");
  auto base64 = env->FindClass("android/util/Base64");
  auto temporary = destination;
  temporary += ".tmp";
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  std::size_t count = 0;
  for (jsize i = 0; output && i < env->GetArrayLength(managers); ++i) {
    auto manager = env->GetObjectArrayElement(managers, i);
    if (!env->IsInstanceOf(manager, trust_class)) { env->DeleteLocalRef(manager); continue; }
    auto certs = static_cast<jobjectArray>(env->CallObjectMethod(manager, env->GetMethodID(trust_class,
        "getAcceptedIssuers", "()[Ljava/security/cert/X509Certificate;")));
    if (!certs || env->ExceptionCheck()) return false;
    for (jsize j = 0; j < env->GetArrayLength(certs); ++j) {
      auto cert = env->GetObjectArrayElement(certs, j);
      auto der = env->CallObjectMethod(cert, env->GetMethodID(cert_class, "getEncoded", "()[B"));
      if (!der || env->ExceptionCheck()) return false;
      auto text = static_cast<jstring>(env->CallStaticObjectMethod(base64, env->GetStaticMethodID(base64,
          "encodeToString", "([BI)Ljava/lang/String;"), der, 2 /* NO_WRAP */));
      if (!text || env->ExceptionCheck()) return false;
      const char* bytes = env->GetStringUTFChars(text, nullptr); // Base64 is ASCII.
      if (!bytes) return false;
      output << "-----BEGIN CERTIFICATE-----\n" << bytes << "\n-----END CERTIFICATE-----\n";
      env->ReleaseStringUTFChars(text, bytes);
      env->DeleteLocalRef(text);
      env->DeleteLocalRef(der);
      env->DeleteLocalRef(cert);
      ++count;
    }
    env->DeleteLocalRef(certs);
    env->DeleteLocalRef(manager);
  }
  output.close();
  if (!count || !output) return false;
  std::filesystem::rename(temporary, destination);
  // Not SSL_CERT_FILE: OpenSSL reads such a file whole or not at all, and a
  // phone's store may hold one certificate it cannot parse -- then nothing
  // was trusted, and every server failed to verify. net.cc's client_tls()
  // adds them one by one from here, passing over what it cannot read.
  return setenv("MUX_CA_FILE", destination.c_str(), 1) == 0;
}

// Android 13 and later post a notification only for an app the user let:
// asked once at the start, where it was not given yet. The answer is the
// system's to keep; nothing here waits for it.
void ask_to_notify(JNIEnv* env) {
  if (SDL_GetAndroidSDKVersion() < 33) return;
  auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (!activity) return;
  auto permission = env->NewStringUTF("android.permission.POST_NOTIFICATIONS");
  auto context = env->FindClass("android/content/Context");
  constexpr jint granted = 0;  // PackageManager.PERMISSION_GRANTED
  if (env->CallIntMethod(activity, env->GetMethodID(context, "checkSelfPermission", "(Ljava/lang/String;)I"), permission) ==
          granted || env->ExceptionCheck()) return;
  auto asked = env->NewObjectArray(1, env->FindClass("java/lang/String"), permission);
  env->CallVoidMethod(activity, env->GetMethodID(env->FindClass("android/app/Activity"), "requestPermissions",
      "([Ljava/lang/String;I)V"), asked, 1);
}
}

bool mux_android_initialize() {
  frame jni;
  if (!jni.ready) return false;
  const char* files = SDL_GetAndroidInternalStoragePath();
  const char* cache = SDL_GetAndroidCachePath();
  if (!files || !cache) return false;
  const std::filesystem::path root(files);
  const auto config = root / "config", state = root / "state";
  std::filesystem::create_directories(config);
  std::filesystem::create_directories(state);
  std::filesystem::create_directories(cache);
  if (setenv("HOME", files, 1) || setenv("XDG_CONFIG_HOME", config.c_str(), 1) ||
      setenv("XDG_STATE_HOME", state.c_str(), 1) || setenv("XDG_CACHE_HOME", cache, 1)) return false;
  ask_to_notify(jni.env);
  if (jni.env->ExceptionCheck()) jni.env->ExceptionClear();
  // Export the platform's trust anchors for OpenSSL; peer/hostname checking
  // stays enabled. Refresh on each launch, including system CA updates.
  return certificates(jni.env, std::filesystem::path(cache) / "mux-ca.pem");
}

bool mux_android_nameserver(char* output, size_t capacity) {
  frame jni;
  if (!jni.ready || !capacity) return false;
  auto* env = jni.env;
  auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (!activity) return false;
  auto manager = env->CallObjectMethod(activity, env->GetMethodID(env->GetObjectClass(activity),
      "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;"), env->NewStringUTF("connectivity"));
  if (!manager || env->ExceptionCheck()) return false;
  auto cls = env->FindClass("android/net/ConnectivityManager");
  // The active network: getActiveNetwork is API 23. Before it, the first
  // network there is (getAllNetworks, API 21); where it names no DNS server,
  // the platform's resolver is used, as below.
  const auto first_network = [&]() -> jobject {
    auto all = static_cast<jobjectArray>(env->CallObjectMethod(manager,
        env->GetMethodID(cls, "getAllNetworks", "()[Landroid/net/Network;")));
    return all && !env->ExceptionCheck() && env->GetArrayLength(all) > 0 ? env->GetObjectArrayElement(all, 0) : nullptr;
  };
  auto network = SDL_GetAndroidSDKVersion() >= 23
      ? env->CallObjectMethod(manager, env->GetMethodID(cls, "getActiveNetwork", "()Landroid/net/Network;"))
      : first_network();
  if (!network || env->ExceptionCheck()) return false;
  auto properties = env->CallObjectMethod(manager, env->GetMethodID(cls, "getLinkProperties", "(Landroid/net/Network;)Landroid/net/LinkProperties;"), network);
  if (!properties || env->ExceptionCheck()) return false;
  auto prop_class = env->FindClass("android/net/LinkProperties");
  // Do not send plaintext SRV queries around Android's Private DNS policy.
  // The normal XMPP fallback uses the platform resolver for domain:5222.
  if (SDL_GetAndroidSDKVersion() >= 28 && env->CallBooleanMethod(properties,
      env->GetMethodID(prop_class, "isPrivateDnsActive", "()Z"))) return false;
  auto servers = env->CallObjectMethod(properties, env->GetMethodID(prop_class, "getDnsServers", "()Ljava/util/List;"));
  if (!servers || env->ExceptionCheck()) return false;
  auto list = env->FindClass("java/util/List");
  if (env->CallIntMethod(servers, env->GetMethodID(list, "size", "()I")) < 1) return false;
  auto server = env->CallObjectMethod(servers, env->GetMethodID(list, "get", "(I)Ljava/lang/Object;"), 0);
  auto address = static_cast<jstring>(env->CallObjectMethod(server, env->GetMethodID(env->FindClass("java/net/InetAddress"), "getHostAddress", "()Ljava/lang/String;")));
  if (!address || env->ExceptionCheck()) return false;
  const char* bytes = env->GetStringUTFChars(address, nullptr);
  if (!bytes) return false;
  const auto length = std::strlen(bytes);
  if (length < capacity) std::memcpy(output, bytes, length + 1);
  env->ReleaseStringUTFChars(address, bytes);
  return length < capacity;
}

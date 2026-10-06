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
#include <mutex>
#include <vector>

namespace {
struct frame {
  JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  bool ready = env && env->PushLocalFrame(64) == JNI_OK;
  ~frame() {
    if (env && env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
    if (ready) env->PopLocalFrame(nullptr);
  }
};
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
  // Certificates are not exported for OpenSSL: Android itself is asked
  // whether it trusts each server's chain (mux_android_trusts), as its own
  // TLS would. A file of its CAs was read by OpenSSL whole or not at all.
  return true;
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

bool mux_android_trusts(const unsigned char* const* certificates, const size_t* sizes, size_t count, const char* host,
                        char* why, size_t capacity) {
  const auto said = [&](const char* text) {
    if (why && capacity) {
      std::strncpy(why, text, capacity - 1);
      why[capacity - 1] = '\0';
    }
    return false;
  };
  frame jni;
  if (!jni.ready || count == 0) return said("Android could not be asked");
  JNIEnv* env = jni.env;
  // The platform's default trust manager, made once and kept (a global
  // reference), behind X509TrustManagerExtensions: the check Android's own
  // TLS makes, the host given for its network security config.
  static jobject extensions = nullptr;
  static std::mutex making;
  {
    const std::lock_guard held(making);
    if (!extensions) {
      auto factory_class = env->FindClass("javax/net/ssl/TrustManagerFactory");
      auto algorithm = env->CallStaticObjectMethod(factory_class, env->GetStaticMethodID(factory_class,
          "getDefaultAlgorithm", "()Ljava/lang/String;"));
      auto factory = env->CallStaticObjectMethod(factory_class, env->GetStaticMethodID(factory_class,
          "getInstance", "(Ljava/lang/String;)Ljavax/net/ssl/TrustManagerFactory;"), algorithm);
      if (!factory || env->ExceptionCheck()) return said("no trust manager factory");
      env->CallVoidMethod(factory, env->GetMethodID(factory_class, "init", "(Ljava/security/KeyStore;)V"), nullptr);
      if (env->ExceptionCheck()) return said("the trust manager factory would not start");
      auto managers = static_cast<jobjectArray>(env->CallObjectMethod(factory, env->GetMethodID(factory_class,
          "getTrustManagers", "()[Ljavax/net/ssl/TrustManager;")));
      if (!managers || env->ExceptionCheck()) return said("no trust managers");
      auto trust_class = env->FindClass("javax/net/ssl/X509TrustManager");
      jobject manager = nullptr;
      for (jsize i = 0; !manager && i < env->GetArrayLength(managers); ++i)
        if (auto one = env->GetObjectArrayElement(managers, i); env->IsInstanceOf(one, trust_class))
          manager = one;
      if (!manager) return said("no X.509 trust manager");
      auto extensions_class = env->FindClass("android/net/http/X509TrustManagerExtensions");
      auto made = env->NewObject(extensions_class, env->GetMethodID(extensions_class, "<init>",
          "(Ljavax/net/ssl/X509TrustManager;)V"), manager);
      if (!made || env->ExceptionCheck()) return said("no X509TrustManagerExtensions");
      extensions = env->NewGlobalRef(made);
    }
  }
  // The chain as Java's certificates, leaf first.
  auto factory_class = env->FindClass("java/security/cert/CertificateFactory");
  auto factory = env->CallStaticObjectMethod(factory_class, env->GetStaticMethodID(factory_class, "getInstance",
      "(Ljava/lang/String;)Ljava/security/cert/CertificateFactory;"), env->NewStringUTF("X.509"));
  if (!factory || env->ExceptionCheck()) return said("no X.509 certificate factory");
  auto stream_class = env->FindClass("java/io/ByteArrayInputStream");
  auto x509_class = env->FindClass("java/security/cert/X509Certificate");
  auto chain = env->NewObjectArray(static_cast<jsize>(count), x509_class, nullptr);
  for (size_t at = 0; at < count; ++at) {
    // jbyte is signed: the DER's bytes converted one by one, not viewed.
    std::vector<jbyte> bytes(sizes[at]);
    for (size_t i = 0; i < sizes[at]; ++i) bytes[i] = static_cast<jbyte>(certificates[at][i]);
    auto array = env->NewByteArray(static_cast<jsize>(bytes.size()));
    env->SetByteArrayRegion(array, 0, static_cast<jsize>(bytes.size()), bytes.data());
    auto stream = env->NewObject(stream_class, env->GetMethodID(stream_class, "<init>", "([B)V"), array);
    auto certificate = env->CallObjectMethod(factory, env->GetMethodID(factory_class, "generateCertificate",
        "(Ljava/io/InputStream;)Ljava/security/cert/Certificate;"), stream);
    if (!certificate || env->ExceptionCheck()) return said("a certificate Android could not read");
    env->SetObjectArrayElement(chain, static_cast<jsize>(at), certificate);
    env->DeleteLocalRef(certificate);
    env->DeleteLocalRef(stream);
    env->DeleteLocalRef(array);
  }
  auto extensions_class = env->GetObjectClass(extensions);
  env->CallObjectMethod(extensions, env->GetMethodID(extensions_class, "checkServerTrusted",
      "([Ljava/security/cert/X509Certificate;Ljava/lang/String;Ljava/lang/String;)Ljava/util/List;"),
      chain, env->NewStringUTF("GENERIC"), env->NewStringUTF(host));
  if (env->ExceptionCheck()) {
    // Its reason: the exception's message, then the exception let go.
    jthrowable thrown = env->ExceptionOccurred();
    env->ExceptionClear();
    auto message = static_cast<jstring>(env->CallObjectMethod(thrown, env->GetMethodID(env->FindClass("java/lang/Throwable"),
        "getMessage", "()Ljava/lang/String;")));
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (message) {
      const char* text = env->GetStringUTFChars(message, nullptr);
      const bool refused = said(text ? text : "not trusted");
      if (text) env->ReleaseStringUTFChars(message, text);
      return refused;
    }
    return said("not trusted");
  }
  return true;
}

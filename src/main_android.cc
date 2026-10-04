// SPDX-License-Identifier: AGPL-3.0-only
#include "android_runtime.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_nativeactivity.h>
#include <android/native_activity.h>
#include <exception>

int mux_main(int argc, char** argv);
namespace {
int run(int argc, char** argv) {
  try {
    if (!mux_android_initialize()) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Android storage or certificate initialization failed");
      return 1;
    }
    return mux_main(argc, argv);
  } catch (const std::exception& error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "mux: %s", error.what());
    return 1;
  }
}
}
extern "C" __attribute__((visibility("default")))
void ANativeActivity_onCreate(ANativeActivity* activity, void* saved, size_t size) {
  SDL_AndroidNativeActivity(activity, saved, size, run);
}

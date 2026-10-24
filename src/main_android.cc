// SPDX-License-Identifier: AGPL-3.0-only
#include "android_runtime.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_nativeactivity.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <exception>
#include <string>
#include <thread>

int mux_main(int argc, char** argv);
// mux.core.ids: a line of the log kept, for Settings, About, Copy log.
extern "C" void mux_keep_log_line(const char* line);
namespace {
// What mux says goes to stderr (log_line, the [paste] and [skiff] lines),
// and Android throws stderr away: none of it was ever seen. A pipe is put in
// its place, and a thread of its own gives each line to logcat, tag "mux"
// (adb logcat -s mux). Once a process: an activity made again keeps it.
void stderr_to_logcat() {
  static bool done = false;
  if (std::exchange(done, true))
    return;
  int ends[2];
  if (pipe(ends) != 0)
    return;
  // Room for a burst of lines: a full pipe would hold the writer -- the app
  // thread itself, as it logs -- until the reader caught up.
  (void)fcntl(ends[1], F_SETPIPE_SZ, 1 << 20);
  if (dup2(ends[1], STDERR_FILENO) < 0) {
    close(ends[0]);
    close(ends[1]);
    return;
  }
  close(ends[1]);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  std::thread([from = ends[0]] {
    std::string pending;
    char chunk[1024];
    for (;;) {
      const ssize_t got = read(from, chunk, sizeof chunk);
      if (got <= 0)
        return;
      pending.append(chunk, static_cast<std::size_t>(got));
      for (std::size_t end; (end = pending.find('\n')) != std::string::npos; pending.erase(0, end + 1)) {
        pending[end] = '\0';
        __android_log_write(ANDROID_LOG_INFO, "mux", pending.c_str());
        mux_keep_log_line(pending.c_str());
      }
      // A line longer than logcat takes: given as it is.
      if (pending.size() > 3000) {
        __android_log_write(ANDROID_LOG_INFO, "mux", pending.c_str());
        pending.clear();
      }
    }
  }).detach();
}
int run(int argc, char** argv) {
  stderr_to_logcat();
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

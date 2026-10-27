#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Compile the Android bridge on the host and check where it enters JNI."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


JNI = r'''
#pragma once
using jint = int; using jsize = int; using jbyte = signed char;
inline constexpr int JNI_OK = 0;
struct object {}; struct klass : object {}; struct string : object {};
struct array : object {}; struct objects : array {}; struct bytes : array {};
using jobject = object*; using jclass = klass*; using jstring = string*;
using jarray = array*; using jobjectArray = objects*; using jbyteArray = bytes*;
using jthrowable = object*;
struct JNIEnv {
  jint PushLocalFrame(jint) { return JNI_OK; }
  void PopLocalFrame(jobject) {}
  bool ExceptionCheck() { return false; }
  void ExceptionDescribe() {} void ExceptionClear() {}
  jthrowable ExceptionOccurred() { return nullptr; }
  template<class... A> jobject CallObjectMethod(A...) { return nullptr; }
  template<class... A> jobject CallStaticObjectMethod(A...) { return nullptr; }
  template<class... A> void CallVoidMethod(A...) {}
  template<class... A> bool CallBooleanMethod(A...) { return false; }
  template<class... A> jint CallIntMethod(A...) { return 0; }
  template<class... A> jclass FindClass(A...) { return nullptr; }
  template<class... A> void* GetMethodID(A...) { return nullptr; }
  template<class... A> void* GetStaticMethodID(A...) { return nullptr; }
  template<class... A> jclass GetObjectClass(A...) { return nullptr; }
  template<class... A> jobject GetObjectArrayElement(A...) { return nullptr; }
  template<class... A> jsize GetArrayLength(A...) { return 0; }
  template<class... A> bool IsInstanceOf(A...) { return false; }
  template<class... A> const char* GetStringUTFChars(A...) { return nullptr; }
  template<class... A> void ReleaseStringUTFChars(A...) {}
  template<class... A> jbyteArray NewByteArray(A...) { return nullptr; }
  template<class... A> jobjectArray NewObjectArray(A...) { return nullptr; }
  template<class... A> jobject NewObject(A...) { return nullptr; }
  template<class... A> jobject NewGlobalRef(A...) { return nullptr; }
  template<class... A> jstring NewStringUTF(A...) { return nullptr; }
  template<class... A> void SetByteArrayRegion(A...) {}
  template<class... A> void SetObjectArrayElement(A...) {}
  template<class... A> void DeleteLocalRef(A...) {}
};
'''

SDL = r'''
#pragma once
void* SDL_GetAndroidJNIEnv(); void* SDL_GetAndroidActivity();
const char* SDL_GetAndroidInternalStoragePath();
const char* SDL_GetAndroidCachePath(); int SDL_GetAndroidSDKVersion();
'''

PROBE = r'''
#include "android_runtime.h"
#include <array>
#include <atomic>
#include <cassert>
#include <functional>
#include <string>
#include <thread>
#include <pthread.h>
#include <ucontext.h>
std::thread::id caller;
std::atomic<int> entries{0};
ucontext_t original, fiber;
std::array<char, 256 * 1024> alternate;
void* SDL_GetAndroidJNIEnv() {
  assert(std::this_thread::get_id() != caller);
  pthread_attr_t attributes;
  assert(pthread_getattr_np(pthread_self(), &attributes) == 0);
  void* start = nullptr;
  std::size_t size = 0;
  assert(pthread_attr_getstack(&attributes, &start, &size) == 0);
  assert(pthread_attr_destroy(&attributes) == 0);
  const int local = 0;
  const std::less<const void*> before;
  assert(!before(&local, start));
  assert(before(&local, static_cast<char*>(start) + size));
  ++entries;
  return nullptr;  // unavailable JNI must fail safely on the worker
}
void* SDL_GetAndroidActivity() { return nullptr; }
const char* SDL_GetAndroidInternalStoragePath() { return nullptr; }
const char* SDL_GetAndroidCachePath() { return nullptr; }
int SDL_GetAndroidSDKVersion() { return 33; }
void call_from_fiber() {
    char output[128] = "unchanged";
    assert(!mux_android_nameserver(output, sizeof output));
    assert(std::string(output) == "unchanged");
    char why[128] = {};
    assert(!mux_android_trusts(nullptr, nullptr, 0, "example.org", why, sizeof why));
    assert(std::string(why) == "Android could not be asked");
}
int main() {
  // Model Boost.Context's separate stack without linking the whole client.
  std::thread origin([] {
    caller = std::this_thread::get_id();
    assert(getcontext(&fiber) == 0);
    fiber.uc_stack.ss_sp = alternate.data();
    fiber.uc_stack.ss_size = alternate.size();
    fiber.uc_link = &original;
    makecontext(&fiber, call_from_fiber, 0);
    assert(swapcontext(&original, &fiber) == 0);
  });
  origin.join();
  assert(entries == 2);  // both calls completed before their buffers went away
}
'''


@unittest.skipUnless(shutil.which('c++'), 'Needs a host C++ compiler')
class AndroidRuntime(unittest.TestCase):
    def test_network_jni_uses_native_pthread_stacks(self):
        source = Path(__file__).resolve().parents[1] / 'src'
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'SDL3').mkdir()
            (root / 'SDL3/SDL.h').write_text(SDL)
            (root / 'jni.h').write_text(JNI)
            (root / 'probe.cc').write_text(PROBE)
            binary = root / 'probe'
            subprocess.run(['c++', '-std=c++23', '-pthread', '-I' + str(root),
                            '-I' + str(source), str(source / 'android_runtime.cc'),
                            str(root / 'probe.cc'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()

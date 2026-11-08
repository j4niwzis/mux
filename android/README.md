# Android packaging without D8

The Android profile targets ARM64 or 32-bit ARM (armeabi-v7a, NEON), API 21
(Android 5.0) or later, and GLES 2.0 or none: where there is no usable GLES,
Skia draws in software and the window is shown through ANativeWindow_lock.
The ABIs are in android/abis.json; tools/android_manifest.py writes the
toolchain manifest for one (the android workflow's abi input).
It uses the NativeActivity profile of the SDL fork. A host C++ program emits
four small callback classes directly as DEX; IME composition and document
selection are implemented through JNI. No D8, R8, Gradle, SDLActivity Java
sources, `android.jar`, or downloaded DEX is needed.

This is an experimental port. The SDL library, Mux's Android platform
modules and a small test application have been cross-compiled and packaged.
The full client and device behavior have not yet been validated. The smoke
application has its own package ID, `io.github.j4niwzis.mux.smoke`, so it does
not replace an installed Mux client.

## Toolchain

Host tools: Clang 23 with libc++ and its module sources, CMake 4.4, Ninja,
Python 3, Git, a JDK 17, `aapt2`, `zipalign`, and `readelf`. Ubuntu 24.04's
`aapt` package supplies the Android 14 resource compiler. Older Android 10
resource compilers cannot compile this platform's resource files. Java is
used to build and run the APK signer; it does not compile the app bridge.

Build `j4niwzis/minimal-android-ndk.cpp` at
`1c68644f3e953ba1fa373a0ba69d5a5a203dd377`. Mux's `toolchain-sources.json`
pins the AOSP repositories and LLVM runtimes used in validation. It excludes
R8. The manifest builds libc++ 23.1.2 using the host Clang 23 compiler.
Mux and its libraries use the C++23 range adaptors in that runtime, including
`std::views::enumerate`; the earlier libc++ 22 runtime does not provide it.

From the Mux checkout, with the toolchain repository in `../ndk-tool`:

```sh
cmake -S ../ndk-tool -B ../ndk-tool/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-23 \
  -DCMAKE_CXX_FLAGS='-stdlib=libc++ -fno-experimental-new-constant-interpreter'
cmake --build ../ndk-tool/build
ANDROID_ROOT="$HOME/.cache/mux-android"
../ndk-tool/build/minimal-android-ndk \
  --root "$ANDROID_ROOT" --manifest "$PWD/android/toolchain-sources.json" \
  --clang clang-23 --llvm-bin /usr/lib/llvm-23/bin \
  build runtimes toolchain-file framework-res apksigner
```

The generated `cmake/target.cmake` supplies the Android compiler target,
sysroot, API stubs, libc++ modules, and native app glue. Do not run the
toolchain builder's generic `apk` step: Mux supplies its own packaging path.
The `runtimes` step is required explicitly: generating the toolchain file
alone does not build compiler-rt, libc++, libc++abi or libunwind.

## Small integration build

Start with this target before building the client. To use an unpublished
fork checkout, add `-DMUX_ANDROID_SDL_SOURCE_DIR=/absolute/path/to/SDL`.

```sh
cmake -S android/smoke -B build-android-smoke -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_ROOT/cmake/target.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCME_BUILD_MACHINE_CXX_COMPILER=clang++-23 \
  -DCME_BUILD_MACHINE_CXX_FLAGS='-stdlib=libc++ -fno-experimental-new-constant-interpreter'
cmake --build build-android-smoke --target mux-apk -j4
```

The result is `build-android-smoke/apk/mux-test-signed.apk`. It opens a blue
window and logs text, IME composition and touch events. Touch the top quarter
to open a document picker; the callback reads its URI and logs the provider's
display name and byte count. Touch below that area to request the keyboard.
`adb logcat -s SDL` shows the events.

## Client build

The full client additionally needs Rust with the `aarch64-linux-android`
target, GN, Perl and the other source-build tools required by its ports.
The `vodozemac` port is explicitly assigned the Android Rust target because
the source-built CMake toolchain uses `CMAKE_SYSTEM_NAME=Linux`.

```sh
rustup target add aarch64-linux-android
cmake -S . -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_ROOT/cmake/target.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DMUX_PROGRAM_JOBS=1 \
  -DMUX_CLI=OFF -DMUX_TESTS=OFF -DMUX_VIDEO=OFF \
  -DCME_BUILD_MACHINE_CXX_COMPILER=clang++-23 \
  -DCME_BUILD_MACHINE_CXX_FLAGS='-stdlib=libc++ -fno-experimental-new-constant-interpreter'
cmake --build build-android --target mux-apk -j4
```

`mux-apk` packages both `apk/stripped/mux-test-signed.apk` and
`apk/debug/mux-test-signed.apk` from one linked library. The debug APK keeps
the symbols present in that library and is **debuggable**: its manifest is
linked with `aapt2 link --debug-mode`, which sets
`android:debuggable="true"`. The stripped APK removes symbols from a copy and
is not debuggable. Configure with `-DCMAKE_BUILD_TYPE=Debug
-DCMAKE_CXX_FLAGS_DEBUG='-O3 -g -fno-omit-frame-pointer'
-DCMAKE_C_FLAGS_DEBUG='-O3 -g -fno-omit-frame-pointer'` to retain source-level
debug information, as CI does. The existing `apk/mux-test-signed.apk` path
contains the stripped APK.

`mux-apk-debug` and `mux-apk-stripped` build individual variants. Append
`-unsigned` to either target to package without signing;
`mux-apk-unsigned` keeps the existing stripped unsigned output path.
The default **test key** is shared between variants. For release packaging,
set `MUX_ANDROID_TEST_KEY=OFF` and sign the unsigned output separately.
Release keys and passwords are never passed to this CMake build.

### Debugging on a device

Android's debugging works only on a debuggable app: `run-as`, attaching
`lldb-server` or a Java debugger, `am start -D`, heap dumps and the native
allocation profiler. Install the debug APK (it replaces the stripped one
in place: both are signed with the same test key, so data is kept):

```sh
adb install -r mux-android-arm64-v8a-client-debug-test-signed-<sha>/mux-test-signed.apk
adb shell run-as io.github.j4niwzis.mux ls files   # the app's private data
```

Native debugging with the NDK's `lldb-server`, which `run-as` copies into
the app's own directory and starts as the app's user:

```sh
NDK_LLDB=$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/*/lib/linux/aarch64/lldb-server
adb push $NDK_LLDB /data/local/tmp/lldb-server
adb shell run-as io.github.j4niwzis.mux cp /data/local/tmp/lldb-server .
adb shell run-as io.github.j4niwzis.mux chmod 700 lldb-server
adb shell am start -n io.github.j4niwzis.mux/org.libsdl.nativeapp.NativeActivity
adb shell run-as io.github.j4niwzis.mux ./lldb-server platform \
  --listen unix-abstract:///io.github.j4niwzis.mux/debug.sock --server &
lldb -o 'platform select remote-android' \
     -o 'platform connect unix-abstract-connect:///io.github.j4niwzis.mux/debug.sock' \
     -o "attach $(adb shell pidof io.github.j4niwzis.mux)"
```

`lib/arm64-v8a/libmux.so` from the same APK gives lldb its symbols
(`target symbols add`, or `settings append target.exec-search-paths`).
Android Studio's *Attach Debugger to Android Process* (debug type *Native
Only* or *Dual*) does the same steps itself for any debuggable app.

The CI build is `-O3 -g`: source lines and frames are right, but many
locals are optimized out. For stepping through code, configure with
`-DCMAKE_CXX_FLAGS_DEBUG='-Og -g -fno-omit-frame-pointer'` (and the same
for C); `-O0` makes the library much larger and the UI slow.

CI artifact names include the full commit hash, ABI, and `debug` or
`stripped`. Keep the debug artifact from the same commit and ABI as a
crashing installation. Extract its `lib/<abi>/libmux.so` to symbolize
native addresses; both variants retain the same ELF Build ID. A stripped
APK can still produce a native crash log, but cannot supply source lines
without its matching debug library.

The packager follows every non-platform `DT_NEEDED` dependency, rejects
unresolved or conflicting libraries, and verifies ARM64 and 16 KB ELF
alignment. `MUX_ANDROID_LIBRARY_DIRS` can add dependency search paths.
Native libraries are compressed and extracted by Android; `zipalign`
checks the package and `apksigner` verifies the v2/v3 signature.

## Platform integration and checks

Mux uses private app storage, system fonts, provider display names and
`content://` streams. It exports Android's accepted trust anchors for
OpenSSL without disabling peer or hostname verification. SRV queries use
the active network's DNS server; with Private DNS active they fall back to
the normal XMPP connection path through the platform resolver.

```sh
python3 test/android_apk_test.py
python3 /path/to/SDL/build-scripts/native-activity/dex/verify.py \
  build-android-smoke/apk/classes.dex
dexdump -c build-android-smoke/apk/classes.dex  # optional AOSP verifier
java -jar "$ANDROID_ROOT/tools/apksigner.jar" verify --verbose \
  build-android-smoke/apk/mux-test-signed.apk
```

Device validation remains necessary: non-Latin IMEs and emoji deletion,
keyboard dismissal, rotation and background/resume, safe areas on a dense
display, local/cloud document providers, clipboard access on focus, and
network changes. There is no background-service or push-notification
integration in this initial port. The fork also does not yet implement
runtime permission prompts, controllers, haptics or cameras.

The manual `android` workflow builds the smoke APK by default; its
`full_client` input opts into the full client. Both produce test-signed
artifacts, never release packages. Both Android and desktop CI allocate
32 GiB of swap for template-heavy compilation.
Like the desktop workflow, Android CI restores and saves the build directory
and CPM's fetched sources, including after a failed build. It also keeps
Cargo's downloaded crates and Git checkouts. Compiled outputs use separate
Android keys for each toolchain and build profile; retries save their new
progress. The source-built Android toolchain has its own cache, saved as
soon as it finishes building.

# Android packaging without D8

The initial Android profile targets ARM64, API 27 or later, and GLES 3.
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
R8. The manifest builds libc++ 22.1.8 using the host Clang 23 compiler.

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
  -DCMAKE_BUILD_TYPE=Debug \
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
  -DCMAKE_BUILD_TYPE=Debug -DMUX_PROGRAM_JOBS=1 \
  -DMUX_CLI=OFF -DMUX_TESTS=OFF -DMUX_VIDEO=OFF \
  -DCME_BUILD_MACHINE_CXX_COMPILER=clang++-23 \
  -DCME_BUILD_MACHINE_CXX_FLAGS='-stdlib=libc++ -fno-experimental-new-constant-interpreter'
cmake --build build-android --target mux-apk -j4
```

`mux-apk-unsigned` builds only the unsigned APK. `mux-apk` signs with a
generated, explicitly named **test key** by default. For release packaging,
set `MUX_ANDROID_TEST_KEY=OFF` and sign the unsigned output separately.
Release keys and passwords are never passed to this CMake build.

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

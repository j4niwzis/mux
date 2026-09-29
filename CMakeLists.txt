cmake_minimum_required(VERSION 4.3.4 FATAL_ERROR)

# mux: one chat client for XMPP and Matrix, as C++26 modules.
#
#   mux.net     all I/O, on one io_context, run by fibers: TCP, TLS, DNS
#   mux.core    the model every protocol fills: accounts, conversations,
#               messages, presence -- and the changes to it
#   mux.xmpp    an XMPP account, through tern
#   mux.http    HTTPS requests from a fiber, with Beast
#   mux.matrix  a Matrix account, through loom
#   mux-cli     the network half on a terminal, until the window is there
#
# Every dependency comes through cmake-everywhere: the machine's copy where
# it has one with what is needed, and a pinned source where it has not.
include(${CMAKE_CURRENT_LIST_DIR}/cmake/import-std.cmake)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT COMMAND cme_declare_port AND NOT CMAKE_PROJECT_TOP_LEVEL_INCLUDES)
  set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/cmake/get_cme.cmake)
endif()

project(mux VERSION 0.1 LANGUAGES C CXX)

# A release build is optimised across its units (ThinLTO with clang), where
# the toolchain can: mux's own targets, set at the end -- not the libraries
# fetched for it, whose archives the system's ar cannot index as bitcode.
include(CheckIPOSupported)
check_ipo_supported(RESULT mux_ipo OUTPUT mux_ipo_said LANGUAGES CXX)

if(NOT COMMAND cme_provider)
  message(FATAL_ERROR
    "cmake-everywhere is not installed in this build, so nothing answers "
    "find_package. CMAKE_PROJECT_TOP_LEVEL_INCLUDES is only read by the "
    "top-level project() call: configure this directory, and not a "
    "subdirectory of it.")
endif()

option(MUX_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})
# The window: SDL3 and skiff, with Skia on GL.
option(MUX_UI "Build the window, and the mux program" ON)

# clang's -fconstexpr-steps: one value for mux and every library of the
# family under it. Each of them adds its own to what imports it, and CMake
# builds a module again for every set of flags it is imported with -- and
# the std module once for each of those. alef, chevron and tern said 1e8 and
# knot and loom 3e8, so a target importing both kinds had a set of flags
# none of them was built with, every module of theirs was built a second
# time, and the collation found std two ways ("Disagreement of the location
# of the 'std' module"). One value, set before any of them is read, and
# there is one set of flags.
set(MUX_CONSTEXPR_STEPS 300000000 CACHE STRING
  "clang's -fconstexpr-steps for mux and the family of libraries under it")
foreach(family IN ITEMS ALEF CHEVRON TERN KNOT LOOM)
  set(${family}_CONSTEXPR_STEPS ${MUX_CONSTEXPR_STEPS} CACHE STRING
    "clang's -fconstexpr-steps, as mux sets it for the whole family")
endforeach()

# The flags that decide which copy of std a target is compiled against are
# given as the libraries of the family give them: to what imports the
# library, not to the whole build. The std target itself is built without
# them, so everything that imports a module -- mux, its tests, googletest --
# is compiled against a std synthesized for its flags, one for all of them.
# Given to the whole build, the std target had them too, and a test that had
# no other difference was handed that one while googletest's interfaces
# named the synthesized one. The step count is the same number for all of
# the family (below), and alef's constant interpreter flag is on mux's
# modules as it is on alef's.

# The libraries of this family, each pinned to a commit.
cme_declare_port(NAME tern PROVIDES tern
  GITHUB_REPOSITORY j4niwzis/tern
  GIT_TAG eb6bc17dd8c7017a09a1fc645a5383cd6b90c7ba
  LICENSE AGPL-3.0-only
  SYSTEM NEVER
  TARGETS tern)
cme_options(tern "TERN_TESTS OFF" "TERN_EXAMPLES OFF")
cme_declare_port(NAME loom PROVIDES loom
  GITHUB_REPOSITORY j4niwzis/loom
  GIT_TAG c78b519b0c8cf26213134d3dc166725e3b9038b8
  LICENSE AGPL-3.0-only
  SYSTEM NEVER
  TARGETS loom)
cme_options(loom "LOOM_TESTS OFF")

# Boost: Asio for the sockets and the loop, Beast for Matrix's HTTP,
# Context for the fibers, and PFR as a module for what reads with it.
cme_find_package(Boost COMPONENTS asio beast pfr[modules] context REQUIRED)
cme_find_package(OpenSSL REQUIRED)

# Boost's definitions reach what links Boost, and nothing else, as in the
# libraries under mux: defined for the whole build they were on the std
# target too, and mux's modules were matched to it rather than to the std
# the family's modules were built against.

# alef, which tern reads addresses with and skiff lays text out with, asked
# for once with all that both of them need, before either asks: a later ask
# for more of it finds it already built without that, and stops.
cme_declare_port(NAME alef PROVIDES alef
  GITHUB_REPOSITORY j4niwzis/alef
  GIT_TAG a345c5485d2c16125f00b10c42728725d8aa71c6
  LICENSE AGPL-3.0-only
  SYSTEM NEVER
  TARGETS alef)
cme_options(alef "ALEF_TESTS OFF")
cme_find_package(alef COMPONENTS precis idna grapheme line bidi emoji REQUIRED)

# knot, at its pretty printer (a7e6cd4): what a room's events are shown as
# in the developer tools. loom asks for an older one; the project's pin is
# the one taken.
cme_declare_port(NAME knot PROVIDES knot
  GITHUB_REPOSITORY j4niwzis/knot
  GIT_TAG a7e6cd44e5f522c1f49ab87ee9a40c4cd96514ad
  LICENSE AGPL-3.0-only
  SYSTEM NEVER)

cme_find_package(tern REQUIRED)
# Of loom, what mux uses: the state a sync leaves, and the parts of the API it
# calls -- logging in, sending, syncing, finding the homeserver.
cme_find_package(loom COMPONENTS state cs-joining cs-leaving cs-login cs-message_pagination cs-event_context cs-receipts cs-redaction cs-room_send cs-rooms cs-sync cs-typing cs-wellknown cs-room_state cs-kicking cs-banning cs-inviting cs-create_room cs-account_data cs-content_repo cs-authed_content_repo cs-room_summary cs-list_public_rooms REQUIRED)

# One library of mux's modules, as the libraries it stands on have: modules
# are what separates them. The window's two join it where the window is
# built.
add_library(mux_modules)
target_sources(mux_modules PUBLIC FILE_SET CXX_MODULES BASE_DIRS src
  FILES src/core.cc src/net.cc src/xmpp.cc src/http.cc src/matrix.cc src/media.cc
        src/matrix/base.cc src/matrix/names.cc src/matrix/account.cc src/matrix/requests.cc src/matrix/sync.cc
        src/matrix/events.cc src/matrix/media.cc
        src/logic/text.cc src/logic/search.cc src/logic/reading.cc src/logic/drafts.cc
        src/logic/links.cc src/logic/sending.cc src/logic/messages.cc
        src/logic/blurhash.cc src/logic/emoji.cc src/logic/markdown.cc src/logic/room_events.cc)
target_compile_features(mux_modules PUBLIC cxx_std_26)
# The flags that decide which std module a target gets: the same on every
# library of modules in the build, or two std modules meet in one importer.
set(mux_std_flags
  $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-fconstexpr-steps=${MUX_CONSTEXPR_STEPS}>
  $<$<AND:$<COMPILE_LANG_AND_ID:CXX,Clang>,$<VERSION_GREATER_EQUAL:$<CXX_COMPILER_VERSION>,23>>:-fno-experimental-new-constant-interpreter>)
target_compile_options(mux_modules PUBLIC ${mux_std_flags})
target_link_libraries(mux_modules PUBLIC tern loom Boost::asio Boost::beast Boost::context
  OpenSSL::SSL OpenSSL::Crypto)

# The accounts kept between runs: a library of its own, on knot alone, so
# that it builds and is tested without the protocols.
add_library(mux_config)
target_sources(mux_config PUBLIC FILE_SET CXX_MODULES BASE_DIRS src FILES src/config.cc)
target_compile_features(mux_config PUBLIC cxx_std_26)
target_compile_options(mux_config PUBLIC ${mux_std_flags})
target_link_libraries(mux_config PUBLIC knot)
target_link_libraries(mux_modules PUBLIC mux_config)
foreach(name IN ITEMS core net xmpp http matrix media logic)
  add_library(mux_${name} ALIAS mux_modules)
endforeach()

if(MUX_UI)
  # skiff and skiff-widgets say the rest of what they are in their own trees.
  cme_declare_port(NAME skiff PROVIDES skiff
    GITHUB_REPOSITORY j4niwzis/skiff
    GIT_TAG 5b92bb1bdbef68faddfaa6cc224b76cddc66f78a)
  cme_declare_port(NAME skiff-widgets PROVIDES skiff-widgets skiff_widgets
    GITHUB_REPOSITORY j4niwzis/skiff-widgets
    GIT_TAG ff31cd4d0fc64ebfadcb07b24f027badfed95769)
  # Skia's features, each an option asking for its component: GL to draw on
  # the GPU (the software renderer is there either way), and the pictures
  # decoded. A system Skia without one is taken where its option is off.
  option(MUX_GL "Draw on the GPU through OpenGL, besides in software" ON)
  option(MUX_PNG "Pictures in PNG" ON)
  option(MUX_JPEG "Pictures in JPEG" ON)
  option(MUX_GIF "Pictures in GIF, played where they move (Skia with Wuffs)" OFF)
  option(MUX_WEBP "Pictures in WebP, played where they move (Skia with libwebp)" OFF)
  # skiff's components as mux draws with them: the system's fonts read from
  # their directory, text shaped by HarfBuzz and laid out by Unicode's rules
  # through alef (Arabic and Hebrew, Indic scripts, emoji sequences, CJK line
  # breaks), GL where asked for, and the pictures mux decodes. skiff asks Skia
  # for what they need; mux asks Skia for nothing itself.
  set(mux_skiff_components fontmgr-directory text-shaping)
  foreach(feature IN ITEMS gl png jpeg gif webp)
    string(TOUPPER ${feature} option_name)
    if(MUX_${option_name})
      list(APPEND mux_skiff_components ${feature})
    endif()
  endforeach()
  cme_find_package(SDL3 REQUIRED)
  # Sound: voice messages are Opus in Ogg; audio files are that or Vorbis.
  cme_find_package(Opus REQUIRED)
  cme_find_package(Vorbis REQUIRED)
  cme_find_package(skiff COMPONENTS ${mux_skiff_components} REQUIRED)
  cme_find_package(skiff-widgets REQUIRED)
  # skiff's modules are C++23 and do not carry the family's flags, so they
  # would be built with a std module of their own: given the same standard
  # and the same flags here.
  foreach(target IN ITEMS skiff::skiff skiff::widgets)
    if(TARGET ${target})
      get_target_property(aliased ${target} ALIASED_TARGET)
      if(aliased)
        set(target ${aliased})
      endif()
      get_target_property(imported ${target} IMPORTED)
      if(NOT imported)
        target_compile_options(${target} PUBLIC ${mux_std_flags})
        set_target_properties(${target} PROPERTIES CXX_STANDARD 26)
      endif()
    endif()
  endforeach()

  # The emoji face the window draws emoji with, as tdesktop has its own:
  # Noto Color Emoji, fetched once at a pinned commit and checked, and put
  # in the binary by host.cc's #embed.
  set(mux_emoji_dir ${CMAKE_BINARY_DIR}/fonts)
  set(mux_emoji_font ${mux_emoji_dir}/NotoColorEmoji-Regular.ttf)
  if(NOT EXISTS ${mux_emoji_font})
    file(DOWNLOAD
      https://raw.githubusercontent.com/google/fonts/142c8963e7606b510c93a644c82a4c4cdeae6ef9/ofl/notocoloremoji/NotoColorEmoji-Regular.ttf
      ${mux_emoji_font}
      EXPECTED_HASH SHA256=4d82a18d8d95f60ba883ce242bbadbf84a576e987745dd7ba38d71c67cff2d73
      TLS_VERIFY ON)
  endif()
  target_sources(mux_modules PRIVATE src/emoji_font.cc)
  set_property(SOURCE src/emoji_font.cc APPEND PROPERTY COMPILE_OPTIONS --embed-dir=${mux_emoji_dir})
  set_property(SOURCE src/emoji_font.cc APPEND PROPERTY OBJECT_DEPENDS ${mux_emoji_font})
  target_sources(mux_modules PUBLIC FILE_SET CXX_MODULES BASE_DIRS src
    FILES src/host.cc src/audio.cc src/dbus.cc src/ui.cc
          src/ui/base.cc src/ui/icons.cc src/ui/avatars.cc src/ui/controls.cc src/ui/themes.cc src/ui/names.cc src/ui/html.cc src/ui/chat_list.cc src/ui/message.cc src/ui/header.cc src/ui/info.cc src/ui/room_settings.cc src/ui/composer.cc src/ui/timeline.cc src/ui/conversations.cc src/ui/forms.cc src/ui/add_account.cc src/ui/accounts.cc src/ui/drawer.cc src/ui/proxies.cc src/ui/appearance.cc src/ui/storage.cc src/ui/settings.cc src/ui/context_menu.cc src/ui/sending.cc src/ui/viewer.cc src/ui/window.cc)
  target_link_libraries(mux_modules PUBLIC SDL3::SDL3 skiff::skiff skiff::widgets Opus::opus Vorbis::vorbisfile)
  add_executable(mux src/main.cc)
  # The program's own parts: its network, its demo, what it keeps on disk,
  # the requests of its window, the names it reads, and the program itself.
  target_sources(mux PRIVATE FILE_SET CXX_MODULES BASE_DIRS src
    FILES src/app/network.cc src/app/workers.cc src/app/demo.cc src/app/store.cc src/app/requests.cc src/app/words.cc
          src/app/kept.cc src/app/services.cc src/app/search.cc src/app/pictures.cc src/app/drafts.cc src/app/reading.cc src/app/outbox.cc src/app/settings.cc src/app/menu.cc
          src/app/program.cc)
  # What the program does, a part each: the module's implementation units.
  target_sources(mux PRIVATE
    src/app/program_core.cc src/app/program_media.cc
    src/app/program_chats.cc src/app/program_links.cc src/app/program_accounts.cc
    src/app/program_settings.cc src/app/program_sending.cc src/app/program_forms.cc
    src/app/scene.cc)
  target_link_libraries(mux PRIVATE mux_modules)
endif()

add_executable(mux-cli src/cli.cc)
target_link_libraries(mux-cli PRIVATE mux_modules)

if(MUX_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

# LTO for what is mux's: the targets made here and in test/.
if(mux_ipo)
  set(mux_dirs ${CMAKE_CURRENT_SOURCE_DIR})
  if(MUX_TESTS)
    list(APPEND mux_dirs ${CMAKE_CURRENT_SOURCE_DIR}/test)
  endif()
  foreach(dir IN LISTS mux_dirs)
    get_property(mux_targets DIRECTORY ${dir} PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS mux_targets)
      get_target_property(kind ${target} TYPE)
      if(kind MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
        set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
      endif()
    endforeach()
  endforeach()
endif()

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
foreach(family IN ITEMS ALEF CHEVRON TERN KNOT LOOM SPLICE)
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
# chevron, which tern reads the XML of streams with, newer than tern's own:
# mux reads a message's HTML with it too (dialect::html). Declared first, it
# is the one tern is built with as well.
cme_declare_port(NAME chevron PROVIDES chevron
  GITHUB_REPOSITORY j4niwzis/chevron
  GIT_TAG 3019faec6c81fbd677736811308967672e7b9911
  LICENSE AGPL-3.0-only
  TARGETS chevron)
cme_options(chevron "CHEVRON_TESTS OFF")
cme_declare_port(NAME tern PROVIDES tern
  GITHUB_REPOSITORY j4niwzis/tern
  GIT_TAG de984ff791a937fff637f44a8d32e506cad18dbc
  LICENSE AGPL-3.0-only
  TARGETS tern)
cme_options(tern "TERN_TESTS OFF" "TERN_EXAMPLES OFF")
cme_declare_port(NAME loom PROVIDES loom
  GITHUB_REPOSITORY j4niwzis/loom
  GIT_TAG 823bded41602b99d1f1eee1b2bb8bb847618672f
  LICENSE AGPL-3.0-only
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
  GIT_TAG 6dc139f9590dc0d3ac1cc0e9748e67d7a1485480
  LICENSE AGPL-3.0-only
  TARGETS alef)
cme_options(alef "ALEF_TESTS OFF")
cme_find_package(alef COMPONENTS precis idna grapheme line bidi emoji REQUIRED)

# splice: what the family shares -- splice::variant (a variant built in time
# linear in its alternatives; outside a release build, reached through
# tables) and splice::overloaded. Its tables as mux's handlers are: erased
# outside a release build where MUX_ERASE_HANDLERS says so.
set(SPLICE_ERASE ${MUX_ERASE_HANDLERS} CACHE BOOL "splice::variant through tables outside a release build, as mux's handlers")
cme_declare_port(NAME splice PROVIDES splice
  GITHUB_REPOSITORY j4niwzis/splice
  GIT_TAG f2c25443061ba5353720eccd0a9438793c2ea053
  LICENSE GPL-3.0-only
  TARGETS splice)
cme_options(splice "SPLICE_TESTS OFF")
cme_find_package(splice REQUIRED)

# knot, at knot::raw as a rest member (4903935): what is not typed kept as
# its JSON text, no tree -- loom asks for the same. The project's pin is the
# one taken.
cme_declare_port(NAME knot PROVIDES knot
  GITHUB_REPOSITORY j4niwzis/knot
  GIT_TAG 035e53fdee8875614e350331a914256495b9bb22
  LICENSE AGPL-3.0-only)

# Found by itself, and linked by itself: mux imports it (a message's HTML),
# not only tern.
cme_find_package(chevron REQUIRED)
cme_find_package(tern REQUIRED)
# Of loom, what mux uses: the state a sync leaves, and the parts of the API it
# calls -- logging in, sending, syncing, finding the homeserver.
cme_find_package(loom COMPONENTS state cs-joining cs-leaving cs-login cs-message_pagination cs-event_context cs-receipts cs-redaction cs-room_send cs-rooms cs-sync cs-typing cs-wellknown cs-room_state cs-kicking cs-banning cs-inviting cs-create_room cs-account_data cs-content_repo cs-authed_content_repo cs-room_summary cs-list_public_rooms cs-space_hierarchy cs-sliding_sync cs-versions cs-users cs-relations cs-threads_list cs-profile cs-device_management cs-room_upgrades cs-knocking REQUIRED)

# One library of mux's modules, as the libraries it stands on have: modules
# are what separates them. The window's two join it where the window is
# built.
add_library(mux_modules)
target_sources(mux_modules PUBLIC FILE_SET CXX_MODULES BASE_DIRS src
  FILES src/core.cc src/net.cc src/xmpp.cc src/http.cc src/preview.cc src/matrix.cc src/media.cc
        src/matrix/base.cc src/matrix/names.cc src/matrix/account.cc src/matrix/requests.cc src/matrix/sync.cc
        src/matrix/events.cc src/matrix/media.cc
        src/logic/text.cc src/logic/search.cc src/logic/reading.cc src/logic/drafts.cc
        src/logic/links.cc src/logic/sending.cc src/logic/messages.cc
        src/logic/blurhash.cc src/logic/emoji.cc src/logic/markdown.cc src/logic/room_events.cc)
target_compile_features(mux_modules PUBLIC cxx_std_26)
# The emoji's keywords, as Unicode's CLDR annotates them in English and in
# Russian -- what the emoji search matches besides the names, as Telegram's
# does ("100" finds the hundred points): fetched once at a pinned commit and
# checked, and put in the binary by emoji_keywords.cc's #embed.
set(mux_cldr_dir ${CMAKE_BINARY_DIR}/cldr)
foreach(pair "en;514bc93ac647211d0f422f63978d1eeec6aa759d0cd8c3e8c5da6cf5a536784b"
             "ru;e8b2257e3139029b1c04dd15dcc50b818bf1830687cfd6fd40f0b5ad336aaa83")
  list(GET pair 0 language)
  list(GET pair 1 hash)
  if(NOT EXISTS ${mux_cldr_dir}/${language}.xml)
    file(DOWNLOAD
      https://raw.githubusercontent.com/unicode-org/cldr/90e46ed15ee4716196d3299f669eff6259ec1dca/common/annotations/${language}.xml
      ${mux_cldr_dir}/${language}.xml
      EXPECTED_HASH SHA256=${hash}
      TLS_VERIFY ON)
  endif()
endforeach()
target_sources(mux_modules PRIVATE src/emoji_keywords.cc)
set_property(SOURCE src/emoji_keywords.cc APPEND PROPERTY COMPILE_OPTIONS --embed-dir=${mux_cldr_dir})
set_property(SOURCE src/emoji_keywords.cc APPEND PROPERTY OBJECT_DEPENDS "${mux_cldr_dir}/en.xml;${mux_cldr_dir}/ru.xml")
# The flags that decide which std module a target gets: the same on every
# library of modules in the build, or two std modules meet in one importer.
set(mux_std_flags
  $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-fconstexpr-steps=${MUX_CONSTEXPR_STEPS}>
  $<$<AND:$<COMPILE_LANG_AND_ID:CXX,Clang>,$<VERSION_GREATER_EQUAL:$<CXX_COMPILER_VERSION>,23>>:-fno-experimental-new-constant-interpreter>)
target_compile_options(mux_modules PUBLIC ${mux_std_flags})
target_link_libraries(mux_modules PUBLIC chevron tern loom Boost::asio Boost::beast Boost::context
  OpenSSL::SSL OpenSSL::Crypto)

# The accounts kept between runs: a library of its own, on knot alone, so
# that it builds and is tested without the protocols.
# Outside a release build, Asio completion handlers erased to their
# signature (net.cc, kErasedHandlers): the network operations made once
# each, not once per place awaited. A release build keeps them static.
option(MUX_ERASE_HANDLERS "Outside a release build, erase Asio completion handlers" ON)
target_compile_definitions(mux_modules PUBLIC
  $<$<AND:$<BOOL:${MUX_ERASE_HANDLERS}>,$<NOT:$<CONFIG:Release>>>:MUX_ERASED_HANDLERS>)

# Text and bytes, each made of the other a byte at a time: below everything.
add_library(mux_bytes)
target_sources(mux_bytes PUBLIC FILE_SET CXX_MODULES BASE_DIRS src FILES src/bytes.cc)
target_compile_features(mux_bytes PUBLIC cxx_std_26)
target_compile_options(mux_bytes PUBLIC ${mux_std_flags})
add_library(mux_config)
target_sources(mux_config PUBLIC FILE_SET CXX_MODULES BASE_DIRS src FILES src/config.cc)
target_compile_features(mux_config PUBLIC cxx_std_26)
target_compile_options(mux_config PUBLIC ${mux_std_flags})
target_link_libraries(mux_config PUBLIC knot splice mux_bytes)
target_link_libraries(mux_modules PUBLIC mux_config splice)
foreach(name IN ITEMS core net xmpp http matrix media logic)
  add_library(mux_${name} ALIAS mux_modules)
endforeach()

if(MUX_UI)
  # skiff and skiff-widgets say the rest of what they are in their own trees.
  cme_declare_port(NAME skiff PROVIDES skiff
    GITHUB_REPOSITORY j4niwzis/skiff
    GIT_TAG 0531469ad26b78ff839e9660c9bfc6e9448b8b64)
  cme_declare_port(NAME skiff-widgets PROVIDES skiff-widgets skiff_widgets
    GITHUB_REPOSITORY j4niwzis/skiff-widgets
    GIT_TAG 99bf5557a3713aa414cd14ee550c5dbb7bc75cf4)
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
  # Video, as tdesktop plays it: FFmpeg -- the system's where it has one,
  # else built with only what a chat's videos are in and made of. Off, the
  # client has no FFmpeg, and a video is played by the system's player.
  option(MUX_VIDEO "Videos played in the window, through FFmpeg" ON)
  set(mux_video_source src/video_off.cc)
  set(mux_video_libraries)
  if(MUX_VIDEO)
    cme_find_package(FFmpeg COMPONENTS mp4 matroska h264 hevc vp9 aac opus mp3 REQUIRED)
    set(mux_video_source src/video.cc)
    set(mux_video_libraries FFmpeg::avformat FFmpeg::avcodec FFmpeg::swscale FFmpeg::swresample FFmpeg::avutil)
  endif()
  cme_find_package(skiff COMPONENTS ${mux_skiff_components} REQUIRED)
  cme_find_package(skiff-widgets REQUIRED)

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
  # Telegram's chat pattern, as tdesktop lays it over a theme's gradient:
  # its art/background.tgv (GPL-3.0, combined under AGPL-3.0's section 13),
  # fetched once at a pinned commit and checked, and put in the binary by
  # telegram_pattern.cc's #embed.
  set(mux_art_dir ${CMAKE_BINARY_DIR}/art)
  set(mux_pattern ${mux_art_dir}/background.tgv)
  if(NOT EXISTS ${mux_pattern})
    file(DOWNLOAD
      https://raw.githubusercontent.com/telegramdesktop/tdesktop/f59db1026742fedd3b1bcee961b6c62ba9baf3bb/Telegram/Resources/art/background.tgv
      ${mux_pattern}
      EXPECTED_HASH SHA256=3c5ef666d033ce9effe703b80348b9d7363323f0076b2856ec041923c78b0ab8
      TLS_VERIFY ON)
  endif()
  target_sources(mux_modules PRIVATE src/telegram_pattern.cc)
  set_property(SOURCE src/telegram_pattern.cc APPEND PROPERTY COMPILE_OPTIONS --embed-dir=${mux_art_dir})
  set_property(SOURCE src/telegram_pattern.cc APPEND PROPERTY OBJECT_DEPENDS ${mux_pattern})
  target_sources(mux_modules PUBLIC FILE_SET CXX_MODULES BASE_DIRS src
    FILES src/host.cc src/audio.cc ${mux_video_source} src/dbus.cc src/ui.cc
          src/ui/base.cc src/ui/icons.cc src/ui/avatars.cc src/ui/controls.cc src/ui/themes.cc src/ui/names.cc src/ui/html.cc src/ui/chat_list.cc src/ui/message.cc src/ui/header.cc src/ui/info.cc src/ui/room_settings.cc src/ui/composer.cc src/ui/timeline.cc src/ui/conversations.cc src/ui/forms.cc src/ui/add_account.cc src/ui/accounts.cc src/ui/drawer.cc src/ui/proxies.cc src/ui/appearance.cc src/ui/storage.cc src/ui/settings.cc src/ui/context_menu.cc src/ui/sending.cc src/ui/viewer.cc src/ui/window.cc)
  target_link_libraries(mux_modules PUBLIC SDL3::SDL3 skiff::skiff skiff::widgets Opus::opus Vorbis::vorbisfile
    ${mux_video_libraries})
  add_executable(mux src/main.cc)
  # Outside a release build, with walks erased: the big subtrees' tables and
  # walks each in a unit of its own, not all in scene.cc.
  set(mux_erased $<AND:$<BOOL:${SKIFF_ERASE_WALKS}>,$<NOT:$<CONFIG:Release>>>)
  if(NOT DEFINED SKIFF_ERASE_WALKS)
    set(mux_erased $<NOT:$<CONFIG:Release>>)
  endif()
  # Outside a release build, the accounts each in a unit of its own, beside
  # network.cc, built in parallel; a release build instantiates them where
  # they are used, in network.cc's unit, all of them in the optimiser's sight.
  set(mux_split $<NOT:$<CONFIG:Release>>)
  target_compile_definitions(mux PRIVATE $<${mux_split}:MUX_SPLIT_ACCOUNTS>)
  target_sources(mux PRIVATE
    $<${mux_split}:src/app/accounts_matrix_sync.cc>
    $<${mux_split}:src/app/accounts_matrix_requests.cc>
    $<${mux_split}:src/app/accounts_matrix_events.cc>
    $<${mux_split}:src/app/accounts_matrix_media.cc>
    $<${mux_split}:src/app/accounts_xmpp.cc>)
  target_sources(mux PRIVATE
    $<${mux_erased}:src/app/walks_window.cc>
    $<${mux_erased}:src/app/walks_chats.cc>
    $<${mux_erased}:src/app/walks_side.cc>
    $<${mux_erased}:src/app/walks_column.cc>
    $<${mux_erased}:src/app/walks_bubbles.cc>
    $<${mux_erased}:src/app/walks_composer.cc>
    $<${mux_erased}:src/app/walks_info.cc>
    $<${mux_erased}:src/app/walks_accounts.cc>
    $<${mux_erased}:src/app/walks_drawer.cc>
    $<${mux_erased}:src/app/walks_settings.cc>
    $<${mux_erased}:src/app/walks_manage.cc>
    $<${mux_erased}:src/app/walks_boxes.cc>
    $<${mux_erased}:src/app/walks_popups.cc>
    $<${mux_erased}:src/app/walks_emoji.cc>)
  # The walks' units at -O1 outside a release build: tens of thousands of
  # small walks each, and -Os spent most of their minutes inlining and
  # optimizing them (walks_chats.cc: 4.4 of its 6.5 minutes in the
  # backend). A release build has none of these units.
  set(MUX_WALKS_OPTIMIZE "-O1" CACHE STRING "Optimization for the erased walks' units, outside a release build")
  set_source_files_properties(src/app/walks_window.cc src/app/walks_chats.cc src/app/walks_side.cc src/app/walks_column.cc src/app/walks_bubbles.cc
    src/app/walks_composer.cc src/app/walks_info.cc src/app/walks_accounts.cc src/app/walks_drawer.cc
    src/app/walks_settings.cc src/app/walks_manage.cc src/app/walks_boxes.cc src/app/walks_popups.cc src/app/walks_emoji.cc
    PROPERTIES COMPILE_OPTIONS "${MUX_WALKS_OPTIMIZE}")
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

# One std module in the whole build. CMake builds std -- and every library
# of modules imported -- once more for each set of flags an importer is
# compiled with; where those copies meet in one unit, clang 23 does not
# refuse it but mixes their declarations up (std::forward found as
# uintmax_t) and crashes, as accounts_matrix_events.cc, pictures.cc and
# ui_test.cc did in a release build. So every library of modules in the
# build -- the family's, skiff's, googletest's, whatever is fetched -- gets
# the same standard and flags as mux's own, LTO included in a release
# build: one set of flags, one std, each module built once.
function(mux_targets_under dir out)
  get_property(found DIRECTORY ${dir} PROPERTY BUILDSYSTEM_TARGETS)
  get_property(subdirectories DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)
  foreach(subdirectory IN LISTS subdirectories)
    mux_targets_under(${subdirectory} more)
    list(APPEND found ${more})
  endforeach()
  set(${out} ${found} PARENT_SCOPE)
endfunction()
mux_targets_under(${CMAKE_CURRENT_SOURCE_DIR} mux_all_targets)
foreach(target IN LISTS mux_all_targets)
  get_target_property(module_sets ${target} CXX_MODULE_SETS)
  get_target_property(imported ${target} IMPORTED)
  if(module_sets AND NOT imported AND NOT target MATCHES "^mux")
    target_compile_options(${target} PUBLIC ${mux_std_flags})
    set_target_properties(${target} PROPERTIES CXX_STANDARD 26 CXX_STANDARD_REQUIRED ON)
    if(mux_ipo AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      # Its archive indexed by the toolchain's ar, which reads bitcode.
      set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
      target_compile_options(${target} PRIVATE
        $<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:CXX,Clang>>:-flto=full>)
    endif()
  endif()
endforeach()

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
      # mux's own only: what a package is built into here (FFmpeg's
      # libraries, by cme) keeps its own flags -- its C is another
      # compiler's, whose LTO objects mux's linker could not read.
      if(target MATCHES "^mux" AND kind MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
        set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        # Full LTO, not CMake's thin: the whole program optimised as one
        # module at the link. Given after CMake's own flag, it is the one kept.
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
          target_compile_options(${target} PRIVATE
            $<$<AND:$<CONFIG:Release>,$<OR:$<COMPILE_LANG_AND_ID:CXX,Clang>,$<COMPILE_LANG_AND_ID:C,Clang>>>:-flto=full>)
          target_link_options(${target} PRIVATE $<$<CONFIG:Release>:-flto=full>)
        endif()
      endif()
    endforeach()
  endforeach()
endif()

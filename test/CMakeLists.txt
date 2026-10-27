# The tests: googletest's, as a module, from cmake-everywhere.
add_test(NAME about-targets COMMAND "${CMAKE_COMMAND}"
  -S "${CMAKE_CURRENT_SOURCE_DIR}/about_targets" -B "${CMAKE_CURRENT_BINARY_DIR}/about-targets")
cme_declare_port(NAME googletest-modules PROVIDES GTest
  GITHUB_REPOSITORY j4niwzis/googletest-modules
  GIT_TAG 706920f1d230f0316d271466d8885735be3e300e
  LICENSE BSD-3-Clause
  SYSTEM NEVER
  TARGETS gtest_main)
cme_options(googletest-modules
  "BUILD_GMOCK OFF" "INSTALL_GTEST OFF" "gtest_build_modules ON"
  "gtest_build_tests OFF" "gtest_build_samples OFF")
cme_find_package(GTest REQUIRED)
# googletest-modules sets C++23 outright; this build is C++26, and CMake
# stops at two std modules in one build ("Disagreement of the location of
# the 'std' module"). Its targets are built as C++26 here.
foreach(target IN ITEMS gtest gtest_main)
  if(TARGET ${target})
    get_target_property(aliased ${target} ALIASED_TARGET)
    if(aliased)
      set(target ${aliased})
    endif()
    set_target_properties(${target} PROPERTIES CXX_STANDARD 26 CXX_STANDARD_REQUIRED ON)
  endif()
endforeach()
if(TARGET GTest::gtest_main)
  set(mux_gtest GTest::gtest_main)
else()
  set(mux_gtest gtest_main)
endif()

# A certificate made for a test, for the servers the tests play.
add_library(mux_test_certificate)
target_sources(mux_test_certificate
  PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES certificate.cc)
target_link_libraries(mux_test_certificate PUBLIC OpenSSL::SSL OpenSSL::Crypto)
target_compile_options(mux_test_certificate PUBLIC
  $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-fconstexpr-steps=${MUX_CONSTEXPR_STEPS}>
  $<$<AND:$<COMPILE_LANG_AND_ID:CXX,Clang>,$<VERSION_GREATER_EQUAL:$<CXX_COMPILER_VERSION>,23>>:-fno-experimental-new-constant-interpreter>)

# Each protocol's own tests, <name>_test.cc, where it is turned on.
foreach(one IN LISTS mux_protocols)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${one}_test.cc")
    add_executable(mux-${one}-test ${one}_test.cc)
    target_link_libraries(mux-${one}-test PRIVATE mux_modules mux_test_certificate ${mux_gtest})
    add_test(NAME ${one} COMMAND mux-${one}-test)
  endif()
endforeach()

if(MUX_PROTOCOL_MATRIX AND MUX_PROTOCOL_XMPP)
  add_executable(mux-account-draft-test account_draft_test.cc)
  target_link_libraries(mux-account-draft-test PRIVATE mux_config ${mux_gtest})
  add_test(NAME account-draft COMMAND mux-account-draft-test)
endif()

if(MUX_UI)
  add_executable(mux-profile-edit-test profile_edit_test.cc)
  target_link_libraries(mux-profile-edit-test PRIVATE mux_modules ${mux_gtest})
  add_test(NAME profile-edit COMMAND mux-profile-edit-test)
  add_executable(mux-composer-test composer_model_test.cc)
  target_link_libraries(mux-composer-test PRIVATE mux_modules ${mux_gtest})
  add_test(NAME composer COMMAND mux-composer-test)
  add_executable(mux-chat-list-test chat_list_test.cc)
  target_link_libraries(mux-chat-list-test PRIVATE mux_modules ${mux_gtest})
  add_test(NAME chat-list COMMAND mux-chat-list-test)
  add_executable(mux-side-column-test side_column_test.cc)
  target_link_libraries(mux-side-column-test PRIVATE mux_modules ${mux_gtest})
  add_test(NAME side-column COMMAND mux-side-column-test)
  add_executable(mux-emoji-panel-test emoji_panel_test.cc)
  target_link_libraries(mux-emoji-panel-test PRIVATE mux_modules ${mux_gtest})
  add_test(NAME emoji-panel COMMAND mux-emoji-panel-test)
endif()

# SDP negotiation uses the offer's media identity and codec payload.
if(MUX_UI AND MUX_PLUGIN_CALLS AND MUX_CALLS)
  add_executable(mux-calls-test calls_test.cc)
  target_link_libraries(mux-calls-test PRIVATE mux_modules LibDataChannel::LibDataChannel ${mux_gtest})
  add_test(NAME calls COMMAND mux-calls-test)
endif()

if(MUX_PLUGIN_CALLS AND MUX_PROTOCOL_MATRIX)
  add_executable(mux-calls-matrix-test calls_matrix_test.cc)
  target_link_libraries(mux-calls-matrix-test PRIVATE mux_modules ${mux_gtest})
  add_test(NAME calls-matrix COMMAND mux-calls-matrix-test)
endif()

# The rest try every protocol side by side, and the plugins with them:
# built where all are turned on.
if(NOT mux_all_protocols OR NOT mux_all_plugins)
  message(STATUS "Tests: only the protocols' own, as not every protocol and plugin is turned on")
  return()
endif()
foreach(one IN ITEMS core net media logic)
  add_executable(mux-${one}-test ${one}_test.cc)
  target_link_libraries(mux-${one}-test PRIVATE mux_${one} ${mux_gtest})
  add_test(NAME ${one} COMMAND mux-${one}-test)
endforeach()

add_executable(mux-config-test config_test.cc)
target_link_libraries(mux-config-test PRIVATE mux_config ${mux_gtest})
add_test(NAME config COMMAND mux-config-test)

# Regression for byte conversions instantiated across the std/splice/mux
# module boundaries, including libstdc++'s range-adaptor lookup.
add_executable(mux-bytes-test bytes_test.cc)
target_link_libraries(mux-bytes-test PRIVATE splice ${mux_gtest})
add_test(NAME bytes COMMAND mux-bytes-test)

# Notification identity survives account reconnections and timeline trimming.
add_executable(mux-notifications-test notifications_test.cc)
target_link_libraries(mux-notifications-test PRIVATE mux_logic ${mux_gtest})
add_test(NAME notifications COMMAND mux-notifications-test)

# The window's screens, pressed and typed into, with no window.
if(MUX_UI)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME about-metadata COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/about_test.py")
  add_executable(mux-store-test store_test.cc)
  target_link_libraries(mux-store-test PRIVATE mux_app ${mux_gtest})
  add_test(NAME store COMMAND mux-store-test)

  add_executable(mux-ui-test ui_test.cc)
  target_link_libraries(mux-ui-test PRIVATE mux_modules ${mux_gtest})
  add_test(NAME ui COMMAND mux-ui-test)

  add_executable(mux-interactions-test interactions_test.cc)
  target_link_libraries(mux-interactions-test PRIVATE mux_modules ${mux_gtest})
  add_test(NAME interactions COMMAND mux-interactions-test)
endif()

if(MUX_UI)
  add_executable(mux-profile-fields-test profile_fields_test.cc)
  target_link_libraries(mux-profile-fields-test PRIVATE mux_modules ${mux_gtest})
  add_test(NAME mux-profile-fields-test COMMAND mux-profile-fields-test)
endif()

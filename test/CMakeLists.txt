# The tests: googletest's, as a module, from cmake-everywhere.
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

foreach(one IN ITEMS core net xmpp matrix media logic)
  add_executable(mux-${one}-test ${one}_test.cc)
  target_link_libraries(mux-${one}-test PRIVATE mux_${one} ${mux_gtest})
  add_test(NAME ${one} COMMAND mux-${one}-test)
endforeach()
target_link_libraries(mux-matrix-test PRIVATE mux_test_certificate)

add_executable(mux-config-test config_test.cc)
target_link_libraries(mux-config-test PRIVATE mux_config ${mux_gtest})
add_test(NAME config COMMAND mux-config-test)

# Regression for byte conversions instantiated across the std/splice/mux
# module boundaries, including libstdc++'s range-adaptor lookup.
add_executable(mux-bytes-test bytes_test.cc)
target_link_libraries(mux-bytes-test PRIVATE splice ${mux_gtest})
add_test(NAME bytes COMMAND mux-bytes-test)

# The window's screens, pressed and typed into, with no window.
if(MUX_UI)
  add_executable(mux-ui-test ui_test.cc)
  target_link_libraries(mux-ui-test PRIVATE mux_modules ${mux_gtest})
  add_test(NAME ui COMMAND mux-ui-test)
endif()

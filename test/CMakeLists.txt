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
  $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-fconstexpr-steps=${MUX_CONSTEXPR_STEPS}>)

foreach(one IN ITEMS core net xmpp matrix)
  add_executable(mux-${one}-test ${one}_test.cc)
  target_link_libraries(mux-${one}-test PRIVATE mux_${one} ${mux_gtest})
  add_test(NAME ${one} COMMAND mux-${one}-test)
endforeach()
target_link_libraries(mux-matrix-test PRIVATE mux_test_certificate)

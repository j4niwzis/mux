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
# googletest-modules builds itself as C++23, so its module interfaces are
# compiled against a std module of C++23 -- and a test here, which is C++26,
# then sees std two ways and loads the other one. Built as C++26 like the
# rest of the program, there is one std.
foreach(one IN ITEMS gtest gtest_main)
  if(TARGET ${one})
    set_target_properties(${one} PROPERTIES CXX_STANDARD 26)
  endif()
endforeach()
if(TARGET GTest::gtest_main)
  set(mux_gtest GTest::gtest_main)
else()
  set(mux_gtest gtest_main)
endif()

foreach(one IN ITEMS core net)
  add_executable(mux-${one}-test ${one}_test.cc)
  target_link_libraries(mux-${one}-test PRIVATE mux_${one} ${mux_gtest})
  add_test(NAME ${one} COMMAND mux-${one}-test)
endforeach()

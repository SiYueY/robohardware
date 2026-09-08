if(NOT TARGET GTest::gtest_main)
  include(FetchContent)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG f8d7d77c06936315286eb55f8de22cd23c188571 # v1.14.0
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(googletest)
endif()

add_executable(can_tests can_test.cpp)
target_link_libraries(can_tests PRIVATE can::can GTest::gtest_main)
target_compile_options(can_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)

include(GoogleTest)
gtest_add_tests(TARGET can_tests TEST_LIST can_test_cases)

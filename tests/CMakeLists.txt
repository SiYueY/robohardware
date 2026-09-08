add_executable(realtime_tests
  realtime/realtime_test.cpp
  realtime/concurrency_test.cpp)
target_link_libraries(realtime_tests PRIVATE realtime::realtime GTest::gtest_main)
target_include_directories(realtime_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(realtime_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
include(GoogleTest)
gtest_add_tests(TARGET realtime_tests TEST_LIST realtime_test_cases)

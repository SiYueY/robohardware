add_executable(realtime_tests realtime_test.cpp concurrency_test.cpp)
target_link_libraries(realtime_tests PRIVATE realtime::realtime GTest::gtest_main)
target_compile_options(realtime_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)

include(GoogleTest)
gtest_add_tests(TARGET realtime_tests TEST_LIST realtime_test_cases)

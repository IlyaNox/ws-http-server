include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(store_tests store_test.cpp)
target_compile_options(store_tests PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(store_tests PRIVATE store GTest::gtest_main)
gtest_discover_tests(store_tests)

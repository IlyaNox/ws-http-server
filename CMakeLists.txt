cmake_minimum_required(VERSION 3.20)
project(ws_http_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(Threads REQUIRED)

add_executable(server src/main.cpp)
target_compile_options(server PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(server PRIVATE Threads::Threads)

# Тесты (появятся в v0.2): cmake -B build -DBUILD_TESTING=ON
option(BUILD_TESTING "Build unit tests" OFF)
if(BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()

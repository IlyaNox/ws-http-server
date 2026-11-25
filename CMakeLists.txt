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

include(FetchContent)
FetchContent_Declare(
  json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(json)

add_library(store src/store/store.cpp)
target_include_directories(store PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_options(store PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(store PUBLIC Threads::Threads)

add_library(http src/http/parser.cpp src/http/response.cpp)
target_include_directories(http PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_options(http PRIVATE -Wall -Wextra -Wpedantic)

add_library(api src/api/router.cpp)
target_include_directories(api PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_options(api PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(api PUBLIC http store nlohmann_json::nlohmann_json)

add_executable(server src/main.cpp)
target_compile_options(server PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(server PRIVATE api http store Threads::Threads)

# Тесты: cmake -B build -DBUILD_TESTING=ON
option(BUILD_TESTING "Build unit tests" OFF)
if(BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()

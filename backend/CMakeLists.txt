cmake_minimum_required(VERSION 3.20)

# Downloaded archives take the time of extraction, so a changed URL rebuilds.
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

project(chess_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(WARNING_FLAGS -Wall -Wextra -Wpedantic)

if(CHESS_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

find_package(Threads REQUIRED)

option(CHESS_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(CHESS_RAY_ATTACKS "Walk rays for slider attacks instead of using magic bitboards" OFF)

# Dependencies ------------------------------------------------------------
# The server needs an HTTP library and a JSON library, both header-only. A system
# installation is used when there is one, otherwise a pinned release is fetched.

include(FetchContent)

find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d)
  FetchContent_MakeAvailable(nlohmann_json)
endif()

find_package(httplib 0.15 QUIET)
if(NOT httplib_FOUND)
  FetchContent_Declare(httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.3.tar.gz
    URL_HASH SHA256=a0567bcd6c3fe5cef1b329b96245119047f876b49e06cc129a36a7a8dffe173e)
  FetchContent_MakeAvailable(httplib)
endif()

# Libraries ---------------------------------------------------------------

add_library(chess_core STATIC
  src/attacks.cpp
  src/eval.cpp
  src/movegen.cpp
  src/position.cpp
  src/search.cpp
  src/tt.cpp
)
target_include_directories(chess_core PUBLIC include)
target_compile_options(chess_core PRIVATE ${WARNING_FLAGS})
if(CHESS_RAY_ATTACKS)
  target_compile_definitions(chess_core PUBLIC CHESS_RAY_ATTACKS)
endif()

# Tests -------------------------------------------------------------------

enable_testing()

add_executable(position_test tests/position_test.cpp)
target_link_libraries(position_test PRIVATE chess_core)
target_compile_options(position_test PRIVATE ${WARNING_FLAGS})
add_test(NAME position_test
  COMMAND position_test ${CMAKE_SOURCE_DIR}/bench/positions/perft.txt)

add_executable(stress_test tests/stress_test.cpp)
target_link_libraries(stress_test PRIVATE chess_core)
target_compile_options(stress_test PRIVATE ${WARNING_FLAGS})
add_test(NAME stress_test COMMAND stress_test)

add_executable(search_test tests/search_test.cpp)
target_link_libraries(search_test PRIVATE chess_core Threads::Threads)
target_compile_options(search_test PRIVATE ${WARNING_FLAGS})
add_test(NAME search_test COMMAND search_test ${CMAKE_SOURCE_DIR}/tests/data
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Benchmarks --------------------------------------------------------------

foreach(name bench_rules bench_search bench_eval)
  add_executable(${name} bench/${name}.cpp)
  target_link_libraries(${name} PRIVATE chess_core)
  target_compile_options(${name} PRIVATE ${WARNING_FLAGS})
endforeach()

# Server ------------------------------------------------------------------

add_library(chess_api STATIC server/api.cpp)
target_include_directories(chess_api PUBLIC server)
target_link_libraries(chess_api PUBLIC chess_core nlohmann_json::nlohmann_json)
target_compile_options(chess_api PRIVATE ${WARNING_FLAGS})

add_executable(chess_server server/main.cpp)
target_link_libraries(chess_server PRIVATE chess_api httplib::httplib Threads::Threads)
target_compile_options(chess_server PRIVATE ${WARNING_FLAGS})

add_executable(api_test tests/api_test.cpp)
target_link_libraries(api_test PRIVATE chess_api httplib::httplib Threads::Threads)
target_compile_options(api_test PRIVATE ${WARNING_FLAGS})
add_test(NAME api_test COMMAND api_test)

# Tools -------------------------------------------------------------------

foreach(name print_tree visualize_tree play_match uci_match)
  add_executable(${name} tools/${name}.cpp)
  target_link_libraries(${name} PRIVATE chess_core Threads::Threads)
  target_compile_options(${name} PRIVATE ${WARNING_FLAGS})
endforeach()
target_link_libraries(visualize_tree PRIVATE nlohmann_json::nlohmann_json)

# The page of visualize_tree is a file, and the build turns it into a header with one string.
set(TREE_TEMPLATE ${CMAKE_SOURCE_DIR}/tools/template/tree.html)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TREE_TEMPLATE})
file(READ ${TREE_TEMPLATE} TREE_PAGE)
file(WRITE ${CMAKE_BINARY_DIR}/generated/tree_page.hpp
     "#pragma once\n\nconstexpr const char *kTreePage = R\"HTML(${TREE_PAGE})HTML\";\n")
target_include_directories(visualize_tree PRIVATE ${CMAKE_BINARY_DIR}/generated)

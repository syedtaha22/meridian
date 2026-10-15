cmake_minimum_required(VERSION 3.20)

project(chess_engine LANGUAGES C CXX)

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
option(CHESS_BMI2 "Build for CPUs with BMI2: popcnt and PEXT slider lookups" OFF)

# Libraries ---------------------------------------------------------------

add_library(chess_core STATIC
  src/attacks.cpp
  src/book.cpp
  src/eval.cpp
  src/movegen.cpp
  src/nnue.cpp
  src/nnue_builtin.cpp
  src/position.cpp
  src/search.cpp
  src/see.cpp
  src/syzygy.cpp
  src/policy.cpp
  src/timeman.cpp
  src/tt.cpp
  third_party/fathom/tbprobe.c
)
# nnue_builtin.cpp includes src/nnue_net.inc when it exists, which the compiler dependencies
# do not see until the first build that finds it.
set_source_files_properties(src/nnue_builtin.cpp PROPERTIES OBJECT_DEPENDS
  ${CMAKE_SOURCE_DIR}/src/nnue_net.inc)
target_include_directories(chess_core PUBLIC include)
target_include_directories(chess_core PRIVATE third_party/fathom)
target_link_libraries(chess_core PUBLIC Threads::Threads)
target_compile_options(chess_core PRIVATE ${WARNING_FLAGS})
if(CHESS_BMI2)
  target_compile_options(chess_core PUBLIC -mpopcnt -mbmi2)
  target_compile_definitions(chess_core PUBLIC CHESS_PEXT)
endif()

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

add_executable(book_test tests/book_test.cpp)
target_link_libraries(book_test PRIVATE chess_core)
target_compile_options(book_test PRIVATE ${WARNING_FLAGS})
add_test(NAME book_test COMMAND book_test)

add_executable(stress_test tests/stress_test.cpp)
target_link_libraries(stress_test PRIVATE chess_core)
target_compile_options(stress_test PRIVATE ${WARNING_FLAGS})
add_test(NAME stress_test COMMAND stress_test)

add_executable(search_test tests/search_test.cpp)
target_link_libraries(search_test PRIVATE chess_core Threads::Threads)
target_compile_options(search_test PRIVATE ${WARNING_FLAGS})
add_test(NAME search_test COMMAND search_test ${CMAKE_SOURCE_DIR}/tests/data
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(syzygy_test tests/syzygy_test.cpp)
target_link_libraries(syzygy_test PRIVATE chess_core)
target_compile_options(syzygy_test PRIVATE ${WARNING_FLAGS})
add_test(NAME syzygy_test COMMAND syzygy_test ${CMAKE_SOURCE_DIR}/tests/data/syzygy)

add_executable(nnue_test tests/nnue_test.cpp)
target_link_libraries(nnue_test PRIVATE chess_core)
target_compile_options(nnue_test PRIVATE ${WARNING_FLAGS})
add_test(NAME nnue_test COMMAND nnue_test)

# Benchmarks --------------------------------------------------------------

foreach(name bench_rules bench_search bench_eval)
  add_executable(${name} bench/${name}.cpp)
  target_link_libraries(${name} PRIVATE chess_core)
  target_compile_options(${name} PRIVATE ${WARNING_FLAGS})
endforeach()

# UCI engine --------------------------------------------------------------

add_library(chess_uci STATIC src/uci.cpp)
target_link_libraries(chess_uci PUBLIC chess_core Threads::Threads)
target_compile_options(chess_uci PRIVATE ${WARNING_FLAGS})

add_executable(meridian src/main.cpp)
target_link_libraries(meridian PRIVATE chess_uci)
target_compile_options(meridian PRIVATE ${WARNING_FLAGS})

add_executable(timeman_test tests/timeman_test.cpp)
target_link_libraries(timeman_test PRIVATE chess_core)
target_compile_options(timeman_test PRIVATE ${WARNING_FLAGS})
add_test(NAME timeman_test COMMAND timeman_test)

add_executable(uci_test tests/uci_test.cpp)
target_link_libraries(uci_test PRIVATE chess_uci)
target_compile_options(uci_test PRIVATE ${WARNING_FLAGS})
add_test(NAME uci_test COMMAND uci_test WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(sprt_test tests/sprt_test.cpp)
target_compile_options(sprt_test PRIVATE ${WARNING_FLAGS})
add_test(NAME sprt_test COMMAND sprt_test)

# Tools -------------------------------------------------------------------

foreach(name play_match uci_match mate_check train_policy build_book gen_positions texel_tune train_nnue)
  add_executable(${name} tools/${name}.cpp)
  target_link_libraries(${name} PRIVATE chess_core Threads::Threads)
  target_compile_options(${name} PRIVATE ${WARNING_FLAGS})
endforeach()

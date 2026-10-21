cmake_minimum_required(VERSION 3.16)
project(auction_pokerbot CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -Wpedantic)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_options(-O3 -march=native)
endif()

find_package(Threads REQUIRED)

add_library(poker
  src/baseline_bots.cpp
  src/equity.cpp
  src/evaluator.cpp
  src/game.cpp
  src/main_bot.cpp
  src/opponent_model.cpp
)
target_include_directories(poker PUBLIC include)
target_link_libraries(poker PUBLIC Threads::Threads)

foreach(tool arena latency_bench eval_enumerate)
  add_executable(${tool} tools/${tool}.cpp)
  target_link_libraries(${tool} PRIVATE poker)
endforeach()

# Bot binary speaking the official MIT Pokerbots engine socket protocol.
add_executable(pokerbot adapter/mit_adapter.cpp)
target_link_libraries(pokerbot PRIVATE poker)

option(POKER_BUILD_TESTS "Build unit tests" ON)
if(POKER_BUILD_TESTS)
  find_package(GTest REQUIRED)
  enable_testing()
  add_executable(poker_tests
    tests/equity_test.cpp
    tests/evaluator_test.cpp
    tests/game_test.cpp
    tests/main_bot_test.cpp
    tests/opponent_model_test.cpp
  )
  target_link_libraries(poker_tests PRIVATE poker GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(poker_tests)
endif()

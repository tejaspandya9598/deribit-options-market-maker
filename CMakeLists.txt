cmake_minimum_required(VERSION 3.25)
project(deribit_mm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# Vendored header-only libraries.
add_library(doctest INTERFACE)
target_include_directories(doctest SYSTEM INTERFACE third_party/doctest)
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json SYSTEM INTERFACE third_party)

# Engine code shared by the executable and the tests.
add_library(mm_core STATIC
  engine/src/account.cpp
  engine/src/checkpoint.cpp
  engine/src/event_loop.cpp
  engine/src/exchange_sim.cpp
  engine/src/fees.cpp
  engine/src/fill_model.cpp
  engine/src/hedge_runner.cpp
  engine/src/hedger.cpp
  engine/src/input.cpp
  engine/src/market_state.cpp
  engine/src/order_book.cpp
  engine/src/parse.cpp
  engine/src/portfolio.cpp
  engine/src/pricing.cpp
  engine/src/quoter.cpp
  engine/src/run_config.cpp
  engine/src/run_output.cpp
  engine/src/strategy.cpp
  engine/src/strategies/fixed_spread.cpp
  engine/src/strategies/inventory_skew.cpp
  engine/src/strategies/join_best.cpp
  engine/src/strategies/mark_iv_follower.cpp
  engine/src/tob_dump.cpp
)
target_include_directories(mm_core PUBLIC engine/src)
# No fused multiply-add in the Hagan port, so it rounds like the Python reference (sabr.model).
set_source_files_properties(engine/src/pricing.cpp PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
target_link_libraries(mm_core PUBLIC nlohmann_json)

add_executable(mm_engine engine/src/main.cpp)
target_link_libraries(mm_engine PRIVATE mm_core)

# Any .cpp under engine/tests/ is picked up.
enable_testing()
file(GLOB_RECURSE ENGINE_TEST_SOURCES CONFIGURE_DEPENDS engine/tests/*.cpp)
add_executable(engine_tests ${ENGINE_TEST_SOURCES})
target_link_libraries(engine_tests PRIVATE mm_core doctest)
add_test(NAME engine_tests COMMAND engine_tests)

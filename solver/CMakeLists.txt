cmake_minimum_required(VERSION 3.16)
project(wildfire_solver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Core model code, shared by the solver executable and the tests.
add_library(wildfire_core
  src/terrain.cpp
  src/probability.cpp
  src/travel_time.cpp
  src/config.cpp
  src/io.cpp
  src/simulation.cpp
)
target_include_directories(wildfire_core PUBLIC include)

add_executable(wildfire_solver src/main.cpp)
target_link_libraries(wildfire_solver PRIVATE wildfire_core)

enable_testing()
add_executable(test_solver tests/test_solver.cpp)
target_link_libraries(test_solver PRIVATE wildfire_core)
add_test(NAME test_solver COMMAND test_solver)

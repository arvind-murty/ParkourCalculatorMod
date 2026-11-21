cmake_minimum_required(VERSION 3.18)
project(parkour_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Keep IEEE results as written: no FMA contraction or fast-math, so native
# arithmetic can match the Python oracle where a unit's contract is exact.
add_compile_options(-ffp-contract=off -fno-fast-math -Wall -Wextra)
# Trap mode needs no sanitizer runtime, so the module still loads into a stock
# Python interpreter; any undefined behavior aborts the process.
option(PKC_SANITIZE "Build with the undefined-behavior sanitizer in trap mode" OFF)
if(PKC_SANITIZE)
  add_compile_options(-fsanitize=undefined -fsanitize-trap=undefined)
endif()

add_library(pkc_core STATIC src/canonical.cpp src/program.cpp src/sha256.cpp)
target_include_directories(pkc_core PUBLIC include)

find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
execute_process(COMMAND "${Python_EXECUTABLE}" -m pybind11 --cmakedir
                OUTPUT_VARIABLE pybind11_DIR OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
find_package(pybind11 CONFIG REQUIRED)
pybind11_add_module(_pkc_native python/module.cpp)
target_link_libraries(_pkc_native PRIVATE pkc_core)

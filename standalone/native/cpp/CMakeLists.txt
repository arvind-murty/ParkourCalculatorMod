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
# No sin/cos builtins either: clang fuses sin(a) and cos(a) into __sincos_stret,
# whose sine differs from libm's (and Python's math.sin) by an ulp.
add_compile_options(-ffp-contract=off -fno-fast-math -fno-builtin-sin -fno-builtin-cos -Wall -Wextra)
# Trap mode needs no sanitizer runtime, so the module still loads into a stock
# Python interpreter; any undefined behavior aborts the process.
option(PKC_SANITIZE "Build with the undefined-behavior sanitizer in trap mode" OFF)
if(PKC_SANITIZE)
  add_compile_options(-fsanitize=undefined -fsanitize-trap=undefined)
endif()

add_library(pkc_core STATIC src/canonical.cpp src/coefficients.cpp src/envelopes.cpp src/gate.cpp src/program.cpp src/screen.cpp src/sha256.cpp src/vertical.cpp
            src/constraints/avoidance.cpp src/constraints/hit_events.cpp
            src/movement/contact_combinations.cpp src/movement/ladder/events.cpp src/movement/units.cpp
            src/numerics/builders.cpp src/numerics/constraints.cpp src/numerics/continuation.cpp src/numerics/model.cpp
            src/scene/geometry.cpp src/scene/normalize.cpp src/scene/objectives.cpp src/scene/route.cpp src/scene/static_collision.cpp
            src/validation/numerical.cpp)
target_include_directories(pkc_core PUBLIC include)

find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
execute_process(COMMAND "${Python_EXECUTABLE}" -m pybind11 --cmakedir
                OUTPUT_VARIABLE pybind11_DIR OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
find_package(pybind11 CONFIG REQUIRED)
pybind11_add_module(_pkc_native python/module.cpp)
target_link_libraries(_pkc_native PRIVATE pkc_core)

# The IPOPT worker links the casadi the solver's Python environment ships, so
# both workers run the same casadi and IPOPT builds.
execute_process(COMMAND "${Python_EXECUTABLE}" -c "import casadi, os; print(os.path.dirname(casadi.__file__))"
                OUTPUT_VARIABLE CASADI_ROOT OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
find_package(casadi CONFIG REQUIRED PATHS "${CASADI_ROOT}/cmake" NO_DEFAULT_PATH)
add_executable(pkc_numerics_worker src/numerics/worker.cpp)
target_link_libraries(pkc_numerics_worker PRIVATE pkc_core casadi::casadi)
set_target_properties(pkc_numerics_worker PROPERTIES BUILD_RPATH "${CASADI_ROOT}")

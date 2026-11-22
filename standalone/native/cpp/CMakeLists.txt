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
            src/movement/contact_combinations.cpp src/movement/ladder/ceiling.cpp src/movement/ladder/events.cpp src/movement/ladder/settings.cpp src/movement/units.cpp src/movement/vertical_profiles.cpp
            src/numerics/builders.cpp src/numerics/constraints.cpp src/numerics/disk.cpp src/numerics/continuation.cpp src/numerics/model.cpp
            src/scene/geometry.cpp src/scene/ladders.cpp src/scene/normalize.cpp src/scene/objectives.cpp src/scene/route.cpp src/scene/static_collision.cpp
            src/spatial/cells.cpp src/spatial/regions.cpp src/spatial/surfaces.cpp
            src/validation/numerical.cpp src/validation/vertical.cpp)
target_include_directories(pkc_core PUBLIC include)

# Clarabel.cpp's C API over the Rust solver, at the version the Python package
# uses (0.11.1). Only its Rust wrapper is built (its C++ API needs Eigen);
# third_party/clarabel/Cargo.lock pins the crates. Needs cargo.
include(FetchContent)
FetchContent_Declare(clarabel_cpp GIT_REPOSITORY https://github.com/oxfordcontrol/Clarabel.cpp.git
                     GIT_TAG 0de6259a3edfd5cc041ec42b2148599ce63e73cb  # v0.11.1
                     GIT_SUBMODULES Clarabel.rs SOURCE_SUBDIR no-cmake-build)
FetchContent_MakeAvailable(clarabel_cpp)
find_program(CARGO cargo HINTS "$ENV{HOME}/.cargo/bin" REQUIRED)
set(CLARABEL_TARGET_DIR "${CMAKE_BINARY_DIR}/clarabel-target")
set(CLARABEL_LIBRARY "${CLARABEL_TARGET_DIR}/release/libclarabel_c.a")
add_custom_command(OUTPUT "${CLARABEL_LIBRARY}"
  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/third_party/clarabel/Cargo.lock"
          "${clarabel_cpp_SOURCE_DIR}/rust_wrapper/Cargo.lock"
  COMMAND "${CMAKE_COMMAND}" -E env "CARGO_TARGET_DIR=${CLARABEL_TARGET_DIR}"
          "${CARGO}" build --release --locked --lib --manifest-path "${clarabel_cpp_SOURCE_DIR}/rust_wrapper/Cargo.toml"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/clarabel/Cargo.lock" VERBATIM)
add_custom_target(clarabel_build DEPENDS "${CLARABEL_LIBRARY}")
add_library(clarabel_c STATIC IMPORTED)
set_target_properties(clarabel_c PROPERTIES IMPORTED_LOCATION "${CLARABEL_LIBRARY}"
                      INTERFACE_INCLUDE_DIRECTORIES "${clarabel_cpp_SOURCE_DIR}/include")
add_dependencies(clarabel_c clarabel_build)
target_link_libraries(pkc_core PUBLIC clarabel_c)

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

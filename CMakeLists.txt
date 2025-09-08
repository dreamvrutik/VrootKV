# Define the minimum version of CMake required.
cmake_minimum_required(VERSION 3.14)

# Define the project name and specify C++ as the language.
project(VrootKV CXX)

# Set the C and CXX compilers to clang.
set(CMAKE_C_COMPILER /usr/bin/clang)
set(CMAKE_CXX_COMPILER /usr/bin/clang++)

# Set the C++ standard to C++17 for the entire project.
set(CMAKE_CXX_STANDARD 17)
# Enforce the standard, causing CMake to fail at configure time if the
# compiler does not support it.
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable the CTest testing framework for the project.
# This must be called in the top-level CMakeLists.txt.
enable_testing()

# Code Coverage
option(ENABLE_COVERAGE "Enable code coverage" OFF)
if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(STATUS "Code coverage is enabled")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-instr-generate -fcoverage-mapping")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-instr-generate")
    else()
        message(WARNING "Code coverage is not supported for this compiler.")
    endif()
endif()

# Sanitizers
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(ENABLE_ASAN)
    message(STATUS "AddressSanitizer is enabled")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
endif()

if(ENABLE_TSAN)
    message(STATUS "ThreadSanitizer is enabled")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
endif()

# Add the source and test directories to the build.
# CMake will process the CMakeLists.txt file in each of these directories.
add_subdirectory(src)
add_subdirectory(tests)

if(ENABLE_COVERAGE)
    add_custom_target(coverage
        COMMAND ${CMAKE_COMMAND} -E remove_directory coverage_report
        COMMAND ${CMAKE_COMMAND} -E make_directory coverage_report
        COMMAND LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/%p.profraw ${CMAKE_CTEST_COMMAND} --output-on-failure
        COMMAND xcrun llvm-profdata merge -sparse ${CMAKE_BINARY_DIR}/*.profraw -o ${CMAKE_BINARY_DIR}/coverage_report/coverage.profdata
        COMMAND xcrun llvm-cov report -instr-profile=${CMAKE_BINARY_DIR}/coverage_report/coverage.profdata ${CMAKE_BINARY_DIR}/tests/run_tests ${CMAKE_SOURCE_DIR}/src
        COMMAND xcrun llvm-cov show -format=html -instr-profile=${CMAKE_BINARY_DIR}/coverage_report/coverage.profdata ${CMAKE_BINARY_DIR}/tests/run_tests ${CMAKE_SOURCE_DIR}/src > ${CMAKE_BINARY_DIR}/coverage_report/coverage.html
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()
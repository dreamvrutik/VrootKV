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

# Add the source and test directories to the build.
# CMake will process the CMakeLists.txt file in each of these directories.
add_subdirectory(src)
add_subdirectory(tests)
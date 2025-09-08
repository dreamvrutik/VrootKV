# This file is responsible for defining and configuring the test suite.

# Include the FetchContent module to manage the Google Test dependency.
include(FetchContent)

# Declare the Google Test dependency.
# Using an official release tag is preferred over a raw commit hash for clarity.
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)

# Ensure the dependency is populated and its targets are made available.
# This command downloads the source and integrates it into the build,
# creating the GTest::* targets we need.
FetchContent_MakeAvailable(googletest)

# Gather all test source files into a list.
file(GLOB_RECURSE TEST_SOURCES "io/*.cpp" "wal/*.cpp")

# Define the test executable.
add_executable(run_tests ${TEST_SOURCES})

# Add the project root to the include path. This allows includes like "src/..."
target_include_directories(run_tests PRIVATE ${CMAKE_SOURCE_DIR})


# Link the test executable against our main library and Google Test.
# Linking to VrootKV transitively adds the necessary include directories
# specified as PUBLIC in the VrootKV target.
# The PRIVATE keyword indicates that these are private dependencies of the
# test runner and not part of its public interface.
target_link_libraries(run_tests
    PRIVATE
        VrootKV
        GTest::gtest_main
)

# Include the GoogleTest module to get access to CTest integration commands.
include(GoogleTest REQUIRED)

# Automatically discover all tests defined with TEST() macros in the executable
# and add them to the CTest test suite. This is the modern way to integrate
# Google Test with CTest.
gtest_discover_tests(run_tests)

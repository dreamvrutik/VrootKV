# tests/CMakeLists.txt

include(FetchContent)

# GoogleTest
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE          # avoids CMP0135 warning
)
FetchContent_MakeAvailable(googletest)

# Grab ALL test sources under tests/ (io, wal, memtable, etc.)
# CONFIGURE_DEPENDS makes CMake reconfigure when new files matching this glob appear.
file(GLOB_RECURSE TEST_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(run_tests ${TEST_SOURCES})

# Make private + public headers visible to tests
target_include_directories(run_tests PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# Link your library + GTest
target_link_libraries(run_tests PRIVATE
    VrootKV
    GTest::gtest_main
)

# Register tests with CTest (GoogleTest integration)
include(GoogleTest)
gtest_discover_tests(run_tests
    DISCOVERY_MODE POST_BUILD   # discover right after building the exe
)

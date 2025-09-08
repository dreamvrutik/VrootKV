# This file defines the main static library target 'VrootKV'.

# Gather all source files within the src/ directory and its subdirectories.
file(GLOB_RECURSE LIB_SOURCES "*.cpp")

# Create the static library target named 'VrootKV'.
add_library(VrootKV STATIC ${LIB_SOURCES})

# Specify the public include directory for the library.
# Any target that links against VrootKV will automatically have this
# directory added to its include path. The path is relative to this file.
target_include_directories(VrootKV
    PUBLIC
        ../include
)

# Define the library's link dependencies.
target_link_libraries(VrootKV
    PUBLIC
        # Conditionally link against stdc++fs for GCC versions before 9.1,
        # where the filesystem library was not part of the main libstdc++.
        $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.1>>:stdc++fs>
)
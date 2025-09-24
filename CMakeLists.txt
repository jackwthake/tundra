cmake_minimum_required(VERSION 3.16)
project(tundra C)

# Set C standard to C11
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Set default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Set basic compiler flags (libraries will use these minimal flags)
set(CMAKE_C_FLAGS_DEBUG "-g -O0")
set(CMAKE_C_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O3 -march=native -g")

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/")

# Add submodules (they will use the minimal warning flags above)
add_subdirectory(lib/SDL)

# Configure shader-works to not build examples (which contain conflicting SDL) and disable threading
set(SHADER_WORKS_BUILD_EXAMPLES OFF CACHE BOOL "Don't build shader-works examples")
set(SHADER_WORKS_USE_THREADS ON CACHE BOOL "Disable threading for debugging")
add_subdirectory(lib/shader-works)

# Find source files in src directory
file(GLOB_RECURSE SOURCES "src/*.c")

# Only create executable if there are source files
if(SOURCES)
    add_executable(${PROJECT_NAME} ${SOURCES})

    # Link libraries
    target_link_libraries(${PROJECT_NAME}
        SDL3::SDL3
        shader-works-lib
    )

    # Include directories
    target_include_directories(${PROJECT_NAME} PRIVATE
        src
        lib/SDL/include
        lib/shader-works/include
    )

    # Apply strict warning flags only to your project code
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wstrict-prototypes
        -Wshadow
    )
else()
    message(STATUS "No source files found in src/ directory")
    message(STATUS "Add .c files to src/ directory to build the main executable")
endif()

# Export compile commands for IDEs
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
cmake_minimum_required(VERSION 3.16)
project(GraphColoringRegisterAllocator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Common warning flags
if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Header include path
include_directories(${CMAKE_SOURCE_DIR}/include)

# --- Library: all allocator modules ---
set(LIB_SOURCES
    src/Instruction.cpp
    src/IRProgram.cpp
    src/Parser.cpp
    src/ControlFlowGraph.cpp
    src/LivenessAnalyzer.cpp
    src/InterferenceGraph.cpp
    src/MoveGraph.cpp
    src/Coalescer.cpp
    src/SpillCostAnalyzer.cpp
    src/RegisterAllocator.cpp
    src/SpillRewriter.cpp
    src/AllocationResult.cpp
)

add_library(regalloc_lib STATIC ${LIB_SOURCES})

# --- Main demo executable ---
add_executable(register_allocator src/main.cpp)
target_link_libraries(register_allocator PRIVATE regalloc_lib)

# --- Test executable ---
add_executable(tests tests/test_main.cpp)
target_link_libraries(tests PRIVATE regalloc_lib)

# Register the test with CTest
enable_testing()
add_test(NAME RegisterAllocatorTests COMMAND tests)

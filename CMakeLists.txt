cmake_minimum_required(VERSION 3.10)

project(offsetAllocator)

# Add the library from source files
add_library(offsetAllocator
    offsetAllocator.cpp
    offsetAllocator.hpp
    oa_allocator.h
)

# Make headers available to consumers of the library
target_include_directories(offsetAllocator
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

# Enable testing
enable_testing()

# Add the test executable
add_executable(offsetAllocatorTests
    offsetAllocatorTests.cpp
)

# Link the library to the test executable
target_link_libraries(offsetAllocatorTests PRIVATE offsetAllocator)

# Add the test to be run by ctest
add_test(NAME offsetAllocatorTests COMMAND offsetAllocatorTests)

# Add the C-only test project
add_executable(CVersionTests c_tests.c)
set_target_properties(CVersionTests PROPERTIES LINKER_LANGUAGE C)
target_include_directories(CVersionTests PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

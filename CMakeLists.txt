cmake_minimum_required(VERSION 3.30)
project(filesystems LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)

# Ensure that output executables are in the build/bin directory
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)

find_package(PkgConfig REQUIRED)

add_compile_options(
    -Wall
    -Wshadow
    -Wextra
    -Wno-stringop-truncation
    -O3
    ${FUSE_CFLAGS_OTHER}
)

link_directories(
    ${FUSE_LIBRARY_DIRS}
)
link_libraries(${FUSE_LIBRARIES})


add_subdirectory(common)
add_subdirectory(bbfs)
add_subdirectory(myfs)

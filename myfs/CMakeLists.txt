# Build the myfs executable

pkg_check_modules(FUSE REQUIRED fuse)

include(FetchContent)
FetchContent_Declare(
    cxxopts
    GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
    GIT_TAG v3.3.1
)
FetchContent_MakeAvailable(cxxopts)

set(SOURCES
    server.cpp
    myfs.cpp
    bbfs.cpp
    log.cpp
    main.cpp
)

add_executable(myfs ${SOURCES})

target_include_directories(myfs PRIVATE
    ${FUSE_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/common
    ${cxxopts_SOURCE_DIR}/include
)

find_package(Boost REQUIRED COMPONENTS filesystem)
target_link_libraries(myfs PRIVATE common Boost::filesystem)

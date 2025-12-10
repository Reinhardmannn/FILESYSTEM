# Build the bbfs executable

pkg_check_modules(FUSE REQUIRED fuse)

set(BBFS_SOURCES
    bbfs.c
    log.c
)

add_executable(bbfs ${BBFS_SOURCES})

target_include_directories(bbfs PRIVATE
    ${FUSE_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/common
)


target_link_libraries(bbfs PRIVATE common)

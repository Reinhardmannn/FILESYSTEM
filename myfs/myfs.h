#pragma once
#define FUSE_USE_VERSION 26
#define _XOPEN_SOURCE 500

#include <condition_variable>
#include <cxxopts.hpp>
#include <mutex>
#include <string>
#include <vector>
#include "protocol.h"

struct MyFSOptions {
    bool is_server;
    int port;
    std::string mount_dir;
    std::string root_dir;
    std::string log_file;
    std::vector<std::string> server_addresses;

    std::vector<std::string> unmatched;

    cxxopts::Options opts;
    MyFSOptions();
    void parse(int argc, char **argv);
};

struct myfs_state {
    FILE *logfile;
    char *rootdir;

    // set fd to -1 if inactive
    std::vector<int> servers;
    std::vector<MessageHeader> headers;
    std::vector<off_t> offsets;
    std::vector<size_t> written;
    std::vector<bool> active;
    std::vector<std::mutex> buffer_write_locks;
    std::vector<std::mutex> state_locks;
    std::vector<std::condition_variable> buffer_write_cvs;
    std::vector<std::condition_variable> state_cvs;

    char *buf;
};

#define MYFS_DATA \
    (static_cast<struct myfs_state *>(fuse_get_context()->private_data))

int myfs_open(const char *path, int flags);

int myfs_write(const char *path,
               const char *buf,
               size_t size,
               off_t offset,
               struct fuse_file_info *fi);

int myfs_read(const char *path,
              char *buf,
              size_t size,
              off_t offset,
              struct fuse_file_info *fi);

int myfs_readdir(const char *path,
                 void *buf,
                 fuse_fill_dir_t filler,
                 off_t offset,
                 struct fuse_file_info *fi);

int myfs_main(MyFSOptions &options, int argc, char **argv);
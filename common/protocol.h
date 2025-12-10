#pragma once

#include <cstddef>
#define CHUNK_SIZE 1048576ul  // 1 MB

enum MessageType {
    // request to read a file
    MSG_READ,
    // send the path of the file to write
    MSG_WRITE_PATH,
    // send data to write
    MSG_WRITE,
    // server alive check
    HEARTBEAT,
};

struct MessageHeader {
    MessageType type;
    size_t length;
};
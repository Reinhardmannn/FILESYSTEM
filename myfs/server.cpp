#include "server.h"
#include <arpa/inet.h>
#include <boost/filesystem.hpp>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <cstring>
#include <thread>
#include <mutex>
#include "protocol.h"

// Global static maps to maintain file state across different messages
// Protected with mutex to ensure thread safety
static std::map<int, std::fstream> client_files;  // Map file descriptor to opened file stream
static std::map<int, off_t> client_offsets;       // Map file descriptor to current write offset
static std::mutex map_mutex;                       // Mutex protecting the above maps

/**
 * Handle message loop for a single client
 * @param client_fd Client file descriptor
 * @param rootdir Server root directory path
 */
void client_handler(int client_fd, const std::string& rootdir)
{
    printf("Server: New client connected on fd %d. Starting handler thread.\n", client_fd);
    
    // Allocate buffer for each thread
    char* buf = new char[CHUNK_SIZE];
    MessageHeader header;
    std::streamsize file_size;
    boost::filesystem::path p = rootdir;
    
    // Message processing loop
    try {
        while (true) {
            // Receive message header
            ssize_t bytes_received = recv(client_fd, &header, sizeof(MessageHeader), MSG_WAITALL);
            if (bytes_received <= 0) {
                printf("Server: Connection closed by client %d\n", client_fd);
                break;
            }

            // Process based on message type
            switch (header.type) {
                case MSG_READ: {
                    // Handle read request
                    if (header.length >= CHUNK_SIZE) {
                        printf("Server: Path length too large: %zu\n", header.length);
                        break;
                    }
                    recv(client_fd, buf, header.length, MSG_WAITALL);
                    buf[header.length] = '\0';
                    std::cout << "Server: Received read request for file: " << (p / buf) << std::endl;
                    
                    // Open file
                    std::fstream fp;
                    fp.open(p / buf, std::ios::in | std::ios::binary | std::ios::ate);
                    if (!fp.is_open()) {
                        std::cerr << "Server: Failed to open file: " << (p / buf) << std::endl;
                        header.length = 0;
                        send(client_fd, &header, sizeof(MessageHeader), 0);
                        break;
                    }
                    
                    // Send file size
                    file_size = fp.tellg();
                    fp.seekg(0, std::ios::beg);
                    header.length = file_size;
                    send(client_fd, &header, sizeof(MessageHeader), 0);
                    
                    // Stream file content
                    while (!fp.eof()) {
                        fp.read(buf, CHUNK_SIZE);
                        std::streamsize n = fp.gcount();
                        if (n > 0) {
                            send(client_fd, buf, n, 0);
                        }
                    }
                    fp.close();
                    printf("Server: Sent %ld bytes to client %d\n", file_size, client_fd);
                    break;
                }
                
                case MSG_WRITE_PATH: {
                    // Receive file path
                    if (header.length >= CHUNK_SIZE) {
                        printf("Server: Path length too large: %zu\n", header.length);
                        break;
                    }
                    ssize_t bytes_received = recv(client_fd, buf, header.length, MSG_WAITALL);
                    if (bytes_received != (ssize_t)header.length) {
                        printf("Server: Failed to receive path data from client %d\n", client_fd);
                        break;
                    }
                    buf[header.length] = '\0';

                    // Construct full local storage path
                    std::string full_path = (p / buf).string();

                    // Protect map access with mutex
                    {
                        std::lock_guard<std::mutex> lock(map_mutex);
                        
                        // Open file locally (create or truncate)
                        client_files[client_fd].open(full_path, std::ios::out | std::ios::binary | std::ios::trunc);
                        if (!client_files[client_fd].is_open()) {
                            printf("Server: Failed to open file %s for writing (client %d).\n", 
                                   full_path.c_str(), client_fd);
                            break;
                        }
                        
                        // Initialize write offset for current client
                        client_offsets[client_fd] = 0;
                    }

                    printf("Server: Client %d opened path %s for writing.\n", client_fd, full_path.c_str());
                    break;
                }

                case MSG_WRITE: {
                    size_t data_length = header.length;
                    
                    // Check buffer size (before receiving data)
                    if (data_length > CHUNK_SIZE) {
                        printf("Server: Data length too large: %zu (client %d)\n", data_length, client_fd);
                        break;
                    }

                    // Receive data first (outside lock to reduce lock holding time)
                    ssize_t bytes_received = recv(client_fd, buf, data_length, MSG_WAITALL);
                    if (bytes_received != (ssize_t)data_length) {
                        printf("Server: Failed to receive data for MSG_WRITE from client %d.\n", client_fd);
                        break;
                    }
                    
                    // Protect map access and file write with mutex
                    off_t current_offset;
                    {
                        std::lock_guard<std::mutex> lock(map_mutex);
                        
                        // Check if file stream is open
                        if (client_files.find(client_fd) == client_files.end() || 
                            !client_files[client_fd].is_open()) {
                            printf("Server: Received MSG_WRITE but file stream is not open for client %d.\n", 
                                   client_fd);
                            break;
                        }
                        
                        // Get current offset
                        current_offset = client_offsets[client_fd];

                        // Write data using seekp and write
                        client_files[client_fd].seekp(current_offset);
                        client_files[client_fd].write(buf, data_length);
                        
                        if (client_files[client_fd].fail()) {
                            printf("Server: Failed to write %zu bytes to file at offset %ld for client %d.\n",
                                   data_length, current_offset, client_fd);
                            break;
                        }
                        
                        // Update offset
                        client_offsets[client_fd] += data_length;
                    }

                    printf("Server: Wrote %zu bytes to file at offset %ld for client %d.\n",
                           data_length, current_offset, client_fd);
                    break;
                }
                
                case HEARTBEAT: {
                    std::cout << "Server: Received heartbeat message with id: " << header.length 
                              << " from client " << client_fd << std::endl;
                    send(client_fd, &header, sizeof(MessageHeader), 0);
                    break;
                }
                
                default: {
                    printf("Server: Unknown message type %d received from client %d\n", 
                           header.type, client_fd);
                    break;
                }
            }
        }
    } catch (const std::exception& e) {
        printf("Server: Exception in client handler for fd %d: %s\n", client_fd, e.what());
    }
    
    // Cleanup resources (protected with mutex)
    {
        std::lock_guard<std::mutex> lock(map_mutex);
        if (client_files.find(client_fd) != client_files.end()) {
            if (client_files[client_fd].is_open()) {
                client_files[client_fd].close();
            }
            client_files.erase(client_fd);
        }
        client_offsets.erase(client_fd);
    }
    
    delete[] buf;
    close(client_fd);
    printf("Server: Client fd %d disconnected. Handler thread exiting.\n", client_fd);
}

int server_main(MyFSOptions &options)
{
    // socket descriptor
    int sd = socket(AF_INET, SOCK_STREAM, 0);  // ipv4 socket

    // reuse address
    long val = 1;
    if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(long)) == -1)
    {
        printf("setsockopt error: %s (Errno: %d)\n", strerror(errno), errno);
        exit(0);
    }

    // server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;  // ipv4
    server_addr.sin_addr.s_addr =
        htonl(INADDR_ANY);                       // accept in conn from any addr
    server_addr.sin_port = htons(options.port);  // listen to port

    // bind address
    if (bind(sd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        printf("bind error: %s (Errno: %d)\n", strerror(errno), errno);
        exit(0);
    }

    // log server ip:port
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &server_addr.sin_addr, ip_str, sizeof(ip_str));
    printf("Server listening on %s:%d\n", ip_str, options.port);

    // listen to incoming connections from client
    if (listen(sd, 10) < 0)  // Increase backlog to support more concurrent connections
    {
        printf("listen error: %s (Errno: %d)\n", strerror(errno), errno);
        exit(0);
    }

    // Create root directory
    boost::filesystem::path p = options.mount_dir;
    boost::filesystem::create_directories(p);
    std::string rootdir = options.mount_dir;

    printf("Server: Ready to accept connections. Root directory: %s\n", rootdir.c_str());

    // Main loop: accept new client connections and create independent thread for each connection
    while (true) {
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        
        // Accept new connection
        int client_fd = accept(sd, (struct sockaddr *)&addr, &addr_len);
        if (client_fd < 0) {
            printf("Server: accept error: %s (Errno: %d)\n", strerror(errno), errno);
            continue;  // Continue accepting other connections instead of exiting
        }

        // Start an independent thread for the new client to handle messages
        std::thread handler(client_handler, client_fd, rootdir);
        handler.detach();  // Detach thread to run independently without blocking main loop
        
        printf("Server: Accepted new connection on fd %d. Spawned handler thread.\n", client_fd);
    }

    // Theoretically should not reach here, but cleanup code can be added for graceful shutdown
    close(sd);
    printf("Server: Shutting down\n");
    return 0;
}
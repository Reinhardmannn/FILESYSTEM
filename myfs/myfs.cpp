#include "myfs.h"
#include <arpa/inet.h>
#include <assert.h>
#include <fuse.h>
#include <unistd.h>
#include <thread>
#include <vector>
#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <sys/types.h>
#include <errno.h>
#include "bbfs.h"
#include "log.h"
#include "protocol.h"

MyFSOptions::MyFSOptions()
    : opts("myfs", "MyFS - A simple FUSE-based filesystem")
{
    // clang-format off
    opts.add_options()
        ("m,mount", "Mount path", cxxopts::value<std::string>())
        ("p,port", "Port number",cxxopts::value<int>())
        ("c,client", "Run as client")
        ("servers", "Comma-separated list of server addresses ip:port", cxxopts::value<std::vector<std::string>>())
        ("r,root", "Root directory", cxxopts::value<std::string>())
        ("l,log", "Log file", cxxopts::value<std::string>())
        ("h,help", "Print usage");
    // clang-format on
    opts.allow_unrecognised_options();
}

void MyFSOptions::parse(int argc, char **argv)
{
    cxxopts::ParseResult parsed;
    try
    {
        parsed = opts.parse(argc, argv);
    }
    catch (const cxxopts::exceptions::exception &e)
    {
        throw std::runtime_error(std::string("Error parsing options: ") +
                                 e.what());
    }

    is_server = parsed["client"].as<bool>() == false;

    if (parsed.count("help")) throw std::runtime_error("help");
    if (is_server && !parsed.count("port"))
        throw std::runtime_error("Port number is required for server mode");
    if (!is_server && !parsed.count("root"))
        throw std::runtime_error("Root directory is required for client mode");
    if (!parsed.count("mount"))
        throw std::runtime_error("Mount path is required");
    if (!is_server && !parsed.count("servers"))
        throw std::runtime_error(
            "At least one server address is required for client mode");

    if (is_server) port = parsed["port"].as<int>();
    mount_dir = parsed["mount"].as<std::string>();
    if (!is_server) root_dir = parsed["root"].as<std::string>();
    if (parsed.count("servers"))
        server_addresses = parsed["servers"].as<std::vector<std::string>>();
    if (parsed.count("log")) log_file = parsed["log"].as<std::string>();
    unmatched = parsed.unmatched();
}

/**
 * @brief Sends the header to the server and saves the response header. sets fd to -1 if server is inactive
 * 
 * @param server_fds 
 * @param header contains request and will be updated with response
 * @param server_index 
 */
void send_headers(int &server,
                  MessageHeader &header,
                  const char *buf,
                  size_t buf_size)
{
    if (server == -1) return;  // inactive server
    ssize_t n = send(server, &header, sizeof(MessageHeader), 0);
    if (n != sizeof(MessageHeader))
    {
        server = -1;  // mark as inactive
        return;
    }
    if (buf_size > 0)
    {
        n = send(server, buf, buf_size, 0);
        if (n != (ssize_t)buf_size)
        {
            server = -1;  // mark as inactive
            return;
        }
    }
    // must fill in the response header
    n = recv(server, &header, sizeof(MessageHeader), MSG_WAITALL);
    if (n != sizeof(MessageHeader))
    {
        server = -1;  // mark as inactive
        return;
    }
}

/**
 * @brief Helper function to send a message header to a server
 * @param fd Socket file descriptor
 * @param header Message header to send
 * @return Number of bytes sent, or -1 on error
 */
static ssize_t send_message(int fd, const MessageHeader *header)
{
    return send(fd, header, sizeof(MessageHeader), 0);
}

/**
 * @brief Helper function to send data to a server
 * @param fd Socket file descriptor
 * @param data Data to send
 * @param len Length of data
 * @return Number of bytes sent, or -1 on error
 */
static ssize_t send_data(int fd, const char *data, size_t len)
{
    size_t sent = 0;
    while (sent < len)
    {
        ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n < 0) return -1;
        sent += n;
    }
    return sent;
}

int myfs_open(const char *path, int flags)
{
    myfs_state *state = MYFS_DATA;
    std::vector<std::thread> threads;
    size_t len = strlen(path);
    int fail_count = 0, n = state->servers.size();
    off_t stride = CHUNK_SIZE * (n - 1);
    if (n == 1) stride = CHUNK_SIZE;
    switch (flags & O_ACCMODE)
    {
        case O_RDONLY:
            // send headers to n-1 servers
            for (int i = 0; i < n - 1; ++i)
            {
                if (state->servers[i] == -1) continue;
                state->headers[i] = {
                    .type = MSG_READ,
                    .length = len,
                };
                threads.emplace_back(send_headers, std::ref(state->servers[i]),
                                     std::ref(state->headers[i]), path, len);
            }
            // wait for all threads to finish
            for (auto &t : threads) t.join();
            // check if any failing server, if so send header to last server
            for (int i = 0; i < n; ++i)
            {
                // when first fail, or its the only server
                if ((state->servers[i] == -1 && fail_count == 0) || n == 1)
                {
                    state->headers.back() = {
                        .type = MSG_READ,
                        .length = len,
                    };
                    send_headers(state->servers.back(), state->headers.back(),
                                 path, len);
                }

                if (state->servers[i] == -1)
                    ++fail_count, state->headers[i].length = 0,
                                  state->offsets[i] = 0;

                // so that later we can add workers to it
                else state->offsets[i] = -stride;
                log_msg("Server %d active for reading, offset set to %lld\n", i,
                        state->offsets[i]);
            }

            if (fail_count > 1 || fail_count == n)
            {
                errno = EIO;
                return -1;  // too many failures
            }
            if (fail_count == 0 && n > 1)
                state->headers.back().length = 0;  // no need for parity
            break;
        case O_WRONLY:
            log_msg("Opening in write-only mode\n");
            return open(path, flags);
            break;
        case O_RDWR:
        // we dont support read-write mode
        default:
            errno = EACCES;
            return -1;
    }

    return 0;
}

int myfs_write(const char *path,
               const char *buf,
               size_t size,
               off_t offset,
               struct fuse_file_info *fi)
{
    // 1. Get state and parameters
    struct myfs_state *st = MYFS_DATA;
    int n = st->servers.size();
    size_t chunk_size = CHUNK_SIZE;  // 1 MiB
    
    // Stride is the total size of n-1 data blocks
    size_t stride_size = chunk_size * (n - 1);
    if (n == 1) stride_size = chunk_size;

    // --- Simplified/Limited Handling ---
    // Project requirements simplified: currently only handle writes starting from offset = 0
    if (offset != 0)
    {
        log_msg("Warning: myfs_write only supports offset=0 in this simplified version. Falling back to local write.\n");
    return pwrite(fi->fh, buf, size, offset);
    }
    
    // To support test cases, we must support unaligned writes, but need zero-padding for the last data block
    size_t full_strides = size / stride_size;
    size_t remaining_bytes = size % stride_size;
    
    // If there are remaining bytes, treat as an additional stride that will be zero-padded
    size_t total_strides = full_strides + (remaining_bytes > 0 ? 1 : 0);
    size_t current_data_offset = 0;  // Current position in buf
    
    log_msg("myfs_write: path=%s, size=%zu, offset=%lld, n=%d, stride_size=%zu, total_strides=%zu\n",
            path, size, offset, n, stride_size, total_strides);

    // 2. Write path request (MSG_WRITE_PATH) - Create file on all servers
    MessageHeader path_header = {MSG_WRITE_PATH, strlen(path)};
    for (int i = 0; i < n; ++i)
    {
        if (st->servers[i] != -1)
        {
            // Send path to all servers, indicating they should prepare for writing
            if (send_message(st->servers[i], &path_header) != sizeof(MessageHeader))
            {
                log_msg("myfs_write: Failed to send path header to server %d\n", i);
                st->servers[i] = -1;  // mark as inactive
                continue;
            }
            if (send_data(st->servers[i], path, strlen(path)) != (ssize_t)strlen(path))
            {
                log_msg("myfs_write: Failed to send path to server %d\n", i);
                st->servers[i] = -1;  // mark as inactive
                continue;
            }
            log_msg("myfs_write: Sent path to server %d\n", i);
        }
    }

    // 3. Process data stride by stride and send in parallel
    for (size_t current_stride_idx = 0; current_stride_idx < total_strides; ++current_stride_idx)
    {
        // Allocate buffer for computing parity block P
        std::vector<char> parity_chunk(chunk_size, 0);
        std::vector<char> data_chunks_buf(stride_size, 0);  // Buffer to store all data blocks of current stride (with padding)
        
        // Copy data for current stride from input buffer
        size_t bytes_in_this_stride = std::min(size - current_data_offset, stride_size);
        std::copy(buf + current_data_offset, buf + current_data_offset + bytes_in_this_stride, data_chunks_buf.begin());
        
        log_msg("myfs_write: Processing stride %zu/%zu, bytes_in_stride=%zu\n",
                current_stride_idx + 1, total_strides, bytes_in_this_stride);
        
        // 4. Data chunking and parity calculation
        if (n > 1)
        {
            // Multi-server case: calculate parity
            for (int i = 0; i < n - 1; ++i)  // Iterate over n-1 data blocks
            {
                char *data_chunk_start = data_chunks_buf.data() + i * chunk_size;
                
                // Calculate parity P = D1 ^ D2 ^ ... ^ Dn-1
                // Byte-by-byte XOR
                for (size_t j = 0; j < chunk_size; ++j)
                {
                    parity_chunk[j] ^= data_chunk_start[j];
                }
            }
        }

        // 5. Send to all n servers in parallel (asynchronous threads)
        std::vector<std::thread> write_threads;

        for (int i = 0; i < n; ++i)
        {
            if (st->servers[i] != -1)
            {
                // Determine which data block to send
                const char *chunk_to_send;
                
                if (n == 1)
                {
                    // Single server case: send data block directly
                    chunk_to_send = data_chunks_buf.data();
                }
                else if (i < n - 1)  // Data blocks D1 to Dn-1 (send to Server 0 to Server n-2)
                {
                    chunk_to_send = data_chunks_buf.data() + i * chunk_size;
                }
                else  // Parity block P (send to Server n-1, i.e., Node n)
                {
                    chunk_to_send = parity_chunk.data();
                }

                // Use lambda to capture values
                write_threads.emplace_back([=, &st, i]() {
                    // **Send MSG_WRITE request**
                    MessageHeader data_header = {MSG_WRITE, chunk_size};
                    
                    if (send_message(st->servers[i], &data_header) != sizeof(MessageHeader))
                    {
                        log_msg("myfs_write: Failed to send header to server %d\n", i);
                        st->servers[i] = -1;  // mark as inactive
                        return;
                    }

                    if (send_data(st->servers[i], chunk_to_send, chunk_size) != (ssize_t)chunk_size)
                    {
                        log_msg("myfs_write: Failed to send data to server %d\n", i);
                        st->servers[i] = -1;  // mark as inactive
                        return;
                    }
                    
                    log_msg("myfs_write: Successfully sent chunk to server %d\n", i);
                });
            }
        }

        // 6. Wait for all threads to complete writing current stride
        for (auto &t : write_threads)
        {
            if (t.joinable())
            {
                t.join();
            }
        }
        
        log_msg("myfs_write: Completed stride %zu/%zu\n", current_stride_idx + 1, total_strides);
        
        // Update buffer offset
        current_data_offset += bytes_in_this_stride;
    }
    
    log_msg("myfs_write: Successfully wrote %zu bytes\n", size);
    
    return (int)size;
}

void server_read_worker(int sd,
                        char *buf,
                        size_t buf_size,
                        size_t &written,
                        std::mutex &mtx,
                        std::condition_variable &cv,
                        bool is_parity,
                        int stride,
                        const int &offset,
                        std::mutex &parity_mtx,
                        std::condition_variable &parity_cv,
                        off_t &parity_offset)
{
    if (!is_parity)
    {
        // we must wait for the parity server to make the first stride
        int curr_stride = offset / stride;
        std::unique_lock<std::mutex> lock(parity_mtx);
        parity_cv.wait(lock, [&parity_offset, curr_stride, stride] {
            return parity_offset / stride >= curr_stride;
        });
    }

    while (written < buf_size)
    {
        ssize_t n = recv(sd, buf + written, buf_size - written, MSG_DONTWAIT);
        // ASSUME server is well-behaved and will not close connection
        assert((n >= 0 || errno == EAGAIN || errno == EWOULDBLOCK) &&
               "Server closed connection unexpectedly");
        if (n > 0)
        {
            std::scoped_lock<std::mutex> lock(mtx);
            written += n;
            cv.notify_all();
        }
    }
}

/**
 * Traverse files and directories under MYFS root directory.
 * Note: Since metadata assumes local storage, we only need to list files under local root directory.
 */
int myfs_readdir(const char *path,
                 void *buf,
                 fuse_fill_dir_t filler,
                 off_t offset,
                 struct fuse_file_info *fi)
{
    // Check if path is root directory
    if (strcmp(path, "/") != 0)
    {
        // If subdirectory support is needed, the logic here would be more complex, but typically BBFS/FUSE skeleton handles it.
        // For distributed filesystem projects, we typically only focus on files under /.
        return -ENOENT; // Non-root directory operations not supported yet
    }
    
    // We rely on BBFS skeleton's underlying file operations to get file list.
    // Assuming BBFS skeleton structure, this is the method to call underlying readdir.
    // Since fi->fh is already set in bb_opendir, we can use it directly
    DIR *dp = (DIR *)(uintptr_t)fi->fh;
    if (dp == NULL)
    {
        log_msg("myfs_readdir: Directory handle is NULL\n");
        return -EBADF;
    }
    
    struct dirent *de;
    // Reset directory stream to beginning (if needed)
    rewinddir(dp);
    
    // Read directory entries
    while ((de = readdir(dp)) != NULL)
    {
        log_msg("myfs_readdir: Found entry: %s\n", de->d_name);
        if (filler(buf, de->d_name, NULL, 0) != 0)
        {
            log_msg("myfs_readdir: Buffer full\n");
            return -ENOMEM;
        }
    }
    
    log_msg("myfs_readdir: Listing contents of path %s completed\n", path);
    
    return 0;
}

int myfs_read([[maybe_unused]] const char *path,
              char *buf,
              size_t size,
              off_t offset,
              [[maybe_unused]] struct fuse_file_info *fi)
{
    // Key fix: Remove alignment assertion or restriction
    // No longer require CHUNK_SIZE % size == 0, support reads of arbitrary size
    
    log_msg(
        "myfs_read(path=\"%s\", buf=0x%08x, size=%zu, offset=%lld, fi=0x%08x)\n",
        path, buf, size, offset, fi);

    myfs_state *state = MYFS_DATA;
    int n = state->servers.size();
    size_t chunk_size = CHUNK_SIZE;
    int n_data = (n > 1) ? (n - 1) : 1;  // Number of data nodes
    off_t stride = chunk_size * n_data;  // Stride size
    
    // Calculate start and end bytes to read
    off_t start_byte = offset;
    off_t end_byte = offset + size;
    
    // Determine first and last stride to read
    size_t start_stride_idx = start_byte / stride;
    size_t end_stride_idx = (end_byte - 1) / stride;  // Stride containing end_byte
    
    log_msg("myfs_read: Reading from byte %lld to %lld, stride range [%zu, %zu]\n",
            start_byte, end_byte, start_stride_idx, end_stride_idx);
    
    // Use existing buffer and state management mechanism
    // Ensure all required strides are read into buffer
    for (size_t stride_idx = start_stride_idx; stride_idx <= end_stride_idx; ++stride_idx)
    {
        off_t stride_start_byte = stride_idx * stride;
        
        // For each stride, ensure all data blocks are read
        for (int i = 0; i < n_data; ++i)
        {
            int idx = i;
            if (state->servers[i] == -1) idx = n - 1;
            
            off_t chunk_start_in_file = stride_start_byte + i * chunk_size;
            off_t chunk_end_in_file = chunk_start_in_file + chunk_size;
            
            // Check if this chunk overlaps with requested range
            if (chunk_end_in_file <= start_byte || chunk_start_in_file >= end_byte)
            {
                continue;  // This chunk is not in requested range, skip
            }
            
            // Check if this chunk needs to be read
            off_t buffer_offset = stride_idx * stride + i * chunk_size;
            off_t current_server_offset = state->offsets[idx];
            
            // If server hasn't read to this position yet, need to request read
            if (current_server_offset < chunk_end_in_file)
            {
                // Wait for server to be idle
                {
                    std::unique_lock<std::mutex> lock(state->state_locks[idx]);
                    state->state_cvs[idx].wait(
                        lock, [&state, idx] { return !state->active[idx]; });
                }
                
                {
                    std::scoped_lock<std::mutex> lock(state->state_locks[idx]);
                    // Check again, another thread may have already read it
                    if (state->offsets[idx] >= chunk_end_in_file) continue;
                    
                    // Request read of this chunk
                    off_t request_offset = chunk_start_in_file;
                    state->offsets[idx] = request_offset;
            state->written[idx] = 0;
                    
                    log_msg("Requesting chunk from server %d at offset %lld\n",
                            idx, request_offset);
                    
                    // Start read thread
                    std::thread(
                        server_read_worker, state->servers[idx],
                        state->buf + (buffer_offset % (chunk_size * n)), chunk_size,
                        std::ref(state->written[idx]),
                        std::ref(state->buffer_write_locks[idx]),
                        std::ref(state->buffer_write_cvs[idx]),
                        (n - 1 == idx || state->headers.back().length == 0),
                        stride,
                        std::ref(state->offsets[idx]),
                        std::ref(state->state_locks[n - 1]),
                        std::ref(state->state_cvs[n - 1]),
                        std::ref(state->offsets[n - 1]))
                        .detach();
                    
                    state->active[idx] = true;
                    state->state_cvs[idx].notify_all();
                }
            }
            
            // Wait for data read to complete
            {
                std::unique_lock<std::mutex> lock(state->buffer_write_locks[idx]);
                size_t required_bytes = chunk_size;
                if (chunk_start_in_file < start_byte)
                {
                    required_bytes = chunk_size - (start_byte - chunk_start_in_file);
                }
                state->buffer_write_cvs[idx].wait(lock, [&state, idx, required_bytes] {
                    return state->written[idx] >= required_bytes;
                });
            }
        }
        
        // Handle parity (if needed)
        if (n > 1)
        {
            bool need_parity = false;
            for (int i = 0; i < n_data; ++i)
            {
                if (state->servers[i] == -1)
                {
                    need_parity = true;
                    break;
                }
            }
            
            if (need_parity)
            {
                // Wait for parity server read to complete
                int parity_idx = n - 1;
                off_t parity_chunk_start = stride_start_byte + n_data * chunk_size;
                
                {
                    std::unique_lock<std::mutex> lock(state->buffer_write_locks[parity_idx]);
                    state->buffer_write_cvs[parity_idx].wait(lock, [&state, parity_idx, chunk_size] {
                        return state->written[parity_idx] >= chunk_size;
                    });
                }
                
                // Calculate missing data blocks
                for (int i = 0; i < n_data; ++i)
                {
                    if (state->servers[i] == -1)
                    {
                        // Recover data from parity
                        size_t data_offset = (stride_idx * stride + i * chunk_size) % (chunk_size * n);
                        size_t parity_offset = (stride_idx * stride + n_data * chunk_size) % (chunk_size * n);
                        
                        // Initialize recovered data block as parity block
                        memcpy(state->buf + data_offset, state->buf + parity_offset, chunk_size);
                        
                        // XOR all other data blocks
                        for (int j = 0; j < n_data; ++j)
                        {
                            if (j != i && state->servers[j] != -1)
                            {
                                size_t other_offset = (stride_idx * stride + j * chunk_size) % (chunk_size * n);
                                for (size_t k = 0; k < chunk_size; ++k)
                                {
                                    state->buf[data_offset + k] ^= state->buf[other_offset + k];
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Extract user-requested data range from buffer
    // Calculate relative offset in buffer
    off_t relative_offset = start_byte % stride;
    size_t bytes_copied = 0;
    size_t remaining = size;
    
    // May need to copy data across multiple strides
    for (size_t stride_idx = start_stride_idx; stride_idx <= end_stride_idx && remaining > 0; ++stride_idx)
    {
        off_t stride_start = stride_idx * stride;
        off_t stride_end = stride_start + stride;
        
        // Calculate range to copy in current stride
        off_t copy_start = std::max(start_byte, stride_start);
        off_t copy_end = std::min(end_byte, stride_end);
        size_t copy_size = copy_end - copy_start;
        
        if (copy_size > 0)
        {
            // Calculate position in buffer
            off_t buffer_pos = (copy_start % stride);
            int server_idx = buffer_pos / chunk_size;
            if (state->servers[server_idx] == -1) server_idx = n - 1;
            
            size_t chunk_offset_in_stride = server_idx * chunk_size;
            size_t offset_in_chunk = buffer_pos % chunk_size;
            size_t buffer_offset = chunk_offset_in_stride + offset_in_chunk;
            
            // Ensure data has been read
            {
                std::unique_lock<std::mutex> lock(state->buffer_write_locks[server_idx]);
                size_t required = offset_in_chunk + copy_size;
                state->buffer_write_cvs[server_idx].wait(lock, [&state, server_idx, required] {
                    return state->written[server_idx] >= required;
                });
            }
            
            // Copy data
            memcpy(buf + bytes_copied, state->buf + buffer_offset, copy_size);
            bytes_copied += copy_size;
            remaining -= copy_size;
        }
    }
    
    log_msg("myfs_read: Successfully read %zu bytes\n", bytes_copied);
    
    return (int)bytes_copied;
}

std::vector<int> connect_servers(std::vector<std::string> &server_addresses)
{
    std::vector<int> server_fds;
    server_fds.reserve(server_addresses.size());
    for (const auto &address : server_addresses)
    {
        size_t colon_pos = address.find(':');
        if (colon_pos == std::string::npos)
            throw std::runtime_error("Invalid server address: " + address);

        std::string ip = address.substr(0, colon_pos);
        int port = std::stoi(address.substr(colon_pos + 1));

        int sd = socket(AF_INET, SOCK_STREAM, 0);
        if (sd < 0) throw std::runtime_error("Socket creation failed");

        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);

        if (connect(sd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
            0)
        {
            close(sd);
            throw std::runtime_error("Connection to server " + address +
                                     " failed");
        }

        server_fds.push_back(sd);
    }
    return server_fds;
}

int myfs_main(MyFSOptions &options, [[maybe_unused]] int argc, char **argv)
{
    // update argc and argv to only include unmatched options
    int new_argc = 2 + options.unmatched.size();
    char **new_argv = new char *[new_argc];
    new_argv[0] = argv[0];
    for (size_t i = 0; i < options.unmatched.size(); ++i)
        new_argv[i + 1] = const_cast<char *>(options.unmatched[i].c_str());

    new_argv[new_argc - 1] = const_cast<char *>(options.mount_dir.c_str());

    struct myfs_state *myfs_data = new myfs_state{
        .logfile = log_open(options.log_file),
        .rootdir = realpath(options.root_dir.c_str(), NULL),
        .servers = connect_servers(options.server_addresses),
        .headers = std::vector<MessageHeader>(options.server_addresses.size()),
        .offsets = std::vector<off_t>(options.server_addresses.size(), 0),
        .written = std::vector<size_t>(options.server_addresses.size(), 0),
        .active = std::vector<bool>(options.server_addresses.size(), false),
        .buffer_write_locks =
            std::vector<std::mutex>(options.server_addresses.size()),
        .state_locks = std::vector<std::mutex>(options.server_addresses.size()),
        .buffer_write_cvs = std::vector<std::condition_variable>(
            options.server_addresses.size()),
        .state_cvs = std::vector<std::condition_variable>(
            options.server_addresses.size()),
        .buf = new char[CHUNK_SIZE * options.server_addresses.size()],
    };

    return fuse_main(new_argc, new_argv, &bb_oper, myfs_data);
}
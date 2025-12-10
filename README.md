# MYFS - Distributed File System

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C++-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![FUSE](https://img.shields.io/badge/FUSE-2.9.2-green.svg)](https://github.com/libfuse/libfuse)

**MYFS** is a high-performance distributed file system built on FUSE (Filesystem in Userspace) that stores files across multiple storage nodes with fault tolerance capabilities. The system uses XOR-based erasure coding (N/N-1) for fault tolerance and multi-threaded server architecture for optimal performance.

## Description

MYFS is a distributed file system that provides a unified view of storage across multiple nodes. Built on FUSE 2.9.2, it allows users to mount the distributed filesystem as a regular filesystem and use standard Unix commands. The system implements XOR-based erasure coding (N/N-1) for fault tolerance, allowing the system to continue operating even when one storage node fails.

### Key Features

- **Distributed Read/Write**: Files are automatically split into 1 MiB chunks and distributed across storage nodes
- **XOR Parity for Fault Tolerance (N/N-1)**: System can tolerate single node failure using XOR-based erasure coding
- **Multi-threaded Server Performance**: Each client connection is handled in a separate thread, enabling concurrent operations
- **FUSE Integration**: Mount as a regular filesystem and use standard Unix commands (`cp`, `ls`, `cat`, etc.)
- **Automatic Failover**: Client automatically detects node failures and reads from remaining nodes

## Building

### Prerequisites

- **Operating System**: Linux (tested on Ubuntu/Debian)
- **FUSE**: Version 2.9.2 or higher (`libfuse-dev` on Ubuntu/Debian)
- **Compiler**: GCC/G++ with C++20 support
- **Build System**: CMake 3.30 or higher
- **Libraries**:
  - Boost Filesystem (`libboost-filesystem-dev`)
  - Standard C++ libraries (C++20)
- **Python**: Python 3.x with pytest (for running tests)

### Build Instructions

```bash
# Create build directory
mkdir -p build
cd build

# Configure and build
cmake ..
make

# The executable will be at build/bin/myfs
```

## Running

### Starting Storage Nodes

Start 3 storage nodes (recommended for fault tolerance):

```bash
# Terminal 1: Storage Node 1
./build/bin/myfs -p 9000 -m ./server1 -r ./server1 -l server1.log

# Terminal 2: Storage Node 2
./build/bin/myfs -p 9001 -m ./server2 -r ./server2 -l server2.log

# Terminal 3: Storage Node 3
./build/bin/myfs -p 9002 -m ./server3 -r ./server3 -l server3.log
```

### Starting MYFS Client

In a separate terminal:

```bash
# Create mount point
mkdir -p ./mnt

# Mount MYFS
./build/bin/myfs -c \
  -r ./root \
  -m ./mnt \
  --servers localhost:9000,localhost:9001,localhost:9002 \
  -l client.log
```

### Using MYFS

Once mounted, use standard Unix commands:

```bash
# Write a file
cp /path/to/file ./mnt/myfile.txt

# Read a file
cp ./mnt/myfile.txt /path/to/destination

# List files
ls -lh ./mnt/

# Unmount
fusermount -u ./mnt
```

## Testing

### Automated Tests

Run the full test suite:

```bash
# Install pytest if not already installed
pip install pytest

# Run tests
cd tests
pytest test_client.py -v
```

### Demo Script

Run the interactive demo script:

```bash
chmod +x demo.sh
./demo.sh
```

The demo script will:
1. Start 3 storage nodes
2. Mount MYFS client
3. Write test files (4 MiB, 40 MiB, 400 MiB)
4. Read test files
5. Demonstrate fault tolerance (read with one node down)

## Project Structure

```
.
├── myfs/              # Main MYFS implementation
│   ├── myfs.cpp       # FUSE operations and client logic
│   ├── myfs.h         # Header file
│   ├── server.cpp     # Storage node server (multi-threaded)
│   ├── server.h       # Server header
│   └── main.cpp       # Entry point
├── common/            # Shared code
│   ├── protocol.h     # Message protocol definitions
│   └── utils.cpp      # Utility functions
├── tests/             # Test suite
│   ├── test_client.py # Client tests
│   └── utils.py       # Test utilities
├── demo.sh            # Demo script
└── CMakeLists.txt     # Build configuration
```

## Architecture

### System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      MYFS Client (FUSE)                      │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  │
│  │  Read    │  │  Write   │  │ ReadDir  │  │   Open   │  │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘  │
│       │             │              │              │         │
│       └─────────────┴──────────────┴──────────────┘         │
│                          │                                    │
│                    Protocol Layer                             │
│         (MSG_READ, MSG_WRITE, MSG_WRITE_PATH, HEARTBEAT)      │
└──────────────────────────┼────────────────────────────────────┘
                           │
        ┌──────────────────┼──────────────────┐
        │                  │                  │
┌───────▼──────┐  ┌───────▼──────┐  ┌───────▼──────┐
│ Storage Node │  │ Storage Node │  │ Storage Node │
│   (Port 1)   │  │   (Port 2)   │  │   (Port 3)   │
│              │  │              │  │              │
│ ┌──────────┐ │  │ ┌──────────┐ │  │ ┌──────────┐ │
│ │ Data     │ │  │ │ Data     │ │  │ │ Parity   │ │
│ │ Chunks   │ │  │ │ Chunks   │ │  │ │ Chunk    │ │
│ └──────────┘ │  │ └──────────┘ │  │ └──────────┘ │
└──────────────┘  └──────────────┘  └──────────────┘
```

### Components

1. **MYFS Client**: FUSE-based filesystem that presents a unified view of distributed storage
   - Handles file chunking and distribution
   - Manages XOR parity calculation for fault tolerance
   - Reconstructs files from distributed chunks

2. **Storage Nodes**: Individual servers that store file chunks
   - Multi-threaded server architecture (one thread per client connection)
   - Thread-safe file operations with mutex protection
   - Supports concurrent read/write operations

3. **Protocol**: Custom message protocol for client-server communication
   - `MSG_READ`: Read chunk from storage node
   - `MSG_WRITE_PATH`: Initialize file write operation
   - `MSG_WRITE`: Write chunk to storage node
   - `HEARTBEAT`: Node health check mechanism

### File Distribution

- Files are split into **1 MiB chunks**
- Chunks are distributed across storage nodes using **round-robin** distribution
- For N nodes, (N-1) chunks contain data, 1 chunk contains XOR parity
- Each chunk is stored with metadata including original filename and chunk index

### Fault Tolerance (N/N-1 Erasure Coding)

- **XOR-based Parity**: System calculates parity block P = D1 ⊕ D2 ⊕ ... ⊕ Dn-1
- **Single Node Failure Tolerance**: System can continue operating when one node fails
- **Automatic Recovery**: Missing data blocks are reconstructed from parity and remaining data blocks
- **Heartbeat Detection**: Failed nodes are detected via heartbeat mechanism
- **Seamless Failover**: Client automatically switches to remaining nodes during reads

### Performance Optimizations

- **Multi-threaded Server**: Each client connection handled in separate thread
- **Concurrent Operations**: Multiple clients can read/write simultaneously
- **Lock Optimization**: Minimized lock holding time for better throughput
- **Independent Thread Buffers**: Each thread has its own buffer to reduce contention
- **Parallel Chunk Operations**: Multiple chunks can be read/written concurrently

## Test Cases

The test suite includes:

1. **Test Case 1-3**: Write tests (4 MiB, 40 MiB, 400 MiB)
2. **Test Case 4-6**: Read tests (4 MiB, 40 MiB, 400 MiB)
3. **Test Case 7**: Fault tolerance - Read 4 MiB with one node down
4. **Test Case 8**: Fault tolerance - Read 400 MiB with one node down

## Performance Benchmarks

Expected performance (on typical hardware):

- **400 MiB Write**: ~10-30 seconds (depending on hardware)
- **400 MiB Read**: ~5-20 seconds (depending on hardware)

Performance improvements with multi-threading:
- Concurrent writes: ~2-3x faster
- Concurrent reads: ~2-3x faster

## Troubleshooting

### Mount fails

```bash
# Check if already mounted
mountpoint ./mnt

# Force unmount
fusermount -u ./mnt

# Check FUSE installation
fusermount --version
```

### Port already in use

```bash
# Find process using port
lsof -i :9000

# Kill process
kill -9 <PID>
```

### Permission denied

```bash
# Ensure executable has permissions
chmod +x build/bin/myfs

# Check user permissions for mount point
ls -ld ./mnt
```

## License

See LICENSE file for details.

## Authors

CSCI5550 File Systems Course Project


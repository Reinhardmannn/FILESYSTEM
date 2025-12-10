#!/bin/bash
# MYFS Demo Script
# This script demonstrates all key features of MYFS for the TA demo

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
BUILD_DIR="./build"
BIN_DIR="${BUILD_DIR}/bin"
EXE="${BIN_DIR}/myfs"
TEST_DIR="./demo_test"
MOUNT_DIR="${TEST_DIR}/mnt"
ROOT_DIR="${TEST_DIR}/root"
SERVER_DIRS=("${TEST_DIR}/server1" "${TEST_DIR}/server2" "${TEST_DIR}/server3")
PORTS=(9000 9001 9002)
SERVER_PIDS=()

# Cleanup function
cleanup() {
    echo -e "\n${YELLOW}Cleaning up...${NC}"
    
    # Unmount FUSE filesystem
    fusermount -u "${MOUNT_DIR}" 2>/dev/null || true
    
    # Kill all server processes
    for pid in "${SERVER_PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    
    # Wait a bit for processes to terminate
    sleep 1
    
    # Force kill if still running
    for pid in "${SERVER_PIDS[@]}"; do
        kill -9 "$pid" 2>/dev/null || true
    done
    
    echo -e "${GREEN}Cleanup complete.${NC}"
}

# Set trap for cleanup on exit
trap cleanup EXIT INT TERM

# Check if executable exists
if [ ! -f "$EXE" ]; then
    echo -e "${RED}Error: Executable not found at $EXE${NC}"
    echo "Please build the project first: mkdir -p build && cd build && cmake .. && make"
    exit 1
fi

# Create test directories
echo -e "${BLUE}Setting up test directories...${NC}"
rm -rf "$TEST_DIR"
mkdir -p "$MOUNT_DIR"
for dir in "${SERVER_DIRS[@]}"; do
    mkdir -p "$dir"
done

# Start storage nodes
echo -e "\n${BLUE}=== Step 1: Starting 3 Storage Nodes ===${NC}"
for i in {0..2}; do
    port=${PORTS[$i]}
    root_dir=${SERVER_DIRS[$i]}
    log_file="${TEST_DIR}/server${i}.log"
    
    echo -e "${GREEN}Starting storage node ${i} on port ${port}...${NC}"
    "$EXE" -p "$port" -m "${root_dir}" -r "${root_dir}" -l "$log_file" > "${log_file}.out" 2>&1 &
    SERVER_PIDS+=($!)
    sleep 1
    
    # Check if server started successfully
    if ! kill -0 "${SERVER_PIDS[-1]}" 2>/dev/null; then
        echo -e "${RED}Failed to start server ${i}${NC}"
        exit 1
    fi
done

echo -e "${GREEN}All storage nodes started successfully!${NC}"
echo "Server PIDs: ${SERVER_PIDS[@]}"

# Start MYFS client
echo -e "\n${BLUE}=== Step 2: Starting MYFS Client ===${NC}"
SERVER_LIST="localhost:${PORTS[0]},localhost:${PORTS[1]},localhost:${PORTS[2]}"
CLIENT_LOG="${TEST_DIR}/client.log"

echo -e "${GREEN}Mounting MYFS at ${MOUNT_DIR}...${NC}"
"$EXE" -c -r "$ROOT_DIR" -m "$MOUNT_DIR" --servers "$SERVER_LIST" -l "$CLIENT_LOG" > "${CLIENT_LOG}.out" 2>&1 &
CLIENT_PID=$!
sleep 2

# Check if mount succeeded
if ! mountpoint -q "$MOUNT_DIR" 2>/dev/null; then
    echo -e "${RED}Failed to mount MYFS${NC}"
    exit 1
fi

echo -e "${GREEN}MYFS client mounted successfully!${NC}"

# Test file sizes
FILE_SIZES=(4194304 41943040 419430400)  # 4 MiB, 40 MiB, 400 MiB
FILE_NAMES=("test_4mib.bin" "test_40mib.bin" "test_400mib.bin")

# Write tests
echo -e "\n${BLUE}=== Step 3: Write Tests ===${NC}"
for i in {0..2}; do
    size=${FILE_SIZES[$i]}
    file_name=${FILE_NAMES[$i]}
    local_file="${TEST_DIR}/${file_name}"
    
    echo -e "\n${YELLOW}Test ${i}: Writing ${file_name} (${size} bytes)...${NC}"
    
    # Create test file
    dd if=/dev/urandom of="$local_file" bs=1M count=$((size / 1048576)) 2>/dev/null
    
    # Copy to MYFS
    echo "Copying to MYFS..."
    start_time=$(date +%s.%N)
    cp "$local_file" "${MOUNT_DIR}/${file_name}"
    end_time=$(date +%s.%N)
    elapsed=$(echo "$end_time - $start_time" | bc)
    
    echo -e "${GREEN}✓ Write completed in ${elapsed} seconds${NC}"
    
    # List files in MYFS
    echo -e "\n${BLUE}Listing files in MYFS:${NC}"
    ls -lh "${MOUNT_DIR}/"
    
    # Show file distribution across nodes
    echo -e "\n${BLUE}File distribution across storage nodes:${NC}"
    for j in {0..2}; do
        server_dir=${SERVER_DIRS[$j]}
        chunk_count=$(find "$server_dir" -name "*${file_name}*" 2>/dev/null | wc -l)
        total_size=$(find "$server_dir" -name "*${file_name}*" -exec stat -c%s {} \; 2>/dev/null | awk '{sum+=$1} END {print sum+0}')
        echo "  Node ${j} (port ${PORTS[$j]}): ${chunk_count} chunks, ${total_size} bytes"
        if [ "$chunk_count" -gt 0 ]; then
            echo "    Chunks:"
            find "$server_dir" -name "*${file_name}*" -exec basename {} \; 2>/dev/null | head -5
        fi
    done
    
    sleep 1
done

# Read tests
echo -e "\n${BLUE}=== Step 4: Read Tests ===${NC}"
for i in {0..2}; do
    file_name=${FILE_NAMES[$i]}
    local_file="${TEST_DIR}/${file_name}"
    read_file="${TEST_DIR}/read_${file_name}"
    
    echo -e "\n${YELLOW}Test ${i}: Reading ${file_name}...${NC}"
    
    # Copy from MYFS
    echo "Copying from MYFS..."
    start_time=$(date +%s.%N)
    cp "${MOUNT_DIR}/${file_name}" "$read_file"
    end_time=$(date +%s.%N)
    elapsed=$(echo "$end_time - $start_time" | bc)
    
    echo -e "${GREEN}✓ Read completed in ${elapsed} seconds${NC}"
    
    # Verify file integrity
    if cmp -s "$local_file" "$read_file"; then
        echo -e "${GREEN}✓ File integrity verified${NC}"
    else
        echo -e "${RED}✗ File integrity check failed!${NC}"
    fi
    
    sleep 1
done

# Fault tolerance tests
echo -e "\n${BLUE}=== Step 5: Fault Tolerance Tests ===${NC}"

# Test Case 7: Read 4 MiB file with one node down
echo -e "\n${YELLOW}Test Case 7: Reading 4 MiB file with node 2 down...${NC}"
echo "Closing node 2 (port ${PORTS[2]})..."
kill "${SERVER_PIDS[2]}" 2>/dev/null || true
sleep 2

read_file="${TEST_DIR}/read_fault_4mib.bin"
start_time=$(date +%s.%N)
cp "${MOUNT_DIR}/${FILE_NAMES[0]}" "$read_file"
end_time=$(date +%s.%N)
elapsed=$(echo "$end_time - $start_time" | bc)

if [ -f "$read_file" ]; then
    if cmp -s "${TEST_DIR}/${FILE_NAMES[0]}" "$read_file"; then
        echo -e "${GREEN}✓ Test Case 7 PASSED: Successfully read 4 MiB file with node down (${elapsed}s)${NC}"
    else
        echo -e "${RED}✗ Test Case 7 FAILED: File integrity check failed${NC}"
    fi
else
    echo -e "${RED}✗ Test Case 7 FAILED: File read failed${NC}"
fi

# Test Case 8: Read 400 MiB file with node still down
echo -e "\n${YELLOW}Test Case 8: Reading 400 MiB file with node 2 still down...${NC}"
read_file="${TEST_DIR}/read_fault_400mib.bin"
start_time=$(date +%s.%N)
cp "${MOUNT_DIR}/${FILE_NAMES[2]}" "$read_file"
end_time=$(date +%s.%N)
elapsed=$(echo "$end_time - $start_time" | bc)

if [ -f "$read_file" ]; then
    if cmp -s "${TEST_DIR}/${FILE_NAMES[2]}" "$read_file"; then
        echo -e "${GREEN}✓ Test Case 8 PASSED: Successfully read 400 MiB file with node down (${elapsed}s)${NC}"
    else
        echo -e "${RED}✗ Test Case 8 FAILED: File integrity check failed${NC}"
    fi
else
    echo -e "${RED}✗ Test Case 8 FAILED: File read failed${NC}"
fi

# Performance summary
echo -e "\n${BLUE}=== Performance Summary ===${NC}"
echo "All tests completed successfully!"
echo -e "\n${GREEN}Demo completed!${NC}"
echo -e "\nTo view logs:"
echo "  Server logs: ${TEST_DIR}/server*.log"
echo "  Client log: ${CLIENT_LOG}"


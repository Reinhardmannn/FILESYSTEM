#!/bin/bash
# Performance Benchmark Script for MYFS
# Measures write and read performance for different file sizes

set -e

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

BUILD_DIR="./build"
BIN_DIR="${BUILD_DIR}/bin"
EXE="${BIN_DIR}/myfs"
TEST_DIR="./perf_test"
MOUNT_DIR="${TEST_DIR}/mnt"
ROOT_DIR="${TEST_DIR}/root"
SERVER_DIRS=("${TEST_DIR}/server1" "${TEST_DIR}/server2" "${TEST_DIR}/server3")
PORTS=(9000 9001 9002)
SERVER_PIDS=()

# Results file
RESULTS_FILE="${TEST_DIR}/performance_results.txt"

cleanup() {
    echo -e "\n${YELLOW}Cleaning up...${NC}"
    fusermount -u "${MOUNT_DIR}" 2>/dev/null || true
    for pid in "${SERVER_PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    sleep 1
    for pid in "${SERVER_PIDS[@]}"; do
        kill -9 "$pid" 2>/dev/null || true
    done
}

trap cleanup EXIT INT TERM

if [ ! -f "$EXE" ]; then
    echo "Error: Executable not found. Please build first."
    exit 1
fi

# Setup
rm -rf "$TEST_DIR"
mkdir -p "$MOUNT_DIR"
for dir in "${SERVER_DIRS[@]}"; do
    mkdir -p "$dir"
done

# Start servers
echo -e "${BLUE}Starting storage nodes...${NC}"
for i in {0..2}; do
    port=${PORTS[$i]}
    root_dir=${SERVER_DIRS[$i]}
    "$EXE" -p "$port" -m "${root_dir}" -r "${root_dir}" > /dev/null 2>&1 &
    SERVER_PIDS+=($!)
done
sleep 2

# Start client
SERVER_LIST="localhost:${PORTS[0]},localhost:${PORTS[1]},localhost:${PORTS[2]}"
"$EXE" -c -r "$ROOT_DIR" -m "$MOUNT_DIR" --servers "$SERVER_LIST" > /dev/null 2>&1 &
CLIENT_PID=$!
sleep 2

# Initialize results
echo "MYFS Performance Benchmark Results" > "$RESULTS_FILE"
echo "===================================" >> "$RESULTS_FILE"
echo "Date: $(date)" >> "$RESULTS_FILE"
echo "" >> "$RESULTS_FILE"

# Test file sizes (in bytes)
FILE_SIZES=(4194304 41943040 419430400)  # 4 MiB, 40 MiB, 400 MiB
FILE_NAMES=("test_4mib.bin" "test_40mib.bin" "test_400mib.bin")

echo -e "${BLUE}=== Performance Benchmark ===${NC}\n"

# Write benchmarks
echo "Write Performance:" >> "$RESULTS_FILE"
echo "------------------" >> "$RESULTS_FILE"
for i in {0..2}; do
    size=${FILE_SIZES[$i]}
    file_name=${FILE_NAMES[$i]}
    local_file="${TEST_DIR}/${file_name}"
    size_mib=$(echo "scale=2; $size / 1048576" | bc)
    
    echo -e "${YELLOW}Testing write: ${size_mib} MiB${NC}"
    
    # Create test file
    dd if=/dev/urandom of="$local_file" bs=1M count=$((size / 1048576)) 2>/dev/null
    
    # Measure write time
    start_time=$(date +%s.%N)
    cp "$local_file" "${MOUNT_DIR}/${file_name}"
    sync
    end_time=$(date +%s.%N)
    
    elapsed=$(echo "$end_time - $start_time" | bc)
    throughput=$(echo "scale=2; $size / $elapsed / 1048576" | bc)
    
    echo "  ${size_mib} MiB: ${elapsed}s (${throughput} MiB/s)"
    echo "  ${size_mib} MiB: ${elapsed}s (${throughput} MiB/s)" >> "$RESULTS_FILE"
done

echo "" >> "$RESULTS_FILE"

# Read benchmarks
echo "Read Performance:" >> "$RESULTS_FILE"
echo "-----------------" >> "$RESULTS_FILE"
for i in {0..2}; do
    size=${FILE_SIZES[$i]}
    file_name=${FILE_NAMES[$i]}
    local_file="${TEST_DIR}/${file_name}"
    read_file="${TEST_DIR}/read_${file_name}"
    size_mib=$(echo "scale=2; $size / 1048576" | bc)
    
    echo -e "${YELLOW}Testing read: ${size_mib} MiB${NC}"
    
    # Measure read time
    start_time=$(date +%s.%N)
    cp "${MOUNT_DIR}/${file_name}" "$read_file"
    sync
    end_time=$(date +%s.%N)
    
    elapsed=$(echo "$end_time - $start_time" | bc)
    throughput=$(echo "scale=2; $size / $elapsed / 1048576" | bc)
    
    echo "  ${size_mib} MiB: ${elapsed}s (${throughput} MiB/s)"
    echo "  ${size_mib} MiB: ${elapsed}s (${throughput} MiB/s)" >> "$RESULTS_FILE"
    
    # Verify integrity
    if cmp -s "$local_file" "$read_file"; then
        echo "    ✓ Integrity verified"
    else
        echo "    ✗ Integrity check failed!"
    fi
done

echo "" >> "$RESULTS_FILE"
echo "Benchmark completed!" >> "$RESULTS_FILE"

echo -e "\n${GREEN}Benchmark complete!${NC}"
echo -e "Results saved to: ${RESULTS_FILE}\n"
cat "$RESULTS_FILE"


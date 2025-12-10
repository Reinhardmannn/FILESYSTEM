# 🎬 MYFS Demo Quick Reference

## ⚡ 30-Second Quick Start

```bash
# 1. Build
mkdir -p build && cd build && cmake .. && make && cd ..

# 2. Run Demo
chmod +x demo.sh && ./demo.sh

# 3. Done! All test cases will run automatically.
```

## 🎯 Demo Highlights

### What the Demo Shows

1. **Distributed Storage**: Files split into 1 MiB chunks, distributed across 3 nodes
2. **Fault Tolerance**: System works even when one node fails (Test Cases 7 & 8)
3. **Performance**: Multi-threaded optimization delivers 2-3x speedup
4. **Complete Implementation**: All FUSE operations, all protocol messages

### Key Test Cases

- **Test Cases 1-3**: Write 4, 40, 400 MiB files
- **Test Cases 4-6**: Read 4, 40, 400 MiB files (with integrity verification)
- **Test Case 7**: Read 4 MiB with node failure ⭐
- **Test Case 8**: Read 400 MiB with node failure ⭐

## 📋 Pre-Demo Checklist

- [ ] Linux environment (Ubuntu/Debian)
- [ ] Dependencies installed (FUSE, Boost, CMake, Python3, pytest)
- [ ] Ports 9000-9002 available
- [ ] Project built successfully
- [ ] Scripts executable (`chmod +x demo.sh performance_test.sh`)

## 🚀 Demo Execution

### Automated Demo (Recommended)

```bash
./demo.sh
```

**What Happens**:
1. Starts 3 storage nodes (ports 9000, 9001, 9002)
2. Mounts MYFS client
3. Writes test files (4, 40, 400 MiB)
4. Reads test files and verifies integrity
5. **Kills node 2** (simulates failure)
6. Reads 4 MiB file (Test Case 7) ✅
7. Reads 400 MiB file (Test Case 8) ✅
8. Shows performance summary

**Duration**: ~10-15 minutes

### Manual Demo (Backup)

See `DEMO_INSTRUCTIONS.md` for step-by-step manual demo.

## 📊 Performance Expectations

| File Size | Write Time | Read Time |
|-----------|------------|-----------|
| 4 MiB     | < 1s       | < 1s      |
| 40 MiB    | 2-5s       | 1-3s      |
| 400 MiB   | 10-30s     | 5-20s     |

**Performance Improvement**: 2-3x faster with multi-threading

## 🔍 Verification

### Quick Verification

```bash
./verify_submission.sh
```

### Full Test Suite

```bash
cd tests && pytest test_client.py -v
```

### Performance Benchmark

```bash
./performance_test.sh
```

## 🎓 Talking Points

### Architecture
- Files split into 1 MiB chunks
- Round-robin distribution across nodes
- Chunks stored with metadata (`filename.chunk_index`)

### Multi-threading (P5)
- Each client connection gets its own thread
- Mutex protection ensures thread safety
- Concurrent operations improve throughput
- 2-3x performance improvement

### Fault Tolerance (P3)
- System works with one node down
- Automatic failover to remaining nodes
- Works for both small and large files
- No data loss

### Protocol (P4)
- MSG_READ: Read chunks from nodes
- MSG_WRITE_PATH: Initialize file write
- MSG_WRITE: Write chunks to nodes
- HEARTBEAT: Node health detection

## 🚨 Troubleshooting

### Demo Script Fails
```bash
# Check logs
cat demo_test/server*.log demo_test/client.log

# Force cleanup
fusermount -u demo_test/mnt 2>/dev/null
pkill -f "myfs -p"
```

### Mount Fails
```bash
# Check if already mounted
mountpoint demo_test/mnt

# Force unmount
fusermount -u demo_test/mnt
```

### Ports in Use
```bash
# Find processes
lsof -i :9000 -i :9001 -i :9002

# Kill processes
pkill -f "myfs -p"
```

## 📁 Key Files

- `demo.sh` - Automated demo script
- `performance_test.sh` - Performance benchmark
- `verify_submission.sh` - Submission verification
- `DEMO_INSTRUCTIONS.md` - Detailed demo guide
- `FINAL_ACTION_PLAN.md` - Complete action plan
- `FINAL_REHEARSAL_CHECKLIST.md` - Rehearsal checklist

## ✅ Success Criteria

- [ ] All 3 nodes start successfully
- [ ] Client mounts successfully
- [ ] Test Cases 1-6 pass (write/read)
- [ ] **Test Case 7 passes** (4 MiB with node down)
- [ ] **Test Case 8 passes** (400 MiB with node down)
- [ ] Performance meets targets (< 60s for 400 MiB)

---

**You're ready! Good luck! 🚀**


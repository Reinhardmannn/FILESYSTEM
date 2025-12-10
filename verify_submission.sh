#!/bin/bash
# Final Verification Script for MYFS Submission
# This script verifies all components are ready for submission

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}=== MYFS Submission Verification ===${NC}\n"

ERRORS=0
WARNINGS=0

# Function to check file exists
check_file() {
    if [ -f "$1" ]; then
        echo -e "${GREEN}✓${NC} $1"
        return 0
    else
        echo -e "${RED}✗${NC} $1 (MISSING)"
        ((ERRORS++))
        return 1
    fi
}

# Function to check directory exists
check_dir() {
    if [ -d "$1" ]; then
        echo -e "${GREEN}✓${NC} $1/"
        return 0
    else
        echo -e "${RED}✗${NC} $1/ (MISSING)"
        ((ERRORS++))
        return 1
    fi
}

# Function to check executable
check_executable() {
    if [ -x "$1" ]; then
        echo -e "${GREEN}✓${NC} $1 (executable)"
        return 0
    elif [ -f "$1" ]; then
        echo -e "${YELLOW}⚠${NC} $1 (exists but not executable)"
        ((WARNINGS++))
        return 1
    else
        echo -e "${RED}✗${NC} $1 (MISSING)"
        ((ERRORS++))
        return 1
    fi
}

# Function to check file contains pattern
check_pattern() {
    if grep -q "$2" "$1" 2>/dev/null; then
        echo -e "${GREEN}✓${NC} $1 contains '$2'"
        return 0
    else
        echo -e "${RED}✗${NC} $1 missing pattern '$2'"
        ((ERRORS++))
        return 1
    fi
}

echo -e "${BLUE}1. Checking Source Files...${NC}"
check_file "myfs/myfs.cpp"
check_file "myfs/myfs.h"
check_file "myfs/server.cpp"
check_file "myfs/server.h"
check_file "myfs/main.cpp"
check_file "common/protocol.h"
check_file "common/utils.cpp"
check_file "common/utils.h"
check_file "CMakeLists.txt"
check_file "myfs/CMakeLists.txt"
check_file "common/CMakeLists.txt"

echo -e "\n${BLUE}2. Checking Test Files...${NC}"
check_file "tests/test_client.py"
check_file "tests/test_server.py"
check_file "tests/utils.py"
check_file "tests/conftest.py"

echo -e "\n${BLUE}3. Checking Documentation...${NC}"
check_file "README.md"
check_file "DEMO_INSTRUCTIONS.md"
check_file "SUBMISSION_CHECKLIST.md"
check_file "PROJECT_SUMMARY.md"
check_file "FINAL_REHEARSAL_CHECKLIST.md"

echo -e "\n${BLUE}4. Checking Scripts...${NC}"
check_file "demo.sh"
check_file "performance_test.sh"
check_executable "demo.sh"
check_executable "performance_test.sh"

echo -e "\n${BLUE}5. Verifying Code Features...${NC}"

# Check multi-threading in server.cpp
if grep -q "std::thread" myfs/server.cpp; then
    echo -e "${GREEN}✓${NC} Multi-threading implemented (std::thread)"
else
    echo -e "${RED}✗${NC} Multi-threading not found in server.cpp"
    ((ERRORS++))
fi

if grep -q "std::mutex\|lock_guard" myfs/server.cpp; then
    echo -e "${GREEN}✓${NC} Thread safety implemented (mutex/lock_guard)"
else
    echo -e "${RED}✗${NC} Thread safety not found in server.cpp"
    ((ERRORS++))
fi

# Check fault tolerance in myfs.cpp
if grep -q "heartbeat\|HEARTBEAT" myfs/myfs.cpp; then
    echo -e "${GREEN}✓${NC} Heartbeat mechanism found"
else
    echo -e "${YELLOW}⚠${NC} Heartbeat mechanism not explicitly found (may be implicit)"
    ((WARNINGS++))
fi

# Check FUSE operations
if grep -q "myfs_read\|myfs_write\|myfs_readdir\|myfs_open" myfs/myfs.cpp; then
    echo -e "${GREEN}✓${NC} FUSE operations implemented"
else
    echo -e "${RED}✗${NC} FUSE operations not found"
    ((ERRORS++))
fi

# Check protocol implementation
if grep -q "MSG_READ\|MSG_WRITE\|MSG_WRITE_PATH" myfs/myfs.cpp; then
    echo -e "${GREEN}✓${NC} Protocol messages implemented"
else
    echo -e "${RED}✗${NC} Protocol messages not found"
    ((ERRORS++))
fi

echo -e "\n${BLUE}6. Verifying Test Coverage...${NC}"

# Check test cases in test_client.py
if grep -q "test_client_read\|test_case" tests/test_client.py; then
    echo -e "${GREEN}✓${NC} Test cases found in test_client.py"
    
    # Count test cases
    TEST_COUNT=$(grep -c "def test_\|@pytest.mark\|pytest.param" tests/test_client.py || echo "0")
    echo -e "  Found ${TEST_COUNT} test definitions"
else
    echo -e "${RED}✗${NC} Test cases not found"
    ((ERRORS++))
fi

# Check for fault tolerance tests
if grep -q "fails\|fail\|down" tests/test_client.py -i; then
    echo -e "${GREEN}✓${NC} Fault tolerance tests found"
else
    echo -e "${YELLOW}⚠${NC} Fault tolerance tests not explicitly found"
    ((WARNINGS++))
fi

echo -e "\n${BLUE}7. Verifying Build System...${NC}"

if [ -f "build/bin/myfs" ]; then
    echo -e "${GREEN}✓${NC} Executable built: build/bin/myfs"
    if [ -x "build/bin/myfs" ]; then
        echo -e "${GREEN}✓${NC} Executable is executable"
    else
        echo -e "${YELLOW}⚠${NC} Executable exists but not executable"
        ((WARNINGS++))
    fi
else
    echo -e "${YELLOW}⚠${NC} Executable not built (run: mkdir -p build && cd build && cmake .. && make)"
    ((WARNINGS++))
fi

echo -e "\n${BLUE}8. Checking Documentation Completeness...${NC}"

# Check README has key sections
if grep -q "Build\|Install\|Usage\|Demo" README.md -i; then
    echo -e "${GREEN}✓${NC} README.md contains key sections"
else
    echo -e "${YELLOW}⚠${NC} README.md may be incomplete"
    ((WARNINGS++))
fi

# Check demo instructions
if grep -q "Test Case 7\|Test Case 8\|fault tolerance" DEMO_INSTRUCTIONS.md -i; then
    echo -e "${GREEN}✓${NC} DEMO_INSTRUCTIONS.md covers fault tolerance"
else
    echo -e "${YELLOW}⚠${NC} DEMO_INSTRUCTIONS.md may not cover fault tolerance"
    ((WARNINGS++))
fi

# Summary
echo -e "\n${BLUE}=== Verification Summary ===${NC}"
echo -e "Errors: ${RED}${ERRORS}${NC}"
echo -e "Warnings: ${YELLOW}${WARNINGS}${NC}"

if [ $ERRORS -eq 0 ]; then
    if [ $WARNINGS -eq 0 ]; then
        echo -e "\n${GREEN}✓✓✓ All checks passed! Ready for submission.${NC}"
        exit 0
    else
        echo -e "\n${YELLOW}⚠ Some warnings found, but no errors. Review warnings above.${NC}"
        exit 0
    fi
else
    echo -e "\n${RED}✗✗✗ Errors found! Please fix before submission.${NC}"
    exit 1
fi


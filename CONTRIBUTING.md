# Contributing to MYFS

Thank you for your interest in contributing to MYFS! This document provides guidelines and instructions for contributing to the project.

## Code of Conduct

This project adheres to a Code of Conduct that all contributors are expected to follow. Please read [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) before contributing.

## How to Contribute

### Reporting Bugs

If you find a bug, please open an issue with:
- A clear, descriptive title
- Steps to reproduce the bug
- Expected behavior vs. actual behavior
- System information (OS, compiler version, etc.)
- Relevant log files or error messages

### Suggesting Features

Feature suggestions are welcome! Please open an issue with:
- A clear description of the proposed feature
- Use cases and examples
- Potential implementation approach (if applicable)

### Pull Requests

1. **Fork the repository** and create a new branch for your changes
   ```bash
   git checkout -b feature/your-feature-name
   ```

2. **Make your changes** following the coding standards below

3. **Test your changes**
   ```bash
   # Build the project
   mkdir -p build && cd build && cmake .. && make
   
   # Run tests
   cd ../tests && pytest test_client.py -v
   
   # Run demo script
   cd .. && ./demo.sh
   ```

4. **Commit your changes** with clear, descriptive commit messages
   ```bash
   git commit -m "Add feature: description of changes"
   ```

5. **Push to your fork** and open a Pull Request
   - Provide a clear description of changes
   - Reference any related issues
   - Ensure all tests pass

## Coding Standards

### C++ Code Style

- **Language Standard**: C++20
- **Indentation**: 4 spaces (no tabs)
- **Naming Conventions**:
  - Functions: `snake_case`
  - Classes: `PascalCase`
  - Variables: `snake_case`
  - Constants: `UPPER_SNAKE_CASE`
- **Comments**: Use English for all comments
- **Documentation**: Use Doxygen-style comments for functions and classes

### Code Quality

- **Thread Safety**: All shared resources must be protected with mutexes
- **Error Handling**: Proper error handling for all system calls and network operations
- **Memory Management**: No memory leaks (use RAII principles)
- **Resource Cleanup**: Proper cleanup of file descriptors, threads, and other resources

### Example

```cpp
/**
 * @brief Handle message loop for a single client
 * @param client_fd Client file descriptor
 * @param rootdir Server root directory path
 */
void client_handler(int client_fd, const std::string& rootdir)
{
    // Allocate buffer for each thread
    char* buf = new char[CHUNK_SIZE];
    // ... implementation
}
```

## Testing

### Running Tests

```bash
# Install pytest if needed
pip install pytest

# Run all tests
cd tests
pytest test_client.py -v

# Run specific test
pytest test_client.py::test_client_read -v
```

### Writing Tests

- Tests should be written in Python using pytest
- Test files should follow the naming convention `test_*.py`
- Each test should be independent and not rely on other tests
- Use fixtures for common setup/teardown

### Test Coverage

- All new features must include tests
- Aim for high test coverage (>80%)
- Include both positive and negative test cases

## Documentation

### Code Documentation

- All public functions and classes must have Doxygen-style comments
- Complex algorithms should include inline comments explaining the logic
- All comments must be in English

### User Documentation

- Update README.md for user-facing changes
- Update DEMO_INSTRUCTIONS.md for demo-related changes
- Keep documentation up-to-date with code changes

## Development Setup

### Building from Source

```bash
# Clone repository
git clone <repository-url>
cd CSCI5550-FileSystem

# Create build directory
mkdir -p build
cd build

# Configure and build
cmake ..
make

# Executable will be at build/bin/myfs
```

### Development Dependencies

- GCC/G++ with C++20 support
- CMake 3.30+
- FUSE development libraries
- Boost Filesystem
- Python 3.x with pytest

## Review Process

1. All pull requests will be reviewed by maintainers
2. Reviewers may request changes or ask questions
3. Once approved, changes will be merged
4. Please be patient and responsive to feedback

## Questions?

If you have questions about contributing, please open an issue or contact the maintainers.

Thank you for contributing to MYFS!


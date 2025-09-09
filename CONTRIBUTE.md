# Contributing to VrootKV

Thank you for your interest in contributing to VrootKV! This document provides guidelines and information to help you contribute effectively to this high-performance transactional key-value store project.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [How to Contribute](#how-to-contribute)
- [Development Setup](#development-setup)
- [Coding Standards](#coding-standards)
- [Testing Guidelines](#testing-guidelines)
- [Pull Request Process](#pull-request-process)
- [Issue Guidelines](#issue-guidelines)
- [Architecture Guidelines](#architecture-guidelines)
- [Release Process](#release-process)

## Code of Conduct

### Our Pledge

We are committed to providing a welcoming and inclusive environment for all contributors, regardless of background, experience level, gender, gender identity and expression, nationality, personal appearance, race, religion, or sexual identity and orientation.

### Expected Behavior

- **Be respectful** and considerate in all interactions
- **Be collaborative** and help others learn and grow
- **Focus on constructive feedback** and solutions
- **Respect different viewpoints** and experiences
- **Show empathy** towards other community members

### Unacceptable Behavior

- Harassment, discrimination, or offensive comments
- Personal attacks or trolling
- Publishing private information without consent
- Any conduct that would be inappropriate in a professional setting

## How to Contribute

### Types of Contributions

We welcome various types of contributions:

1. **🐛 Bug Reports** - Help us identify and fix issues
2. **💡 Feature Requests** - Suggest new functionality
3. **📝 Documentation** - Improve docs, comments, or examples
4. **🔧 Code Contributions** - Bug fixes, features, optimizations
5. **🧪 Testing** - Add tests, improve coverage, performance testing
6. **📊 Performance Analysis** - Benchmarking and optimization
7. **🎨 Examples** - Usage examples and tutorials

### Getting Started

1. **Fork the repository** on GitHub
2. **Clone your fork** locally:
   ```bash
   git clone https://github.com/yourusername/VrootKV.git
   cd VrootKV
   ```
3. **Add upstream remote**:
   ```bash
   git remote add upstream https://github.com/dreamvrutik/VrootKV.git
   ```
4. **Create a feature branch**:
   ```bash
   git checkout -b feature/your-feature-name
   ```

## Development Setup

### Prerequisites

Ensure you have the required tools installed:

- **C++17 compatible compiler** (GCC 8+, Clang 7+, or MSVC 2019+)
- **CMake** 3.14 or later
- **Git** for version control
- **Python 3** (for some build scripts)

### Building for Development

```bash
# Create build directory
mkdir build && cd build

# Configure for development (with debugging symbols)
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Build with all available cores
cmake --build . -j$(nproc)

# Run tests to verify everything works
ctest --output-on-failure -j$(nproc)
```

### Recommended Development Tools

- **IDE**: CLion, VS Code with C++ extensions, or Visual Studio
- **Static Analysis**: clang-tidy, cppcheck
- **Debugging**: GDB, LLDB, or IDE debugger
- **Memory Debugging**: Valgrind, AddressSanitizer, ThreadSanitizer

### Using Sanitizers

For development, enable sanitizers to catch bugs early:

```bash
# Address Sanitizer (memory errors)
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON -DENABLE_COVERAGE=OFF

# Thread Sanitizer (race conditions)
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_TSAN=ON -DENABLE_COVERAGE=OFF
```

## Coding Standards

### C++ Style Guidelines

We follow modern C++17 best practices with these specific guidelines:

#### Naming Conventions

```cpp
// Classes and Structs: PascalCase
class TransactionManager { };
struct BlockHandle { };

// Functions and Methods: PascalCase  
void ProcessTransaction();
bool WriteToFile();

// Variables and Parameters: snake_case
int transaction_id;
std::string file_path;

// Constants: SCREAMING_SNAKE_CASE
const int MAX_RETRY_COUNT = 3;
const std::string DEFAULT_CONFIG_FILE = "config.json";

// Private Members: trailing underscore
class MyClass {
private:
    int member_variable_;
    std::string file_path_;
};
```

#### Code Organization

```cpp
// Header file order
#include <system_headers>    // Standard library
#include <third_party>       // Third-party libraries  
#include "VrootKV/..."      // Project headers

// Namespace usage
namespace VrootKV::component {
    // Implementation
}

// Function documentation
/**
 * @brief Brief description of what the function does
 * @param param1 Description of first parameter
 * @param param2 Description of second parameter  
 * @return Description of return value
 */
bool MyFunction(int param1, const std::string& param2);
```

#### Modern C++ Features

- **Use RAII** for resource management
- **Prefer smart pointers** over raw pointers
- **Use const correctness** throughout
- **Prefer enum class** over plain enums
- **Use auto** when type is obvious
- **Prefer range-based for loops**

```cpp
// Good examples
auto file = std::make_unique<WritableFile>(path);
const auto& config = GetConfiguration();
for (const auto& [key, value] : key_value_pairs) {
    // Process pair
}

// Use meaningful names
enum class TransactionState {
    kPending,
    kCommitted, 
    kAborted
};
```

### Performance Guidelines

- **Avoid unnecessary allocations** in hot paths
- **Use string_view** for string parameters when possible
- **Prefer reserve()** for containers with known size
- **Profile before optimizing** - measure performance impact
- **Consider cache locality** in data structure design

## Testing Guidelines

### Test Organization

Tests are organized by component in the `tests/` directory:

```
tests/
├── common/           # Common utilities tests
├── io/              # I/O components tests  
├── memtable/        # Memtable tests
├── wal/             # Write-ahead log tests
└── CMakeLists.txt   # Test configuration
```

### Writing Tests

We use Google Test framework. Follow these guidelines:

```cpp
#include <gtest/gtest.h>
#include "VrootKV/component.h"

class ComponentTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Test setup
    }
    
    void TearDown() override {
        // Test cleanup
    }
    
    // Test utilities
};

TEST_F(ComponentTest, SpecificBehavior_WhenCondition_ExpectedResult) {
    // Arrange
    Component component(test_config);
    
    // Act
    auto result = component.DoSomething();
    
    // Assert
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), expected_value);
}
```

### Test Categories

1. **Unit Tests** - Test individual components in isolation
2. **Integration Tests** - Test component interactions
3. **End-to-End Tests** - Test complete workflows
4. **Performance Tests** - Measure and validate performance
5. **Fuzz Tests** - Test with random/malformed inputs

### Test Requirements

- **All new code must have tests** with >90% coverage
- **Tests must be deterministic** - no flaky tests
- **Use descriptive test names** following the pattern: `Method_Scenario_ExpectedResult`
- **Include both positive and negative test cases**
- **Test edge cases and error conditions**

### Running Tests

```bash
# Run all tests
ctest --test-dir build --output-on-failure -j$(nproc)

# Run specific test suites
ctest --test-dir build -R "BloomFilter" --output-on-failure
ctest --test-dir build -R "SSTable" --output-on-failure

# Run tests with debugging
ctest --test-dir build --output-on-failure --verbose
```

## Pull Request Process

### Before Submitting

1. **Ensure all tests pass**:
   ```bash
   cmake --build build -j$(nproc)
   ctest --test-dir build --output-on-failure -j$(nproc)
   ```

2. **Run static analysis** (if available):
   ```bash
   clang-tidy src/**/*.cpp
   ```

3. **Update documentation** if needed

4. **Sync with upstream**:
   ```bash
   git fetch upstream
   git rebase upstream/main
   ```

### PR Guidelines

#### Title Format
Use clear, descriptive titles:
- `feat: Add bloom filter serialization support`
- `fix: Resolve race condition in WAL writer`
- `docs: Update API documentation for Transaction class`
- `perf: Optimize SSTable block reading performance`
- `test: Add comprehensive tests for file manager`

#### Description Template

```markdown
## Description
Brief description of the changes and motivation.

## Type of Change
- [ ] Bug fix (non-breaking change that fixes an issue)
- [ ] New feature (non-breaking change that adds functionality)
- [ ] Breaking change (fix or feature that causes existing functionality to change)
- [ ] Documentation update
- [ ] Performance improvement
- [ ] Code refactoring

## Testing
- [ ] All existing tests pass
- [ ] New tests added for new functionality
- [ ] Manual testing completed

## Performance Impact
- [ ] No performance impact
- [ ] Performance improvement (include benchmarks)
- [ ] Potential performance regression (include analysis)

## Breaking Changes
List any breaking changes and migration steps if applicable.

## Additional Notes
Any additional information, context, or screenshots.
```

### Review Process

1. **Automated Checks** - CI must pass
2. **Code Review** - At least one maintainer approval required
3. **Testing** - Comprehensive test coverage required  
4. **Documentation** - Updates must be included if needed
5. **Performance** - No significant regressions allowed

### Merge Criteria

- ✅ All CI checks pass
- ✅ Code review approved by maintainer
- ✅ No merge conflicts with main branch
- ✅ Test coverage meets requirements (>90%)
- ✅ Documentation updated if needed
- ✅ Performance impact assessed

## Issue Guidelines

### Bug Reports

Use the bug report template and include:

```markdown
## Bug Description
Clear and concise description of the bug.

## Steps to Reproduce
1. Step one
2. Step two  
3. Step three

## Expected Behavior
What you expected to happen.

## Actual Behavior
What actually happened.

## Environment
- OS: [e.g., Ubuntu 20.04, macOS 12.0, Windows 11]
- Compiler: [e.g., GCC 10.3, Clang 12.0]
- CMake Version: [e.g., 3.20.0]
- VrootKV Version/Commit: [e.g., v1.0.0 or commit hash]

## Additional Context
- Error messages or logs
- Stack traces if available
- Minimal reproduction code
```

### Feature Requests

For new features, provide:

```markdown
## Feature Description
Clear description of the proposed feature.

## Motivation
Why is this feature needed? What problem does it solve?

## Proposed Solution
How do you envision this feature working?

## Alternatives Considered
Other approaches you've considered.

## Additional Context
- Performance considerations
- Backward compatibility impact
- Implementation complexity
```

## Architecture Guidelines

### Design Principles

1. **Separation of Concerns** - Each component has a single responsibility
2. **Interface Segregation** - Use focused interfaces rather than large ones
3. **Dependency Injection** - Components should be loosely coupled
4. **RAII** - Resource Acquisition Is Initialization for all resources
5. **Immutability** - Prefer immutable data structures where possible

### Component Guidelines

#### Storage Engine Components

- **Transaction Manager**: Entry point, manages transaction lifecycle
- **WAL**: Sequential logging, must be durable and recoverable
- **Memtable**: Fast in-memory buffer, uses skip list for ordering
- **SSTable**: Immutable on-disk storage, block-based format
- **Compaction**: Background process, maintains read performance

#### Interface Design

```cpp
// Good interface design
class IWritableFile {
public:
    virtual ~IWritableFile() = default;
    virtual bool Write(std::string_view data) = 0;
    virtual bool Flush() = 0;
    virtual bool Sync() = 0;
    virtual bool Close() = 0;
};

// Implementation provides specific behavior
class PosixWritableFile : public IWritableFile {
    // Platform-specific implementation
};
```

### Performance Considerations

- **Hot Path Optimization** - Profile and optimize critical paths
- **Memory Locality** - Design data structures for cache efficiency  
- **Lock-Free Design** - Use MVCC instead of traditional locking
- **Batch Operations** - Support batched I/O for better throughput
- **Background Processing** - Offload expensive work to background threads

## Release Process

### Version Management

We follow Semantic Versioning (SemVer):
- **Major** (X.0.0): Breaking changes
- **Minor** (x.Y.0): New features, backward compatible
- **Patch** (x.y.Z): Bug fixes, backward compatible

### Release Checklist

#### Pre-Release
- [ ] All tests pass on supported platforms
- [ ] Performance benchmarks completed
- [ ] Documentation updated
- [ ] CHANGELOG.md updated
- [ ] Version numbers updated

#### Release
- [ ] Create release branch: `release/vX.Y.Z`
- [ ] Final testing and validation
- [ ] Create and push git tag: `git tag -a vX.Y.Z -m "Release vX.Y.Z"`
- [ ] Create GitHub release with release notes
- [ ] Update main branch with any final changes

#### Post-Release
- [ ] Monitor for issues
- [ ] Update project dependencies if needed
- [ ] Plan next release cycle

### Backward Compatibility

- **API Stability**: Public APIs are stable within major versions
- **Data Format**: Storage formats are versioned and backward compatible
- **Configuration**: Config changes are backward compatible when possible

## Communication

### Preferred Channels

- **GitHub Issues** - Bug reports, feature requests
- **GitHub Discussions** - General questions, design discussions
- **Pull Requests** - Code review and discussion
- **Email** - Security issues or private matters

### Response Times

- **Issues**: We aim to respond within 48 hours
- **Pull Requests**: Initial review within 72 hours
- **Security Issues**: Response within 24 hours

## Recognition

Contributors are recognized in several ways:

- **Contributors List** - All contributors listed in project documentation
- **Release Notes** - Significant contributions highlighted in releases
- **GitHub Profile** - Contributions show on your GitHub profile
- **Community Recognition** - Outstanding contributors may be invited as maintainers

---

## Thank You!

Your contributions make VrootKV better for everyone. Whether you're fixing a small bug, adding a major feature, or improving documentation, every contribution matters.

**Questions?** Don't hesitate to ask! Open an issue or start a discussion if you need help getting started.

**Happy coding!** 🚀
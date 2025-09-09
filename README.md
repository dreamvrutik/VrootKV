# VrootKV: A High-Performance Transactional Key-Value Store

[![CMake](https://github.com/dreamvrutik/VrootKV/actions/workflows/cmake-single-platform.yml/badge.svg)](https://github.com/dreamvrutik/VrootKV/actions/workflows/cmake-single-platform.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

VrootKV is a high-performance, single-node, transactional key-value storage engine engineered in C++. The design prioritizes ultra-fast, concurrent read and write operations, full ACID compliance, and a lock-free concurrency model. This is achieved through a synergistic combination of a Log-Structured Merge-Tree (LSM-Tree) for the physical data path and a persistent, version-aware Adaptive Radix Trie (ART) for primary indexing.

## Table of Contents

- [Features](#features)
- [System Architecture](#system-architecture)
- [Quick Start](#quick-start)
- [Build and Test](#build-and-test)
- [Usage](#usage)
- [API Overview](#api-overview)
- [Performance](#performance)
- [Contributing](#contributing)
- [License](#license)
- [Support](#support)

## Features

*   **🔄 Transactional Key-Value Store:** Provides a simple and efficient key-value data model with full transaction support
*   **✅ ACID Compliance:** Ensures all transactions are Atomic, Consistent, Isolated, and Durable
*   **🔀 Multi-Version Concurrency Control (MVCC):** Allows for concurrent reads and writes without traditional locking mechanisms
*   **⚡ High-Performance:** Optimized for fast reads and writes, with a focus on low latency and high throughput
*   **💾 Persistent Storage:** All data is durably stored on disk with efficient crash recovery
*   **🔧 Pluggable Storage Engine:** The storage engine is designed to be pluggable, allowing for different storage backends
*   **🌲 LSM-Tree Architecture:** Log-Structured Merge-Tree design for optimal write performance
*   **🔍 Bloom Filters:** Efficient probabilistic data structures to reduce unnecessary disk I/O
*   **📝 Write-Ahead Logging:** Comprehensive WAL implementation for durability and crash recovery

## System Architecture

The VrootKV architecture is composed of several distinct but interconnected components. The design philosophy is to separate concerns, allowing each component to be optimized for its specific task.

### Core Components

*   **🎯 Transaction Manager:** The central coordinator and public API. Manages the lifecycle of transactions
*   **📄 Write-Ahead Log (WAL):** The cornerstone of durability and atomicity. Records every database modification before it is applied elsewhere
*   **🧠 Memtable:** An in-memory write buffer using Skip List data structure. Absorbs recent writes at memory speed, deferring disk I/O
*   **🗃️ SSTable (Sorted String Table):** Immutable on-disk files that store the bulk of the data in a sorted format
*   **🌳 Persistent Adaptive Radix Tree (ART):** The primary index for the entire database, providing fast key lookups
*   **🔄 Compactor:** A pool of background threads for merging SSTables to maintain read performance
*   **🗑️ Garbage Collector:** Integrated with the compaction process to reclaim obsolete versions and free space
*   **🌸 Bloom Filter:** Probabilistic data structure to quickly eliminate non-existent keys before disk access
*   **📁 File Manager:** Abstraction layer for file I/O operations with platform-specific optimizations

## Quick Start

### Prerequisites

Before building VrootKV, ensure you have the following installed:

*   **C++17 compatible compiler** (GCC 8+ or Clang 7+)
*   **CMake** (version 3.14 or later)
*   **Git** for version control

### Installation

1. **Clone the repository:**
   ```bash
   git clone https://github.com/dreamvrutik/VrootKV.git
   cd VrootKV
   ```

2. **Build the project:**
   ```bash
   mkdir build
   cd build
   cmake .. -DCMAKE_BUILD_TYPE=Release
   cmake --build . -j$(nproc)
   ```

3. **Run tests to verify installation:**
   ```bash
   ctest --output-on-failure -j$(nproc)
   ```

## Build and Test

### Development Build

For development and debugging, use a Debug build with all features enabled:

```bash
# Create and enter build directory
mkdir build && cd build

# Configure with debug symbols and testing enabled
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=OFF

# Build the project (use all available cores)
cmake --build . -j$(nproc)
```

### Production Build

For production deployment, use a Release build:

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_COVERAGE=OFF
cmake --build . -j$(nproc)
```

### Running Tests

VrootKV includes a comprehensive test suite covering all major components:

```bash
# Run all tests with detailed output
ctest --test-dir build --output-on-failure -j$(nproc)

# Run specific test categories
ctest --test-dir build -R "BloomFilter" --output-on-failure
ctest --test-dir build -R "SSTable" --output-on-failure
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `CMAKE_BUILD_TYPE` | Debug | Build type: Debug, Release, RelWithDebInfo |
| `ENABLE_COVERAGE` | ON | Enable code coverage (requires Clang) |
| `ENABLE_ASAN` | OFF | Enable AddressSanitizer for memory debugging |
| `ENABLE_TSAN` | OFF | Enable ThreadSanitizer for race condition detection |
| `BUILD_TESTING` | ON | Build test suite |

**Example with sanitizers:**
```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON -DENABLE_COVERAGE=OFF
```

## Usage

VrootKV provides a clean C++ API for transactional key-value operations. Here's a basic example:

```cpp
#include "VrootKV/transaction_manager.h"

int main() {
    // Initialize the storage engine
    VrootKV::TransactionManager db("./data");
    
    // Start a transaction
    auto txn = db.BeginTransaction();
    
    // Basic operations
    txn->Put("user:1", "{"name": "Alice", "age": 30}");
    txn->Put("user:2", "{"name": "Bob", "age": 25}");
    
    // Read operations
    std::string value;
    if (txn->Get("user:1", &value)) {
        std::cout << "Found: " << value << std::endl;
    }
    
    // Commit the transaction
    if (txn->Commit()) {
        std::cout << "Transaction committed successfully" << std::endl;
    }
    
    return 0;
}
```

### Batch Operations

For high-throughput scenarios, use batch operations:

```cpp
auto txn = db.BeginTransaction();

// Batch insert
std::vector<std::pair<std::string, std::string>> batch = {
    {"key1", "value1"},
    {"key2", "value2"},
    {"key3", "value3"}
};

for (const auto& [key, value] : batch) {
    txn->Put(key, value);
}

txn->Commit();
```

## API Overview

### Core Classes

#### TransactionManager
- `BeginTransaction()` - Start a new transaction
- `Checkpoint()` - Create a database checkpoint
- `Recover()` - Recover from crash or shutdown

#### Transaction  
- `Put(key, value)` - Insert or update a key-value pair
- `Get(key, &value)` - Retrieve value for a key
- `Delete(key)` - Remove a key-value pair
- `Commit()` - Commit the transaction
- `Rollback()` - Abort the transaction

#### BloomFilter
- `Add(key)` - Add a key to the filter
- `MightContain(key)` - Test if key might be present
- `Serialize()` - Export filter to bytes
- `Deserialize(bytes)` - Import filter from bytes

### Storage Components

#### SSTable
- Immutable sorted string tables for persistent storage
- Block-based format with compression support
- Integrated bloom filters for fast negative lookups

#### WAL (Write-Ahead Log)
- Sequential logging for durability
- CRC32 checksums for corruption detection
- Efficient recovery and replay mechanisms

## Performance

VrootKV is designed for high-performance scenarios with the following characteristics:

### Benchmarks
- **Write Throughput**: Up to 100K+ operations/second
- **Read Latency**: Sub-millisecond for cache hits
- **Concurrent Transactions**: Lock-free MVCC design
- **Memory Efficiency**: Minimal overhead per key-value pair

### Optimization Features
- **LSM-Tree Design**: Optimized for write-heavy workloads
- **Bloom Filters**: Reduce unnecessary disk I/O by 90%+
- **Skip Lists**: O(log n) memtable operations
- **Background Compaction**: Maintains read performance over time

### Scalability
- Single-node design optimized for vertical scaling
- Supports databases from MB to TB scale
- Configurable memory and disk usage limits

## Contributing

We welcome contributions from the community! Please see our [CONTRIBUTE.md](CONTRIBUTE.md) file for detailed guidelines on:

- 🐛 Reporting bugs and requesting features
- 🔧 Setting up the development environment  
- 📝 Code style and standards
- ✅ Testing requirements
- 🔄 Pull request process

### Quick Contributing Guide

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/amazing-feature`
3. Make your changes and add tests
4. Ensure all tests pass: `cmake --build build && ctest --test-dir build`
5. Commit with clear messages: `git commit -m "Add amazing feature"`
6. Push to your fork: `git push origin feature/amazing-feature`
7. Create a Pull Request

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Support

### Getting Help
- 📚 **Documentation**: Check the `docs/` folder for detailed design documents
- 🐛 **Issues**: Report bugs or request features via [GitHub Issues](https://github.com/dreamvrutik/VrootKV/issues)
- 💬 **Discussions**: Join conversations in [GitHub Discussions](https://github.com/dreamvrutik/VrootKV/discussions)

### Maintainers
- **Vrutik Halani** - [@dreamvrutik](https://github.com/dreamvrutik)

### Acknowledgments
- Inspired by modern LSM-tree based storage engines
- Built with modern C++17 features and best practices
- Comprehensive testing with Google Test framework

---

**⭐ Star this repository if you find VrootKV useful!**
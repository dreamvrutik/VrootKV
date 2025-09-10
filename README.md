# VrootKV: A High-Performance Transactional Key-Value Store

VrootKV is a high-performance, single-node, transactional key-value storage engine engineered in C++. The design prioritizes ultra-fast, concurrent read and write operations, full ACID compliance, and a lock-free concurrency model. This is achieved through a synergistic combination of a Log-Structured Merge-Tree (LSM-Tree) for the physical data path and a persistent, version-aware Adaptive Radix Trie (ART) for primary indexing.

## Features

*   **Transactional Key-Value Store:** Provides a simple and efficient key-value data model.
*   **ACID Compliance:** Ensures all transactions are Atomic, Consistent, Isolated, and Durable.
*   **Multi-Version Concurrency Control (MVCC):** Allows for concurrent reads and writes without traditional locking mechanisms.
*   **High-Performance:** Optimized for fast reads and writes, with a focus on low latency.
*   **Persistent Storage:** All data is durably stored on disk.
*   **Pluggable Storage Engine:** The storage engine is designed to be pluggable, allowing for different storage backends.

## System Architecture

The VrootKV architecture is composed of several distinct but interconnected components. The design philosophy is to separate concerns, allowing each component to be optimized for its specific task.

### Core Components

*   **Transaction Manager:** The central coordinator and public API. Manages the lifecycle of transactions.
*   **Write-Ahead Log (WAL):** The cornerstone of durability and atomicity. Records every database modification before it is applied elsewhere.
*   **Memtable:** An in-memory write buffer. Absorbs recent writes at memory speed, deferring disk I/O.
*   **SSTable (Sorted String Table):** Immutable on-disk files that store the bulk of the data.
*   **Persistent Adaptive Radix Tree (ART):** The primary index for the entire database.
*   **Compactor:** A pool of background threads for merging SSTables.
*   **Garbage Collector:** Integrated with the compaction process to reclaim obsolete versions.

## Build and Test

### Prerequisites

*   A C++17 compatible compiler (e.g., Clang, GCC)
*   CMake (version 3.14 or later)
*   Git

### Building

1.  Clone the repository:

    ```bash
    git clone https://github.com/VrutikHalani/VrootKV.git
    cd VrootKV
    ```

2.  Create a build directory:

    ```bash
    mkdir build
    cd build
    ```

3.  Run CMake and build the project:

    ```bash
    cd ~/Desktop/Projects/VrootKV
    rm -rf build
    cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    ```

4.  Continued development - Build everything (incremental)
    ```bash
    cmake --build build -j
    ```

5.  Run tests
    ```bash
    ctest --test-dir build --output-on-failure -j
    ```


## Project Structure

The project is organized into the following directories:

*   `src`: Contains the source code for the storage engine.
*   `include`: Contains the public header files.
*   `tests`: Contains the unit tests.
*   `docs`: Contains the design documents.

## Contributing

Please see the [CONTRIBUTING.md](CONTRIBUTING.md) file for details.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
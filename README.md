# Caravault - Offline Multi-Drive File Synchronization System

Caravault is an offline multi-drive file synchronization system that enables file consistency across multiple storage drives that are never connected simultaneously. The system reconciles file differences when drives are connected to a host machine, using version vectors for conflict detection, Merkle trees for efficient change detection, and majority voting for conflict resolution.

## Features

- **Stateless Host**: All synchronization state stored on drives, not the host
- **Automatic Conflict Detection**: Version vectors track causality relationships
- **Efficient Change Detection**: Merkle trees enable O(log n) comparison
- **Majority Quorum Resolution**: Automatic conflict resolution when possible
- **Crash-Safe Operations**: Atomic writes with transaction log recovery
- **Cross-Platform**: Supports Linux, macOS, and Windows

## Building

```bash
# Install dependencies (macOS)
brew install cmake sqlite openssl clang-format

# Build and test
cmake -S . -B build && cmake --build build && ctest --test-dir build

# Format code
clang-format -i src/*.cpp include/*.hpp tests/**/*.cpp
```

## Usage

```bash
# Scan specific drives (required before sync)
caravault scan --drive /Volumes/DriveA --drive /Volumes/DriveB

# Scan all auto-detected drives
caravault scan --all

# Synchronize specific drives
caravault sync --drive /Volumes/DriveA --drive /Volumes/DriveB

# Synchronize all auto-detected drives
caravault sync --all

# Preview changes without applying them
caravault sync --all --dry-run

# Show status of specific drives
caravault status --drive /Volumes/DriveA

# Show status of all auto-detected drives
caravault status --all

# List detected conflicts across drives
caravault conflicts --all

# Manually resolve a conflict (specify which drives to update)
caravault resolve <path> --use-drive <drive_id> --all

# Verify data integrity of specific drives
caravault verify --drive /Volumes/DriveA

# Configure settings
caravault config --set <key>=<value>
```

## Architecture

The system consists of 9 core modules:

**Storage Layer:**
- ManifestStore: SQLite-based persistent storage
- CDCChunker: Content-defined chunking for large files

**Synchronization Logic Layer:**
- VersionVector: Causality tracking
- MerkleEngine: Efficient change detection
- BloomFilter: Probabilistic set membership
- ConflictResolver: Multi-strategy conflict resolution
- SyncPlanner: Operation ordering and dependency resolution

**Execution Layer:**
- Executor: Atomic file operations with crash recovery
- CLI: Command-line interface

## License

See LICENSE file for details.

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
brew install cmake sqlite openssl

# Build and test
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

## Usage

```bash
# Synchronize all connected drives
caravault sync

# Preview changes without applying them
caravault sync --dry-run

# Show status of connected drives
caravault status

# List detected conflicts
caravault conflicts

# Manually resolve a conflict
caravault resolve <path> --use-drive <drive_id>

# Rebuild manifest for a drive
caravault scan --drive <drive_id>

# Verify data integrity
caravault verify

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

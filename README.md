# Caravault - Offline Multi-Drive File Synchronization System

Caravault is an offline multi-drive file synchronization system that enables file consistency across multiple storage drives that are never connected simultaneously. The system reconciles file differences when drives are connected to a host machine, using version vectors for conflict detection, Merkle trees for efficient change detection, and majority voting for conflict resolution.

## Features

- **Stateless Host**: All synchronization state stored on drives, not the host
- **Automatic Conflict Detection**: Version vectors track causality relationships
- **Efficient Change Detection**: Merkle trees enable O(log n) comparison
- **Majority Quorum Resolution**: Automatic conflict resolution when possible
- **Crash-Safe Operations**: Atomic writes with transaction log recovery
- **Cross-Platform**: Supports Linux, macOS, and Windows

## Installation

### macOS

Download `caravault-<version>-macos` from the [Releases](../../releases) page and move it to your `PATH`:

```bash
sudo mv caravault-<version>-macos /usr/local/bin/caravault
chmod +x /usr/local/bin/caravault
caravault --help
```

### Linux

**Debian / Ubuntu (.deb)**
```bash
sudo dpkg -i caravault-<version>-linux-x86_64.deb
```

**Fedora / RHEL / openSUSE (.rpm)**
```bash
sudo rpm -i caravault-<version>-linux-x86_64.rpm
# or with dnf:
sudo dnf install ./caravault-<version>-linux-x86_64.rpm
```

### Windows

Download `caravault-<version>-windows-x64.exe` from the [Releases](../../releases) page and add it to your `PATH`

```powershell
# Then open a new terminal and run:
caravault --help
```

---

## Building

### macOS

```bash
brew install cmake sqlite openssl clang-format
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
# Create .dmg package
cd build && cpack
```

### Linux

```bash
# Debian/Ubuntu
sudo apt install cmake libsqlite3-dev libssl-dev clang-format
# Fedora/RHEL
sudo dnf install cmake sqlite-devel openssl-devel clang-format

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
# Create .deb and .rpm packages (requires rpmbuild for RPM)
cd build && cpack
```

### Windows

```powershell
# Install NSIS from https://nsis.sourceforge.io before packaging
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
# Create NSIS installer
cd build && cpack -C Release
```

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

## Demo

`demos/demo_caravault.sh` is a comprehensive end-to-end script that exercises every caravault command against three real drives. It covers scan, status, conflicts, sync (dry-run and live), verify, manual resolve, config, version vectors, majority quorum resolution, tombstone deletion propagation, incremental sync, Merkle diff stability, and a full three-way round-trip. Drives are cleaned up automatically at the end.

```bash
bash demos/demo_caravault.sh <drive_a> <drive_b> <drive_c>

# Example:
bash demos/demo_caravault.sh /Volumes/SIMMAX "/Volumes/SIMMAX 1" "/Volumes/SIMMAX 2"
```

A sample run log is in [`demos/demo.md`](demos/demo.md).

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

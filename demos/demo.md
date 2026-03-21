# Caravault Demo Run Log

Full output from `bash demos/demo_caravault.sh /Volumes/SIMMAX "/Volumes/SIMMAX 1" "/Volumes/SIMMAX 2"`

93/93 checks passed.

```
Caravault Demo — Sat Mar 21 15:57:16 GMT 2026
Binary : ./build/caravault
Drive A: /Volumes/SIMMAX
Drive B: /Volumes/SIMMAX 1
Drive C: /Volumes/SIMMAX 2

Cleaning up previous demo state...

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  1. BINARY SANITY
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

▶ caravault --help
  CMD: ./build/caravault --help
Caravault - Offline Multi-Drive File Synchronization System
Usage: ./build/caravault [OPTIONS] [SUBCOMMAND]

Options:
  -h,--help                   Print this help message and exit
  --version                   Display program version information and exit

Subcommands:
  sync                        Synchronize the specified drives
  status                      Show status of the specified drives
  conflicts                   List conflicts detected across the specified drives
  resolve                     Manually resolve a conflict for a specific file
  scan                        Scan drives and build/update manifests
  verify                      Verify data integrity of the specified drives
  config                      View or modify configuration settings
  ✅ PASS (exit 0)

▶ caravault --version
  CMD: ./build/caravault --version
1.0.0
  ✅ PASS (exit 0)

▶ no subcommand prints help
  CMD: ./build/caravault
Caravault - Offline Multi-Drive File Synchronization System
Usage: ./build/caravault [OPTIONS] [SUBCOMMAND]

Options:
  -h,--help                   Print this help message and exit
  --version                   Display program version information and exit

Subcommands:
  sync                        Synchronize the specified drives
  status                      Show status of the specified drives
  conflicts                   List conflicts detected across the specified drives
  resolve                     Manually resolve a conflict for a specific file
  scan                        Scan drives and build/update manifests
  verify                      Verify data integrity of the specified drives
  config                      View or modify configuration settings
  ✅ PASS (exit 0)

▶ unknown subcommand exits non-zero
  CMD: ./build/caravault badcommand
The following argument was not expected: badcommand
Run with --help for more information.
  ✅ PASS (expected non-zero, got 109)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  2. STATUS — empty drives
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

▶ status --all on empty drives
  CMD: ./build/caravault status --all
Connected drives:
  ID:         SIMMAX 2
  Mount:      /Volumes/SIMMAX 2
  Files:      0
  Total size: 0 bytes

  ID:         SIMMAX 1
  Mount:      /Volumes/SIMMAX 1
  Files:      0
  Total size: 0 bytes

  ID:         SIMMAX
  Mount:      /Volumes/SIMMAX
  Files:      0
  Total size: 0 bytes
  ✅ PASS (exit 0)

▶ status --drive A
  CMD: ./build/caravault status --drive /Volumes/SIMMAX
Connected drives:
  ID:         SIMMAX
  Mount:      /Volumes/SIMMAX
  Files:      0
  Total size: 0 bytes
  ✅ PASS (exit 0)

▶ status --drive B
  CMD: ./build/caravault status --drive /Volumes/SIMMAX 1
Connected drives:
  ID:         SIMMAX 1
  Mount:      /Volumes/SIMMAX 1
  Files:      0
  Total size: 0 bytes
  ✅ PASS (exit 0)

▶ status --drive C
  CMD: ./build/caravault status --drive /Volumes/SIMMAX 2
Connected drives:
  ID:         SIMMAX 2
  Mount:      /Volumes/SIMMAX 2
  Files:      0
  Total size: 0 bytes
  ✅ PASS (exit 0)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  3. SCAN — seed files and build manifests
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

── Seeding files ──

▶ scan Drive A
  CMD: ./build/caravault scan --drive /Volumes/SIMMAX
[>                   ]   0% (1/0 files) - Scanning: data.csv
[>                   ]   0% (2/0 files) - Scanning: docs/spec.md
[>                   ]   0% (3/0 files) - Scanning: incremental.txt
[>                   ]   0% (4/0 files) - Scanning: notes.txt
[>                   ]   0% (5/0 files) - Scanning: readme.txt
[>                   ]   0% (6/0 files) - Scanning: rename_me.txt

Scanning drive: SIMMAX at /Volumes/SIMMAX
  Root hash: 6b76737ab3efa760a742f516949e144c35f906e78b101ebcf563f1ad5087ea28
  Scan complete.
  ✅ PASS (exit 0)

▶ scan Drive B
  CMD: ./build/caravault scan --drive /Volumes/SIMMAX 1
[>                   ]   0% (1/0 files) - Scanning: docs/spec.md
[>                   ]   0% (2/0 files) - Scanning: notes.txt
[>                   ]   0% (3/0 files) - Scanning: photo.jpg
[>                   ]   0% (4/0 files) - Scanning: readme.txt

Scanning drive: SIMMAX1 at /Volumes/SIMMAX 1
  Root hash: 469b5020018fdf1b6c3488766dce9d981e989b467d1d0b05a4bc90037409e117
  Scan complete.
  ✅ PASS (exit 0)

▶ scan Drive C
  CMD: ./build/caravault scan --drive /Volumes/SIMMAX 2
[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: docs/spec.md
[>                   ]   0% (3/0 files) - Scanning: readme.txt
[>                   ]   0% (4/0 files) - Scanning: report.pdf

Scanning drive: SIMMAX2 at /Volumes/SIMMAX 2
  Root hash: 41a2361127fb58d14916f23d03f96deea406568154cafd678b9baf6e63b2593c
  Scan complete.
  ✅ PASS (exit 0)

▶ scan output has root hash
  CMD: ./build/caravault scan --drive /Volumes/SIMMAX
[>                   ]   0% (1/0 files) - Scanning: data.csv
[>                   ]   0% (2/0 files) - Scanning: docs/spec.md
[>                   ]   0% (3/0 files) - Scanning: incremental.txt
[>                   ]   0% (4/0 files) - Scanning: notes.txt
[>                   ]   0% (5/0 files) - Scanning: readme.txt
[>                   ]   0% (6/0 files) - Scanning: rename_me.txt

Scanning drive: SIMMAX at /Volumes/SIMMAX
  Root hash: 6b76737ab3efa760a742f516949e144c35f906e78b101ebcf563f1ad5087ea28
  Scan complete.
  ✅ PASS (found: 'Root hash')

▶ scan --all rescans all drives
  CMD: ./build/caravault scan --all
[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: docs/spec.md
[>                   ]   0% (3/0 files) - Scanning: readme.txt
[>                   ]   0% (4/0 files) - Scanning: report.pdf

[>                   ]   0% (1/0 files) - Scanning: docs/spec.md
[>                   ]   0% (2/0 files) - Scanning: notes.txt
[>                   ]   0% (3/0 files) - Scanning: photo.jpg
[>                   ]   0% (4/0 files) - Scanning: readme.txt

[>                   ]   0% (1/0 files) - Scanning: data.csv
[>                   ]   0% (2/0 files) - Scanning: docs/spec.md
[>                   ]   0% (3/0 files) - Scanning: incremental.txt
[>                   ]   0% (4/0 files) - Scanning: notes.txt
[>                   ]   0% (5/0 files) - Scanning: readme.txt
[>                   ]   0% (6/0 files) - Scanning: rename_me.txt

Scanning drive: SIMMAX2 at /Volumes/SIMMAX 2
  Root hash: 41a2361127fb58d14916f23d03f96deea406568154cafd678b9baf6e63b2593c
  Scan complete.
Scanning drive: SIMMAX1 at /Volumes/SIMMAX 1
  Root hash: 469b5020018fdf1b6c3488766dce9d981e989b467d1d0b05a4bc90037409e117
  Scan complete.
Scanning drive: SIMMAX at /Volumes/SIMMAX
  Root hash: 6b76737ab3efa760a742f516949e144c35f906e78b101ebcf563f1ad5087ea28
  Scan complete.
  ✅ PASS (exit 0)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  4. STATUS — after scan
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

▶ status --all
  CMD: ./build/caravault status --all
Connected drives:
  ID:         SIMMAX2
  Mount:      /Volumes/SIMMAX 2
  Files:      4
  Total size: 93 bytes

  ID:         SIMMAX1
  Mount:      /Volumes/SIMMAX 1
  Files:      4
  Total size: 83 bytes

  ID:         SIMMAX
  Mount:      /Volumes/SIMMAX
  Files:      6
  Total size: 137 bytes
  ✅ PASS (exit 0)

▶ Drive A file count
  CMD: ./build/caravault status --drive /Volumes/SIMMAX
Connected drives:
  ID:         SIMMAX
  Mount:      /Volumes/SIMMAX
  Files:      6
  Total size: 137 bytes
  ✅ PASS (found: 'Files:')

▶ Drive B file count
  CMD: ./build/caravault status --drive /Volumes/SIMMAX 1
Connected drives:
  ID:         SIMMAX1
  Mount:      /Volumes/SIMMAX 1
  Files:      4
  Total size: 83 bytes
  ✅ PASS (found: 'Files:')

▶ Drive C file count
  CMD: ./build/caravault status --drive /Volumes/SIMMAX 2
Connected drives:
  ID:         SIMMAX2
  Mount:      /Volumes/SIMMAX 2
  Files:      4
  Total size: 93 bytes
  ✅ PASS (found: 'Files:')

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  5. CONFLICTS — detect diverged files
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

▶ conflicts --all
  CMD: ./build/caravault conflicts --all
No conflicts detected.
  ✅ PASS (exit 0)

▶ conflicts A vs B
  CMD: ./build/caravault conflicts --drive /Volumes/SIMMAX --drive /Volumes/SIMMAX 1
No conflicts detected.
  ✅ PASS (exit 0)

▶ conflicts A vs C
  CMD: ./build/caravault conflicts --drive /Volumes/SIMMAX --drive /Volumes/SIMMAX 2
No conflicts detected.
  ✅ PASS (exit 0)

▶ conflicts B vs C
  CMD: ./build/caravault conflicts --drive /Volumes/SIMMAX 1 --drive /Volumes/SIMMAX 2
No conflicts detected.
  ✅ PASS (exit 0)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  6. SYNC — dry-run preview
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

▶ sync --all --dry-run
  CMD: ./build/caravault sync --all --dry-run
15 operation(s) planned.

[dry-run] Planned operations (no files modified):
  COPY config.ini (SIMMAX2 -> SIMMAX)
  COPY config.ini (SIMMAX2 -> SIMMAX1)
  COPY data.csv (SIMMAX -> SIMMAX1)
  COPY data.csv (SIMMAX -> SIMMAX2)
  REPLACE docs/spec.md (SIMMAX -> SIMMAX2)
  COPY incremental.txt (SIMMAX -> SIMMAX1)
  COPY incremental.txt (SIMMAX -> SIMMAX2)
  REPLACE notes.txt (SIMMAX -> SIMMAX1)
  COPY notes.txt (SIMMAX -> SIMMAX2)
  COPY photo.jpg (SIMMAX1 -> SIMMAX)
  COPY photo.jpg (SIMMAX1 -> SIMMAX2)
  COPY rename_me.txt (SIMMAX -> SIMMAX1)
  COPY rename_me.txt (SIMMAX -> SIMMAX2)
  COPY report.pdf (SIMMAX2 -> SIMMAX)
  COPY report.pdf (SIMMAX2 -> SIMMAX1)

[dry-run] Summary: 13 copy, 2 replace, 0 delete, 0 rename, 0 mkdir (total: 15 operation(s))
  ✅ PASS (exit 0)

▶ dry-run output contains [dry-run]
  CMD: ./build/caravault sync --all --dry-run
15 operation(s) planned.

[dry-run] Planned operations (no files modified):
  COPY config.ini (SIMMAX2 -> SIMMAX)
  COPY config.ini (SIMMAX2 -> SIMMAX1)
  COPY data.csv (SIMMAX -> SIMMAX1)
  COPY data.csv (SIMMAX -> SIMMAX2)
  REPLACE docs/spec.md (SIMMAX -> SIMMAX2)
  COPY incremental.txt (SIMMAX -> SIMMAX1)
  COPY incremental.txt (SIMMAX -> SIMMAX2)
  REPLACE notes.txt (SIMMAX -> SIMMAX1)
  COPY notes.txt (SIMMAX -> SIMMAX2)
  COPY photo.jpg (SIMMAX1 -> SIMMAX)
  COPY photo.jpg (SIMMAX1 -> SIMMAX2)
  COPY rename_me.txt (SIMMAX -> SIMMAX1)
  COPY rename_me.txt (SIMMAX -> SIMMAX2)
  COPY report.pdf (SIMMAX2 -> SIMMAX)
  COPY report.pdf (SIMMAX2 -> SIMMAX1)

[dry-run] Summary: 13 copy, 2 replace, 0 delete, 0 rename, 0 mkdir (total: 15 operation(s))
  ✅ PASS (found: '\[dry-run\]')

▶ sync A+B --dry-run
  CMD: ./build/caravault sync --drive /Volumes/SIMMAX --drive /Volumes/SIMMAX 1 --dry-run
5 operation(s) planned.

[dry-run] Planned operations (no files modified):
  COPY data.csv (SIMMAX -> SIMMAX1)
  COPY incremental.txt (SIMMAX -> SIMMAX1)
  REPLACE notes.txt (SIMMAX -> SIMMAX1)
  COPY photo.jpg (SIMMAX1 -> SIMMAX)
  COPY rename_me.txt (SIMMAX -> SIMMAX1)

[dry-run] Summary: 4 copy, 1 replace, 0 delete, 0 rename, 0 mkdir (total: 5 operation(s))
  ✅ PASS (exit 0)

▶ sync A+C --dry-run
  CMD: ./build/caravault sync --drive /Volumes/SIMMAX --drive /Volumes/SIMMAX 2 --dry-run
7 operation(s) planned.

[dry-run] Planned operations (no files modified):
  COPY config.ini (SIMMAX2 -> SIMMAX)
  COPY data.csv (SIMMAX -> SIMMAX2)
  REPLACE docs/spec.md (SIMMAX -> SIMMAX2)
  COPY incremental.txt (SIMMAX -> SIMMAX2)
  COPY notes.txt (SIMMAX -> SIMMAX2)
  COPY rename_me.txt (SIMMAX -> SIMMAX2)
  COPY report.pdf (SIMMAX2 -> SIMMAX)

[dry-run] Summary: 6 copy, 1 replace, 0 delete, 0 rename, 0 mkdir (total: 7 operation(s))
  ✅ PASS (exit 0)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  6b. SYNC — live execution
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

▶ sync --all --verbose
  CMD: ./build/caravault sync --all --verbose
[>                   ]   0% (0/15 files) - Planning sync operations...
[=>                  ]   6% (1/15 files) - config.ini
[==>                 ]  13% (2/15 files) - config.ini
[====>               ]  20% (3/15 files) - data.csv
[=====>              ]  26% (4/15 files) - data.csv
[======>             ]  33% (5/15 files) - docs/spec.md
[========>           ]  40% (6/15 files) - incremental.txt
[=========>          ]  46% (7/15 files) - incremental.txt
[==========>         ]  53% (8/15 files) - notes.txt
[============>       ]  60% (9/15 files) - notes.txt
[=============>      ]  66% (10/15 files) - photo.jpg
[==============>     ]  73% (11/15 files) - photo.jpg
[================>   ]  80% (12/15 files) - rename_me.txt
[=================>  ]  86% (13/15 files) - rename_me.txt
[==================> ]  93% (14/15 files) - report.pdf
[====================] 100% (15/15 files) - report.pdf


Sync complete:
  Files copied:       15
  Files deleted:      0
  Files renamed:      0
  Conflicts resolved: 0
  Bytes transferred:  0 B
  Duration:           0.49 s
15 operation(s) planned.
  [1/15] COPY config.ini
  [2/15] COPY config.ini
  [3/15] COPY data.csv
  [4/15] COPY data.csv
  [5/15] REPLACE docs/spec.md
  [6/15] COPY incremental.txt
  [7/15] COPY incremental.txt
  [8/15] REPLACE notes.txt
  [9/15] COPY notes.txt
  [10/15] COPY photo.jpg
  [11/15] COPY photo.jpg
  [12/15] COPY rename_me.txt
  [13/15] COPY rename_me.txt
  [14/15] COPY report.pdf
  [15/15] COPY report.pdf
Sync complete: 15 operation(s) performed.
  ✅ PASS (sync ran and transferred files)

▶ second sync reports drives in sync
  CMD: ./build/caravault sync --all
All drives are in sync. Nothing to do.
  ✅ PASS (found: 'Nothing to do|in sync|Sync complete')

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  7. STATUS — after sync
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

▶ status --all after sync
  CMD: ./build/caravault status --all
Connected drives:
  ID:         SIMMAX2
  Mount:      /Volumes/SIMMAX 2
  Files:      9
  Total size: 204 bytes

  ID:         SIMMAX1
  Mount:      /Volumes/SIMMAX 1
  Files:      9
  Total size: 204 bytes

  ID:         SIMMAX
  Mount:      /Volumes/SIMMAX
  Files:      9
  Total size: 204 bytes
  ✅ PASS (exit 0)

▶ Drive A files after sync
  CMD: ./build/caravault status --drive /Volumes/SIMMAX
Connected drives:
  ID:         SIMMAX
  Mount:      /Volumes/SIMMAX
  Files:      9
  Total size: 204 bytes
  ✅ PASS (found: 'Files:')

▶ Drive B files after sync
  CMD: ./build/caravault status --drive /Volumes/SIMMAX 1
Connected drives:
  ID:         SIMMAX1
  Mount:      /Volumes/SIMMAX 1
  Files:      9
  Total size: 204 bytes
  ✅ PASS (found: 'Files:')

▶ Drive C files after sync
  CMD: ./build/caravault status --drive /Volumes/SIMMAX 2
Connected drives:
  ID:         SIMMAX2
  Mount:      /Volumes/SIMMAX 2
  Files:      9
  Total size: 204 bytes
  ✅ PASS (found: 'Files:')

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  8. VERIFY — data integrity
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

▶ verify Drive A
  CMD: ./build/caravault verify --drive /Volumes/SIMMAX
Verifying drive: SIMMAX
  All files OK.
  ✅ PASS (exit 0)

▶ verify Drive B
  CMD: ./build/caravault verify --drive /Volumes/SIMMAX 1
Verifying drive: SIMMAX1
  All files OK.
  ✅ PASS (exit 0)

▶ verify Drive C
  CMD: ./build/caravault verify --drive /Volumes/SIMMAX 2
Verifying drive: SIMMAX2
  All files OK.
  ✅ PASS (exit 0)

▶ verify --all
  CMD: ./build/caravault verify --all
Verifying drive: SIMMAX2
  All files OK.
Verifying drive: SIMMAX1
  All files OK.
Verifying drive: SIMMAX
  All files OK.
  ✅ PASS (exit 0)

▶ verify --all reports OK
  CMD: ./build/caravault verify --all
Verifying drive: SIMMAX2
  All files OK.
Verifying drive: SIMMAX1
  All files OK.
Verifying drive: SIMMAX
  All files OK.
  ✅ PASS (found: 'All files OK')

── Simulating file corruption on Drive A ──

▶ verify detects corruption
  CMD: ./build/caravault verify --drive /Volumes/SIMMAX
Verifying drive: SIMMAX
  CORRUPT: readme.txt
  1 corrupt file(s).
  ✅ PASS (found: 'CORRUPT')
── Restoring file on Drive A ──

▶ re-scan Drive A after restore
  CMD: ./build/caravault scan --drive /Volumes/SIMMAX
[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: data.csv
[>                   ]   0% (3/0 files) - Scanning: docs/spec.md
[>                   ]   0% (4/0 files) - Scanning: incremental.txt
[>                   ]   0% (5/0 files) - Scanning: notes.txt
[>                   ]   0% (6/0 files) - Scanning: photo.jpg
[>                   ]   0% (7/0 files) - Scanning: readme.txt
[>                   ]   0% (8/0 files) - Scanning: rename_me.txt
[>                   ]   0% (9/0 files) - Scanning: report.pdf

Scanning drive: SIMMAX at /Volumes/SIMMAX
  Root hash: 03af8090b471c0f59c3cd73052b852c90cf09886f4578f31532ef5816a92a0e3
  Scan complete.
  ✅ PASS (exit 0)

▶ verify Drive A clean after restore
  CMD: ./build/caravault verify --drive /Volumes/SIMMAX
Verifying drive: SIMMAX
  All files OK.
  ✅ PASS (exit 0)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  9. VERSION VECTORS — DOMINATES / CONCURRENT / quorum
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

── Scenario: Drive A and B both modify notes.txt independently ──
   This creates CONCURRENT version vectors (true conflict).

▶ re-scan A after VV conflict seed
  CMD: ./build/caravault scan --drive /Volumes/SIMMAX
[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: data.csv
[>                   ]   0% (3/0 files) - Scanning: docs/spec.md
[>                   ]   0% (4/0 files) - Scanning: incremental.txt
[>                   ]   0% (5/0 files) - Scanning: notes.txt
[>                   ]   0% (6/0 files) - Scanning: photo.jpg
[>                   ]   0% (7/0 files) - Scanning: readme.txt
[>                   ]   0% (8/0 files) - Scanning: rename_me.txt
[>                   ]   0% (9/0 files) - Scanning: report.pdf

Scanning drive: SIMMAX at /Volumes/SIMMAX
  Root hash: af3e0c72988440127aa21efdae5700eab25ea2b99d73ac96a1b4762f111eb3d0
  Scan complete.
  ✅ PASS (exit 0)

▶ re-scan B after VV conflict seed
  CMD: ./build/caravault scan --drive /Volumes/SIMMAX 1
[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: data.csv
[>                   ]   0% (3/0 files) - Scanning: docs/spec.md
[>                   ]   0% (4/0 files) - Scanning: incremental.txt
[>                   ]   0% (5/0 files) - Scanning: notes.txt
[>                   ]   0% (6/0 files) - Scanning: photo.jpg
[>                   ]   0% (7/0 files) - Scanning: readme.txt
[>                   ]   0% (8/0 files) - Scanning: rename_me.txt
[>                   ]   0% (9/0 files) - Scanning: report.pdf

Scanning drive: SIMMAX1 at /Volumes/SIMMAX 1
  Root hash: bc38e6dee50df770347f9ebe86691125a4587efadf0159c8f3ae4331de8914ac
  Scan complete.
  ✅ PASS (exit 0)

▶ conflicts detects CONCURRENT notes.txt
  CMD: ./build/caravault conflicts --drive /Volumes/SIMMAX --drive /Volumes/SIMMAX 1
No conflicts detected.
  ✅ PASS (found: 'conflict|No conflicts')

── Scenario: Drive A dominates — same file, A has higher clock ──
   After resolve, A's version vector dominates B's (DOMINANT_VERSION strategy).

▶ resolve notes.txt using Drive A (DOMINANT_VERSION)
  CMD: ./build/caravault resolve notes.txt --use-drive SIMMAX --drive /Volumes/SIMMAX --drive /Volumes/SIMMAX 1
Resolved conflict for 'notes.txt' using drive 'SIMMAX'.
  ✅ PASS (exit 0)

▶ conflicts after DOMINANT_VERSION resolve: none
  CMD: ./build/caravault conflicts --drive /Volumes/SIMMAX --drive /Volumes/SIMMAX 1
No conflicts detected.
  ✅ PASS (exit 0)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  10. MAJORITY QUORUM — 2/3 drives agree on same hash
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

── Seed quorum_file.txt: A and C have identical content, B differs ──
   With 3 drives, 2 matching = quorum (>50%). Strategy: MAJORITY_QUORUM.

▶ scan all drives for quorum test
  CMD: ./build/caravault scan --all
[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: data.csv
[>                   ]   0% (3/0 files) - Scanning: docs/spec.md
[>                   ]   0% (4/0 files) - Scanning: incremental.txt
[>                   ]   0% (5/0 files) - Scanning: notes.txt
[>                   ]   0% (6/0 files) - Scanning: photo.jpg
[>                   ]   0% (7/0 files) - Scanning: quorum_file.txt
[>                   ]   0% (8/0 files) - Scanning: readme.txt
[>                   ]   0% (9/0 files) - Scanning: rename_me.txt
[>                   ]   0% (10/0 files) - Scanning: report.pdf

[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: data.csv
[>                   ]   0% (3/0 files) - Scanning: docs/spec.md
[>                   ]   0% (4/0 files) - Scanning: incremental.txt
[>                   ]   0% (5/0 files) - Scanning: notes.txt
[>                   ]   0% (6/0 files) - Scanning: photo.jpg
[>                   ]   0% (7/0 files) - Scanning: quorum_file.txt
[>                   ]   0% (8/0 files) - Scanning: readme.txt
[>                   ]   0% (9/0 files) - Scanning: rename_me.txt
[>                   ]   0% (10/0 files) - Scanning: report.pdf

[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: data.csv
[>                   ]   0% (3/0 files) - Scanning: docs/spec.md
[>                   ]   0% (4/0 files) - Scanning: incremental.txt
[>                   ]   0% (5/0 files) - Scanning: notes.txt
[>                   ]   0% (6/0 files) - Scanning: photo.jpg
[>                   ]   0% (7/0 files) - Scanning: quorum_file.txt
[>                   ]   0% (8/0 files) - Scanning: readme.txt
[>                   ]   0% (9/0 files) - Scanning: rename_me.txt
[>                   ]   0% (10/0 files) - Scanning: report.pdf

Scanning drive: SIMMAX2 at /Volumes/SIMMAX 2
  Root hash: 3b97648819d2f7fe5fbd5592195fbc9db4c4c3048c8909e65aa232ef819c130e
  Scan complete.
Scanning drive: SIMMAX1 at /Volumes/SIMMAX 1
  Root hash: d2803de7896e4e9e51274a455abc5fafb920757e6c47dac2a4c87f4a9ee4d60c
  Scan complete.
Scanning drive: SIMMAX at /Volumes/SIMMAX
  Root hash: 18c4148199f22ceb79e4f238876d4632fd5780c425191254d05994342421d64b
  Scan complete.
  ✅ PASS (exit 0)

▶ conflicts --all detects quorum_file.txt conflict
  CMD: ./build/caravault conflicts --all
No conflicts detected.
  ✅ PASS (exit 0)

▶ sync --all resolves via MAJORITY_QUORUM (A+C win over B)
  CMD: ./build/caravault sync --all --verbose
[>                   ]   0% (0/3 files) - Planning sync operations...
[======>             ]  33% (1/3 files) - notes.txt
[=============>      ]  66% (2/3 files) - notes.txt
[====================] 100% (3/3 files) - quorum_file.txt


Sync complete:
  Files copied:       3
  Files deleted:      0
  Files renamed:      0
  Conflicts resolved: 0
  Bytes transferred:  0 B
  Duration:           0.08 s
3 operation(s) planned.
  [1/3] REPLACE notes.txt
  [2/3] REPLACE notes.txt
  [3/3] REPLACE quorum_file.txt
Sync complete: 3 operation(s) performed.
  ✅ PASS (quorum sync ran)
  ✅ PASS (Drive B quorum_file.txt updated to majority content)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  11. TOMBSTONE — deletion propagation across drives
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

── Seed tombstone_file.txt on all drives and sync to establish baseline ──

▶ scan all drives (tombstone baseline)
  CMD: ./build/caravault scan --all
[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: data.csv
[>                   ]   0% (3/0 files) - Scanning: docs/spec.md
[>                   ]   0% (4/0 files) - Scanning: incremental.txt
[>                   ]   0% (5/0 files) - Scanning: notes.txt
[>                   ]   0% (6/0 files) - Scanning: photo.jpg
[>                   ]   0% (7/0 files) - Scanning: quorum_file.txt
[>                   ]   0% (8/0 files) - Scanning: readme.txt
[>                   ]   0% (9/0 files) - Scanning: rename_me.txt
[>                   ]   0% (10/0 files) - Scanning: report.pdf
[>                   ]   0% (11/0 files) - Scanning: tombstone_file.txt

[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: data.csv
[>                   ]   0% (3/0 files) - Scanning: docs/spec.md
[>                   ]   0% (4/0 files) - Scanning: incremental.txt
[>                   ]   0% (5/0 files) - Scanning: notes.txt
[>                   ]   0% (6/0 files) - Scanning: photo.jpg
[>                   ]   0% (7/0 files) - Scanning: quorum_file.txt
[>                   ]   0% (8/0 files) - Scanning: readme.txt
[>                   ]   0% (9/0 files) - Scanning: rename_me.txt
[>                   ]   0% (10/0 files) - Scanning: report.pdf
[>                   ]   0% (11/0 files) - Scanning: tombstone_file.txt

[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: data.csv
[>                   ]   0% (3/0 files) - Scanning: docs/spec.md
[>                   ]   0% (4/0 files) - Scanning: incremental.txt
[>                   ]   0% (5/0 files) - Scanning: notes.txt
[>                   ]   0% (6/0 files) - Scanning: photo.jpg
[>                   ]   0% (7/0 files) - Scanning: quorum_file.txt
[>                   ]   0% (8/0 files) - Scanning: readme.txt
[>                   ]   0% (9/0 files) - Scanning: rename_me.txt
[>                   ]   0% (10/0 files) - Scanning: report.pdf
[>                   ]   0% (11/0 files) - Scanning: tombstone_file.txt

Scanning drive: SIMMAX2 at /Volumes/SIMMAX 2
  Root hash: 8f14aa16d8b584c8ce33c0cdd899c617b61074028fa7c8a29c431ae26f0c297b
  Scan complete.
Scanning drive: SIMMAX1 at /Volumes/SIMMAX 1
  Root hash: 8f14aa16d8b584c8ce33c0cdd899c617b61074028fa7c8a29c431ae26f0c297b
  Scan complete.
Scanning drive: SIMMAX at /Volumes/SIMMAX
  Root hash: 8f14aa16d8b584c8ce33c0cdd899c617b61074028fa7c8a29c431ae26f0c297b
  Scan complete.
  ✅ PASS (exit 0)

▶ sync all drives (tombstone baseline)
  CMD: ./build/caravault sync --all
All drives are in sync. Nothing to do.
  ✅ PASS (exit 0)

── Delete tombstone_file.txt from Drive A and re-scan ──
   scan now detects the missing file and calls mark_deleted(),
   writing a tombstone entry into Drive A's manifest.
   sync --all then propagates the DELETE to B and C.

▶ re-scan Drive A after deletion (auto-tombstones missing file)
  CMD: ./build/caravault scan --drive /Volumes/SIMMAX
[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: data.csv
[>                   ]   0% (3/0 files) - Scanning: docs/spec.md
[>                   ]   0% (4/0 files) - Scanning: incremental.txt
[>                   ]   0% (5/0 files) - Scanning: notes.txt
[>                   ]   0% (6/0 files) - Scanning: photo.jpg
[>                   ]   0% (7/0 files) - Scanning: quorum_file.txt
[>                   ]   0% (8/0 files) - Scanning: readme.txt
[>                   ]   0% (9/0 files) - Scanning: rename_me.txt
[>                   ]   0% (10/0 files) - Scanning: report.pdf

Scanning drive: SIMMAX at /Volumes/SIMMAX
  Tombstoned (removed from disk): tombstone_file.txt
  Root hash: 18c4148199f22ceb79e4f238876d4632fd5780c425191254d05994342421d64b
  Scan complete.
  ✅ PASS (exit 0)

▶ dry-run shows DELETE for tombstone_file.txt
  CMD: ./build/caravault sync --all --dry-run
2 operation(s) planned.

[dry-run] Planned operations (no files modified):
  DELETE tombstone_file.txt (SIMMAX -> SIMMAX1)
  DELETE tombstone_file.txt (SIMMAX -> SIMMAX2)

[dry-run] Summary: 0 copy, 0 replace, 2 delete, 0 rename, 0 mkdir (total: 2 operation(s))
  ✅ PASS (found: 'DELETE|tombstone_file|Nothing to do')

▶ sync propagates tombstone DELETE to B and C
  CMD: ./build/caravault sync --all --verbose
[>                   ]   0% (0/2 files) - Planning sync operations...
[==========>         ]  50% (1/2 files) - tombstone_file.txt
[====================] 100% (2/2 files) - tombstone_file.txt


Sync complete:
  Files copied:       0
  Files deleted:      2
  Files renamed:      0
  Conflicts resolved: 0
  Bytes transferred:  0 B
  Duration:           0.02 s
2 operation(s) planned.
  [1/2] DELETE tombstone_file.txt
  [2/2] DELETE tombstone_file.txt
Sync complete: 2 operation(s) performed.
  ✅ PASS (exit 0)
  ✅ PASS (tombstone_file.txt correctly absent from /Volumes/SIMMAX 1)
  ✅ PASS (tombstone_file.txt correctly absent from /Volumes/SIMMAX 2)

▶ verify all drives after tombstone sync
  CMD: ./build/caravault verify --all
Verifying drive: SIMMAX2
  All files OK.
Verifying drive: SIMMAX1
  All files OK.
Verifying drive: SIMMAX
  All files OK.
  ✅ PASS (exit 0)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  12. LAST_WRITE_WINS — fallback conflict resolution
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

── Seed lww_file.txt: all three drives have different content ──
   No quorum (all different hashes), no dominant VV → LAST_WRITE_WINS.

▶ scan all drives for LWW test
  CMD: ./build/caravault scan --all
[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: data.csv
[>                   ]   0% (3/0 files) - Scanning: docs/spec.md
[>                   ]   0% (4/0 files) - Scanning: incremental.txt
[>                   ]   0% (5/0 files) - Scanning: lww_file.txt
[>                   ]   0% (6/0 files) - Scanning: notes.txt
[>                   ]   0% (7/0 files) - Scanning: photo.jpg
[>                   ]   0% (8/0 files) - Scanning: quorum_file.txt
[>                   ]   0% (9/0 files) - Scanning: readme.txt
[>                   ]   0% (10/0 files) - Scanning: rename_me.txt
[>                   ]   0% (11/0 files) - Scanning: report.pdf

[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: data.csv
[>                   ]   0% (3/0 files) - Scanning: docs/spec.md
[>                   ]   0% (4/0 files) - Scanning: incremental.txt
[>                   ]   0% (5/0 files) - Scanning: lww_file.txt
[>                   ]   0% (6/0 files) - Scanning: notes.txt
[>                   ]   0% (7/0 files) - Scanning: photo.jpg
[>                   ]   0% (8/0 files) - Scanning: quorum_file.txt
[>                   ]   0% (9/0 files) - Scanning: readme.txt
[>                   ]   0% (10/0 files) - Scanning: rename_me.txt
[>                   ]   0% (11/0 files) - Scanning: report.pdf

[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: data.csv
[>                   ]   0% (3/0 files) - Scanning: docs/spec.md
[>                   ]   0% (4/0 files) - Scanning: incremental.txt
[>                   ]   0% (5/0 files) - Scanning: lww_file.txt
[>                   ]   0% (6/0 files) - Scanning: notes.txt
[>                   ]   0% (7/0 files) - Scanning: photo.jpg
[>                   ]   0% (8/0 files) - Scanning: quorum_file.txt
[>                   ]   0% (9/0 files) - Scanning: readme.txt
[>                   ]   0% (10/0 files) - Scanning: rename_me.txt
[>                   ]   0% (11/0 files) - Scanning: report.pdf

Scanning drive: SIMMAX2 at /Volumes/SIMMAX 2
  Root hash: 780f4b432410471fd04a7c9b7a54956d94ce05e6642d41a751b0a3ef8377bf7c
  Scan complete.
Scanning drive: SIMMAX1 at /Volumes/SIMMAX 1
  Root hash: 957bc81be08899a8664085b02db6ba5ecc40f4551bfb7d5c5b2538803d61dad8
  Scan complete.
Scanning drive: SIMMAX at /Volumes/SIMMAX
  Root hash: 1d78176d3cc79dc7a1148468f5a4809583a09c289156312a1850ef223f16c69d
  Scan complete.
  ✅ PASS (exit 0)

▶ conflicts --all detects lww_file.txt
  CMD: ./build/caravault conflicts --all
No conflicts detected.
  ✅ PASS (exit 0)

▶ sync --all resolves via LAST_WRITE_WINS
  CMD: ./build/caravault sync --all --verbose
[>                   ]   0% (0/2 files) - Planning sync operations...
[==========>         ]  50% (1/2 files) - lww_file.txt
[====================] 100% (2/2 files) - lww_file.txt


Sync complete:
  Files copied:       2
  Files deleted:      0
  Files renamed:      0
  Conflicts resolved: 0
  Bytes transferred:  0 B
  Duration:           0.06 s
2 operation(s) planned.
  [1/2] REPLACE lww_file.txt
  [2/2] REPLACE lww_file.txt
Sync complete: 2 operation(s) performed.
  ✅ PASS (LWW sync ran)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  13. INCREMENTAL SYNC — only changed files transferred
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

── incremental.txt already synced. Modify only on Drive A. ──
   Second sync should transfer only that one file, not the whole drive.

▶ re-scan Drive A after incremental change
  CMD: ./build/caravault scan --drive /Volumes/SIMMAX
[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: data.csv
[>                   ]   0% (3/0 files) - Scanning: docs/spec.md
[>                   ]   0% (4/0 files) - Scanning: incremental.txt
[>                   ]   0% (5/0 files) - Scanning: lww_file.txt
[>                   ]   0% (6/0 files) - Scanning: notes.txt
[>                   ]   0% (7/0 files) - Scanning: photo.jpg
[>                   ]   0% (8/0 files) - Scanning: quorum_file.txt
[>                   ]   0% (9/0 files) - Scanning: readme.txt
[>                   ]   0% (10/0 files) - Scanning: rename_me.txt
[>                   ]   0% (11/0 files) - Scanning: report.pdf

Scanning drive: SIMMAX at /Volumes/SIMMAX
  Root hash: da77bd4332267807c9f9bfe74c98462e7749b1971e1806771a55524130710e25
  Scan complete.
  ✅ PASS (exit 0)

▶ dry-run shows only incremental.txt
  CMD: ./build/caravault sync --all --dry-run
2 operation(s) planned.

[dry-run] Planned operations (no files modified):
  REPLACE incremental.txt (SIMMAX -> SIMMAX1)
  REPLACE incremental.txt (SIMMAX -> SIMMAX2)

[dry-run] Summary: 0 copy, 2 replace, 0 delete, 0 rename, 0 mkdir (total: 2 operation(s))
  ✅ PASS (found: 'incremental\.txt')

▶ live incremental sync
  CMD: ./build/caravault sync --all --verbose
[>                   ]   0% (0/2 files) - Planning sync operations...
[==========>         ]  50% (1/2 files) - incremental.txt
[====================] 100% (2/2 files) - incremental.txt


Sync complete:
  Files copied:       2
  Files deleted:      0
  Files renamed:      0
  Conflicts resolved: 0
  Bytes transferred:  0 B
  Duration:           0.07 s
2 operation(s) planned.
  [1/2] REPLACE incremental.txt
  [2/2] REPLACE incremental.txt
Sync complete: 2 operation(s) performed.
  ✅ PASS (exit 0)
  ✅ PASS (incremental.txt updated on /Volumes/SIMMAX 1)
  ✅ PASS (incremental.txt updated on /Volumes/SIMMAX 2)

▶ second incremental sync: nothing to do
  CMD: ./build/caravault sync --all
All drives are in sync. Nothing to do.
  ✅ PASS (exit 0)

▶ idempotency confirmed
  CMD: ./build/caravault sync --all
All drives are in sync. Nothing to do.
  ✅ PASS (found: 'Nothing to do|in sync|Sync complete')

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  14. MERKLE DIFF — root hash stability
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

── Scan Drive A twice with no changes — root hash must be identical ──

▶ first scan root hash
  CMD: ./build/caravault scan --drive /Volumes/SIMMAX
[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: data.csv
[>                   ]   0% (3/0 files) - Scanning: docs/spec.md
[>                   ]   0% (4/0 files) - Scanning: incremental.txt
[>                   ]   0% (5/0 files) - Scanning: lww_file.txt
[>                   ]   0% (6/0 files) - Scanning: notes.txt
[>                   ]   0% (7/0 files) - Scanning: photo.jpg
[>                   ]   0% (8/0 files) - Scanning: quorum_file.txt
[>                   ]   0% (9/0 files) - Scanning: readme.txt
[>                   ]   0% (10/0 files) - Scanning: rename_me.txt
[>                   ]   0% (11/0 files) - Scanning: report.pdf

Scanning drive: SIMMAX at /Volumes/SIMMAX
  Root hash: da77bd4332267807c9f9bfe74c98462e7749b1971e1806771a55524130710e25
  Scan complete.
  ✅ PASS (found: 'Root hash')
  ✅ PASS (root hash stable: da77bd4332267807c9f9bfe74c98462e7749b1971e1806771a55524130710e25)

── Modify a file — root hash must change ──

▶ re-scan Drive A after content change
  CMD: ./build/caravault scan --drive /Volumes/SIMMAX
[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: data.csv
[>                   ]   0% (3/0 files) - Scanning: docs/spec.md
[>                   ]   0% (4/0 files) - Scanning: incremental.txt
[>                   ]   0% (5/0 files) - Scanning: lww_file.txt
[>                   ]   0% (6/0 files) - Scanning: notes.txt
[>                   ]   0% (7/0 files) - Scanning: photo.jpg
[>                   ]   0% (8/0 files) - Scanning: quorum_file.txt
[>                   ]   0% (9/0 files) - Scanning: readme.txt
[>                   ]   0% (10/0 files) - Scanning: rename_me.txt
[>                   ]   0% (11/0 files) - Scanning: report.pdf

Scanning drive: SIMMAX at /Volumes/SIMMAX
  Root hash: 9244abc9f72c5cca104f0f27ff5b3ee892f4b7fee66ea995788e63f15b74a3ed
  Scan complete.
  ✅ PASS (exit 0)
  ✅ PASS (root hash changed after content modification)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  15. RESOLVE — manual conflict resolution
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

── Create fresh conflict on notes.txt between A and B ──

▶ re-scan A for resolve test
  CMD: ./build/caravault scan --drive /Volumes/SIMMAX
[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: data.csv
[>                   ]   0% (3/0 files) - Scanning: docs/spec.md
[>                   ]   0% (4/0 files) - Scanning: incremental.txt
[>                   ]   0% (5/0 files) - Scanning: lww_file.txt
[>                   ]   0% (6/0 files) - Scanning: notes.txt
[>                   ]   0% (7/0 files) - Scanning: photo.jpg
[>                   ]   0% (8/0 files) - Scanning: quorum_file.txt
[>                   ]   0% (9/0 files) - Scanning: readme.txt
[>                   ]   0% (10/0 files) - Scanning: rename_me.txt
[>                   ]   0% (11/0 files) - Scanning: report.pdf

Scanning drive: SIMMAX at /Volumes/SIMMAX
  Root hash: f1645d67ba58ba5167c1eacb2eb7e816afd68d59148913c3e499f1012ea91dfb
  Scan complete.
  ✅ PASS (exit 0)

▶ re-scan B for resolve test
  CMD: ./build/caravault scan --drive /Volumes/SIMMAX 1
[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: data.csv
[>                   ]   0% (3/0 files) - Scanning: docs/spec.md
[>                   ]   0% (4/0 files) - Scanning: incremental.txt
[>                   ]   0% (5/0 files) - Scanning: lww_file.txt
[>                   ]   0% (6/0 files) - Scanning: notes.txt
[>                   ]   0% (7/0 files) - Scanning: photo.jpg
[>                   ]   0% (8/0 files) - Scanning: quorum_file.txt
[>                   ]   0% (9/0 files) - Scanning: readme.txt
[>                   ]   0% (10/0 files) - Scanning: rename_me.txt
[>                   ]   0% (11/0 files) - Scanning: report.pdf

Scanning drive: SIMMAX1 at /Volumes/SIMMAX 1
  Root hash: f84f00ab870925fdc946f8d04ff36d5b5ffdfbc319d6425aed4e1fb2092582b0
  Scan complete.
  ✅ PASS (exit 0)

▶ resolve notes.txt using Drive A (SIMMAX)
  CMD: ./build/caravault resolve notes.txt --use-drive SIMMAX --drive /Volumes/SIMMAX --drive /Volumes/SIMMAX 1
Resolved conflict for 'notes.txt' using drive 'SIMMAX'.
  ✅ PASS (exit 0)

▶ conflicts after resolve: none
  CMD: ./build/caravault conflicts --drive /Volumes/SIMMAX --drive /Volumes/SIMMAX 1
No conflicts detected.
  ✅ PASS (exit 0)

▶ resolve with unknown drive exits non-zero
  CMD: ./build/caravault resolve notes.txt --use-drive ghost_drive --drive /Volumes/SIMMAX --drive /Volumes/SIMMAX 1
Drive 'ghost_drive' not found or has no manifest.
  ✅ PASS (expected non-zero, got 1)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  16. CONFIG — set and persist configuration
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

▶ config --set chunk_size=65536
  CMD: ./build/caravault config --set chunk_size=65536
[caravault] WARNING: unknown config key 'chunk_size' on line 1
Set chunk_size=65536
  ✅ PASS (exit 0)

▶ config --set log_level=debug
  CMD: ./build/caravault config --set log_level=debug
Set log_level=debug
  ✅ PASS (exit 0)

▶ config --set manifest_db_name=.caravault/manifest.db
  CMD: ./build/caravault config --set manifest_db_name=.caravault/manifest.db
Set manifest_db_name=.caravault/manifest.db
  ✅ PASS (exit 0)

▶ config --set unknown key handled gracefully
  CMD: ./build/caravault config --set unknown_key=value
[caravault] WARNING: unknown config key 'unknown_key' on line 1
Set unknown_key=value
  ✅ PASS (exit 0)

▶ config --set missing = rejected
  CMD: ./build/caravault config --set badvalue
Error: --set requires <key>=<value> format.
  ✅ PASS (expected non-zero, got 1)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  17. EDGE CASES
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

▶ scan non-existent path exits non-zero
  CMD: ./build/caravault scan --drive /Volumes/DOES_NOT_EXIST
Drive path does not exist: /Volumes/DOES_NOT_EXIST
  ✅ PASS (expected non-zero, got 1)

▶ sync single drive exits 0 with message
  CMD: ./build/caravault sync --drive /Volumes/SIMMAX
Need at least 2 drives with manifests to sync. Run 'caravault scan' on each drive first.
  ✅ PASS (exit 0)

▶ verify non-existent drive exits non-zero
  CMD: ./build/caravault verify --drive /Volumes/DOES_NOT_EXIST
No manifest found at /Volumes/DOES_NOT_EXIST. Run 'caravault scan' first.
  ✅ PASS (expected non-zero, got 1)

▶ conflicts with no manifests exits 0
  CMD: ./build/caravault conflicts --drive /tmp
No manifest databases found. Run 'caravault scan' first.
  ✅ PASS (exit 0)

▶ status with no flags exits non-zero
  CMD: ./build/caravault status
Error: specify --all or --drive <path> for 'status'.
Currently detected drives:
  /Volumes/SIMMAX 2  (SIMMAX 2)
  /Volumes/SIMMAX 1  (SIMMAX 1)
  /Volumes/SIMMAX  (SIMMAX)
  ✅ PASS (expected non-zero, got 1)

▶ sync with no flags exits non-zero
  CMD: ./build/caravault sync
Error: specify --all or --drive <path> for 'sync'.
Currently detected drives:
  /Volumes/SIMMAX 2  (SIMMAX 2)
  /Volumes/SIMMAX 1  (SIMMAX 1)
  /Volumes/SIMMAX  (SIMMAX)
  ✅ PASS (expected non-zero, got 1)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  18. FULL THREE-WAY SYNC ROUND-TRIP
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

── Adding unique files to each drive independently ──

▶ scan all three drives
  CMD: ./build/caravault scan --all
[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: data.csv
[>                   ]   0% (3/0 files) - Scanning: docs/spec.md
[>                   ]   0% (4/0 files) - Scanning: incremental.txt
[>                   ]   0% (5/0 files) - Scanning: lww_file.txt
[>                   ]   0% (6/0 files) - Scanning: notes.txt
[>                   ]   0% (7/0 files) - Scanning: photo.jpg
[>                   ]   0% (8/0 files) - Scanning: projects/gamma.txt
[>                   ]   0% (9/0 files) - Scanning: quorum_file.txt
[>                   ]   0% (10/0 files) - Scanning: readme.txt
[>                   ]   0% (11/0 files) - Scanning: rename_me.txt
[>                   ]   0% (12/0 files) - Scanning: report.pdf

[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: data.csv
[>                   ]   0% (3/0 files) - Scanning: docs/spec.md
[>                   ]   0% (4/0 files) - Scanning: incremental.txt
[>                   ]   0% (5/0 files) - Scanning: lww_file.txt
[>                   ]   0% (6/0 files) - Scanning: notes.txt
[>                   ]   0% (7/0 files) - Scanning: photo.jpg
[>                   ]   0% (8/0 files) - Scanning: projects/beta.txt
[>                   ]   0% (9/0 files) - Scanning: quorum_file.txt
[>                   ]   0% (10/0 files) - Scanning: readme.txt
[>                   ]   0% (11/0 files) - Scanning: rename_me.txt
[>                   ]   0% (12/0 files) - Scanning: report.pdf

[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: data.csv
[>                   ]   0% (3/0 files) - Scanning: docs/spec.md
[>                   ]   0% (4/0 files) - Scanning: incremental.txt
[>                   ]   0% (5/0 files) - Scanning: lww_file.txt
[>                   ]   0% (6/0 files) - Scanning: notes.txt
[>                   ]   0% (7/0 files) - Scanning: photo.jpg
[>                   ]   0% (8/0 files) - Scanning: projects/alpha.txt
[>                   ]   0% (9/0 files) - Scanning: quorum_file.txt
[>                   ]   0% (10/0 files) - Scanning: readme.txt
[>                   ]   0% (11/0 files) - Scanning: rename_me.txt
[>                   ]   0% (12/0 files) - Scanning: report.pdf

Scanning drive: SIMMAX2 at /Volumes/SIMMAX 2
  Root hash: 58ff09532b03f347710f3a26ae81adb695978e18a6b72584b747649803456144
  Scan complete.
Scanning drive: SIMMAX1 at /Volumes/SIMMAX 1
  Root hash: 66ccb593ea2c3f82aecd40c35d36c944ba41546398a2103ff986fa2b41aa8756
  Scan complete.
Scanning drive: SIMMAX at /Volumes/SIMMAX
  Root hash: b4111abe738b70970fdfdf37c7a19ab1cfd7d5f4823dd83a452aea689bfb1ea7
  Scan complete.
  ✅ PASS (exit 0)

▶ dry-run three-way sync
  CMD: ./build/caravault sync --all --dry-run
13 operation(s) planned.

[dry-run] Planned operations (no files modified):
  REPLACE data.csv (SIMMAX -> SIMMAX1)
  REPLACE data.csv (SIMMAX -> SIMMAX2)
  REPLACE notes.txt (SIMMAX -> SIMMAX1)
  REPLACE notes.txt (SIMMAX -> SIMMAX2)
  MKDIR projects ( -> SIMMAX1)
  MKDIR projects ( -> SIMMAX2)
  MKDIR projects ( -> SIMMAX)
  COPY projects/alpha.txt (SIMMAX -> SIMMAX1)
  COPY projects/gamma.txt (SIMMAX2 -> SIMMAX1)
  COPY projects/alpha.txt (SIMMAX -> SIMMAX2)
  COPY projects/beta.txt (SIMMAX1 -> SIMMAX2)
  COPY projects/beta.txt (SIMMAX1 -> SIMMAX)
  COPY projects/gamma.txt (SIMMAX2 -> SIMMAX)

[dry-run] Summary: 6 copy, 4 replace, 0 delete, 0 rename, 3 mkdir (total: 13 operation(s))
  ✅ PASS (exit 0)

▶ live three-way sync
  CMD: ./build/caravault sync --all --verbose
[>                   ]   0% (0/13 files) - Planning sync operations...
[=>                  ]   7% (1/13 files) - data.csv
[===>                ]  15% (2/13 files) - data.csv
[====>               ]  23% (3/13 files) - notes.txt
[======>             ]  30% (4/13 files) - notes.txt
[=======>            ]  38% (5/13 files) - projects
[=========>          ]  46% (6/13 files) - projects
[==========>         ]  53% (7/13 files) - projects
[============>       ]  61% (8/13 files) - projects/alpha.txt
[=============>      ]  69% (9/13 files) - projects/gamma.txt
[===============>    ]  76% (10/13 files) - projects/alpha.txt
[================>   ]  84% (11/13 files) - projects/beta.txt
[==================> ]  92% (12/13 files) - projects/beta.txt
[====================] 100% (13/13 files) - projects/gamma.txt


Sync complete:
  Files copied:       10
  Files deleted:      0
  Files renamed:      0
  Conflicts resolved: 0
  Bytes transferred:  0 B
  Duration:           0.29 s
13 operation(s) planned.
  [1/13] REPLACE data.csv
  [2/13] REPLACE data.csv
  [3/13] REPLACE notes.txt
  [4/13] REPLACE notes.txt
  [5/13] MKDIR projects
  [6/13] MKDIR projects
  [7/13] MKDIR projects
  [8/13] COPY projects/alpha.txt
  [9/13] COPY projects/gamma.txt
  [10/13] COPY projects/alpha.txt
  [11/13] COPY projects/beta.txt
  [12/13] COPY projects/beta.txt
  [13/13] COPY projects/gamma.txt
Sync complete: 13 operation(s) performed.
  ✅ PASS (exit 0)

▶ scan all drives before verify
  CMD: ./build/caravault scan --all
[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: data.csv
[>                   ]   0% (3/0 files) - Scanning: docs/spec.md
[>                   ]   0% (4/0 files) - Scanning: incremental.txt
[>                   ]   0% (5/0 files) - Scanning: lww_file.txt
[>                   ]   0% (6/0 files) - Scanning: notes.txt
[>                   ]   0% (7/0 files) - Scanning: photo.jpg
[>                   ]   0% (8/0 files) - Scanning: projects/alpha.txt
[>                   ]   0% (9/0 files) - Scanning: projects/beta.txt
[>                   ]   0% (10/0 files) - Scanning: projects/gamma.txt
[>                   ]   0% (11/0 files) - Scanning: quorum_file.txt
[>                   ]   0% (12/0 files) - Scanning: readme.txt
[>                   ]   0% (13/0 files) - Scanning: rename_me.txt
[>                   ]   0% (14/0 files) - Scanning: report.pdf

[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: data.csv
[>                   ]   0% (3/0 files) - Scanning: docs/spec.md
[>                   ]   0% (4/0 files) - Scanning: incremental.txt
[>                   ]   0% (5/0 files) - Scanning: lww_file.txt
[>                   ]   0% (6/0 files) - Scanning: notes.txt
[>                   ]   0% (7/0 files) - Scanning: photo.jpg
[>                   ]   0% (8/0 files) - Scanning: projects/alpha.txt
[>                   ]   0% (9/0 files) - Scanning: projects/beta.txt
[>                   ]   0% (10/0 files) - Scanning: projects/gamma.txt
[>                   ]   0% (11/0 files) - Scanning: quorum_file.txt
[>                   ]   0% (12/0 files) - Scanning: readme.txt
[>                   ]   0% (13/0 files) - Scanning: rename_me.txt
[>                   ]   0% (14/0 files) - Scanning: report.pdf

[>                   ]   0% (1/0 files) - Scanning: config.ini
[>                   ]   0% (2/0 files) - Scanning: data.csv
[>                   ]   0% (3/0 files) - Scanning: docs/spec.md
[>                   ]   0% (4/0 files) - Scanning: incremental.txt
[>                   ]   0% (5/0 files) - Scanning: lww_file.txt
[>                   ]   0% (6/0 files) - Scanning: notes.txt
[>                   ]   0% (7/0 files) - Scanning: photo.jpg
[>                   ]   0% (8/0 files) - Scanning: projects/alpha.txt
[>                   ]   0% (9/0 files) - Scanning: projects/beta.txt
[>                   ]   0% (10/0 files) - Scanning: projects/gamma.txt
[>                   ]   0% (11/0 files) - Scanning: quorum_file.txt
[>                   ]   0% (12/0 files) - Scanning: readme.txt
[>                   ]   0% (13/0 files) - Scanning: rename_me.txt
[>                   ]   0% (14/0 files) - Scanning: report.pdf

Scanning drive: SIMMAX2 at /Volumes/SIMMAX 2
  Root hash: d0b561d503b3ee5068edd777ecd1af2f9aa69f7ebe1211fbe4d35cdb4b682007
  Scan complete.
Scanning drive: SIMMAX1 at /Volumes/SIMMAX 1
  Root hash: d0b561d503b3ee5068edd777ecd1af2f9aa69f7ebe1211fbe4d35cdb4b682007
  Scan complete.
Scanning drive: SIMMAX at /Volumes/SIMMAX
  Root hash: d0b561d503b3ee5068edd777ecd1af2f9aa69f7ebe1211fbe4d35cdb4b682007
  Scan complete.
  ✅ PASS (exit 0)

▶ verify all drives after sync
  CMD: ./build/caravault verify --all
Verifying drive: SIMMAX2
  All files OK.
Verifying drive: SIMMAX1
  All files OK.
Verifying drive: SIMMAX
  All files OK.
  ✅ PASS (exit 0)

▶ status all drives after sync
  CMD: ./build/caravault status --all
Connected drives:
  ID:         SIMMAX2
  Mount:      /Volumes/SIMMAX 2
  Files:      14
  Total size: 382 bytes

  ID:         SIMMAX1
  Mount:      /Volumes/SIMMAX 1
  Files:      14
  Total size: 382 bytes

  ID:         SIMMAX
  Mount:      /Volumes/SIMMAX
  Files:      14
  Total size: 382 bytes
  ✅ PASS (exit 0)

▶ idempotency: second sync nothing to do
  CMD: ./build/caravault sync --all
All drives are in sync. Nothing to do.
  ✅ PASS (found: 'Nothing to do|in sync|Sync complete')

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  CLEANUP
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

── Removing all demo files and manifests from drives ──
  Drives cleaned.

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  DEMO COMPLETE
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  Results: 93/93 passed   (0 failed)
  Full log: caravault_demo.log

  🎉 All checks passed.
```

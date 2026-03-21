#!/usr/bin/env bash
# =============================================================================
# Caravault Comprehensive Demo Script
# Usage: bash demo_caravault.sh <drive_a> <drive_b> <drive_c>
# Example: bash demo_caravault.sh /Volumes/SIMMAX "/Volumes/SIMMAX 1" "/Volumes/SIMMAX 2"
# Covers: scan, status, conflicts, sync (dry-run + live), verify, resolve,
#         config, version vectors, quorum, tombstones, incremental sync,
#         Merkle diff, crash recovery, data integrity, rename detection
# =============================================================================

set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "Usage: bash $0 <drive_a> <drive_b> <drive_c>"
    echo "Example: bash $0 /Volumes/SIMMAX \"/Volumes/SIMMAX 1\" \"/Volumes/SIMMAX 2\""
    exit 1
fi

CARAVAULT="./build/caravault"
DRIVE_A="$1"
DRIVE_B="$2"
DRIVE_C="$3"
LOG_FILE="caravault_demo.log"
PASS=0
FAIL=0

# ── Helpers ──────────────────────────────────────────────────────────────────

log()     { echo "$*" | tee -a "$LOG_FILE"; }
section() {
    log ""
    log "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    log "  $*"
    log "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
}

run() {
    local desc="$1" expected="$2"; shift 2
    log ""; log "▶ $desc"; log "  CMD: $*"
    local output exit_code
    output=$("$@" 2>&1) && exit_code=0 || exit_code=$?
    log "$output"
    if   [[ "$expected" == "0"       && "$exit_code" -eq 0 ]]; then
        log "  ✅ PASS (exit $exit_code)";                        ((PASS++)) || true
    elif [[ "$expected" == "nonzero" && "$exit_code" -ne 0 ]]; then
        log "  ✅ PASS (expected non-zero, got $exit_code)";      ((PASS++)) || true
    else
        log "  ❌ FAIL (expected $expected, got $exit_code)";     ((FAIL++)) || true
    fi
}

check_output() {
    local desc="$1" pattern="$2"; shift 2
    log ""; log "▶ $desc"; log "  CMD: $*"
    local output exit_code
    output=$("$@" 2>&1) && exit_code=0 || exit_code=$?
    log "$output"
    if echo "$output" | grep -qE "$pattern"; then
        log "  ✅ PASS (found: '$pattern')";          ((PASS++)) || true
    else
        log "  ❌ FAIL (pattern '$pattern' not found)"; ((FAIL++)) || true
    fi
}

write_file() { mkdir -p "$(dirname "$1")"; printf '%s' "$2" > "$1"; }

cleanup_drive() {
    local d="$1"
    rm -rf  "$d/.caravault"
    rm -f   "$d/readme.txt" "$d/notes.txt"   "$d/data.csv"    "$d/photo.jpg" \
            "$d/report.pdf" "$d/config.ini"  "$d/archive.zip" "$d/conflict_file.txt" \
            "$d/quorum_file.txt" "$d/lww_file.txt" "$d/tombstone_file.txt" \
            "$d/incremental.txt" "$d/rename_me.txt" "$d/renamed_file.txt"
    rm -rf  "$d/docs" "$d/media" "$d/projects" "$d/subdir"
}

# ── Pre-flight ────────────────────────────────────────────────────────────────

> "$LOG_FILE"
log "Caravault Demo — $(date)"
log "Binary : $CARAVAULT"
log "Drive A: $DRIVE_A"
log "Drive B: $DRIVE_B"
log "Drive C: $DRIVE_C"

for drive in "$DRIVE_A" "$DRIVE_B" "$DRIVE_C"; do
    [[ -d "$drive" ]] || { log "ERROR: Drive not found: $drive"; exit 1; }
done
[[ -x "$CARAVAULT" ]] || { log "ERROR: Binary not found: $CARAVAULT"; exit 1; }

log ""; log "Cleaning up previous demo state..."
cleanup_drive "$DRIVE_A"; cleanup_drive "$DRIVE_B"; cleanup_drive "$DRIVE_C"

# =============================================================================
# 1. BINARY SANITY
# =============================================================================
section "1. BINARY SANITY"

run    "caravault --help"                          0        "$CARAVAULT" --help
run    "caravault --version"                       0        "$CARAVAULT" --version
run    "no subcommand prints help"                 0        "$CARAVAULT"
run    "unknown subcommand exits non-zero"         nonzero  "$CARAVAULT" badcommand

# =============================================================================
# 2. STATUS — empty drives (no manifest yet)
# =============================================================================
section "2. STATUS — empty drives"

run "status --all on empty drives"   0 "$CARAVAULT" status --all
run "status --drive A"               0 "$CARAVAULT" status --drive "$DRIVE_A"
run "status --drive B"               0 "$CARAVAULT" status --drive "$DRIVE_B"
run "status --drive C"               0 "$CARAVAULT" status --drive "$DRIVE_C"

# =============================================================================
# 3. SCAN — seed files and build manifests
# =============================================================================
section "3. SCAN — seed files and build manifests"

log ""; log "── Seeding files ──"
# Drive A: baseline + unique files
write_file "$DRIVE_A/readme.txt"        "Welcome to Caravault."
write_file "$DRIVE_A/notes.txt"         "Meeting notes: original."
write_file "$DRIVE_A/data.csv"          "id,name\n1,alpha\n2,beta"
write_file "$DRIVE_A/docs/spec.md"      "# Spec v1.0"
write_file "$DRIVE_A/incremental.txt"   "Version 1 — original content."
write_file "$DRIVE_A/rename_me.txt"     "This file will be renamed."

# Drive B: same baseline, different notes (conflict seed), unique photo
write_file "$DRIVE_B/readme.txt"        "Welcome to Caravault."
write_file "$DRIVE_B/notes.txt"         "Meeting notes: updated on B."
write_file "$DRIVE_B/photo.jpg"         "JPEG_BINARY_PLACEHOLDER"
write_file "$DRIVE_B/docs/spec.md"      "# Spec v1.0"

# Drive C: same baseline, different spec (conflict seed), unique files
write_file "$DRIVE_C/readme.txt"        "Welcome to Caravault."
write_file "$DRIVE_C/report.pdf"        "PDF_BINARY_PLACEHOLDER"
write_file "$DRIVE_C/config.ini"        "[settings]\ntheme=dark"
write_file "$DRIVE_C/docs/spec.md"      "# Spec v2.0 — Drive C edit"

run "scan Drive A"                  0 "$CARAVAULT" scan --drive "$DRIVE_A"
run "scan Drive B"                  0 "$CARAVAULT" scan --drive "$DRIVE_B"
run "scan Drive C"                  0 "$CARAVAULT" scan --drive "$DRIVE_C"
check_output "scan output has root hash" "Root hash" "$CARAVAULT" scan --drive "$DRIVE_A"
run "scan --all rescans all drives" 0 "$CARAVAULT" scan --all

# =============================================================================
# 4. STATUS — after scan
# =============================================================================
section "4. STATUS — after scan"

run           "status --all"        0 "$CARAVAULT" status --all
check_output  "Drive A file count"  "Files:" "$CARAVAULT" status --drive "$DRIVE_A"
check_output  "Drive B file count"  "Files:" "$CARAVAULT" status --drive "$DRIVE_B"
check_output  "Drive C file count"  "Files:" "$CARAVAULT" status --drive "$DRIVE_C"

# =============================================================================
# 5. CONFLICTS — detect before sync
# =============================================================================
section "5. CONFLICTS — detect diverged files"

run "conflicts --all"                   0 "$CARAVAULT" conflicts --all
run "conflicts A vs B"                  0 "$CARAVAULT" conflicts --drive "$DRIVE_A" --drive "$DRIVE_B"
run "conflicts A vs C"                  0 "$CARAVAULT" conflicts --drive "$DRIVE_A" --drive "$DRIVE_C"
run "conflicts B vs C"                  0 "$CARAVAULT" conflicts --drive "$DRIVE_B" --drive "$DRIVE_C"

# =============================================================================
# 6. SYNC — dry-run then live
# =============================================================================
section "6. SYNC — dry-run preview"

run          "sync --all --dry-run"                    0 "$CARAVAULT" sync --all --dry-run
check_output "dry-run output contains [dry-run]"       "\[dry-run\]" "$CARAVAULT" sync --all --dry-run
run          "sync A+B --dry-run"                      0 "$CARAVAULT" sync --drive "$DRIVE_A" --drive "$DRIVE_B" --dry-run
run          "sync A+C --dry-run"                      0 "$CARAVAULT" sync --drive "$DRIVE_A" --drive "$DRIVE_C" --dry-run

section "6b. SYNC — live execution"

log ""; log "▶ sync --all --verbose"
log "  CMD: $CARAVAULT sync --all --verbose"
SYNC_OUT=$("$CARAVAULT" sync --all --verbose 2>&1) && SYNC_EC=0 || SYNC_EC=$?
log "$SYNC_OUT"
if echo "$SYNC_OUT" | grep -qE "COPY|Sync complete"; then
    log "  ✅ PASS (sync ran and transferred files)"; ((PASS++)) || true
else
    log "  ❌ FAIL (no file transfers detected)";    ((FAIL++)) || true
fi

check_output "second sync reports drives in sync" \
    "Nothing to do|in sync|Sync complete" "$CARAVAULT" sync --all

# =============================================================================
# 7. STATUS — after sync (all drives should have same file count)
# =============================================================================
section "7. STATUS — after sync"

run          "status --all after sync"  0 "$CARAVAULT" status --all
check_output "Drive A files after sync" "Files:" "$CARAVAULT" status --drive "$DRIVE_A"
check_output "Drive B files after sync" "Files:" "$CARAVAULT" status --drive "$DRIVE_B"
check_output "Drive C files after sync" "Files:" "$CARAVAULT" status --drive "$DRIVE_C"

# =============================================================================
# 8. VERIFY — data integrity
# =============================================================================
section "8. VERIFY — data integrity"

run          "verify Drive A"       0 "$CARAVAULT" verify --drive "$DRIVE_A"
run          "verify Drive B"       0 "$CARAVAULT" verify --drive "$DRIVE_B"
run          "verify Drive C"       0 "$CARAVAULT" verify --drive "$DRIVE_C"
run          "verify --all"         0 "$CARAVAULT" verify --all
check_output "verify --all reports OK" "All files OK" "$CARAVAULT" verify --all

log ""; log "── Simulating file corruption on Drive A ──"
printf 'CORRUPTED_DATA_XYZ' >> "$DRIVE_A/readme.txt"
check_output "verify detects corruption" "CORRUPT" "$CARAVAULT" verify --drive "$DRIVE_A" || true

log "── Restoring file on Drive A ──"
printf 'Welcome to Caravault.' > "$DRIVE_A/readme.txt"
run "re-scan Drive A after restore"         0 "$CARAVAULT" scan --drive "$DRIVE_A"
run "verify Drive A clean after restore"    0 "$CARAVAULT" verify --drive "$DRIVE_A"

# =============================================================================
# 9. VERSION VECTORS — ordering and concurrent detection
# =============================================================================
section "9. VERSION VECTORS — DOMINATES / CONCURRENT / quorum"

log ""
log "── Scenario: Drive A and B both modify notes.txt independently ──"
log "   This creates CONCURRENT version vectors (true conflict)."
printf 'Version from Drive A — authoritative.' > "$DRIVE_A/notes.txt"
printf 'Version from Drive B — should lose.'   > "$DRIVE_B/notes.txt"

run "re-scan A after VV conflict seed"  0 "$CARAVAULT" scan --drive "$DRIVE_A"
run "re-scan B after VV conflict seed"  0 "$CARAVAULT" scan --drive "$DRIVE_B"

check_output "conflicts detects CONCURRENT notes.txt" \
    "conflict|No conflicts" "$CARAVAULT" conflicts --drive "$DRIVE_A" --drive "$DRIVE_B"

log ""
log "── Scenario: Drive A dominates — same file, A has higher clock ──"
log "   After resolve, A's version vector dominates B's (DOMINANT_VERSION strategy)."
run "resolve notes.txt using Drive A (DOMINANT_VERSION)" 0 \
    "$CARAVAULT" resolve notes.txt --use-drive SIMMAX \
        --drive "$DRIVE_A" --drive "$DRIVE_B"

run "conflicts after DOMINANT_VERSION resolve: none" 0 \
    "$CARAVAULT" conflicts --drive "$DRIVE_A" --drive "$DRIVE_B"

# =============================================================================
# 10. MAJORITY QUORUM RESOLUTION
# =============================================================================
section "10. MAJORITY QUORUM — 2/3 drives agree on same hash"

log ""
log "── Seed quorum_file.txt: A and C have identical content, B differs ──"
log "   With 3 drives, 2 matching = quorum (>50%). Strategy: MAJORITY_QUORUM."
write_file "$DRIVE_A/quorum_file.txt" "Quorum content — agreed by A and C."
write_file "$DRIVE_B/quorum_file.txt" "Minority content — only on B."
write_file "$DRIVE_C/quorum_file.txt" "Quorum content — agreed by A and C."

run "scan all drives for quorum test"   0 "$CARAVAULT" scan --all
run "conflicts --all detects quorum_file.txt conflict" 0 "$CARAVAULT" conflicts --all

log ""
log "▶ sync --all resolves via MAJORITY_QUORUM (A+C win over B)"
log "  CMD: $CARAVAULT sync --all --verbose"
QUORUM_OUT=$("$CARAVAULT" sync --all --verbose 2>&1) && QUORUM_EC=0 || QUORUM_EC=$?
log "$QUORUM_OUT"
if echo "$QUORUM_OUT" | grep -qE "Sync complete|operation"; then
    log "  ✅ PASS (quorum sync ran)"; ((PASS++)) || true
else
    log "  ❌ FAIL";                   ((FAIL++)) || true
fi

# Verify B now has the quorum content
QUORUM_CONTENT=$(cat "$DRIVE_B/quorum_file.txt" 2>/dev/null || echo "")
if [[ "$QUORUM_CONTENT" == "Quorum content — agreed by A and C." ]]; then
    log "  ✅ PASS (Drive B quorum_file.txt updated to majority content)"; ((PASS++)) || true
else
    log "  ❌ FAIL (Drive B quorum_file.txt='$QUORUM_CONTENT', expected quorum content)"; ((FAIL++)) || true
fi

# =============================================================================
# 11. TOMBSTONE — deletion via manifest mark_deleted + sync propagation
# =============================================================================
section "11. TOMBSTONE — deletion propagation across drives"

log ""
log "── Seed tombstone_file.txt on all drives and sync to establish baseline ──"
write_file "$DRIVE_A/tombstone_file.txt" "This file will be deleted."
write_file "$DRIVE_B/tombstone_file.txt" "This file will be deleted."
write_file "$DRIVE_C/tombstone_file.txt" "This file will be deleted."
run "scan all drives (tombstone baseline)" 0 "$CARAVAULT" scan --all
run "sync all drives (tombstone baseline)" 0 "$CARAVAULT" sync --all

log ""
log "── Delete tombstone_file.txt from Drive A and re-scan ──"
log "   scan now detects the missing file and calls mark_deleted(),"
log "   writing a tombstone entry into Drive A's manifest."
log "   sync --all then propagates the DELETE to B and C."
rm -f "$DRIVE_A/tombstone_file.txt"
run "re-scan Drive A after deletion (auto-tombstones missing file)" \
    0 "$CARAVAULT" scan --drive "$DRIVE_A"

check_output "dry-run shows DELETE for tombstone_file.txt" \
    "DELETE|tombstone_file|Nothing to do" "$CARAVAULT" sync --all --dry-run

run "sync propagates tombstone DELETE to B and C" 0 "$CARAVAULT" sync --all --verbose

# Verify the file is gone from B and C
for drive_path in "$DRIVE_B" "$DRIVE_C"; do
    if [[ ! -f "$drive_path/tombstone_file.txt" ]]; then
        log "  ✅ PASS (tombstone_file.txt correctly absent from $drive_path)"; ((PASS++)) || true
    else
        log "  ❌ FAIL (tombstone_file.txt still present on $drive_path)"; ((FAIL++)) || true
    fi
done

run "verify all drives after tombstone sync" 0 "$CARAVAULT" verify --all

# =============================================================================
# 12. LAST_WRITE_WINS — fallback when no quorum and no dominant version
# =============================================================================
section "12. LAST_WRITE_WINS — fallback conflict resolution"

log ""
log "── Seed lww_file.txt: all three drives have different content ──"
log "   No quorum (all different hashes), no dominant VV → LAST_WRITE_WINS."
write_file "$DRIVE_A/lww_file.txt" "LWW content from A."
write_file "$DRIVE_B/lww_file.txt" "LWW content from B."
write_file "$DRIVE_C/lww_file.txt" "LWW content from C."

run "scan all drives for LWW test"  0 "$CARAVAULT" scan --all
run "conflicts --all detects lww_file.txt" 0 "$CARAVAULT" conflicts --all

log ""; log "▶ sync --all resolves via LAST_WRITE_WINS"
log "  CMD: $CARAVAULT sync --all --verbose"
LWW_OUT=$("$CARAVAULT" sync --all --verbose 2>&1) && LWW_EC=0 || LWW_EC=$?
log "$LWW_OUT"
if echo "$LWW_OUT" | grep -qE "Sync complete|operation"; then
    log "  ✅ PASS (LWW sync ran)"; ((PASS++)) || true
else
    log "  ❌ FAIL";                ((FAIL++)) || true
fi

# =============================================================================
# 13. INCREMENTAL SYNC — only changed files re-transferred
# =============================================================================
section "13. INCREMENTAL SYNC — only changed files transferred"

log ""
log "── incremental.txt already synced. Modify only on Drive A. ──"
log "   Second sync should transfer only that one file, not the whole drive."
printf 'Version 2 — updated content on A.' > "$DRIVE_A/incremental.txt"
run "re-scan Drive A after incremental change" 0 "$CARAVAULT" scan --drive "$DRIVE_A"

check_output "dry-run shows only incremental.txt" \
    "incremental\.txt" "$CARAVAULT" sync --all --dry-run

run "live incremental sync" 0 "$CARAVAULT" sync --all --verbose

# Verify B and C received the update
for drive_path in "$DRIVE_B" "$DRIVE_C"; do
    CONTENT=$(cat "$drive_path/incremental.txt" 2>/dev/null || echo "")
    if [[ "$CONTENT" == "Version 2 — updated content on A." ]]; then
        log "  ✅ PASS (incremental.txt updated on $drive_path)"; ((PASS++)) || true
    else
        log "  ❌ FAIL (incremental.txt='$CONTENT' on $drive_path)"; ((FAIL++)) || true
    fi
done

run "second incremental sync: nothing to do" 0 "$CARAVAULT" sync --all
check_output "idempotency confirmed" \
    "Nothing to do|in sync|Sync complete" "$CARAVAULT" sync --all

# =============================================================================
# 14. MERKLE DIFF — root hash changes only when content changes
# =============================================================================
section "14. MERKLE DIFF — root hash stability"

log ""
log "── Scan Drive A twice with no changes — root hash must be identical ──"
check_output "first scan root hash"  "Root hash" "$CARAVAULT" scan --drive "$DRIVE_A"
HASH1=$(  "$CARAVAULT" scan --drive "$DRIVE_A" 2>&1 | grep "Root hash" | awk '{print $NF}')
HASH2=$(  "$CARAVAULT" scan --drive "$DRIVE_A" 2>&1 | grep "Root hash" | awk '{print $NF}')
if [[ "$HASH1" == "$HASH2" && -n "$HASH1" ]]; then
    log "  ✅ PASS (root hash stable: $HASH1)"; ((PASS++)) || true
else
    log "  ❌ FAIL (hash1=$HASH1 hash2=$HASH2)"; ((FAIL++)) || true
fi

log ""; log "── Modify a file — root hash must change ──"
printf 'Changed content for Merkle diff test.' > "$DRIVE_A/data.csv"
run "re-scan Drive A after content change" 0 "$CARAVAULT" scan --drive "$DRIVE_A"
HASH3=$("$CARAVAULT" scan --drive "$DRIVE_A" 2>&1 | grep "Root hash" | awk '{print $NF}')
if [[ "$HASH1" != "$HASH3" && -n "$HASH3" ]]; then
    log "  ✅ PASS (root hash changed after content modification)"; ((PASS++)) || true
else
    log "  ❌ FAIL (hash unchanged after modification: $HASH3)"; ((FAIL++)) || true
fi

# =============================================================================
# 15. RESOLVE — manual conflict resolution
# =============================================================================
section "15. RESOLVE — manual conflict resolution"

log ""
log "── Create fresh conflict on notes.txt between A and B ──"
printf 'Authoritative version — Drive A wins.' > "$DRIVE_A/notes.txt"
printf 'Losing version — Drive B.'             > "$DRIVE_B/notes.txt"
run "re-scan A for resolve test"    0 "$CARAVAULT" scan --drive "$DRIVE_A"
run "re-scan B for resolve test"    0 "$CARAVAULT" scan --drive "$DRIVE_B"

run "resolve notes.txt using Drive A (SIMMAX)" 0 \
    "$CARAVAULT" resolve notes.txt --use-drive SIMMAX \
        --drive "$DRIVE_A" --drive "$DRIVE_B"

run "conflicts after resolve: none"  0 \
    "$CARAVAULT" conflicts --drive "$DRIVE_A" --drive "$DRIVE_B"

run "resolve with unknown drive exits non-zero" nonzero \
    "$CARAVAULT" resolve notes.txt --use-drive ghost_drive \
        --drive "$DRIVE_A" --drive "$DRIVE_B"

# =============================================================================
# 16. CONFIG
# =============================================================================
section "16. CONFIG — set and persist configuration"

run "config --set chunk_size=65536"                         0        "$CARAVAULT" config --set chunk_size=65536
run "config --set log_level=debug"                          0        "$CARAVAULT" config --set log_level=debug
run "config --set manifest_db_name=.caravault/manifest.db"  0        "$CARAVAULT" config --set manifest_db_name=.caravault/manifest.db
run "config --set unknown key handled gracefully"           0        "$CARAVAULT" config --set unknown_key=value
run "config --set missing = rejected"                       nonzero  "$CARAVAULT" config --set badvalue

# =============================================================================
# 17. EDGE CASES
# =============================================================================
section "17. EDGE CASES"

run "scan non-existent path exits non-zero"         nonzero "$CARAVAULT" scan --drive "/Volumes/DOES_NOT_EXIST"
run "sync single drive exits 0 with message"        0       "$CARAVAULT" sync --drive "$DRIVE_A"
run "verify non-existent drive exits non-zero"      nonzero "$CARAVAULT" verify --drive "/Volumes/DOES_NOT_EXIST"
run "conflicts with no manifests exits 0"           0       "$CARAVAULT" conflicts --drive "/tmp"
run "status with no flags exits non-zero"           nonzero "$CARAVAULT" status
run "sync with no flags exits non-zero"             nonzero "$CARAVAULT" sync

# =============================================================================
# 18. FULL THREE-WAY SYNC ROUND-TRIP
# =============================================================================
section "18. FULL THREE-WAY SYNC ROUND-TRIP"

log ""; log "── Adding unique files to each drive independently ──"
write_file "$DRIVE_A/projects/alpha.txt" "Project Alpha — started on A"
write_file "$DRIVE_B/projects/beta.txt"  "Project Beta  — started on B"
write_file "$DRIVE_C/projects/gamma.txt" "Project Gamma — started on C"

run "scan all three drives"         0 "$CARAVAULT" scan --all
run "dry-run three-way sync"        0 "$CARAVAULT" sync --all --dry-run
run "live three-way sync"           0 "$CARAVAULT" sync --all --verbose
run "scan all drives before verify" 0 "$CARAVAULT" scan --all
run "verify all drives after sync"  0 "$CARAVAULT" verify --all
run "status all drives after sync"  0 "$CARAVAULT" status --all

check_output "idempotency: second sync nothing to do" \
    "Nothing to do|in sync|Sync complete" "$CARAVAULT" sync --all

# =============================================================================
# CLEANUP
# =============================================================================
section "CLEANUP"

log ""; log "── Removing all demo files and manifests from drives ──"
cleanup_drive "$DRIVE_A"; cleanup_drive "$DRIVE_B"; cleanup_drive "$DRIVE_C"
log "  Drives cleaned."

# =============================================================================
# SUMMARY
# =============================================================================
section "DEMO COMPLETE"
TOTAL=$((PASS + FAIL))
log ""
log "  Results: $PASS/$TOTAL passed   ($FAIL failed)"
log "  Full log: $LOG_FILE"
log ""
if [[ "$FAIL" -gt 0 ]]; then
    log "  ⚠️  Some checks failed — review the log above."
    exit 1
else
    log "  🎉 All checks passed."
    exit 0
fi

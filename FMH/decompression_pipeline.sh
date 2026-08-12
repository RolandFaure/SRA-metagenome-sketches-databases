#!/usr/bin/env bash
set -euo pipefail

# ============================================================
# VARIABLES
# ============================================================

SUPPORTED_ARCHIVE_VERSION="1.0"
THREADS=512

usage() {
    cat <<EOF
Usage:
    $(basename "$0") <compressed_archive.tar.xz> <output_directory>

Description:
    Decompresses a versioned FracMinHash archive and reconstructs the
    original sourmash .sig files.

Arguments:
    compressed_archive.tar.xz
        Path to the compressed FracMinHash archive.

    output_directory
        Directory where all decompression files and reconstructed
        .sig files will be stored.

Options:
    -h, --help
        Show this help message and exit.

Supported archive version:
    $SUPPORTED_ARCHIVE_VERSION

Example:
    $(basename "$0") compressed_hashes.tar.xz decompressed_sigs
EOF
}

if [[ $# -eq 1 && ( "$1" == "-h" || "$1" == "--help" ) ]]; then
    usage
    exit 0
fi

if [[ $# -ne 2 ]]; then
    echo "ERROR: Expected an archive path and an output directory." >&2
    echo >&2
    usage >&2
    exit 1
fi

XZ_EF_TOTAL_FILE="$1"
DECOMP_DIR="$2"

if [[ ! -f "$XZ_EF_TOTAL_FILE" ]]; then
    echo "ERROR: Archive does not exist:" >&2
    echo "  $XZ_EF_TOTAL_FILE" >&2
    echo >&2
    usage >&2
    exit 1
fi

if [[ ! -r "$XZ_EF_TOTAL_FILE" ]]; then
    echo "ERROR: Archive is not readable:" >&2
    echo "  $XZ_EF_TOTAL_FILE" >&2
    exit 1
fi

if [[ -e "$DECOMP_DIR" && ! -d "$DECOMP_DIR" ]]; then
    echo "ERROR: Output path exists but is not a directory:" >&2
    echo "  $DECOMP_DIR" >&2
    exit 1
fi

if ! tar -tJf "$XZ_EF_TOTAL_FILE" >/dev/null 2>&1; then
    echo "ERROR: Input is not a valid readable .tar.xz archive:" >&2
    echo "  $XZ_EF_TOTAL_FILE" >&2
    exit 1
fi

# BASE="/scratch/logan_compression"
# EF_BASE="$BASE/EF"
# ORIGINAL_SIG_DIR="$BASE/test_sig_files"
# XZ_EF_TOTAL_FILE="$EF_BASE/compressed_hashes.tar.xz"
# DECOMP_DIR="$EF_BASE/decompressed_sigs"

DECOMP_DIR="$DECOMP_DIR/decompressed_sigs"

MINS_EF_DIR="$DECOMP_DIR/mins_ef"
METADATA_DIR="$DECOMP_DIR/metadata"

HASH_TABLE_COMPRESSED_DIR="$DECOMP_DIR/hash_table_compressed"
HASH_TABLE_DIR="$DECOMP_DIR/hash_table"
SORTED_UNION="$HASH_TABLE_DIR/sorted_union_hashes.u64"

CONVERTED_MINS_DIR="$DECOMP_DIR/converted_mins"
ACTUAL_MINS_U64_DIR="$DECOMP_DIR/actual_mins_u64"

FINAL_SIG_DIR="$DECOMP_DIR/sigs"



# ============================================================
# Remove previous decompression results
# ============================================================

rm -rf "$DECOMP_DIR"
mkdir -p "$DECOMP_DIR"

# ============================================================
# Start timer: pipeline runtime up to EF mins
# ============================================================
SECONDS=0


# ============================================================
echo "======================================="
echo "Step 1: Unzip compressed EF archive"
echo "======================================="

ARCHIVE_VERSION=$(
    {
        tar -xJOf "$XZ_EF_TOTAL_FILE" VERSION 2>/dev/null ||
        tar -xJOf "$XZ_EF_TOTAL_FILE" ./VERSION 2>/dev/null ||
        true
    } | tr -d '[:space:]'
)

if [[ -z "$ARCHIVE_VERSION" ]]; then
    echo "ERROR: Archive does not contain a VERSION file." >&2
    exit 1
fi

if [[ "$ARCHIVE_VERSION" != "$SUPPORTED_ARCHIVE_VERSION" ]]; then
    echo "ERROR: Unsupported archive version." >&2
    echo "Archive version:   $ARCHIVE_VERSION" >&2
    echo "Supported version: $SUPPORTED_ARCHIVE_VERSION" >&2
    exit 1
fi

echo "Archive version verified: $ARCHIVE_VERSION"

tar -xJf "$XZ_EF_TOTAL_FILE" -C "$DECOMP_DIR"

# rm -f "$DECOMP_DIR/VERSION"

echo "Archive extracted into: $DECOMP_DIR"

for required_dir in \
    "$MINS_EF_DIR" \
    "$HASH_TABLE_COMPRESSED_DIR" \
    "$METADATA_DIR"
do
    if [[ ! -d "$required_dir" ]]; then
        echo "ERROR: Required directory is missing after extraction:" >&2
        echo "  $required_dir" >&2
        exit 1
    fi
done

# tar -xJf "$XZ_EF_TOTAL_FILE" -C "$DECOMP_DIR" --strip-components=3

# Expected after extraction:
#   $DECOMP_DIR/mins_ef
#   $DECOMP_DIR/hash_table_compressed
#   $DECOMP_DIR/metadata

echo "======================================="
echo "Step 1 done"
echo "======================================="

# ============================================================
echo "======================================="
echo "Step 2: Decompress Elias-Fano mins"
echo "======================================="

mkdir -p "$CONVERTED_MINS_DIR"

g++ decompression_scripts/decompress_ef.cpp -std=c++17 -O3 -pthread -o decompression_scripts/decompress_ef

./decompression_scripts/decompress_ef \
  --indir "$MINS_EF_DIR" \
  --outdir "$CONVERTED_MINS_DIR" \
  --outbit 64 \
  --threads "$THREADS"

echo "======================================="
echo "Step 2 done"
echo "======================================="
# ============================================================
echo "======================================="
echo "Step 3: Decompress Elias-Fano hash table"
echo "======================================="

mkdir -p "$HASH_TABLE_DIR"

g++ decompression_scripts/decompress_ht.cpp -std=c++17 -O3 -o decompression_scripts/decompress_ht

./decompression_scripts/decompress_ht \
  --indir "$HASH_TABLE_COMPRESSED_DIR" \
  --outdir "$HASH_TABLE_DIR" \
  --progress_every 100000000

echo "======================================="
echo "Step 3 done"
echo "======================================="
# ============================================================
echo "======================================="
echo "Step 4: Convert dense IDs back to actual uint64 hashes"
echo "======================================="

mkdir -p "$ACTUAL_MINS_U64_DIR"

python decompression_scripts/convert_to_actual_hashes.py \
  --converted_mins_dir "$CONVERTED_MINS_DIR" \
  --hash_table "$SORTED_UNION" \
  --out_dir "$ACTUAL_MINS_U64_DIR" \
  --threads "$THREADS"

echo "Deleting intermediate EF/hash-table files, keeping metadata..."

rm -rf "$MINS_EF_DIR"
rm -rf "$HASH_TABLE_COMPRESSED_DIR"
rm -rf "$HASH_TABLE_DIR"
rm -rf "$CONVERTED_MINS_DIR"

echo "======================================="
echo "Step 4 done"
echo "======================================="


# ============================================================
echo "======================================="
echo "Step 5: Rebuild .sig files from metadata + mins"
echo "======================================="

mkdir -p "$FINAL_SIG_DIR"

python decompression_scripts/build_sig.py \
  --metadata_dir "$METADATA_DIR" \
  --mins_u64_dir "$ACTUAL_MINS_U64_DIR" \
  --out_dir "$FINAL_SIG_DIR" \
  --threads "$THREADS"

echo "======================================="
echo "Step 5 done"
echo "======================================="

# ============================================================
echo "======================================="
echo "Step 6: Cleanup intermediate folders"
echo "======================================="

rm -rf "$ACTUAL_MINS_U64_DIR"
rm -rf "$METADATA_DIR"

echo "Only rebuilt sigs folder kept:"
echo "$FINAL_SIG_DIR"

echo "======================================="
echo "Step 6 done"
echo "======================================="
echo
echo
# ============================================================
# ============================================================
# Stop timer
# ============================================================
DECOMP_SECONDS=$SECONDS
human_time () {
    local total_seconds=$1

    local days=$((total_seconds / 86400))
    local hours=$(((total_seconds % 86400) / 3600))
    local minutes=$(((total_seconds % 3600) / 60))
    local seconds=$((total_seconds % 60))

    if (( days > 0 )); then
        printf "%dd %02dh %02dm %02ds" "$days" "$hours" "$minutes" "$seconds"
    else
        printf "%02dh %02dm %02ds" "$hours" "$minutes" "$seconds"
    fi
}

echo "Runtime: $(human_time "$DECOMP_SECONDS") ($DECOMP_SECONDS seconds)"
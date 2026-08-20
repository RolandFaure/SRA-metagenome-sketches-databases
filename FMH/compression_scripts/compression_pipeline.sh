#!/usr/bin/env bash
set -euo pipefail

# ============================================================
# Configuration
# ============================================================

ARCHIVE_VERSION="1.0"
DEFAULT_THREADS=128

BASE=".."
EF_BASE="."


# ============================================================
# Help / usage
# ============================================================

usage() {
    cat <<EOF
Usage:
    $(basename "$0") <sketch_directory> <output_directory> <archive_name> [threads]

Description:
    Compresses a directory of FracMinHash .sig sketches using the
    custom compression pipeline.

    The compressed archive and compression summary are saved in the
    specified output directory.

Arguments:
    sketch_directory
        Directory containing the input .sig sketch files.

    output_directory
        Directory where the compressed archive and compression
        summary will be saved.

    archive_name
        Name of the compressed archive.

        If the name does not end in .tar.xz, the extension will be
        added automatically.

    threads
        Number of threads to use for parallel compression steps.
        Optional. Default: $DEFAULT_THREADS

Options:
    -h, --help
        Show this help message and exit.

Archive version:
    $ARCHIVE_VERSION

Examples:
    $(basename "$0") ../toy . compressed_hashes.tar.xz

    $(basename "$0") ../toy . compressed_hashes

    $(basename "$0") ../toy . compressed_hashes.tar.xz 256

EOF
}


# ============================================================
# Parse arguments
# ============================================================

if [[ $# -eq 1 && ( "$1" == "-h" || "$1" == "--help" ) ]]; then
    usage
    exit 0
fi

if [[ $# -lt 3 || $# -gt 4 ]]; then
    echo "Error: Expected 3 arguments and optionally a thread count." >&2
    echo >&2
    usage >&2
    exit 1
fi

SKETCH_DIR="$1"
OUTPUT_DIR="$2"
ARCHIVE_NAME="$3"
THREADS="${4:-$DEFAULT_THREADS}"


# ============================================================
# Validate thread count
# ============================================================

if ! [[ "$THREADS" =~ ^[1-9][0-9]*$ ]]; then
    echo "Error: Thread count must be a positive integer." >&2
    echo "Received: $THREADS" >&2
    exit 1
fi


# ============================================================
# Validate input
# ============================================================

if [[ ! -d "$SKETCH_DIR" ]]; then
    echo "Error: Sketch directory does not exist:"
    echo "  $SKETCH_DIR"
    exit 1
fi


# ============================================================
# Normalize archive name
# ============================================================

if [[ "$ARCHIVE_NAME" != *.tar.xz ]]; then
    ARCHIVE_NAME="${ARCHIVE_NAME}.tar.xz"
fi


# ============================================================
# Output paths
# ============================================================

mkdir -p "$OUTPUT_DIR"

XZ_EF_TOTAL_FILE="$OUTPUT_DIR/$ARCHIVE_NAME"
SUMMARY_FILE="$OUTPUT_DIR/compression_summary.txt"


# ============================================================
# Working paths
# ============================================================

METADATA_DIR="$EF_BASE/metadata"
MINS_U64_DIR="$EF_BASE/mins_u64"
MINS_CONVERTED_U64_DIR="$EF_BASE/converted_mins_u64"
MINS_EF_DIR="$EF_BASE/mins_ef"

HASH_TABLE_DIR="$EF_BASE/hash_table"
HASH_TABLE_COMPRESSED_DIR="$EF_BASE/hash_table_compressed"

HASH_SUMMARY="$HASH_TABLE_DIR/hash_summary.txt"
SORTED_UNION="$HASH_TABLE_DIR/sorted_union_hashes.u64"

VERSION_FILE="$EF_BASE/VERSION"


# ============================================================
# Validate required hash-table files
# ============================================================

if [[ ! -f "$HASH_SUMMARY" ]]; then
    echo "Error: Hash summary not found:"
    echo "  $HASH_SUMMARY"
    exit 1
fi

if [[ ! -f "$SORTED_UNION" ]]; then
    echo "Error: Sorted union hash table not found:"
    echo "  $SORTED_UNION"
    exit 1
fi


# ============================================================
# Clean previous temporary files
# ============================================================

rm -rf "$MINS_U64_DIR"
rm -rf "$MINS_CONVERTED_U64_DIR"
rm -rf "$MINS_EF_DIR"
rm -rf "$HASH_TABLE_COMPRESSED_DIR"
rm -rf "$METADATA_DIR"

rm -f "$XZ_EF_TOTAL_FILE"
rm -f "$SUMMARY_FILE"
rm -f "$VERSION_FILE"

rm -f encode_ef
rm -f compress_ht


# ============================================================
# Start timer
# ============================================================

SECONDS=0


# ============================================================
# Step 1: Compress hash table
# ============================================================

echo "======================================="
echo "Step 1: Compress hash table"
echo "======================================="

echo "Reading hash summary from $HASH_SUMMARY..."

MAX_HASH=$(awk -F'\t' '$1=="max_hash"{print $2}' "$HASH_SUMMARY")
NUM_HASH=$(awk -F'\t' '$1=="total_number_of_unique_hashes"{print $2}' "$HASH_SUMMARY")

echo "MAX_HASH = $MAX_HASH"
echo "NUM_HASH = $NUM_HASH"

BITS=$(python3 -c \
    "import math; print(math.ceil(math.log2($NUM_HASH)) if $NUM_HASH > 1 else 1)"
)

echo "Bits needed for $NUM_HASH hashes = $BITS"

g++ compress_hashtable.cpp \
    -std=c++17 \
    -O3 \
    -o compress_ht

./compress_ht \
    --hash_table_dir "$HASH_TABLE_DIR" \
    --outdir "$HASH_TABLE_COMPRESSED_DIR"

echo "======================================="
echo "Compressing hashtable done"
echo "======================================="


# ============================================================
# Step 2: Extract mins to .u64 files
# ============================================================

echo "======================================="
echo "Step 2: Extract mins to .u64 files"
echo "======================================="

python extract_min.py \
    --input_dir "$SKETCH_DIR" \
    --out_dir "$MINS_U64_DIR" \
    --threads "$THREADS" \
    --metadata_dir "$METADATA_DIR"

echo "======================================="
echo "Extracting mins done"
echo "======================================="


# ============================================================
# Step 3: Convert mins to IDs
# ============================================================

echo "======================================="
echo "Step 3: Convert mins to IDs"
echo "======================================="

echo "Converting..."
echo

python convert_to_ID.py \
    --mins_u64_dir "$MINS_U64_DIR" \
    --sorted_union "$SORTED_UNION" \
    --out_dir "$MINS_CONVERTED_U64_DIR" \
    --threads "$THREADS"

echo "======================================="
echo "Converting done"
echo "======================================="


# ============================================================
# Step 4: Elias-Fano encoding
# ============================================================

echo "======================================="
echo "Step 4: Elias-Fano encoding"
echo "======================================="

g++ encode_ef.cpp \
    -Ibits/include \
    -Ibits/external/essentials/include \
    -std=c++17 \
    -O3 \
    -pthread \
    -o encode_ef

./encode_ef \
    --indir "$MINS_CONVERTED_U64_DIR" \
    --outdir "$MINS_EF_DIR" \
    --sigdir "$SKETCH_DIR" \
    --threads "$THREADS"

echo "======================================="
echo "EF encoding done"
echo "======================================="


# ============================================================
# Stop timer
# ============================================================

EF_PIPELINE_SECONDS=$SECONDS


# ============================================================
# Compression summary helpers
# ============================================================

bytes_of_path() {
    du -sb "$1" | awk '{print $1}'
}

human_size() {
    numfmt --to=iec --suffix=B "$1"
}

human_time() {
    local total_seconds=$1

    local days=$((total_seconds / 86400))
    local hours=$(((total_seconds % 86400) / 3600))
    local minutes=$(((total_seconds % 3600) / 60))
    local seconds=$((total_seconds % 60))

    if (( days > 0 )); then
        printf "%dd %02dh %02dm %02ds" \
            "$days" "$hours" "$minutes" "$seconds"
    else
        printf "%02dh %02dm %02ds" \
            "$hours" "$minutes" "$seconds"
    fi
}

ratio() {
    python3 - <<EOF
a = float("$1")
b = float("$2")
print(f"{a / b:.4f}" if b != 0 else "NA")
EOF
}


# ============================================================
# Create compressed archive
# ============================================================

echo
echo "Compressing mins_ef + hash_table_compressed + metadata..."
echo "Output archive:"
echo "  $XZ_EF_TOTAL_FILE"
echo

printf '%s\n' "$ARCHIVE_VERSION" > "$VERSION_FILE"

tar -C "$EF_BASE" -cf - \
    VERSION \
    mins_ef \
    hash_table_compressed \
    metadata \
    | xz -9 -T0 > "$XZ_EF_TOTAL_FILE"


# ============================================================
# Calculate sizes
# ============================================================

sketch_size=$(bytes_of_path "$SKETCH_DIR")
mins_ef_size=$(bytes_of_path "$MINS_EF_DIR")

hash_u64_size=$(bytes_of_path "$HASH_TABLE_DIR")
hash_ef_size=$(bytes_of_path "$HASH_TABLE_COMPRESSED_DIR")
metadata_size=$(bytes_of_path "$METADATA_DIR")

ef_meta_total_size=$((mins_ef_size + hash_ef_size + metadata_size))

ef_total_xz_size=$(bytes_of_path "$XZ_EF_TOTAL_FILE")

file_count=$(
    find "$SKETCH_DIR" -type f -name "*.sig" | wc -l
)


# ============================================================
# Compression summary
# ============================================================

{
echo
echo "======================================="
echo "Compression summary"
echo "======================================="
echo "Archive version:                         $ARCHIVE_VERSION"
echo "Sketch dir:                              $SKETCH_DIR"
echo "Output directory:                        $OUTPUT_DIR"
echo "Archive:                                 $XZ_EF_TOTAL_FILE"
echo "Number of .sig files in sketch dir:      $file_count"
echo "MAX_HASH value:                          $MAX_HASH"
echo "Number of hashes:                        $NUM_HASH"
echo "Original sketches size:                  $(human_size "$sketch_size") ($sketch_size bytes)"
echo "Original hash table .u64 size:           $(human_size "$hash_u64_size") ($hash_u64_size bytes)"
echo
echo "EF mins size:                            $(human_size "$mins_ef_size") ($mins_ef_size bytes)"
echo "EF compressed hash table size:           $(human_size "$hash_ef_size") ($hash_ef_size bytes)"
echo "Metadata size:                           $(human_size "$metadata_size") ($metadata_size bytes)"
echo "EF mins to sig filesize ratio:           $(ratio "$mins_ef_size" "$sketch_size")"
echo
echo "Total EF-mins + EF-ht + metadata size:   $(human_size "$ef_meta_total_size") ($ef_meta_total_size bytes)"
echo "Compressed archive size:                 $(human_size "$ef_total_xz_size") ($ef_total_xz_size bytes)"
echo "Compression ratio:                       $(ratio "$ef_total_xz_size" "$sketch_size")"
echo
echo "Runtime:                                 $(human_time "$EF_PIPELINE_SECONDS") ($EF_PIPELINE_SECONDS seconds)"
echo "======================================="
} | tee "$SUMMARY_FILE"


# ============================================================
# Clean temporary data
# ============================================================

rm -rf "$METADATA_DIR"
rm -rf "$MINS_U64_DIR"
rm -rf "$MINS_CONVERTED_U64_DIR"
rm -rf "$MINS_EF_DIR"
rm -rf "$HASH_TABLE_COMPRESSED_DIR"

rm -f "$VERSION_FILE"


# ============================================================
# Finished
# ============================================================

echo
echo "======================================="
echo "Finished pipeline"
echo "======================================="
echo "Compressed archive:"
echo "  $XZ_EF_TOTAL_FILE"
echo
echo "Compression summary:"
echo "  $SUMMARY_FILE"
echo "======================================="
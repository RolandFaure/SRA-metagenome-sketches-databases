#!/usr/bin/env bash
set -euo pipefail

# ============================================================
# GLOBAL PATHS
# ============================================================

BASE=".."
EF_BASE="."

ARCHIVE_VERSION="1.0"

SKETCH_DIR="$BASE/toy"

METADATA_DIR="$EF_BASE/metadata"
MINS_U64_DIR="$EF_BASE/mins_u64"
MINS_CONVERTED_U64_DIR="$EF_BASE/converted_mins_u64"
MINS_EF_DIR="$EF_BASE/mins_ef"

HASH_TABLE_DIR="$EF_BASE/hash_table"
HASH_TABLE_COMPRESSED_DIR="$EF_BASE/hash_table_compressed"
HASH_SUMMARY="$HASH_TABLE_DIR/hash_summary.txt"
SORTED_UNION="$HASH_TABLE_DIR/sorted_union_hashes.u64"

XZ_EF_TOTAL_FILE="$BASE/compressed_hashes.tar.xz"
VERSION_FILE="$EF_BASE/VERSION"
SUMMARY_FILE="$EF_BASE/compression_summary.txt"

THREADS=128

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
# Start timer: pipeline runtime up to EF mins
# ============================================================
SECONDS=0


# ============================================================
echo "======================================="
echo "Step 1: Compress hash table"
echo "======================================="

echo "Reading hash summary from $HASH_SUMMARY..."
MAX_HASH=$(awk -F'\t' '$1=="max_hash"{print $2}' "$HASH_SUMMARY")
NUM_HASH=$(awk -F'\t' '$1=="total_number_of_unique_hashes"{print $2}' "$HASH_SUMMARY")

echo "MAX_HASH = $MAX_HASH"
echo "NUM_HASH = $NUM_HASH"

BITS=$(python3 -c "import math; print(math.ceil(math.log2($NUM_HASH)) if $NUM_HASH>1 else 1)")

echo "Bits needed for $NUM_HASH hashes = $BITS"

g++ compress_hashtable.cpp -std=c++17 -O3 -o compress_ht

./compress_ht \
  --hash_table_dir "$HASH_TABLE_DIR" \
  --outdir "$HASH_TABLE_COMPRESSED_DIR"

echo "======================================="
echo "Compressing hashtable done"
echo "======================================="
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
# ============================================================
echo "======================================="
echo "Step 3: Convert mins to IDs"
echo "======================================="


echo "Converting..."
echo ""

python convert_to_ID.py \
    --mins_u64_dir "$MINS_U64_DIR" \
    --sorted_union "$SORTED_UNION" \
    --out_dir "$MINS_CONVERTED_U64_DIR" \
    --threads "$THREADS"


echo "======================================="
echo "Converting done"
echo "======================================="

# ============================================================
echo "======================================="
echo "Step 4: Elias-Fano encoding"
echo "======================================="

g++ encode_ef.cpp \
  -Ibits/include \
  -Ibits/external/essentials/include \
  -std=c++17 -O3 -pthread -o encode_ef

./encode_ef \
  --indir "$MINS_CONVERTED_U64_DIR" \
  --outdir "$MINS_EF_DIR" \
  --sigdir "$SKETCH_DIR" \
  --threads "$THREADS"


echo "======================================="
echo "EF encoding done"
echo "======================================="

# # ============================================================

# ============================================================
# Stop timer
# ============================================================
EF_PIPELINE_SECONDS=$SECONDS

# # ============================================================
# # Compression summary helpers
# # ============================================================

bytes_of_path () {
    du -sb "$1" | awk '{print $1}'
}

human_size () {
    numfmt --to=iec --suffix=B "$1"
}

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

ratio () {
    python3 - <<EOF
a = float("$1")
b = float("$2")
print(f"{a / b:.4f}" if b != 0 else "NA")
EOF
}

echo "Compressing mins_ef + hash_table_compressed + metadata together..."
# tar -cf - "$MINS_EF_DIR" "$HASH_TABLE_COMPRESSED_DIR" "$METADATA_DIR" | xz -9 -T0 > "$XZ_EF_TOTAL_FILE"
printf '%s\n' "$ARCHIVE_VERSION" > "$VERSION_FILE"
tar -C "$EF_BASE" -cf - \
    VERSION \
    mins_ef \
    hash_table_compressed \
    metadata \
    | xz -9 -T0 > "$XZ_EF_TOTAL_FILE"

sketch_size=$(bytes_of_path "$SKETCH_DIR")
mins_ef_size=$(bytes_of_path "$MINS_EF_DIR")

hash_u64_size=$(bytes_of_path "$HASH_TABLE_DIR")
hash_ef_size=$(bytes_of_path "$HASH_TABLE_COMPRESSED_DIR")
metadata_size=$(bytes_of_path "$METADATA_DIR")

ef_meta_total_size=$((mins_ef_size + hash_ef_size + metadata_size))
ef_total_xz_size=$(bytes_of_path "$XZ_EF_TOTAL_FILE")

file_count=$(find "$SKETCH_DIR" -type f -name "*.sig" | wc -l)
# ============================================================
# Last summary block: print to console AND file
# ============================================================
{
echo
echo "======================================="
echo "Compression summary"
echo "======================================="
echo "Archive version:                     $ARCHIVE_VERSION"
echo "Sketch dir:                          $SKETCH_DIR"
echo "Number of .sig files in sketch dir:  $file_count"
echo "MAX_HASH value:                      $MAX_HASH"
echo "Number of hashes:                    $NUM_HASH"
echo "Original sketches size:              $(human_size "$sketch_size") ($sketch_size bytes)"
echo "Original hash table .u64 size:       $(human_size "$hash_u64_size") ($hash_u64_size bytes)"
echo
echo "EF mins size:                        $(human_size "$mins_ef_size") ($mins_ef_size bytes)"
echo "EF compressed hash table size:       $(human_size "$hash_ef_size") ($hash_ef_size bytes)"
echo "Metadata size:                       $(human_size "$metadata_size") ($metadata_size bytes)"
echo "EF mins to sig filesize ratio:       $(ratio "$mins_ef_size" "$sketch_size")"
echo
echo "Total EF-mins + EF-ht + metadata size:    $(human_size "$ef_meta_total_size") ($ef_meta_total_size bytes)"
echo "Total EF-mins + EF-ht + metadata XZ size: $(human_size "$ef_total_xz_size") ($ef_total_xz_size bytes)"
echo "Ratio:                                    $(ratio "$ef_total_xz_size" "$sketch_size")"
echo
echo
echo "Runtime:                               $(human_time "$EF_PIPELINE_SECONDS") ($EF_PIPELINE_SECONDS seconds)"
echo "======================================="
} | tee "$SUMMARY_FILE"

rm -rf "$METADATA_DIR"
rm -rf "$MINS_U64_DIR"
rm -rf "$MINS_CONVERTED_U64_DIR"
rm -rf "$MINS_EF_DIR"
rm -rf "$HASH_TABLE_COMPRESSED_DIR"
rm -f "$VERSION_FILE"

echo "======================================="
echo "Finished pipeline"
echo "======================================="
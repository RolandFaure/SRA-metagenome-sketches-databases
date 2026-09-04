set -euo pipefail

INPUT_DIR="$1"
NUM_THREADS="$2"
OUTPUT_DIR="$3"

mkdir -p "$OUTPUT_DIR"

START_TIME=$SECONDS

# Pipe tar directly into zstd to avoid writing an uncompressed tar file to disk
tar -cf - -C "$INPUT_DIR" . | zstd -T"${NUM_THREADS}" -11 > "${OUTPUT_DIR}/matrix.tar.zst"

ELAPSED_TIME=$(($SECONDS - $START_TIME))

echo "zstd Total compression completed in $ELAPSED_TIME seconds"

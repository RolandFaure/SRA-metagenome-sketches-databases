#!/bin/bash

set -euo pipefail

INPUT_FILE="$1"
OUTPUT_DIR="$2"

mkdir -p "$OUTPUT_DIR"

START_TIME=$SECONDS

# Decompress zstd directly into tar and extract without
# writing an intermediate uncompressed tar file to disk.
zstd -dc "$INPUT_FILE" | tar -xf - -C "$OUTPUT_DIR"

ELAPSED_TIME=$(($SECONDS - $START_TIME))

echo "zstd Total decompression completed in $ELAPSED_TIME seconds"
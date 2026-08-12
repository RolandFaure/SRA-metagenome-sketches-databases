#!/usr/bin/env bash
set -euo pipefail

# ============================================================
# USAGE
# ============================================================
#
# Sample mode:
#   bash unit_test.sh \
#       --sample N \
#       ORIGINAL_SIG_DIR \
#       DECOMP_DIR
#
# Sample mode with custom threads:
#   bash unit_test.sh \
#       --sample N \
#       ORIGINAL_SIG_DIR \
#       DECOMP_DIR \
#       --threads THREADS
#
# Check every original .sig file:
#   bash unit_test.sh \
#       --all \
#       ORIGINAL_SIG_DIR \
#       DECOMP_DIR
#
# All mode with custom threads:
#   bash unit_test.sh \
#       --all \
#       ORIGINAL_SIG_DIR \
#       DECOMP_DIR \
#       --threads THREADS
#
# Show help:
#   bash unit_test.sh --help
#
# ============================================================

usage() {
    cat <<EOF
Usage:
    $0 --sample N ORIGINAL_SIG_DIR DECOMP_DIR [--threads THREADS]
    $0 --all ORIGINAL_SIG_DIR DECOMP_DIR [--threads THREADS]
    $0 --help

Modes:
    --sample N
        Test:
            N largest original .sig files
            N smallest original .sig files
            N random original .sig files

        If 3*N is greater than the total number of original
        .sig files, the script exits without running comparisons.

    --all
        Test every original .sig file.

Arguments:
    ORIGINAL_SIG_DIR
        Directory containing the original .sig files.

    DECOMP_DIR
        Directory containing the decompressed/rebuilt .sig files.

Optional:
    --threads THREADS
        Number of parallel comparison processes.
        Default: 256

Output:
    DECOMP_DIR/unit_test_results
EOF
}


# ============================================================
# Parse command-line arguments
# ============================================================

MODE=""
SAMPLE_N=0

# Default number of parallel comparison processes.
THREADS=256

case "${1:-}" in

    --sample)
        if [[ $# -ne 4 && $# -ne 6 ]]; then
            echo "ERROR: --sample requires:" >&2
            echo "  N ORIGINAL_SIG_DIR DECOMP_DIR" >&2
            echo >&2
            echo "Optional:" >&2
            echo "  --threads THREADS" >&2
            echo >&2
            usage >&2
            exit 2
        fi

        MODE="sample"
        SAMPLE_N="$2"
        ORIGINAL_SIG_DIR="$3"
        DECOMP_DIR="$4"

        if [[ $# -eq 6 ]]; then
            if [[ "$5" != "--threads" ]]; then
                echo "ERROR: Unknown option: $5" >&2
                echo >&2
                usage >&2
                exit 2
            fi

            THREADS="$6"
        fi
        ;;

    --all)
        if [[ $# -ne 3 && $# -ne 5 ]]; then
            echo "ERROR: --all requires:" >&2
            echo "  ORIGINAL_SIG_DIR DECOMP_DIR" >&2
            echo >&2
            echo "Optional:" >&2
            echo "  --threads THREADS" >&2
            echo >&2
            usage >&2
            exit 2
        fi

        MODE="all"
        ORIGINAL_SIG_DIR="$2"
        DECOMP_DIR="$3"

        if [[ $# -eq 5 ]]; then
            if [[ "$4" != "--threads" ]]; then
                echo "ERROR: Unknown option: $4" >&2
                echo >&2
                usage >&2
                exit 2
            fi

            THREADS="$5"
        fi
        ;;

    -h|--help)
        usage
        exit 0
        ;;

    "")
        echo "ERROR: A mode is required." >&2
        echo "Use --sample N or --all." >&2
        echo >&2
        usage >&2
        exit 2
        ;;

    *)
        echo "ERROR: Unknown option: $1" >&2
        echo >&2
        usage >&2
        exit 2
        ;;
esac


# ============================================================
# Validate inputs
# ============================================================

if [[ "$MODE" = "sample" ]]; then
    if [[ ! "$SAMPLE_N" =~ ^[1-9][0-9]*$ ]]; then
        echo "ERROR: N for --sample must be a positive integer." >&2
        echo "Received: $SAMPLE_N" >&2
        exit 2
    fi

    # The same N is used for largest, smallest, and random.
    M_EXTREME="$SAMPLE_N"
    N_RANDOM="$SAMPLE_N"
else
    # Retained from the original script. These lists do not
    # control the final selection when --all is used.
    M_EXTREME=1000
    N_RANDOM=10000
fi

if [[ ! "$THREADS" =~ ^[1-9][0-9]*$ ]]; then
    echo "ERROR: THREADS must be a positive integer." >&2
    echo "Received: $THREADS" >&2
    exit 2
fi

if [[ ! -d "$ORIGINAL_SIG_DIR" ]]; then
    echo "ERROR: Original signature directory does not exist:" >&2
    echo "  $ORIGINAL_SIG_DIR" >&2
    exit 2
fi

if [[ ! -r "$ORIGINAL_SIG_DIR" ]]; then
    echo "ERROR: Original signature directory is not readable:" >&2
    echo "  $ORIGINAL_SIG_DIR" >&2
    exit 2
fi

if [[ ! -d "$DECOMP_DIR" ]]; then
    echo "ERROR: Decompressed signature directory does not exist:" >&2
    echo "  $DECOMP_DIR" >&2
    exit 2
fi

if [[ ! -r "$DECOMP_DIR" ]]; then
    echo "ERROR: Decompressed signature directory is not readable:" >&2
    echo "  $DECOMP_DIR" >&2
    exit 2
fi


# ============================================================
# Output directory
# ============================================================

OUTDIR="$DECOMP_DIR/unit_test_results"

rm -rf "$OUTDIR"
mkdir -p "$OUTDIR"


# ============================================================
# Files retained after completion
# ============================================================

LARGEST_NAMES="$OUTDIR/largest_names.txt"
SMALLEST_NAMES="$OUTDIR/smallest_names.txt"
RANDOM_NAMES="$OUTDIR/random_names_from_remaining.txt"
FINAL_RESULTS="$OUTDIR/final_results.tsv"


# ============================================================
# Temporary working directory
#
# Everything inside this directory is removed automatically,
# including the generated Python comparator.
# ============================================================

TEMP_DIR=$(mktemp -d "$OUTDIR/.unit_test_tmp.XXXXXX")

cleanup() {
    rm -rf "$TEMP_DIR"
}

trap cleanup EXIT

ORIG_LIST="$TEMP_DIR/original_sigs.tsv"
DECOMP_LIST="$TEMP_DIR/decompressed_sigs.tsv"

EXTREME_NAMES="$TEMP_DIR/extreme_names.txt"
REMAINING_NAMES="$TEMP_DIR/remaining_names_after_extremes.txt"
SELECTED_NAMES="$TEMP_DIR/selected_names.txt"

JOBS_FILE="$TEMP_DIR/comparison_jobs.tsv"
MISSING_RESULTS="$TEMP_DIR/missing_results.tsv"
COMPARE_RESULTS="$TEMP_DIR/compare_results.tsv"

COMPARE_PY="$TEMP_DIR/compare_sourmash_sig_content.py"


# ============================================================
# Timer
# ============================================================

SECONDS=0

human_time() {
    local total_seconds=$1

    local days=$((total_seconds / 86400))
    local hours=$(((total_seconds % 86400) / 3600))
    local minutes=$(((total_seconds % 3600) / 60))
    local seconds=$((total_seconds % 60))

    if ((days > 0)); then
        printf "%dd %02dh %02dm %02ds" \
            "$days" "$hours" "$minutes" "$seconds"
    else
        printf "%02dh %02dm %02ds" \
            "$hours" "$minutes" "$seconds"
    fi
}


# ============================================================
# Embedded Python comparator
# ============================================================

cat > "$COMPARE_PY" <<'PY'
#!/usr/bin/env python3

import gzip
import hashlib
import json
import sys
from pathlib import Path


def load_json(path):
    """
    Load a plain or gzip-compressed sourmash signature file.
    """
    path = Path(path)
    data = path.read_bytes()

    # Gzip magic bytes.
    if data[:2] == b"\x1f\x8b":
        data = gzip.decompress(data)

    return json.loads(data.decode("utf-8"))


def normalize_mins(mins):
    """
    Normalize the sourmash 'mins' field.

    mins may be:
      1. A list of hash values.
      2. A dictionary mapping hashes to abundances.

    Hash ordering is ignored.
    """
    if isinstance(mins, list):
        return sorted(int(value) for value in mins)

    if isinstance(mins, dict):
        return {
            str(key): mins[key]
            for key in sorted(
                mins.keys(),
                key=lambda value: int(value),
            )
        }

    return mins


def normalize(obj, parent_key=None):
    """
    Recursively normalize sourmash signature JSON.

    Ignored:
      - JSON dictionary key order
      - Hash order inside 'mins'
      - Top-level signature-record order
      - Record order inside 'signatures'

    All actual field values are still compared.
    """
    if isinstance(obj, dict):
        normalized = {}

        for key, value in obj.items():
            if key == "mins":
                normalized[key] = normalize_mins(value)
            else:
                normalized[key] = normalize(
                    value,
                    parent_key=key,
                )

        return {
            str(key): normalized[key]
            for key in sorted(normalized.keys(), key=str)
        }

    if isinstance(obj, list):
        normalized = [
            normalize(value, parent_key=parent_key)
            for value in obj
        ]

        # Order of complete signature records is not meaningful.
        if (
            parent_key in (None, "signatures")
            and all(
                isinstance(value, dict)
                for value in normalized
            )
        ):
            return sorted(
                normalized,
                key=lambda value: json.dumps(
                    value,
                    sort_keys=True,
                    separators=(",", ":"),
                ),
            )

        return normalized

    return obj


def canonical_sha256(obj):
    """
    Generate a canonical SHA-256 digest for diagnostics.
    """
    payload = json.dumps(
        obj,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=False,
    ).encode("utf-8")

    return hashlib.sha256(payload).hexdigest()


def main():
    if len(sys.argv) != 3:
        print(
            "Usage: compare_sourmash_sig_content.py "
            "original.sig rebuilt.sig",
            file=sys.stderr,
        )
        return 2

    original_path = sys.argv[1]
    rebuilt_path = sys.argv[2]

    try:
        original = normalize(load_json(original_path))
        rebuilt = normalize(load_json(rebuilt_path))
    except Exception as error:
        print(
            f"ERROR loading/parsing JSON: {error}",
            file=sys.stderr,
        )
        return 2

    if original == rebuilt:
        return 0

    print("CONTENT DIFF", file=sys.stderr)

    print(
        f"original_sha256={canonical_sha256(original)}",
        file=sys.stderr,
    )

    print(
        f"rebuilt_sha256={canonical_sha256(rebuilt)}",
        file=sys.stderr,
    )

    return 1


if __name__ == "__main__":
    sys.exit(main())
PY

chmod +x "$COMPARE_PY"


# ============================================================
# Step 1: Find and count .sig files
# ============================================================

echo "======================================="
echo "Step 1: Counting .sig files"
echo "======================================="
echo "Comparison mode: $MODE"
echo "Comparison threads: $THREADS"

if [[ "$MODE" = "sample" ]]; then
    echo "Requested sample size per category: $SAMPLE_N"
fi

echo

# %f means basename only.
#
# Example:
#   /path/to/batch_01/DRR114383.sig
#
# becomes:
#   DRR114383.sig

find "$ORIGINAL_SIG_DIR" \
    -type f \
    -name "*.sig" \
    -printf "%f\t%p\t%s\n" |
    sort -t $'\t' -k1,1 > "$ORIG_LIST"

find "$DECOMP_DIR" \
    -type f \
    -name "*.sig" \
    -printf "%f\t%p\t%s\n" |
    sort -t $'\t' -k1,1 > "$DECOMP_LIST"

ORIG_COUNT=$(wc -l < "$ORIG_LIST")
DECOMP_COUNT=$(wc -l < "$DECOMP_LIST")

echo "Original .sig count:     $ORIG_COUNT"
echo "Decompressed .sig count: $DECOMP_COUNT"

if [ "$ORIG_COUNT" -eq "$DECOMP_COUNT" ]; then
    echo "[OK] .sig file counts match."
else
    echo "[WARNING] .sig file counts do not match."
fi


# ============================================================
# Validate requested sample size
# ============================================================

if [ "$MODE" = "sample" ]; then
    REQUESTED_SAMPLE_TOTAL=$((3 * SAMPLE_N))

    if [ "$REQUESTED_SAMPLE_TOTAL" -gt "$ORIG_COUNT" ]; then
        echo >&2
        echo "ERROR: Requested sample size is too large." >&2
        echo "N:                           $SAMPLE_N" >&2
        echo "Largest requested:           $SAMPLE_N" >&2
        echo "Smallest requested:          $SAMPLE_N" >&2
        echo "Random requested:            $SAMPLE_N" >&2
        echo "Total requested, 3*N:        $REQUESTED_SAMPLE_TOTAL" >&2
        echo "Total original .sig files:   $ORIG_COUNT" >&2
        echo >&2
        echo "Choose N such that 3*N <= $ORIG_COUNT." >&2
        exit 2
    fi
fi


# ============================================================
# Step 2: Select largest and smallest files
# ============================================================

echo
echo "======================================="
echo "Step 2: Selecting largest/smallest files"
echo "======================================="

sort -t $'\t' -k3,3nr "$ORIG_LIST" |
    awk -F'\t' -v count="$M_EXTREME" \
        'NR <= count {print $1}' \
        > "$LARGEST_NAMES"

sort -t $'\t' -k3,3n "$ORIG_LIST" |
    awk -F'\t' -v count="$M_EXTREME" \
        'NR <= count {print $1}' \
        > "$SMALLEST_NAMES"

cat "$LARGEST_NAMES" "$SMALLEST_NAMES" |
    sort -u > "$EXTREME_NAMES"

LARGEST_COUNT=$(wc -l < "$LARGEST_NAMES")
SMALLEST_COUNT=$(wc -l < "$SMALLEST_NAMES")
EXTREME_COUNT=$(wc -l < "$EXTREME_NAMES")

echo "Largest files selected: $LARGEST_COUNT"
echo "Smallest files selected: $SMALLEST_COUNT"


# ============================================================
# Step 3: Select random files
# ============================================================

echo
echo "======================================="
echo "Step 3: Selecting random files"
echo "======================================="

awk '
    NR == FNR {
        extreme[$1] = 1
        next
    }

    !($1 in extreme) {
        print $1
    }
' "$EXTREME_NAMES" <(cut -f1 "$ORIG_LIST") \
    > "$REMAINING_NAMES"

shuf -n "$N_RANDOM" "$REMAINING_NAMES" \
    > "$RANDOM_NAMES"

RANDOM_COUNT=$(wc -l < "$RANDOM_NAMES")

echo "Random files selected: $RANDOM_COUNT"


# ============================================================
# Step 4: Build final test selection
# ============================================================

echo
echo "======================================="
echo "Step 4: Building test selection"
echo "======================================="

if [ "$MODE" = "all" ]; then
    # Select every original signature basename.
    cut -f1 "$ORIG_LIST" |
        sort -u > "$SELECTED_NAMES"

    echo "Mode: checking every original .sig file."
else
    # Select largest, smallest, and random files.
    cat \
        "$LARGEST_NAMES" \
        "$SMALLEST_NAMES" \
        "$RANDOM_NAMES" |
        sort -u > "$SELECTED_NAMES"

    echo "Mode: checking largest, smallest, and random files."
fi

SELECTED_COUNT=$(wc -l < "$SELECTED_NAMES")

echo "Files selected for comparison: $SELECTED_COUNT"

if [ "$MODE" = "sample" ]; then
    EXPECTED_MAX=$((EXTREME_COUNT + RANDOM_COUNT))

    if [ "$SELECTED_COUNT" -ne "$EXPECTED_MAX" ]; then
        echo "[WARNING] Selected count is smaller than expected."
        echo "The largest and smallest groups may overlap."
    fi
fi


# ============================================================
# Step 5: Build basename lookup maps
# ============================================================

declare -A ORIG_PATH
declare -A ORIG_SIZE
declare -A DECOMP_PATH

while IFS=$'\t' read -r name full_path size; do
    ORIG_PATH["$name"]="$full_path"
    ORIG_SIZE["$name"]="$size"
done < "$ORIG_LIST"

while IFS=$'\t' read -r name full_path size; do
    DECOMP_PATH["$name"]="$full_path"
done < "$DECOMP_LIST"

: > "$JOBS_FILE"
: > "$MISSING_RESULTS"
: > "$COMPARE_RESULTS"

while IFS= read -r name; do
    [ -z "$name" ] && continue

    original="${ORIG_PATH[$name]}"
    original_size="${ORIG_SIZE[$name]}"
    rebuilt="${DECOMP_PATH[$name]:-}"

    if [ -z "$rebuilt" ]; then
        printf "%s\t%s\t%s\t%s\t%s\t%s\n" \
            "$name" \
            "$original_size" \
            "MISSING" \
            "$original" \
            "." \
            "No file with the same basename found in decompressed directory" \
            >> "$MISSING_RESULTS"
    else
        printf "%s\t%s\t%s\t%s\n" \
            "$name" \
            "$original_size" \
            "$original" \
            "$rebuilt" \
            >> "$JOBS_FILE"
    fi
done < "$SELECTED_NAMES"

JOB_COUNT=$(wc -l < "$JOBS_FILE")
MISSING_BEFORE_COMPARE=$(wc -l < "$MISSING_RESULTS")

echo
echo "Comparison jobs:       $JOB_COUNT"
echo "Missing rebuilt files: $MISSING_BEFORE_COMPARE"


# ============================================================
# Step 6: Compare signatures in parallel
# ============================================================

echo
echo "======================================="
echo "Step 5: Comparing sourmash signatures"
echo "======================================="

export COMPARE_PY
export TEMP_DIR

if [ "$JOB_COUNT" -gt 0 ]; then
    xargs \
        -d '\n' \
        -P "$THREADS" \
        -n 1 \
        bash -c '
            line="$1"

            IFS=$'\''\t'\'' read -r \
                name \
                original_size \
                original \
                rebuilt \
                <<< "$line"

            error_file=$(
                mktemp "$TEMP_DIR/compare_error.XXXXXX"
            )

            if "$COMPARE_PY" \
                "$original" \
                "$rebuilt" \
                > /dev/null \
                2> "$error_file"
            then
                printf "%s\t%s\t%s\t%s\t%s\t%s\n" \
                    "$name" \
                    "$original_size" \
                    "PASS" \
                    "$original" \
                    "$rebuilt" \
                    "."
            else
                reason=$(
                    tr "\t\n" "  " < "$error_file"
                )

                printf "%s\t%s\t%s\t%s\t%s\t%s\n" \
                    "$name" \
                    "$original_size" \
                    "FAIL" \
                    "$original" \
                    "$rebuilt" \
                    "$reason"
            fi

            rm -f "$error_file"
        ' bash < "$JOBS_FILE" \
        > "$COMPARE_RESULTS"
fi

# Sort results because parallel workers finish in arbitrary order.
sort -t $'\t' -k1,1 \
    "$COMPARE_RESULTS" \
    -o "$COMPARE_RESULTS"

sort -t $'\t' -k1,1 \
    "$MISSING_RESULTS" \
    -o "$MISSING_RESULTS"


# ============================================================
# Step 7: Write final results
# ============================================================

echo
echo "======================================="
echo "Step 6: Writing final results"
echo "======================================="

{
    echo -e \
        "filename\toriginal_size_bytes\tstatus\toriginal_path\trebuilt_path\treason"

    cat "$MISSING_RESULTS"
    cat "$COMPARE_RESULTS"
} > "$FINAL_RESULTS"

TOTAL_TESTED=$(
    tail -n +2 "$FINAL_RESULTS" |
        wc -l
)

PASS_COUNT=$(
    awk -F'\t' '
        $3 == "PASS" {
            count++
        }

        END {
            print count + 0
        }
    ' "$FINAL_RESULTS"
)

FAIL_COUNT=$(
    awk -F'\t' '
        $3 == "FAIL" {
            count++
        }

        END {
            print count + 0
        }
    ' "$FINAL_RESULTS"
)

MISSING_COUNT=$(
    awk -F'\t' '
        $3 == "MISSING" {
            count++
        }

        END {
            print count + 0
        }
    ' "$FINAL_RESULTS"
)

echo "Final results written to:"
echo "$FINAL_RESULTS"


# ============================================================
# Final summary
# ============================================================

echo
echo "======================================="
echo "Final unit-test summary"
echo "======================================="

echo "Mode:                         $MODE"

if [ "$MODE" = "sample" ]; then
    echo "Sample size per category:     $SAMPLE_N"
fi

echo "Threads:                      $THREADS"
echo "Original directory:           $ORIGINAL_SIG_DIR"
echo "Decompressed directory:       $DECOMP_DIR"
echo "Unit-test result directory:   $OUTDIR"
echo

echo "Original .sig count:          $ORIG_COUNT"
echo "Decompressed .sig count:      $DECOMP_COUNT"
echo

echo "Largest selected:             $LARGEST_COUNT"
echo "Smallest selected:            $SMALLEST_COUNT"
echo "Random selected:              $RANDOM_COUNT"
echo "Files selected for testing:   $SELECTED_COUNT"
echo "Total tested:                 $TOTAL_TESTED"
echo

echo "Passed:                       $PASS_COUNT"
echo "Failed:                       $FAIL_COUNT"
echo "Missing rebuilt files:        $MISSING_COUNT"
echo

RUNTIME=$SECONDS

echo "Runtime: $(human_time "$RUNTIME") ($RUNTIME seconds)"

echo
echo "Output files retained:"
echo "  Largest names:  $LARGEST_NAMES"
echo "  Smallest names: $SMALLEST_NAMES"
echo "  Random names:   $RANDOM_NAMES"
echo "  Final results:  $FINAL_RESULTS"

echo

ALL_SELECTION_OK=1

if [ "$MODE" = "all" ] &&
   [ "$TOTAL_TESTED" -ne "$ORIG_COUNT" ]; then
    ALL_SELECTION_OK=0

    echo "[ERROR] All mode did not test every original file."
    echo "Expected: $ORIG_COUNT"
    echo "Tested:   $TOTAL_TESTED"
fi

if [ "$ORIG_COUNT" -eq "$DECOMP_COUNT" ] &&
   [ "$FAIL_COUNT" -eq 0 ] &&
   [ "$MISSING_COUNT" -eq 0 ] &&
   [ "$ALL_SELECTION_OK" -eq 1 ]; then
    echo "[SUCCESS] Unit test passed."
    exit 0
else
    echo "[ERROR] Unit test failed."
    exit 1
fi
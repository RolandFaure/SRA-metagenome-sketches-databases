#!/usr/bin/env python3

import argparse
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed

import numpy as np


def process_one(u64_file: Path, mins_u64_dir: Path, out_dir: Path, sorted_hashes):
    # Original minhash values
    arr = np.fromfile(u64_file, dtype=np.uint64)

    # Find rank/position in sorted global union
    idx = np.searchsorted(sorted_hashes, arr)

    # Safety check:
    # np.searchsorted gives insertion position, so we verify actual match.
    if len(idx) > 0:
        bad = idx >= len(sorted_hashes)

        good = ~bad
        if np.any(good):
            bad[good] = sorted_hashes[idx[good]] != arr[good]

        if np.any(bad):
            bad_values = arr[bad][:10]
            raise ValueError(
                f"{u64_file} has hashes not found in sorted union. "
                f"Example bad values: {bad_values}"
            )

    # Dense IDs are 1..n
    # Store IDs as uint64
    mapped = (idx + 1).astype(np.uint64)

    base = u64_file.name.replace("_mins.u64", "")

    # Preserve batch folder structure
    # Example:
    # mins_u64/batch_01/DRR000001_mins.u64
    # converted_mins_u64/batch_01/DRR000001_ids.u64
    rel_parent = u64_file.parent.relative_to(mins_u64_dir)
    out_subdir = out_dir / rel_parent
    out_subdir.mkdir(parents=True, exist_ok=True)

    out_file = out_subdir / f"{base}_ids.u64"

    mapped.tofile(out_file)

    return u64_file.name, rel_parent, len(arr), len(mapped), out_file


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--mins_u64_dir",
        default="/scratch/logan_compression/EF/mins_u64/",
        help="Base directory containing batch folders with *_mins.u64 files",
    )

    parser.add_argument(
        "--sorted_union",
        default="/scratch/logan_compression/hash_table/sorted_union_hashes.u64",
        help="Sorted global union of hashes as uint64 binary file",
    )

    parser.add_argument(
        "--out_dir",
        default="/scratch/logan_compression/EF/converted_mins_u64/",
        help="Output base directory for dense ID .u64 files",
    )

    parser.add_argument(
        "--threads",
        type=int,
        default=64,
        help="Number of threads",
    )

    args = parser.parse_args()

    mins_u64_dir = Path(args.mins_u64_dir).resolve()
    out_dir = Path(args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    print("Loading sorted union hashes...")
    sorted_hashes = np.fromfile(args.sorted_union, dtype=np.uint64)

    print(f"Loaded sorted union with {len(sorted_hashes)} hashes")

    if len(sorted_hashes) == 0:
        raise ValueError("sorted_union_hashes.u64 is empty")

    bits_needed = max(1, int(len(sorted_hashes)).bit_length())


    # Recursive search because mins_u64 has batch folders
    u64_files = sorted(mins_u64_dir.rglob("*_mins.u64"))

    print(f"Found {len(u64_files)} .u64 files recursively")
    print(f"Writing uint64 ID files to: {out_dir}")
    print(f"Threads: {args.threads}")

    if len(u64_files) == 0:
        print("No *_mins.u64 files found. Exiting.")
        return

    with ThreadPoolExecutor(max_workers=args.threads) as executor:
        futures = [
            executor.submit(
                process_one,
                f,
                mins_u64_dir,
                out_dir,
                sorted_hashes,
            )
            for f in u64_files
        ]

        for i, future in enumerate(as_completed(futures), 1):
            try:
                name, batch_folder, old_n, new_n, out_file = future.result()

                print(
                    f"[{i}/{len(u64_files)}] "
                    f"{batch_folder}/{name}: "
                    f"{old_n} hashes -> {new_n} uint64 IDs -> {out_file}"
                )

            except Exception as e:
                print(f"[ERROR] {e}")

    print("Done.")


if __name__ == "__main__":
    main()
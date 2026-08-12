#!/usr/bin/env python3

import argparse
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed

import numpy as np


def output_name_from_id_file(converted_file: Path):
    """
    Example:
        DRR000001_ids.u64  -> DRR000001_mins.u64
        DRR000001.u64      -> DRR000001_mins.u64
    """
    base = converted_file.name

    if base.endswith(".u64"):
        base = base[:-4]

    if base.endswith("_ids"):
        base = base[:-len("_ids")]

    if not base.endswith("_mins"):
        base = base + "_mins"

    return base + ".u64"


def process_one(converted_file: Path,
                converted_root: Path,
                out_root: Path,
                sorted_union: np.ndarray):
    # These are dense IDs, stored as uint64.
    ids = np.fromfile(converted_file, dtype=np.uint64)

    if len(ids) == 0:
        actual = np.array([], dtype=np.uint64)
    else:
        # IDs are 1-based, so valid IDs are 1..len(sorted_union)
        if np.any(ids == 0):
            raise ValueError(f"{converted_file} has ID 0, but IDs should be 1-based")

        idx = ids - 1

        if np.any(idx >= len(sorted_union)):
            bad_ids = ids[idx >= len(sorted_union)][:10]
            raise ValueError(
                f"{converted_file} has ID outside hash table. "
                f"Example bad IDs: {bad_ids}"
            )

        # Convert dense IDs back to original uint64 hash values
        actual = sorted_union[idx].astype(np.uint64)

    # Preserve batch folder structure
    # converted_mins/batch_01/DRR000001_ids.u64
    # actual_mins_u64/batch_01/DRR000001_mins.u64
    rel_parent = converted_file.parent.relative_to(converted_root)
    out_dir = out_root / rel_parent
    out_dir.mkdir(parents=True, exist_ok=True)

    out_file = out_dir / output_name_from_id_file(converted_file)

    actual.tofile(out_file)

    return converted_file.name, rel_parent, len(ids), out_file


def process_one_batch(batch_dir: Path,
                      converted_root: Path,
                      out_root: Path,
                      sorted_union: np.ndarray,
                      threads: int):
    files = sorted(batch_dir.glob("*.u64"))

    print("=======================================")
    print(f"Batch: {batch_dir.name}")
    print(f"Input: {batch_dir}")
    print(f"Files: {len(files)}")
    print(f"Threads: {threads}")
    print("=======================================")

    if len(files) == 0:
        print(f"[SKIP] No .u64 files found in {batch_dir}")
        return

    ok = 0
    err = 0

    with ThreadPoolExecutor(max_workers=threads) as executor:
        futures = [
            executor.submit(
                process_one,
                f,
                converted_root,
                out_root,
                sorted_union,
            )
            for f in files
        ]

        for i, future in enumerate(as_completed(futures), 1):
            try:
                name, batch_folder, n, out_file = future.result()
                ok += 1

                print(
                    f"[{i}/{len(files)}] "
                    f"{batch_folder}/{name}: "
                    f"{n} IDs -> actual hashes -> {out_file}"
                )

            except Exception as e:
                err += 1
                print(f"[ERROR] {e}")

    print(
        f"Batch done: {batch_dir.name} "
        f"successful={ok} errors={err} total={len(files)}"
    )


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--converted_mins_dir",
        required=True,
        help="Directory containing decompressed dense-ID .u64 files, with batch folders",
    )

    parser.add_argument(
        "--hash_table",
        required=True,
        help="Path to sorted_union_hashes.u64",
    )

    parser.add_argument(
        "--out_dir",
        required=True,
        help="Output directory for actual uint64 mins",
    )

    parser.add_argument(
        "--threads",
        type=int,
        default=64,
    )

    args = parser.parse_args()

    converted_root = Path(args.converted_mins_dir).resolve()
    out_root = Path(args.out_dir).resolve()
    out_root.mkdir(parents=True, exist_ok=True)

    print("Loading hash table as memory map...")
    sorted_union = np.memmap(args.hash_table, dtype=np.uint64, mode="r")
    print(f"Loaded hash table with {len(sorted_union)} hashes")
    print(f"Converted mins root: {converted_root}")
    print(f"Output root: {out_root}")

    batch_dirs = sorted([p for p in converted_root.iterdir() if p.is_dir()])

    if len(batch_dirs) == 0:
        print("No batch folders found. Treating converted_mins_dir as one batch.")
        process_one_batch(
            converted_root,
            converted_root,
            out_root,
            sorted_union,
            args.threads,
        )
    else:
        print(f"Found {len(batch_dirs)} batch folders")

        for batch_dir in batch_dirs:
            process_one_batch(
                batch_dir,
                converted_root,
                out_root,
                sorted_union,
                args.threads,
            )

    print("Done.")


if __name__ == "__main__":
    main()
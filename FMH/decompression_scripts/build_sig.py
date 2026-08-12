#!/usr/bin/env python3

import argparse
import json
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed

import numpy as np


def clean_sig_name(path: Path):
    name = path.name

    if name.endswith(".sig"):
        return name[:-len(".sig")]

    return path.stem


def load_metadata(path: Path):
    with open(path, "r") as f:
        return json.load(f)


def save_sig(obj, out_path: Path):
    out_path.parent.mkdir(parents=True, exist_ok=True)

    with open(out_path, "w") as f:
        json.dump(obj, f)


def insert_mins(obj, mins):
    sigs = obj if isinstance(obj, list) else [obj]

    for sig in sigs:
        for s in sig.get("signatures", []):
            s["mins"] = mins

    return obj


def process_one(metadata_file: Path,
                metadata_root: Path,
                mins_root: Path,
                out_root: Path):
    base = clean_sig_name(metadata_file)

    # Preserve batch folder structure
    # metadata/batch_01/DRR000001.sig
    # actual_mins_u64/batch_01/DRR000001_mins.u64
    rel_parent = metadata_file.parent.relative_to(metadata_root)

    mins_file = mins_root / rel_parent / f"{base}_mins.u64"

    if not mins_file.exists():
        raise FileNotFoundError(
            f"Missing mins file for {metadata_file}: {mins_file}"
        )

    mins = np.fromfile(mins_file, dtype=np.uint64).tolist()

    obj = load_metadata(metadata_file)
    obj = insert_mins(obj, mins)

    out_dir = out_root / rel_parent
    out_file = out_dir / metadata_file.name

    save_sig(obj, out_file)

    return metadata_file.name, rel_parent, len(mins), out_file


def process_one_batch(batch_metadata_dir: Path,
                      metadata_root: Path,
                      mins_root: Path,
                      out_root: Path,
                      threads: int):
    metadata_files = sorted(batch_metadata_dir.glob("*.sig"))

    print("=======================================")
    print(f"Batch: {batch_metadata_dir.name}")
    print(f"Metadata input: {batch_metadata_dir}")
    print(f"Metadata files: {len(metadata_files)}")
    print(f"Threads: {threads}")
    print("=======================================")

    if len(metadata_files) == 0:
        print(f"[SKIP] No .sig files found in {batch_metadata_dir}")
        return

    ok = 0
    err = 0

    with ThreadPoolExecutor(max_workers=threads) as executor:
        futures = [
            executor.submit(
                process_one,
                f,
                metadata_root,
                mins_root,
                out_root,
            )
            for f in metadata_files
        ]

        for i, future in enumerate(as_completed(futures), 1):
            try:
                name, batch_folder, n_mins, out_file = future.result()
                ok += 1

                print(
                    f"[{i}/{len(metadata_files)}] "
                    f"{batch_folder}/{name}: "
                    f"inserted {n_mins} mins -> {out_file}"
                )

            except Exception as e:
                err += 1
                print(f"[ERROR] {e}")

    print(
        f"Batch done: {batch_metadata_dir.name} "
        f"successful={ok} errors={err} total={len(metadata_files)}"
    )


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--metadata_dir",
        required=True,
        help="Base directory containing batch folders with metadata .sig files",
    )

    parser.add_argument(
        "--mins_u64_dir",
        required=True,
        help="Base directory containing batch folders with actual *_mins.u64 files",
    )

    parser.add_argument(
        "--out_dir",
        required=True,
        help="Output base directory for rebuilt .sig files",
    )

    parser.add_argument(
        "--threads",
        type=int,
        default=64,
    )

    args = parser.parse_args()

    metadata_root = Path(args.metadata_dir).resolve()
    mins_root = Path(args.mins_u64_dir).resolve()
    out_root = Path(args.out_dir).resolve()

    out_root.mkdir(parents=True, exist_ok=True)

    print(f"Metadata root: {metadata_root}")
    print(f"Mins root: {mins_root}")
    print(f"Output root: {out_root}")
    print(f"Threads: {args.threads}")

    batch_dirs = sorted([p for p in metadata_root.iterdir() if p.is_dir()])

    if len(batch_dirs) == 0:
        print("No batch folders found. Treating metadata_dir as one batch.")

        process_one_batch(
            metadata_root,
            metadata_root,
            mins_root,
            out_root,
            args.threads,
        )

    else:
        print(f"Found {len(batch_dirs)} batch folders")

        for batch_dir in batch_dirs:
            process_one_batch(
                batch_dir,
                metadata_root,
                mins_root,
                out_root,
                args.threads,
            )

    print("Done.")


if __name__ == "__main__":
    main()
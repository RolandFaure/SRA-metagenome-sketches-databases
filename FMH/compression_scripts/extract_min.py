#!/usr/bin/env python3

import json
import argparse
import numpy as np
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed


def load_json_sig(path: Path):
    with open(path, "r") as f:
        return json.load(f)


def save_json_sig(obj, out_path: Path):
    out_path.parent.mkdir(parents=True, exist_ok=True)

    with open(out_path, "w") as f:
        json.dump(obj, f)


def extract_mins_and_clear(obj):
    mins_all = []

    # sourmash sig can be either one object or a list of objects
    sigs = obj if isinstance(obj, list) else [obj]

    for sig in sigs:
        for s in sig.get("signatures", []):
            mins = s.get("mins", [])

            mins_all.extend(int(x) for x in mins)

            # remove mins from metadata copy
            s["mins"] = []

    return mins_all, obj


def clean_sig_name(path: Path):
    name = path.name

    if name.endswith(".sig"):
        name = name[:-len(".sig")]

    return name


def process_one(sig_file: Path, input_dir: Path, out_dir: Path, metadata_dir: Path):
    obj = load_json_sig(sig_file)

    mins, metadata_obj = extract_mins_and_clear(obj)

    # Assumption:
    # mins are already sorted
    # mins have no duplicates
    arr = np.array(mins, dtype=np.uint64)

    base = clean_sig_name(sig_file)

    # preserve batch folder structure
    # Example:
    # input_dir/batch_01/DRR000001.sig
    # rel_parent = batch_01
    rel_parent = sig_file.parent.relative_to(input_dir)

    out_subdir = out_dir / rel_parent
    metadata_subdir = metadata_dir / rel_parent

    out_subdir.mkdir(parents=True, exist_ok=True)
    metadata_subdir.mkdir(parents=True, exist_ok=True)

    # save mins as binary uint64
    out_file = out_subdir / f"{base}_mins.u64"
    arr.tofile(out_file)

    # save metadata sig with mins=[]
    metadata_file = metadata_subdir / sig_file.name
    save_json_sig(metadata_obj, metadata_file)

    return sig_file.name, rel_parent, len(arr), out_file, metadata_file


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--input_dir",
        default="/scratch/logan_compression/test_batches",
        help="Directory containing batch folders with .sig files",
    )

    parser.add_argument(
        "--out_dir",
        default="/scratch/logan_compression/EF/mins_u64",
        help="Output base directory for *_mins.u64 files",
    )

    parser.add_argument(
        "--metadata_dir",
        default="/scratch/logan_compression/EF/metadata",
        help="Output base directory for metadata .sig files with mins removed",
    )

    parser.add_argument(
        "--threads",
        type=int,
        default=64,
        help="Number of threads",
    )

    args = parser.parse_args()

    input_dir = Path(args.input_dir).resolve()
    out_dir = Path(args.out_dir)
    metadata_dir = Path(args.metadata_dir)

    out_dir.mkdir(parents=True, exist_ok=True)
    metadata_dir.mkdir(parents=True, exist_ok=True)

    # recursively find only .sig files
    sig_files = sorted(input_dir.rglob("*.sig"))

    print(f"Input directory: {input_dir}")
    print(f"Found {len(sig_files)} .sig files recursively")
    print(f"Writing uint64 mins to: {out_dir}")
    print(f"Writing metadata to: {metadata_dir}")
    print(f"Threads: {args.threads}")

    if len(sig_files) == 0:
        print("No .sig files found. Exiting.")
        return

    with ThreadPoolExecutor(max_workers=args.threads) as executor:
        futures = [
            executor.submit(
                process_one,
                sig_file,
                input_dir,
                out_dir,
                metadata_dir,
            )
            for sig_file in sig_files
        ]

        for i, future in enumerate(as_completed(futures), 1):
            try:
                name, batch_folder, n_mins, out_file, metadata_file = future.result()

                print(
                    f"[{i}/{len(sig_files)}] "
                    f"{batch_folder}/{name}: "
                    f"{n_mins} mins -> {out_file}; "
                    f"metadata -> {metadata_file}"
                )

            except Exception as e:
                print(f"[ERROR] {e}")

    print("Done.")


if __name__ == "__main__":
    main()
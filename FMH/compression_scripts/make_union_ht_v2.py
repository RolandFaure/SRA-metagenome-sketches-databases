#!/usr/bin/env python3

import argparse
import gzip
import json
import math
import shutil
from multiprocessing import Pool
from pathlib import Path

import numpy as np

try:
    import orjson

    HAS_ORJSON = True
except ImportError:
    HAS_ORJSON = False


# ============================================================
# JSON loading
# ============================================================

def load_json_file(sig_file: Path):
    data = sig_file.read_bytes()

    if data[:2] == b"\x1f\x8b":
        data = gzip.decompress(data)

    if HAS_ORJSON:
        return orjson.loads(data)

    return json.loads(data.decode("utf-8"))


def iter_sourmash_sigs(obj):
    if isinstance(obj, list):
        for item in obj:
            yield from iter_sourmash_sigs(item)

    elif isinstance(obj, dict):
        if "signatures" in obj:
            for sig in obj["signatures"]:
                yield sig

        elif "mins" in obj:
            yield obj


# ============================================================
# Fast conversion
# ============================================================

def mins_to_u64_array(mins):
    """
    Convert sourmash mins to a uint64 NumPy array.

    mins may be:
      - list of integers
      - list of strings
      - dictionary with hash values as keys
    """

    if isinstance(mins, dict):
        return np.fromiter(
            (int(value) for value in mins.keys()),
            dtype=np.uint64,
        )

    if isinstance(mins, list):
        if len(mins) == 0:
            return np.empty(0, dtype=np.uint64)

        return np.asarray(mins, dtype=np.uint64)

    return np.fromiter(
        (int(value) for value in mins),
        dtype=np.uint64,
    )


def extract_mins_array(
    sig_file: Path,
    target_ksize: int,
) -> np.ndarray:
    obj = load_json_file(sig_file)

    arrays = []

    for sig in iter_sourmash_sigs(obj):
        ksize = int(sig.get("ksize", -1))

        if target_ksize is not None and ksize != target_ksize:
            continue

        mins = sig.get("mins", [])
        arr = mins_to_u64_array(mins)

        if arr.size > 0:
            arrays.append(arr)

    if not arrays:
        return np.empty(0, dtype=np.uint64)

    if len(arrays) == 1:
        return arrays[0]

    return np.concatenate(arrays)


# ============================================================
# Sort and unique
# ============================================================

def sort_unique_to_file(
    x: np.ndarray,
    out_file: Path,
    write_block_items: int = 50_000_000,
):
    """
    Sort a uint64 array in RAM and write unique values directly
    to disk.

    This avoids using np.unique() on the complete array, which
    would create another large output array in memory.
    """

    out_file = Path(out_file)

    if x.size == 0:
        out_file.write_bytes(b"")
        return 0

    x.sort()

    n_unique = 0
    last_written = None

    with open(out_file, "wb") as output:
        for start in range(0, x.size, write_block_items):
            end = min(
                start + write_block_items,
                x.size,
            )

            block = x[start:end]

            keep = np.empty(
                block.size,
                dtype=bool,
            )

            if last_written is None:
                keep[0] = True
            else:
                keep[0] = block[0] != last_written

            if block.size > 1:
                keep[1:] = block[1:] != block[:-1]

            unique_block = block[keep]

            if unique_block.size > 0:
                unique_block.tofile(output)

                n_unique += int(unique_block.size)
                last_written = unique_block[-1]

    return n_unique


# ============================================================
# Worker-side chunk writing
# ============================================================

def write_unique_chunk(
    buffers,
    out_file: Path,
):
    if len(buffers) == 1:
        x = buffers[0]
    else:
        x = np.concatenate(buffers)

    n_unique = sort_unique_to_file(
        x,
        out_file,
    )

    del x

    return str(out_file), n_unique


def process_worker_group(args):
    (
        worker_id,
        sig_files,
        target_ksize,
        chunk_hashes,
        temp_dir,
        progress_every,
    ) = args

    temp_dir = Path(temp_dir)

    worker_chunk_files = []

    buffers = []
    buffer_count = 0
    chunk_id = 0

    total_raw_mins = 0
    processed = 0

    for sig_file in sig_files:
        sig_file = Path(sig_file)

        arr = extract_mins_array(
            sig_file,
            target_ksize,
        )

        n_mins = int(arr.size)

        processed += 1
        total_raw_mins += n_mins

        if n_mins > 0:
            buffers.append(arr)
            buffer_count += n_mins

        if buffer_count >= chunk_hashes:
            out_file = (
                temp_dir
                / (
                    f"worker_{worker_id:04d}"
                    f"_chunk_{chunk_id:06d}.u64"
                )
            )

            raw_before_unique = buffer_count

            _, n_unique = write_unique_chunk(
                buffers,
                out_file,
            )

            worker_chunk_files.append(
                str(out_file)
            )

            print(
                f"Worker {worker_id}: "
                f"wrote chunk {chunk_id} | "
                f"raw = {raw_before_unique} | "
                f"unique = {n_unique}",
                flush=True,
            )

            buffers = []
            buffer_count = 0
            chunk_id += 1

        if (
            progress_every > 0
            and processed % progress_every == 0
        ):
            print(
                f"Worker {worker_id}: "
                f"processed {processed} sigs | "
                f"raw mins = {total_raw_mins}",
                flush=True,
            )

    if buffer_count > 0:
        out_file = (
            temp_dir
            / (
                f"worker_{worker_id:04d}"
                f"_chunk_{chunk_id:06d}.u64"
            )
        )

        raw_before_unique = buffer_count

        _, n_unique = write_unique_chunk(
            buffers,
            out_file,
        )

        worker_chunk_files.append(
            str(out_file)
        )

        print(
            f"Worker {worker_id}: "
            f"wrote final chunk {chunk_id} | "
            f"raw = {raw_before_unique} | "
            f"unique = {n_unique}",
            flush=True,
        )

    return {
        "worker_id": worker_id,
        "processed": processed,
        "total_raw_mins": total_raw_mins,
        "chunk_files": worker_chunk_files,
    }


# ============================================================
# Fast RAM-heavy final merge
# ============================================================

def final_merge_in_memory(
    chunk_files,
    out_file: Path,
):
    """
    Read all sorted unique chunk files into one uint64 array,
    sort once, deduplicate once, and write the final union.
    """

    chunk_files = [
        Path(file)
        for file in chunk_files
    ]

    out_file = Path(out_file)

    sizes = [
        file.stat().st_size // 8
        for file in chunk_files
    ]

    total_items = int(sum(sizes))

    print()
    print("Final RAM merge")
    print(f"Chunks: {len(chunk_files)}")
    print(
        "Total chunk hashes before final dedup: "
        f"{total_items}"
    )
    print(
        "Approximate raw RAM for final array: "
        f"{total_items * 8 / 1024**3:.2f} GiB"
    )
    print()

    if total_items == 0:
        out_file.write_bytes(b"")
        return 0

    x = np.empty(
        total_items,
        dtype=np.uint64,
    )

    offset = 0

    for index, (file, n_items) in enumerate(
        zip(chunk_files, sizes),
        start=1,
    ):
        arr = np.fromfile(
            file,
            dtype=np.uint64,
        )

        if arr.size != n_items:
            raise RuntimeError(
                f"Size mismatch while reading {file}"
            )

        x[offset:offset + n_items] = arr
        offset += n_items

        del arr

        if (
            index % 25 == 0
            or index == len(chunk_files)
        ):
            print(
                f"Loaded {index}/{len(chunk_files)} "
                "chunks into RAM",
                flush=True,
            )

    print()
    print(
        "Sorting final array and writing unique hashes...",
        flush=True,
    )

    n_unique = sort_unique_to_file(
        x,
        out_file,
    )

    del x

    return n_unique


# ============================================================
# Lower-RAM fallback merge mode
# ============================================================

def merge_chunk_group_task(args):
    group_files, out_file = args

    group_files = [
        Path(file)
        for file in group_files
    ]

    out_file = Path(out_file)

    sizes = [
        file.stat().st_size // 8
        for file in group_files
    ]

    total_items = int(sum(sizes))

    x = np.empty(
        total_items,
        dtype=np.uint64,
    )

    offset = 0

    for file, n_items in zip(
        group_files,
        sizes,
    ):
        arr = np.fromfile(
            file,
            dtype=np.uint64,
        )

        if arr.size != n_items:
            raise RuntimeError(
                f"Size mismatch while reading {file}"
            )

        x[offset:offset + n_items] = arr
        offset += n_items

        del arr

    n_unique = sort_unique_to_file(
        x,
        out_file,
    )

    del x

    return (
        str(out_file),
        [str(file) for file in group_files],
        n_unique,
    )


def merge_all_chunks_rounds(
    chunk_files,
    temp_dir,
    fan_in,
    merge_workers,
):
    temp_dir = Path(temp_dir)
    chunk_files = list(chunk_files)

    if len(chunk_files) == 0:
        return None

    round_id = 0

    while len(chunk_files) > 1:
        round_id += 1

        print()
        print(
            f"Merge round {round_id}: "
            f"{len(chunk_files)} chunks",
            flush=True,
        )

        tasks = []

        for index in range(
            0,
            len(chunk_files),
            fan_in,
        ):
            group = chunk_files[
                index:index + fan_in
            ]

            out_file = (
                temp_dir
                / (
                    f"merge_r{round_id:02d}_"
                    f"{index // fan_in:06d}.u64"
                )
            )

            tasks.append(
                (
                    group,
                    str(out_file),
                )
            )

        new_files = []

        with Pool(
            processes=merge_workers
        ) as pool:
            results = pool.imap_unordered(
                merge_chunk_group_task,
                tasks,
                chunksize=1,
            )

            for (
                out_file,
                old_files,
                n_unique,
            ) in results:

                print(
                    f"Merged -> {out_file} | "
                    f"unique hashes = {n_unique}",
                    flush=True,
                )

                new_files.append(out_file)

                for old_file in old_files:
                    Path(old_file).unlink()

        chunk_files = sorted(new_files)

    return Path(chunk_files[0])


# ============================================================
# Helpers
# ============================================================

def round_robin_split(
    files,
    n_groups,
):
    groups = [
        []
        for _ in range(n_groups)
    ]

    for index, file in enumerate(files):
        groups[index % n_groups].append(
            str(file)
        )

    return groups


def read_final_summary(
    sorted_hashes_file: Path,
):
    file_size = sorted_hashes_file.stat().st_size
    n_hashes = file_size // 8

    if n_hashes > 0:
        mm = np.memmap(
            sorted_hashes_file,
            dtype=np.uint64,
            mode="r",
        )

        min_hash = int(mm[0])
        max_hash = int(mm[-1])

        del mm

    else:
        min_hash = None
        max_hash = None

    log_total = (
        math.log2(n_hashes)
        if n_hashes > 0
        else 0
    )

    bits_needed = (
        math.ceil(log_total)
        if n_hashes > 1
        else 1
    )

    return (
        n_hashes,
        min_hash,
        max_hash,
        log_total,
        bits_needed,
    )


# ============================================================
# Main
# ============================================================

def main():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--sigs_dir",
        default=(
            "/scratch/akn5655/"
            "Logan_metagenome_FMH_sketches_compression/toy"
        ),
        help=(
            "Directory containing .sig files directly. "
            "No batch subdirectories are expected."
        ),
    )

    parser.add_argument(
        "--out_dir",
        default=(
            "/scratch/akn5655/"
            "Logan_metagenome_FMH_sketches_compression/"
            "hash_table_v2"
        ),
        help="Output directory",
    )

    parser.add_argument(
        "--ksize",
        type=int,
        default=31,
        help="Only extract mins from this k-mer size",
    )

    parser.add_argument(
        "--workers",
        type=int,
        default=128,
        help=(
            "Number of parallel workers used to read "
            ".sig files"
        ),
    )

    parser.add_argument(
        "--chunk_hashes",
        type=int,
        default=200_000_000,
        help=(
            "Raw hashes buffered per worker before "
            "writing a unique chunk"
        ),
    )

    parser.add_argument(
        "--final_merge_mode",
        choices=[
            "memory",
            "rounds",
        ],
        default="memory",
        help=(
            "memory = fastest RAM-heavy final merge; "
            "rounds = slower lower-RAM merge"
        ),
    )

    parser.add_argument(
        "--merge_fan_in",
        type=int,
        default=32,
        help=(
            "Number of chunk files merged at a time "
            "in rounds mode"
        ),
    )

    parser.add_argument(
        "--merge_workers",
        type=int,
        default=8,
        help=(
            "Number of parallel workers used for "
            "merge rounds"
        ),
    )

    parser.add_argument(
        "--progress_every",
        type=int,
        default=5000,
        help=(
            "Print worker progress every N files. "
            "Use 0 to disable."
        ),
    )

    parser.add_argument(
        "--keep_temp",
        action="store_true",
        help="Keep temporary chunk files",
    )

    args = parser.parse_args()

    sigs_dir = Path(args.sigs_dir)
    out_dir = Path(args.out_dir)

    if not sigs_dir.exists():
        raise RuntimeError(
            f"Sigs directory does not exist: {sigs_dir}"
        )

    if not sigs_dir.is_dir():
        raise RuntimeError(
            f"Sigs path is not a directory: {sigs_dir}"
        )

    out_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    temp_dir = out_dir / "tmp_chunks"

    if temp_dir.exists():
        shutil.rmtree(temp_dir)

    temp_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    # Find .sig files directly inside sigs_dir.
    sig_files = sorted(
        sigs_dir.glob("*.sig")
    )

    print(f"Sigs dir: {sigs_dir}")
    print(f"Output dir: {out_dir}")
    print(f"Found .sig files: {len(sig_files)}")
    print(f"Target ksize: {args.ksize}")
    print(f"Read workers: {args.workers}")
    print(
        "Chunk hashes per worker: "
        f"{args.chunk_hashes}"
    )
    print(
        "Final merge mode: "
        f"{args.final_merge_mode}"
    )
    print(
        "Merge fan-in: "
        f"{args.merge_fan_in}"
    )
    print(
        "Merge workers: "
        f"{args.merge_workers}"
    )
    print()

    if len(sig_files) == 0:
        raise RuntimeError(
            "No .sig files found directly inside: "
            f"{sigs_dir}"
        )

    n_workers = min(
        args.workers,
        len(sig_files),
    )

    groups = round_robin_split(
        sig_files,
        n_workers,
    )

    worker_tasks = [
        (
            worker_id,
            group,
            args.ksize,
            args.chunk_hashes,
            str(temp_dir),
            args.progress_every,
        )
        for worker_id, group in enumerate(groups)
        if len(group) > 0
    ]

    all_chunk_files = []

    total_raw_mins = 0
    total_processed = 0

    print("Starting extraction...")
    print()

    with Pool(processes=n_workers) as pool:
        results = pool.imap_unordered(
            process_worker_group,
            worker_tasks,
            chunksize=1,
        )

        for result in results:
            worker_id = result["worker_id"]

            print()
            print(
                f"Worker {worker_id} finished | "
                f"processed = {result['processed']} | "
                f"raw mins = "
                f"{result['total_raw_mins']} | "
                f"chunks = "
                f"{len(result['chunk_files'])}",
                flush=True,
            )

            total_processed += result["processed"]

            total_raw_mins += result[
                "total_raw_mins"
            ]

            all_chunk_files.extend(
                result["chunk_files"]
            )

    print()
    print(
        "Total processed sig files: "
        f"{total_processed}"
    )
    print(
        "Total raw mins before dedup: "
        f"{total_raw_mins}"
    )
    print(
        "Initial unique chunk files: "
        f"{len(all_chunk_files)}"
    )

    sorted_hashes_file = (
        out_dir
        / "sorted_union_hashes.u64"
    )

    if len(all_chunk_files) == 0:
        sorted_hashes_file.write_bytes(b"")

    elif args.final_merge_mode == "memory":
        n_unique = final_merge_in_memory(
            all_chunk_files,
            sorted_hashes_file,
        )

        print()
        print(
            "Final unique hashes after RAM merge: "
            f"{n_unique}"
        )

        for old_file in all_chunk_files:
            Path(old_file).unlink()

    else:
        final_chunk = merge_all_chunks_rounds(
            all_chunk_files,
            temp_dir,
            args.merge_fan_in,
            args.merge_workers,
        )

        shutil.move(
            final_chunk,
            sorted_hashes_file,
        )

    (
        n_hashes,
        min_hash,
        max_hash,
        log_total,
        bits_needed,
    ) = read_final_summary(
        sorted_hashes_file
    )

    summary_file = (
        out_dir
        / "hash_summary.txt"
    )

    with open(summary_file, "w") as output:
        output.write(
            f"sigs_dir\t{sigs_dir}\n"
        )

        output.write(
            f"total_sig_files\t{len(sig_files)}\n"
        )

        output.write(
            "total_raw_mins_before_dedup\t"
            f"{total_raw_mins}\n"
        )

        output.write(
            f"min_hash\t{min_hash}\n"
        )

        output.write(
            f"max_hash\t{max_hash}\n"
        )

        output.write(
            "total_number_of_unique_hashes\t"
            f"{n_hashes}\n"
        )

        output.write(
            f"log2_total_hashes\t{log_total}\n"
        )

        output.write(
            f"bits_needed\t{bits_needed}\n"
        )

    if not args.keep_temp:
        shutil.rmtree(temp_dir)

    print()
    print("Done.")
    print(
        "Sorted union saved to: "
        f"{sorted_hashes_file}"
    )
    print(
        "Summary saved to: "
        f"{summary_file}"
    )
    print()
    print(
        "Total raw mins before dedup: "
        f"{total_raw_mins}"
    )
    print(
        f"Total unique hashes: {n_hashes}"
    )
    print(
        f"Bits needed: {bits_needed}"
    )


if __name__ == "__main__":
    main()
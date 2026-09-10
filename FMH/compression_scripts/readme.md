# FracMinHash Compression Pipeline

This pipeline compresses a collection of sourmash FracMinHash `.sig` files into a versioned `.tar.xz` archive. Before running the compression pipeline, the global sorted union of all hash values must first be generated from the original signature files.

The complete workflow is:

1. Build the global sorted union hash table from all original `.sig` files.
2. Compress the global hash table.
3. Extract MinHash values and metadata from the original `.sig` files.
4. Convert each hash value to its dense integer ID in the global hash table.
5. Elias–Fano encode the dense IDs.
6. Package the encoded signatures, compressed hash table, metadata, and archive version into one `.tar.xz` archive.
---

# Requirements

This pipeline has the same requirements as the decompression pipeline, listed in the [FMH README](../README.md#requirements): Linux, Bash, `tar`, `xz`, a C++17 compiler, and Python 3 available on the `PATH` as `python` with NumPy installed. The required Python packages are listed in [requirements.txt](../requirements.txt).

---

# Step 1: Build the Global Sorted Hash Table

Go to compression directory.

```bash
cd compression_scripts
```

Before running `compression_pipeline.sh`, run the global hash-table construction script `make_union_ht_v2.py`.

```bash
python make_union_ht_v2.py --sigs_dir ../toy --out_dir hash_table
```

The script reads the MinHash values from all original `.sig` files, combines them, sorts them, removes duplicate hashes, and creates the global sorted union.

## Hash-Table Construction Arguments

### `--sigs_dir`

Directory containing the original `.sig` files directly.

### `--out_dir`

Directory where the global sorted hash table and summary are written.

The script generates:

```text
./hash_table/sorted_union_hashes.u64
./hash_table/hash_summary.txt
```
These two files are required by the compression pipeline.

---

# Step 2: Run the Compression Pipeline

After the global hash table has been successfully created, run the compression pipeline.

Make the script executable:

```bash
chmod +x compression_pipeline.sh
```

Run the compression pipeline using:

```bash
./compression_pipeline.sh <sketch_directory> <output_directory> <archive_name> [threads]
```

### Arguments

| Argument           | Description                                                                                   |
| ------------------ | --------------------------------------------------------------------------------------------- |
| `sketch_directory` | Directory containing the input FracMinHash `.sig` files.                                      |
| `output_directory` | Directory where the compressed archive and compression summary will be saved.                 |
| `archive_name`     | Name of the compressed archive. If `.tar.xz` is not provided, it will be added automatically. |
| `threads`          | Optional number of threads to use. Default: `128`.                                            |

### Example

Using the default number of threads (`128`):

```bash
./compression_pipeline.sh ../toy . compressed_hashes
```

Using a custom number of threads:

```bash
./compression_pipeline.sh ../toy . compressed_hashes 256
```

---

# Compression Outputs

## Final Archive

The compressed archive is saved in the specified output directory:

```text
<output_directory>/<archive_name>.tar.xz
```

For the example above:

```text
./compressed_hashes.tar.xz
```

## Compression Summary

The compression summary is printed to the terminal and written to:

```text
<output_directory>/compression_summary.txt
```

For the example above:

```text
./compression_summary.txt
```

---




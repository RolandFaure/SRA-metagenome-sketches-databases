# Unit Testing Reconstructed Sourmash Signatures

This script verifies that reconstructed sourmash `.sig` files contain the same information as the original `.sig` files.
The script can either:
- Test a sample containing the same number of largest, smallest, and random signature files.
- Test every original signature file.

The comparison ignores differences that do not affect signature content, including:
- JSON dictionary key order
- Hash ordering inside the `mins` field
- Top-level signature-record order
- Record order inside the `signatures` field

All actual metadata values, hash values, and abundance values are still compared. The script supports both plain-text and gzip-compressed sourmash signature files.

---
## Requirements

Bash and Python 3, available on the `PATH` as `python3`. The script uses only the Python standard library, so no additional packages are required.

---
## Usage

### Sample mode

```bash

./unit_test.sh --sample N ORIGINAL_SIG_DIR DECOMP_DIR [--threads THREADS]

```
### Test all signature files

```bash

./unit_test.sh --all ORIGINAL_SIG_DIR DECOMP_DIR [--threads THREADS]

```
### Show the help message

```bash

./unit_test.sh --help

```
---

## Command-Line Arguments

### `--sample N`
Runs the unit test on three groups of original signatures:
- The `N` largest `.sig` files by size
- The `N` smallest `.sig` files by size
- `N` randomly selected `.sig` files

The random files are selected after excluding the largest and smallest groups.

### `--all`
Tests every original `.sig` file.
Each original file is matched with a reconstructed file having the same basename.

### `ORIGINAL_SIG_DIR`
Directory containing the original sourmash `.sig` files.
The directory is searched recursively. Therefore, the original files may be stored directly in the directory or inside subdirectories.

### `DECOMP_DIR`
Directory containing the reconstructed or decompressed `.sig` files.
This directory is also searched recursively.

### `--threads THREADS`
Optional argument specifying the number of comparisons to run in parallel. The default value is 256.

---
## Examples
Before running the script for the first time, make it executable:

```bash
cd test
chmod +x unit_test.sh

```
### Test 5 largest, 5 smallest, and 5 random files

```bash

./unit_test.sh --sample 5 ../toy ../decompressed_sigs

```
### Test all signature files 
```bash

./unit_test.sh --all ../toy ../decompressed_sigs

```




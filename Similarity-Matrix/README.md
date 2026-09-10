# Similarity Matrix

This folder contains code for constructing similarity matrices from genomic data using hypervector sketches. These matrices support efficient similarity estimation and large-scale comparison of metagenomic datasets.

## Downloading the Similarity Matrix

To download the full similarity matrix (842 GB compressed):
```bash
wget https://g-bb0f1.ffdaa9.e229.data.globus.org/matrix.tar.zst
```

The filtered similarity matrix (64 GB compressed, 90 GB extracted) is a smaller alternative. For each accession, it retains all neighbors with Jaccard similarity `>= 0.2`, as well as the closest remaining neighbors, within the limit of `10,000` neighbors per accession. Accessions with fewer neighbors than this limit retain all of them, and in practice no pair with a similarity below approximately 0.05 is stored. It is available from this [ScholarSphere resource](https://scholarsphere.psu.edu/resources/294e84a2-3d39-4965-8e1b-bd147166a4ed), which provides `matrix.tar.zst` through a browser, or directly with:

```bash
wget https://g-3887d.ffdaa9.e229.data.globus.org/matrix.tar.zst
```

To decompress the downloaded file, use the `decompress.sh` script provided in `src` folder. It requires [zstd](https://github.com/facebook/zstd) and `tar` to be available on the `PATH`.
```bash
./src/decompress.sh matrix.tar.zst matrix_decompressed/
```

Decompression produces two folders, corresponding to the two folder arguments expected by the `query` executable:

```text
matrix_decompressed/
    ├── db/       # pass this to --db
    └── matrix/   # pass this to --matrix
```

## Installation Guide

Follow these steps to set up the necessary environment and build the executables.

### Setup

You can use conda to install the dependencies. `cxx-compiler` provides the C++17 toolchain and OpenMP support, `zlib` and `hdf5` are linked by the executables, `zstd` is required by `src/decompress.sh`, and `h5py` is required only for reading `.h5` query output in Python.

```shell
conda create -n mgs python=3.12
conda activate mgs
conda install -c conda-forge cxx-compiler make cmake zlib zstd hdf5 h5py
```

All remaining dependencies, including Eigen, cnpy, HighFive, and the bits library, are included in this folder, so no submodules need to be initialized.

### Build the Executables

Clone the repository, enter this folder, then create a build folder and compile the C++ code using CMake. This step generates all necessary executables inside the build folder.

```Shell
git clone https://github.com/RolandFaure/SRA-metagenome-sketches-databases.git
cd SRA-metagenome-sketches-databases/Similarity-Matrix
mkdir build
cd build
cmake ..      
make -j 8
```

## Usage Examples

The following examples use the example data inside the `test/` folder. All compiled executables are located inside the `build` folder.

> **Tip:** Running any executable without arguments displays the complete command-line help.

### Query the Pairwise Matrix

The `query` executable allows you to query the similarity matrix.

```Shell
Query Pairwise Comparison Matrix

Usage:
        ./build/query --matrix <folder> --db <folder> [--query_file <file>] [--top <int>] [--thread
                      <int>] [--batch_size <int>] [--write_to_file <file>] [--show_all] [--print]
                      [--help]

        ./build/query --matrix <folder> --db <folder> [--query_ids <ids>...] [--top <int>] [--thread
                      <int>] [--batch_size <int>] [--write_to_file <file>] [--show_all] [--print]
                      [--help]

        ./build/query --matrix <folder> --db <folder> [--row_file <row> [--col_file] <col>] [--top
                      <int>] [--thread <int>] [--batch_size <int>] [--write_to_file <file>]
                      [--show_all] [--print] [--help]

        ./build/query --matrix <folder> --db <folder> [--nf <uint64_t> <double> [--out] <folder>]
                      [--top <int>] [--thread <int>] [--batch_size <int>] [--write_to_file <file>]
                      [--show_all] [--print] [--help]

Options:
  --matrix        : Folder containing the pairwise matrix files [Required]
  --db            : Folder containing the matrix metadata [Required]
  --query_file    : File containing query IDs (one per line)
  --query_ids     : Query IDs as command line arguments (identifiers separated by space)
  --row_file      : File containing query row IDs (one per line)
  --col_file      : File containing query col IDs (one per line)
  --nf            : Filter matrix to include at least top N neighbors and all neighbors with Jaccard >= J
  --out           : Output folder for the filtered matrix
  --top           : Number of top Jaccard values to show [default 10]
  --batch_size    : Number of queries to process per batch [default 1000]
  --thread        : Number of threads to use [default 1]
  --write_to_file : Where to save the output. Expected format: 
                    - *.csv/*tsv/*txt for regular query.
                    - *.csv/*.tsv/*.npy/*npz/*h5 for row-col query.
  --show_all      : Whether to show all neighbors instead of top N
  --print         : Whether to print the outputs to screen
  --help          : Show this help message
```

<!-- > **To query from all accessions inside the server, use `--matrix /scratch/mgs_project/matrix/ --db /scratch/mgs_project/db/`** -->

Inside the `test` folder, there are three example query files (`query_samples.txt`, `row_samples.txt`, and `col_samples.txt`) that will be used for the following examples.
The example matrix folder is `test/toy_matrix`, and the example metadata folder is `test/toy_db`.

The commands below are intended to be run from inside the `test` folder:

```Shell
cd ../test
```

Three different kinds of queries are supported:

#### Regular Query (Nearest Neighbors)

Query the example matrix, `toy_matrix`, for neighbors of specific IDs listed in a file (`query_samples.txt`):

```Shell
../build/query --matrix toy_matrix --db toy_db/ --query_file query_samples.txt --write_to_file toy_neighbors.txt --batch_size 5 --thread 2 --show_all
```

This command outputs one file per query ID (e.g., `DRR000821_toy_neighbors.txt`) containing all neighbors, as `--show_all` is specified.

#### Sliced Matrix Query (Sub-matrix)

Create a slice of the matrix (a sub-matrix) from specified IDs in a row file (`row_samples.txt`) and a column file (`col_samples.txt`):

```Shell
../build/query --matrix toy_matrix --db toy_db/  --row_file row_samples.txt --col_file col_samples.txt --write_to_file row_col.h5 --batch_size 5 --thread 2
```

Here, use `*.h5` ([HDF5](https://www.hdfgroup.org/solutions/hdf5/)) as the output format to get the most compressed output. This format can be accessed conveniently in Python using the [h5py](https://docs.h5py.org/en/stable/) library.

#### Filter Matrix

This option (`--nf`) keeps at least top N neighbors, and for the remaining neighbors, filters everyone below a threshold from [0,1]. Then, writes the corresponding matrix to a new location. In the following example, we keep at least 20 neighbors for each accession, and for the remaining neighbors, filter everyone below a Jaccard estimate of 0.2. We write the filtered matrix to `filtered_toy_matrix` directory:

```Shell
../build/query --matrix toy_matrix --db toy_db/ --nf 20 0.2 --out filtered_toy_matrix --thread 2
```

> **Note**: Batches are executed in parallel, up to the configured number of threads. Within each batch, queries are processed sequentially. The write phase for sliced queries is also performed sequentially.

```txt
Important Output Format Note:
    Regular Query: Output file must be *.csv, *.tsv, or *.txt.

    Sliced (Row-Col) Query: Output file must be *.csv, *.tsv, *.npy, *npz or *h5. *h5 gives the most compressed output.
```

### Build the Vector Database

We also provide executables to generate the similarity matrix. To do that, first, use `project_everything` to create projected vectors from FracMinHash data.

```shell
Project FracMinHash Signatures to Vectors
Usage:
  Convert mode:
    ./project_everything convert <signature_folder> <hash_file> [-t threads]
      signature_folder : Path to folder containing signature files
      hash_file        : Output hash file path
      -t, --threads    : Number of threads (default: 1)

  Sketch mode:
    ./project_everything sketch <hash_file> <db_folder> [-t threads] [-d dimension] 
      hash_file        : Input hash file path
      db_folder        : Output folder for generated vector and auxiliary files
      -t, --threads    : Number of threads (default: 1)
      -d, --dimension  : Vector dimension (default: 2048)
  Convert & Sketch mode:
    ./project_everything build <signature_folder> <db_folder> [-t threads] [-d dimension]
      signature_folder : Path to folder containing signature files
      db_folder        : Output folder for generated vector and auxiliary files
      -t, --threads    : Number of threads (default: 1)
      -d, --dimension  : Vector dimension (default: 2048)
```

For example, from the `test` folder, to create and store vectors from the FracMinHash signature files inside the `toy/` folder to a new folder (`my_toy_db/`):

```shell
../build/project_everything build toy my_toy_db/ -t 8 -d 2048
```

Note that the output is written to a new folder rather than to `toy_db/`, which is included in the repository and used by the query examples above.

### Compute Pairwise Comparison Matrix

The `construct_matrix` executable computes the similarity matrix among all vectors using the folder created by the `project_everything` executable.

```shell
Create Pairwise Comparison Matrix

Usage:
        ./construct_matrix --db <folder> --output_folder <folder> [--num_shards <int>]
                           [--max_memory_gb <float>] [--num_threads <int>] [--help]

Options:
  --db              Folder containing the matrix metadata [Required]
  --output_folder   Folder where to store the matrix [Required]
  --num_shards      Number of shards to use [default 1]
  --max_memory_gb   Max memory to be used per thread [default 1 GB]
  --num_threads     Number of threads to use [default 1]
  --help            Show this help message
```

For example, using the vector data inside the `my_toy_db/` folder constructed in the previous step, one can create a similarity matrix inside a `my_toy_matrix` folder using:

```Shell
../build/construct_matrix --db my_toy_db/ --output_folder my_toy_matrix/ --num_threads 2 --num_shards 2  --max_memory_gb 12 
```

The resulting matrix can then be queried in the same way as the included example, using `--matrix my_toy_matrix --db my_toy_db/`.

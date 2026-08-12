# All-vs-all Similarity Matrix

This page explains how to download and manipulate the all-vs-all similarity matrix for all 4.8 million metagenomic datasets in Logan v1, including all datasets released on the SRA before December 2023. Similarity is defined as the Jaccard index between the 31-mers of each dataset's assembly. All similarity values above 0.2 are included in the matrix, as well as all similarity values above 0.05 within the limits of 10,000 neighbors per accession.

More information about the construction of the similarity matrix can be found in the [metagenome_vector_sketches](https://github.com/RolandFaure/metagenome_vector_sketches) repository.

## Downloading the Similarity Matrix
The similarity matrix is available here: [TODO: Add link to the database]

## Manipulating the Similarity Matrix

### 🛠️ Installation Guide [TODO: all paths have changed]

Follow these steps to set up the necessary environment and build the executables.

#### Setting up the Repository

Clone the repository and its submodules recursively:

```Shell
git clone --recursive https://github.com/RolandFaure/metagenome_vector_sketches.git
cd metagenome_vector_sketches
git submodule update --init --recursive
```

You can use conda to install the dependencies:

```shell
conda create -n mgs python=3.12
conda activate mgs
conda install -c conda-forge hdf5 h5py cmake
```

#### Build the Executables

Create a build folder, and compile the C++ code using cmake. This step generates all necessary executables inside the build folder.

```Shell
mkdir build
cd build
cmake ..      
make -j 8
```

### Query the Pairwise Matrix

The `query_pc_mat` executable allows you to query the computed similarity matrix.

```Shell
Query Pairwise Comparison Matrix

Usage:
        ./query_pc_mat --matrix <folder> --db <folder> [--query_file <file>] [--top <int>] [--thread
                       <int>] [--batch_size <int>] [--write_to_file <file>] [--show_all] [--print]
                       [--help]

        ./query_pc_mat --matrix <folder> --db <folder> [--query_ids <ids>...] [--top <int>]
                       [--thread <int>] [--batch_size <int>] [--write_to_file <file>] [--show_all]
                       [--print] [--help]

        ./query_pc_mat --matrix <folder> --db <folder> [--row_file <row> [--col_file] <col>] [--top
                       <int>] [--thread <int>] [--batch_size <int>] [--write_to_file <file>]
                       [--show_all] [--print] [--help]

        ./query_pc_mat --matrix <folder> --db <folder> [--filter <double> [--out] <folder>] [--top
                       <int>] [--thread <int>] [--batch_size <int>] [--write_to_file <file>]
                       [--show_all] [--print] [--help]

Options:
  --matrix        : Folder containing the pairwise matrix files [Required]
  --db            : Folder containing the matrix meta data [Required]
  --query_file    : File containing query IDs (one per line)
  --query_ids     : Query IDs as command line arguments (identifiers separated by space)
  --row_file      : File containing query row IDs (one per line)
  --col_file      : File containing query col IDs (one per line)
  --filter        : Filter values below threshold from matrix
  --out           : Output folder for the filtered matrix
  --top           : Number of top jaccard values to show [default 10]
  --batch_size    : Number of queries to process per batch [default 1000]
  --thread        : Number of threads to use [default 1]
  --write_to_file : Where to save the output. Expected format: 
                    - *.csv/*tsv/*txt for regular query.
                    - *.csv/*.tsv/*.npy/*npz/*h5 for row-col query.
  --show_all      : Whether to show all neighbors instead of top N
  --print         : Whether to print the outputs to screen
  --help          : Show this help message

```

> **Note**: Batches are executed in parallel, up to the configured number of threads. Within each batch, queries are processed sequentially. The write phase for sliced queries is also performed sequentially.


Inside the `test` folder, there are three example query files (`query_samples.txt`, `row_samples.txt` and `col_samples.txt`) that will be used for the following examples.
Three different kinds of queries are supported:

#### Regular Query (Nearest Neighbors)

Query the constructed matrix, `toy_matrix` for neighbors of specific IDs listed in a file (`query_samples.txt`):

```Shell
../build/query_pc_mat --matrix toy_matrix --db toy_db/ --query_file query_samples.txt --write_to_file toy_neighbors.txt --batch_size 5 --thread 2 --show_all
```

This command outputs one file per query ID (e.g., `DRR000821_toy_neighbors.txt`) containing all neighbors, as `--show_all` is specified.

#### Sliced Matrix Query (Sub-matrix)

Create a slice of the matrix (a sub-matrix) from specificed IDs in a row file (`row_samples.txt`) and a column file (`col_samples.txt`):

```Shell
../build/query_pc_mat --matrix toy_matrix --db toy_db/  --row_file row_samples.txt --col_file col_samples.txt --write_to_file row_col.h5 --batch_size 5 --thread 2
```

Here, use `*.h5` ([HDF5](https://www.hdfgroup.org/solutions/hdf5/)) as the output format to get the most compressed output. This format can be accessed conveniently in Python using the [h5py](https://docs.h5py.org/en/stable/) library.

#### Filter Matrix

Filter all accessions below a threshold from [0, 1] and write the corresponding matrix to a new location:

```Shell
../build/query_pc_mat --matrix toy_matrix --db toy_db/ --filter 0.2 --out filtered_toy_matrix --thread 2
```

```
Important Output Format Note:
    Regular Query: Output file must be *.csv, *.tsv, or *.txt.

    Sliced (Row-Col) Query: Output file must be *.csv, *.tsv, *.npy, *npz or *h5. *h5 gives the most compressed output.
```

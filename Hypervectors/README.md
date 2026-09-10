# SRA metagenomes hypervectors

This repository provides the database containing the hypervector sketches of all metagenomic accession of the SRA before December 2023, and the code needed to interact with it. You can directly interact with the database through the [similarity.logan-search.org](https://similarity.logan-search.org) website or you can download it locally (8.9 GB compressed, 31 GB extracted). The fundamental property of hypervectors is that the dot product between the hypervectors of datasets A and B, once each vector has been divided by `sqrt(2048)` as described below, is on average the number of hashes shared by their FracMinHash sketches. Since the sketches retain one 31-mer in every thousand, multiplying this dot product by 1000 estimates the size of the intersection of the two full 31-mer sets.

This repository contains scripts and tutorials for interacting with the database. Specifically, it includes:
- [Instructions to download the hypervector database locally](#database-download)
- [Scripts to sketch your own datasets](#creating-hypervectors-from-custom-datasets)
- [Tools to compare locally sketched datasets against all SRA datasets](#comparing-datasets-against-the-database)

## Database Download

The database is available on AWS. Download and extract it into a dedicated folder, which is the folder expected by the `--db` argument of the `query` tool described below.

```bash
wget https://s3.amazonaws.com/logan-pub/paper/fracminhash/hypervector_db.tar.xz
mkdir hypervector_db
tar -xJf hypervector_db.tar.xz -C hypervector_db
```

The download is 8.9 GB and expands to 31 GB once extracted. The same data is also deposited on [ScholarSphere](https://scholarsphere.psu.edu/resources/294e84a2-3d39-4965-8e1b-bd147166a4ed) as `vector.tar.zst`. ScholarSphere provides downloads through a browser rather than through `wget`.

### Database Contents

In the database, you will find five files:

- **dimension.txt** — Contains a single number representing the dimension of the hypervectors (should be 2048)
- **metadata.tsv** — A TSV file containing the list of all accessions with their corresponding metadata, one row per accession, in the same order as `vectors.bin`. The fields are documented in the [Metadata folder](../Metadata/)
- **vectors.bin** — Contains all the hypervectors in byte format. Each vector is a concatenation of 2048 int32 values. Each value is stored as an int for convenience but should be divided by sqrt(2048) when used. The file is the concatenation of the hypervectors in the order described in metadata.tsv
- **vector_norms.txt** — A fixed-width file containing one line per accession, giving the accession and the norm of its hypervector, in the same order as `vectors.bin`. This file is used by `query` to associate each hit with its accession
- **all_errors_confidence_interval_95.txt** — A file containing precomputed 95% confidence intervals

## Creating hypervectors from custom datasets

### Installation

Building requires `make` and a C++17 compiler with OpenMP support (`g++` 9 or newer). Install the remaining dependencies, for example with conda:

```bash
conda create -n logan-hypervectors
conda activate logan-hypervectors
conda install main::pkg-config conda-forge::sourmash-minimal conda-forge::eigen
```

`sourmash` is required at run time as well as at installation time, since `dna_to_vector` calls it to sketch the input. The environment should therefore remain active when running the tools.

Clone the repository and build the project:

```bash
git clone https://github.com/RolandFaure/SRA-metagenome-sketches-databases.git
cd SRA-metagenome-sketches-databases/Hypervectors
make
```

Executables will be created in the `SRA-metagenome-sketches-databases/Hypervectors/bin/` directory. Check the installation by running:

```bash
./bin/dna_to_vector --help
```

### Creating Hypervectors of Your Datasets
The input file can be a fasta or fastq file, optionally gzipped. The output is a binary file containing the hypervector of your dataset.

```bash
./bin/dna_to_vector <input.fa/fq[.gz]> <output.bin>
```

#### Options

- `-m, --mode <M>` — Processing mode: `reads` or `assembly` (default: `assembly`)
  - `assembly`: Use all k-mers from the dataset
  - `reads`: Keep only k-mers with abundance ≥ 2 (filters out single-occurrence k-mers)
- `-d, --dim <N>` — Target dimension for random projection (default: 2048)
- `-k <N>` — K-mer size for sourmash (default: 31)
- `-s <N>` — Scaled factor for sourmash (default: 1000)
- `-h, --help` — Show help message

**Note:** Do not change `-d`, `-k`, or `-s` if you want to interact with the downloadable database.

#### Modes Explained

- **assembly**: Uses all k-mers detected in the input file.
- **reads**: Filters k-mers by abundance, keeping only those seen 2 or more times. This is useful for sequencing reads, which often contain errors that manifest as unique k-mers (seen only once). Filtering out these singletons reduces noise.

#### Output Format

The `.bin` file contains a byte-packed vector where each value is stored as an int32. Values should be divided by sqrt(d) before local usage or uploaded as such on [similarity.logan-search.org](https://similarity.logan-search.org) and in other commands of this tool suite.

### Comparing Datasets Against the Database

To find SRA accessions with the highest Jaccard similarity to your dataset(s):

1. Sketch your dataset(s) using the script [above](#creating-hypervectors-of-your-datasets)
2. If you have multiple hypervectors, concatenate them into a single bin file
3. Upload the sketch to [similarity.logan-search.org](https://similarity.logan-search.org) or run the `query` script against the local database:

```bash
./bin/query --query <file> --db <folder> --output <file> \
            [--num_threads <int>] [--top_k <int>] [--help]
```

Here, `--db` is the folder into which `hypervector_db.tar.xz` was extracted, not the folder containing the executables.

This returns a TSV file with the top `k` most similar accessions in the SRA (up to 2023) ranked by Jaccard similarity. The three columns are:
- Index of your query (if multiple hypervectors were queried)
- Accession hit
- Jaccard similarity

## Tutorial

Let's find the most similar SRA datasets to DRR018843. In practice, this will work with any dataset.
First, download the DRR018843 reads with the sra-tools `fastq-dump` command:

```bash
conda install bioconda::sra-tools #if not already installed
fastq-dump DRR018843
#or go on https://trace.ncbi.nlm.nih.gov/Traces/?view=run_browser&acc=DRR018843 and manually download
```

Then, sketch the dataset to create a hypervector. Use the `reads` mode since this is a sequencing dataset, in order to filter out single-occurrence k-mers, which likely represent sequencing errors:

```bash
path/to/SRA-metagenome-sketches-databases/Hypervectors/bin/dna_to_vector DRR018843.fastq DRR018843.bin -m reads
```

Finally, query the hypervector against the database to obtain the top 100 most similar SRA datasets. Note the two distinct paths: the executable is located in the cloned repository, while `--db` refers to the folder into which the downloaded archive was extracted.

```bash
path/to/SRA-metagenome-sketches-databases/Hypervectors/bin/query --query DRR018843.bin --db path/to/hypervector_db --output DRR018843_results.tsv --top_k 100
```

You should obtain a TSV file which looks like this:

```
QueryIndex      Accession       Jaccard_Similarity
0       DRR018843       1
0       DRR018849       0.238232
0       DRR018904       0.199214
0       DRR018911       0.189668
0       DRR018892       0.182519
```

## License

MIT License - See [LICENSE](LICENSE) for details

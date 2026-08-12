# SRA Sketches Database

This repository provides access to and documentation for the sketch databases containing compressed representations of all metagenomic datasets in the SRA up to December 2023, based on [Logan v1](https://github.com/IndexThePlanet/Logan).

## Overview

The Logan project comprises the assemblies of 4.8 million publicly available metagenomic datasets released before December 2023. To enable efficient analysis of the generated unitigs without requiring access to the complete 4.5PB collection, we have generated sketch representations using two complementary techniques. This repository releases all sketches to facilitate dataset exploration and comparison at scale.

The two sketching methods provided are:
- **FracMinHash** – See the [FracMinHashes](#fracminhashes) section
- **Hypervectors** – See the [Hypervectors](#hypervectors) section

Additionally, we provide an all-vs-all similarity matrix for all 4.8 million datasets, where similarity is defined as the Jaccard index between the 31-mer sets of the Logan assemblies. See [all-vs-all similarity matrix](#all-vs-all-similarity-matrix) section.

The metadata associated with all the metagenomes in the database is available here [TODO].

Tutorials on how to use these resources are available here:
[TODO: example use cases with the resources - taxonomic classification (FracMinHash)? Cluster accessions from a BioProject by similarity (similarity matrix and vector database)? Find the biome of a given metagenome (vector database or directly similarity.logan-search.org]

## FracMinHashes

FracMinHash sketches have been generated for all datasets using [sourmash](https://sourmash.readthedocs.io/en/latest/index.html), following the [FracMinHash](https://joss.theoj.org/papers/10.21105/joss.00027) methodology. Each sketch was created by selecting one in every thousand 31-mers using the command: `sourmash sketch dna -p dna,k=31,scaled=1000,abund file_to_sketch.fa -o sketch_file`.

The FracMinHash sketch database is distributed as a compressed archive. See [the dedicated page](FMH/FMH.md) <!-- (https://github.com/adrita1999/Logan_metagenome_FMH_sketches_decompression) --> for instructions on downloading and decompressing the archive into individual `sourmash` `.sig` files.

### Generating Your Own Sketch

To create a FracMinHash sketch for your dataset and compare it against the database, follow these steps:

**Installation (via Conda, recommended):**
```
conda create logan-sketches
conda activate logan-sketches
conda install -y bioconda::sourmash
```

**Sketch Generation:**
```
sourmash sketch dna -p dna,k=31,scaled=1000,abund <file_to_sketch.fa> -o sketch_file
```

[TODO: Document database format and user interaction procedures]

## Hypervectors

Hypervectors represent each dataset as a fixed-dimensional 2048-element vector.

The key property of hypervectors is that the scalar product of two dataset sketches provides an unbiased estimate of the intersection size of their 31-mer sets, scaled in our case by the factor 1000/2048. For a comprehensive understanding of hypervector methodology, limitations, and approximation characteristics, please refer to the our paper [TODO: Add citation to ourselves].

See [hypervector_database](https://github.com/RolandFaure/hypervector_database) for instructions on downloading the hypervector database, the format of its files, and how to query it against your own sketches.

#### Generating Your Own Sketch

To create a hypervector for your dataset and compare it against the database, use the DNA_to_vector tool:

**Installation (via Conda, recommended):**
```
conda create logan-sketches
conda activate logan-sketches
conda install -y bioconda::sourmash
git clone https://github.com/RolandFaure/DNA_to_vector.git
cd DNA_to_vector
make
```

**Sketch Generation:**
```
/path_to_DNA_to_vector/DNA_to_vector/bin/dna_to_vector <input.fa> <output.bin> [options]

Arguments:
  input.fa       Input FASTA file with DNA sequences
  output.bin     Output binary file with projected vectors

Options:
  -m, --mode <M> Processing mode: 'reads' or 'assembly' (default: assembly)
                 - 'assembly': use all k-mers
                 - 'reads': keep only k-mers with abundance >= 2
  -h, --help     Display help message
```

### Compare Your Dataset Against Logan

An online service is available at [similarity.logan-search.org](https://similarity.logan-search.org) for direct comparison of your sketch against all database sketches. The service returns a ranked list of datasets with the highest similarity scores, where similarity is defined by the Jaccard index of the 31-mer sets.

## All-vs-All Similarity Matrix

An all-vs-all similarity matrix has been computed for all 4.8 million datasets in Logan v1, including all datasets released on the SRA before December 2023. Similarity is defined as the Jaccard index between the 31-mers of each dataset's assembly. Only similarity values above 0.05 are included in the matrix.

See [metagenome_vector_sketches](https://github.com/RolandFaure/metagenome_vector_sketches) for instructions on building, storing, and querying the similarity matrix, including its on-disk format.

## Citation

[TODO]

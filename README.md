# SRA Sketches Database

This repository provides access to and documentation for the sketch databases containing compressed representations of all metagenomic datasets in the SRA up to December 2023, based on [Logan v1](https://github.com/IndexThePlanet/Logan).

## Overview

The Logan project comprises the assemblies of 4.8 million publicly available metagenomic datasets released before December 2023. To enable efficient analysis of the generated unitigs without requiring access to the complete 4.5PB collection, we have generated sketch representations using two complementary techniques. This repository releases all sketches to facilitate dataset exploration and comparison at scale.

The two sketching methods provided are:
- **FracMinHash** – See the [FracMinHashes](#fracminhashes) section
- **Hypervectors** – See the [Hypervectors](#hypervectors) section

Additionally, we provide an all-vs-all similarity matrix for all 4.8 million datasets, where similarity is defined as the Jaccard index between the 31-mer sets of the Logan assemblies. See [all-vs-all similarity matrix](#all-vs-all-similarity-matrix) section.

The metadata associated with all the metagenomes in the database is available here [TODO].

## FracMinHashes

FracMinHash sketches have been generated for all datasets using [sourmash](https://sourmash.readthedocs.io/en/latest/index.html), following the [FracMinHash](https://joss.theoj.org/papers/10.21105/joss.00027) methodology. Each sketch was created by selecting one in every thousand 31-mers using the command: `sourmash sketch dna -p dna,k=31,scaled=1000,abund file_to_sketch.fa -o sketch_file`.

The FracMinHash sketch database is distributed as a compressed archive. See [the dedicated folder](FMH/) for instructions on downloading the archive and comparing it against custom datasets.


## Hypervectors

Hypervectors represent each dataset as a fixed-dimensional 2048-element vector.

The key property of hypervectors is that the scalar product of two dataset sketches provides an unbiased estimate of the intersection size of their 31-mer sets, scaled in our case by the factor 1000/2048. For a comprehensive understanding of hypervector methodology, limitations, and approximation characteristics, please refer to the our paper [TODO: Add citation to ourselves].

Hypervectors allow to very efficiently search through all the 4.8 million indexed metagenomes by Jaccard similarity. This can be done either by downloading the hypervector database and querying it locally, or by using the online service at [similarity.logan-search.org](https://similarity.logan-search.org).

See [the dedicated folder](Hypervectors/) for instructions on creating a sketch from your own data, and downloading and manipulating the hypervector database locally.


## All-vs-All Similarity Matrix

An all-vs-all similarity matrix has been computed for all 4.8 million metagenomic datasets. Similarity is defined as the Jaccard index between the 31-mers of each dataset's assembly. All similarity values above 0.2 are included in the matrix, as well as all similarity values above 0.05 within the limits of 10,000 neighbors per accession.

See [the dedicated folder](similarity_matrix/) for instructions on downloading and querying the similarity matrix.

## Citation

[TODO]

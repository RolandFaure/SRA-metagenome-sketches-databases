# SRA Metagenome Sketches Database

This repository provides access to and documentation for the sketch databases containing compressed representations of all metagenomic datasets in the SRA up to December 2023, based on [Logan v1](https://github.com/IndexThePlanet/Logan).

## Overview

The Logan project comprises the assemblies of a December 2023 freeze of the entire public SRA. Of these, 4.8 million accessions are metagenomic datasets, which are the ones covered by this repository. To enable efficient analysis of the generated unitigs without requiring access to the complete 4.5PB collection, we have generated sketch representations using two complementary techniques. This repository releases all sketches to facilitate dataset exploration and comparison at scale.

The two sketching methods provided are:
- **FracMinHash** – See the [FracMinHashes](#fracminhashes) section
- **Hypervectors** – See the [Hypervectors](#hypervectors) section

Additionally, we provide an all-vs-all similarity matrix for all 4.8 million datasets, where similarity is defined as the Jaccard index between the 31-mer sets of the Logan assemblies. See the [all-vs-all similarity matrix](#all-vs-all-similarity-matrix) section.

To download the metadata associated with all the metagenomes in the database - See the [the dedicated folder](Metadata/).

## FracMinHashes

FracMinHash sketches have been generated for all datasets using [sourmash](https://sourmash.readthedocs.io/en/latest/index.html), following the [FracMinHash](https://doi.org/10.1101/2022.01.11.475838) methodology. Each sketch was created by selecting one in every thousand 31-mers using the command: `sourmash sketch dna -p dna,k=31,scaled=1000,abund file_to_sketch.fa -o sketch_file`.

The FracMinHash sketch database is distributed as a compressed archive. See [the dedicated folder](FMH/) for instructions on downloading the archive and reconstructing the original sourmash signature files from it.


## Hypervectors

Hypervectors represent each dataset as a fixed-dimensional 2048-element vector.

The key property of hypervectors is that the scalar product of two dataset sketches provides an unbiased estimate of the intersection size of their 31-mer sets, scaled in our case by the factor 1000/2048. Concretely, if `a` and `b` are the raw stored integer vectors of two datasets, then `(a · b) * 1000 / 2048` estimates the number of 31-mers shared by the two assemblies. Equivalently, once each vector has been divided by `sqrt(2048)` as described in the [Hypervectors folder](Hypervectors/), the scalar product estimates the number of hashes shared by the two FracMinHash sketches, and multiplying by the scale factor 1000 recovers the 31-mer estimate. For a comprehensive understanding of hypervector methodology, limitations, and approximation characteristics, please refer to the paper [TODO: Add citation to ourselves].

 Hypervectors allow users to search efficiently through all 4.8 million indexed metagenomes by Jaccard similarity. This can be done either by downloading the hypervector database and querying it locally, or by using the online service at [similarity.logan-search.org](https://similarity.logan-search.org).

See [the dedicated folder](Hypervectors/) for instructions on creating a sketch from your own data, and downloading and manipulating the hypervector database locally.


## All-vs-All Similarity Matrix

An all-vs-all similarity matrix has been computed for all 4.8 million metagenomic datasets. Similarity is defined as the Jaccard index between the 31-mers of each dataset's assembly.

Two versions are distributed. The full matrix (904 GB compressed) contains every computed pair. The filtered matrix (69 GB compressed) retains, for each accession, all similarity values above 0.2, as well as the closest remaining neighbors, within the limit of 10,000 neighbors per accession. Accessions with fewer neighbors than this limit retain all of them, and in practice the filtered matrix contains no pair with a similarity below approximately 0.05.

See [the dedicated folder](Similarity-Matrix/) for instructions on downloading and querying either version.

## Citation

This work will soon be deposited on biorxiv. In the meantime, if you wish to cite this work, please contact us directly.

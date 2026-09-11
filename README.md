# SRA Metagenome Sketches Database

This repository provides access to and documentation for the sketch databases containing compressed representations of all metagenomic datasets in the SRA up to December 2023, based on [Logan v1](https://github.com/IndexThePlanet/Logan).

## Overview

The Logan project comprises the assemblies of 4.8 million publicly available metagenomic datasets released before December 2023. To enable efficient analysis of the generated unitigs without requiring access to the complete 4.5PB collection, we have generated sketch representations using two complementary techniques. This repository releases all sketches to facilitate dataset exploration and comparison at scale.

The two sketching methods provided are:
- **FracMinHash** – See the [FracMinHashes](#fracminhashes) section
- **Hypervectors** – See the [Hypervectors](#hypervectors) section

Additionally, we provide an all-vs-all similarity matrix for all 4.8 million datasets, where similarity is defined as the Jaccard index between the 31-mer sets of the Logan assemblies. See the [all-vs-all similarity matrix](#all-vs-all-similarity-matrix) section.

To download the metadata associated with all the metagenomes in the database - See the [the dedicated folder](Metadata/).

## FracMinHashes

FracMinHash sketches have been generated for all datasets using [sourmash](https://sourmash.readthedocs.io/en/latest/index.html), following the [FracMinHash](https://joss.theoj.org/papers/10.21105/joss.00027) methodology. Each sketch was created by selecting one in every thousand 31-mers using the command: `sourmash sketch dna -p dna,k=31,scaled=1000,abund file_to_sketch.fa -o sketch_file`.

The FracMinHash sketch database is distributed as a compressed archive. See [the dedicated folder](FMH/) for instructions on downloading the archive and comparing it against custom datasets.


## Hypervectors

Hypervectors represent each dataset as a fixed-dimensional 2048-element vector.

The key property of hypervectors is that the scalar product of two dataset sketches provides an unbiased estimate of the intersection size of their 31-mer sets, scaled in our case by the factor 1000/2048. For a comprehensive understanding of hypervector methodology, limitations, and approximation characteristics, please refer to the paper [TODO: Add citation to ourselves].

 Hypervectors allow users to search efficiently through all 4.8 million indexed metagenomes by Jaccard similarity. This can be done either by downloading the hypervector database and querying it locally, or by using the online service at [similarity.logan-search.org](https://similarity.logan-search.org).

See [the dedicated folder](Hypervectors/) for instructions on creating a sketch from your own data, and downloading and manipulating the hypervector database locally.


## All-vs-All Similarity Matrix

An all-vs-all similarity matrix has been computed for all 4.8 million metagenomic datasets. Similarity is defined as the Jaccard index between the 31-mers of each dataset's assembly. All similarity values above 0.2 are included in the matrix, as well as all similarity values above 0.05 within the limits of 10,000 neighbors per accession.

See [the dedicated folder](Similarity-Matrix/) for instructions on downloading and querying the similarity matrix.

## Locations of Artifacts
The hypervector database, the filtered similarity matrix, and the metadata files are hosted through Penn State’s ScholarSphere service and are available at http://doi.org/10.26207/bvdw-mq63. The full similarity matrix and the FMH sketches are available over anonymous HTTPS from a public Globus guest collection. This collection also contains mirrors of other artifacts, for users who prefer to retrieve through traditional Unix interfaces. No Globus account, login, or client installation is required; a single `wget` or `curl` invocation is sufficient, such as:

```bash
wget https://g-3887d.ffdaa9.e229.data.globus.org/matrix.tar.zst
```
The download links for all artifacts are given below.

| Artifact | Size | Download URL |
|---|---:|---|
| FMH hashes | 469 GB | https://g-77cfc.ffdaa9.e229.data.globus.org/compressed_hashes.tar.xz |
| Hypervector database | 9.1 GB | https://g-55efb.ffdaa9.e229.data.globus.org/vector.tar.zst |
| Filtered similarity matrix | 64 GB | https://g-3887d.ffdaa9.e229.data.globus.org/matrix.tar.zst |
| Full similarity matrix | 842 GB | https://g-bb0f1.ffdaa9.e229.data.globus.org/matrix.tar.zst |
| Metadata (Parquet file) | 502 MB | https://g-77cfc.ffdaa9.e229.data.globus.org/metadata/metagenome_metadata.parquet |
| Metadata (CSV file) | 692 MB | https://g-77cfc.ffdaa9.e229.data.globus.org/metadata/metagenome_metadata.csv.gz |

## Citation

This work will soon be deposited on biorxiv. In the meantime, if you wish to cite this work, please contact us directly.

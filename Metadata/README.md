\# Metadata for SRA metagenomes

## Downloading the metadata files

The metadata associated with all the metagenomes in the database is available here: https://scholarsphere.psu.edu/resources/294e84a2-3d39-4965-8e1b-bd147166a4ed.

The metadata is provided in both \*\*CSV\*\* and \*\*Parquet\*\* formats.



\## Metadata fields



| Field | Description |

|---|---|

| `accession` | |

| `field\_2` | |

| `field\_3` | |



\## Example use of the metadata Parquet file



The Parquet file can be queried directly using DuckDB.



\### Install DuckDB



Using Conda:



```bash

conda install -c conda-forge duckdb

```



For example, the following command retrieves all WGS accessions sequenced using the Illumina platform, released after 2022, and annotated as soil metagenomes.



```bash

duckdb -c "

SELECT accession

FROM read\_parquet('metagenome\_metadata.parquet')

WHERE assay\_type = 'WGS'

&#x20; AND platform = 'ILLUMINA'

&#x20; AND CAST(releasedate AS DATE) >= DATE '2023-01-01'

&#x20; AND LOWER(organism) = 'soil metagenome';

"

```


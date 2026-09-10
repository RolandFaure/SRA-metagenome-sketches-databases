# Metadata for SRA metagenomes

## Downloading the metadata files

The metadata associated with all the metagenomes in the database is available here: https://scholarsphere.psu.edu/resources/294e84a2-3d39-4965-8e1b-bd147166a4ed.

The metadata is provided in both **CSV** and **Parquet** formats.

## Metadata fields

| Field | Description |
|---|---|
| `acc` | SRA Run accession in the form of SRR####### (ERR or DRR for INSDC partners) |
| `assay_type` | Type of library (i.e. AMPLICON, RNA-Seq, WGS, etc) |
| `center_name` | Name of the sequencing center |
| `consent` | Type of consent need to access the data (i.e. public is available to all, others are for dbGaP) |
| `experiment` | The accession in the form of SRX####### (ERX or DRX for INSDC partners) |
| `sample_name` | Name of the sample |
| `instrument` | Name of the sequencing platform model |
| `librarylayout` | Whether the data is SINGLE or PAIRED |
| `libraryselection` | Library selection methodology (i.e. PCR, RANDOM, etc) |
| `librarysource` | Source of the biological data (i.e. GENOMIC, METAGENOMIC, etc) |
| `platform` | Name of the sequencing platform (i.e. ILLUMINA) |
| `sample_acc` | SRA Sample accession in the form of SRS####### (ERS or DRS for INSDC partners) |
| `biosample` | BioSample accession in the form of SAMN######## (SAMEA##### or SAMD##### for INSDC partners) |
| `organism` | Scientific name of the organism that was sequenced (as found in the NCBI Taxonomy Browser) |
| `sra_study` | SRA Study accession in the form of SRP######## (ERP or DRP for INSDC partners) |
| `releasedate` | The date on which the data was released |
| `bioproject` | BioProject accession in the form of PRJNA######## (PRJEB######## or PRJDB######## for INSDC partners) |
| `mbytes` | Number of mega bytes of data in the SRA Run |
| `loaddate` | The date when the data was loaded into SRA |
| `avgspotlen` | Calculated average read length |
| `mbases` | Number of mega bases in the SRA Runs |
| `insertsize` | Submitter provided insert size |
| `library_name` | The name of the library |
| `biosamplemodel_sam` | The BioSample package/model that was picked |
| `collection_date_sam` | The collection date of the sample |
| `geo_loc_name_country_calc` | Name of the country where the sample was collected |
| `geo_loc_name_country_continent_calc` | Name of the continent where the sample was collected |
| `geo_loc_name_sam` | Full location of collection |
| `ena_first_public_run` | Date when INSDC partner record was public |
| `ena_last_update_run` | Date when INSDC partner record was updated |
| `sample_name_sam` | INSDC sample name |
| `datastore_filetype` | Type of files available to download from SRA |
| `datastore_provider` | Locations of where the files are available to download from |
| `datastore_region` | Regions of where the data is located |
| `attributes` | Full list of sample attributes in a nested(array) structure |
| `jattr` | JSON based string of the sample attributes |
| `run_file_version` | Version of the SRA Run file |
| `lat_lon` | Location in the globe (latitude, longitude) following the WGS84 standard (4326 in postGIS) and encoded in WKB form |
| `elevation` | Height above mean sea level in meters (negative elevations are present) |
| `country` | Three letter code of the country whose the coordinate belongs to. Empty if no match to any country boundary. |
| `biome` | Major biome code according to WWF TEW (see above) |
| `confidence` | Confidence value, higher is better |

## Example use of the metadata Parquet file

The Parquet file can be queried directly using DuckDB.

DuckDB CLI can be installed using Conda:

```bash
conda install -c conda-forge duckdb-cli
```

For example, the following command retrieves all WGS accessions sequenced using the Illumina platform, released after 2022, and annotated as soil metagenomes.

```bash
duckdb -c "
SELECT acc
FROM read_parquet('metagenome_metadata.parquet')
WHERE assay_type = 'WGS'
  AND platform = 'ILLUMINA'
  AND CAST(releasedate AS DATE) >= DATE '2023-01-01'
  AND LOWER(organism) = 'soil metagenome';
"
```

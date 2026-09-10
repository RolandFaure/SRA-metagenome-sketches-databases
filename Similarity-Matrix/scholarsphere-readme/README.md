### Overview

This archive contains files associated with the following work:

```txt
Sketching the SRA: experiment-wide similarity search across 4.8 million metagenomes

Roland Faure, Adrita Hossain Nakshi, Md. Hasin Abrar, Teo Lemane, Mohsen Taheri, Stephanie Won, Haonan Wu, David Koslicki, and Paul Medvedev
```

| File	| Description  	|
|---	|---	|
| matrix.tar.zst  	|  Filtered matrix, retaining for each accession all neighbors with similarity >= 0.2, as well as the closest remaining neighbors, within the limit of 10,000 neighbors per accession	|
| vector.tar.zst 	|  Hypervector database, the same data as `hypervector_db.tar.xz` distributed on AWS     |
| metagenome_metadata.parquet 	|  Metadata file in parquet format 	|
| metagenome_metadata.csv.gz  	|  Metadata file in CSV format (gzipped) 	|

To extract `*.tar.zst` files, you can use the `decompress.sh` script inside the `Similarity-Matrix/src/` folder in the [SRA Metagenome Sketches Database](https://github.com/RolandFaure/SRA-metagenome-sketches-databases) repository.
For instructions on using the matrix, see the `README` inside the `Similarity-Matrix` folder of the same repository. For instructions on using the hypervectors, see the `README` inside the `Hypervectors` folder.

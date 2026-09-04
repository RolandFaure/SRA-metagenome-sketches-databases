### Overview

This archive contains files associated with the following work:

```txt
Sketching the SRA: experiment-wide similarity search across 4.8 million metagenomes

Roland Faure, Adrita Hossain Nakshi, Md. Hasin Abrar, Teo Lemane, Mohsen Taheri, Stephanie Won, Haonan Wu, David Koslicki, and Paul Medvedev
```

| File	| Description  	|
|---	|---	|
| matrix.tar.zst  	|  Filtered matrix containing at least 10,000 neighbors, and all neighbors with similarity >= 0.2	|
| vector.tar.zst 	|  Hypervector file     |
| metagenome_metadata.parquet 	|  Metadata file in parquet format 	|
| metagenome_metadata.csv.gz  	|  Metadata file in CSV format (gzipped) 	|

To extract `*.tar.zst` files, you can use the `decompress.sh` script inside `Similarity-Matrix/src/` folder in the [SRA Metagenome Sketches Database](https://github.com/RolandFaure/SRA-metagenome-sketches-databases) repository.
For instructions on using the matrix, see the `README` inside the `Similarity-Matrix` folder of the same repository.

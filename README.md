# Logan-datasets-sketches-database
This repository describes how to access and interact with the databases containing the sketches of every dataset contained in [Logan v1](https://github.com/IndexThePlanet/Logan).

## Overview
The Logan project assembled 4.8 million publicly available datasets released before December 2023. These datasets have been sketched using two different techniques. We are releasing here all the sketches, to help scientists manipulate the 4.8 million datasets without having to download the associated 700TB of Logan data.

## FracMinHashes
A [FracMinHash](https://joss.theoj.org/papers/10.21105/joss.00027) of all the datasets has been produced using [sourmash](https://sourmash.readthedocs.io/en/latest/index.html). We selected one in a thousand 31-mers, using command `sourmash sketch dna -p dna,k=31,scaled=1000,abund file_to_sketch.fa -o sketch_file`.

[TODO: explain how to access the database]

### Obtaining your own sketch
To obtain the FracMinHash sketch of your own dataset and compare it to the database you can use sourmash:

Recommended conda installation
```
conda create logan-sketches
conda activate logan-sketches
conda install -y bioconda::sourmash
```
And generation of the sketch:
```
sourmash sketch dna -p dna,k=31,scaled=1000,abund <file_to_sketch.fa> -o sketch_file
```

[TODO: I'm not sure what is the format of our database, explain how the user will be able to interact with it]

## DotHashes
A [DotHash](https://bpb-us-e2.wpmucdn.com/sites.uci.edu/dist/4/4834/files/2023/06/DotHash_KDD23.pdf) sketch of all the datasets have been produced. DotHash is a way to represent each dataset as a 2048-dimensional vectors. Vectors that are close in space are expected to share sequences. The full detail of those sketches is available in our paper [TODO: cite ourselves]. Compared to FracMinHashes, DotHashes sketches may be faster and easier to manipulate, as each dataset is represented by a fixed-dimension vector. The key thing about DotHash vectors is that the scalar product of the sketches of two datasets is an unbiased estimation of the size of the intersection of their 31-mer set scaled by a factor 1000/2048. Please read the paper [TODO: cite ourselves] to understand the limitations and approximation of such sketches.

[TODO: explain how to access the database]


### Obtaining your own sketch
To obtain the Dothash sketch of your own dataset and compare it to the database you can use our DNA_to_vector script:
Recommended conda installation
```
conda create logan-sketches
conda activate logan-sketches
conda install -y bioconda::sourmash
git clone https://github.com/RolandFaure/DNA_to_vector.git
cd DNA_to_vector
make
```
And generation of the sketch:
```
/path_to_DNA_to_vector/DNA_to_vector/bin/dna_to_vector <input.fa> <output.bin>
```
## Compare your dataset against all the datasets of Logan


## Citation
[TODO]

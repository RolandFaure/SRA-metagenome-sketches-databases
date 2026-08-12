#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <Eigen/Dense>
#include <omp.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include "clipp.h"

namespace fs = std::filesystem;
using namespace Eigen;
using namespace std;

using MatrixXll = Eigen::Matrix<int64_t, Eigen::Dynamic, Eigen::Dynamic>;

// Load a block of vectors from binary file
MatrixXll load_vectors_block(const string& file_path, int dimension, int start_idx, int end_idx) {
    ifstream file(file_path, ios::binary);
    if (!file) {
        cerr << "Error opening file: " << file_path << endl;
        return MatrixXll();
    }
    
    uint64_t vector_size = dimension * sizeof(int32_t);
    file.seekg(start_idx * vector_size);
    
    int num_vectors = end_idx - start_idx;
    vector<int32_t> buffer(num_vectors * dimension);
    file.read(reinterpret_cast<char*>(buffer.data()), num_vectors * vector_size);
    file.close();
    
    MatrixXll matrix(dimension, num_vectors);
    for (int i = 0; i < num_vectors; ++i) {
        for (int j = 0; j < dimension; ++j) {
            matrix(j, i) = buffer[i * dimension + j];
        }
    }
    
    return matrix;
}

// Get total number of vectors in a binary file
int get_num_vectors(const string& file_path, int dimension) {
    ifstream file(file_path, ios::ate | ios::binary);
    if (!file) {
        cerr << "Error opening file: " << file_path << endl;
        return 0;
    }
    
    int64_t file_size = file.tellg();
    file.close();
    
    uint64_t vector_size = dimension * sizeof(int32_t);
    return file_size / vector_size;
}

// Struct to hold query result: (database_index, jaccard_similarity)
struct QueryResult {
    int db_index;
    double jaccard;
    
    bool operator<(const QueryResult& other) const {
        return jaccard > other.jaccard; // Descending order
    }
};

// Compute L2 norm squared for each vector (column-wise)
VectorXd compute_norms_squared(const MatrixXll& matrix) {
    VectorXd norms(matrix.cols());
    #pragma omp parallel for
    for (int i = 0; i < matrix.cols(); ++i) {
        norms(i) = matrix.col(i).cast<double>().squaredNorm();
    }
    return norms;
}

// Compute dot products between query vectors and database vectors
// Returns a matrix where result(i, j) = dot product of query_i with db_j
MatrixXll compute_all_dot_products(const MatrixXll& query_vectors, const MatrixXll& db_vectors) {
    return query_vectors.transpose() * db_vectors;
}

// Write results to output file
void write_results(const string& output_file, 
                   vector<vector<QueryResult>>& all_results,
                   int top_k, 
                   const string& vector_norms_file) {

    ofstream out(output_file);
    
    if (!out) {
        cerr << "Error opening output file: " << output_file << endl;
        return;
    }
    out << "QueryIndex\tAccession\tJaccard_Similarity" << endl;

    ifstream vector_norms_stream(vector_norms_file);

    //sort all_results for each query by jaccard similarity in descending order
    for (auto& results : all_results) {
        sort(results.begin(), results.end());
    }
    
    for (size_t query_idx = 0; query_idx < all_results.size(); ++query_idx) {
        
        // Write top-k results
        int count = 0;
        for (const auto& result : all_results[query_idx]) {
            if (count >= top_k) break;
            // get the name of the hit from the vector_norms_file (each line in the file is exaclty 19 chars, so multiply the index by 20 to get the correct position in the file)
            vector_norms_stream.seekg(result.db_index * 20);
            string hit_name;
            getline(vector_norms_stream, hit_name);
            //the hit name is only up to the first space, so we need to trim it
            hit_name = hit_name.substr(0, hit_name.find(' '));
            out << query_idx << "\t" << hit_name << "\t" << result.jaccard << endl;
            count++;
        }
        out << "\n";
    }
    
    out.close();
    cout << "Results written to " << output_file << endl;
}

int main(int argc, char* argv[]) {
    // Argument parsing using clipp
    string query_file, db_folder, output_file;
    int dimension = 2048;
    int num_threads = 1;
    int top_k = 10;
    bool show_help = false;

    auto cli = (
        clipp::required("--query") & clipp::value("file", query_file),
        clipp::required("--db") & clipp::value("folder", db_folder),
        clipp::required("--output") & clipp::value("file", output_file),
        clipp::option("-t", "--num_threads") & clipp::value("int", num_threads),
        clipp::option("--top_k").doc("Number of closest neighbors to return") & clipp::value("int", top_k)
    );

    if (!clipp::parse(argc, argv, cli) || show_help) {
        cout << "Usage:\n"
             << clipp::usage_lines(cli, argv[0]) << endl;
        return show_help ? 0 : 1;
    }

    // Validate files exist
    if (!fs::exists(query_file)) {
        cerr << "Error: Query file not found: " << query_file << endl;
        return 1;
    }
    
    string vector_file = fs::path(db_folder) / "vectors.bin";
    string dimension_file = fs::path(db_folder) / "dimension.txt";
    string norms_file = fs::path(db_folder) / "vector_norms.txt";
    if (!fs::exists(db_folder)) {
        cerr << "Error: Database folder not found: " << db_folder << endl;
        //check that the files vectors.bin, dimension.txt and vector_norms.txt exist in the folder
        if (!fs::exists(fs::path(db_folder) / "vectors.bin")) {
            cerr << "Error: vectors.bin not found in database folder: " << db_folder << endl;
        }
        if (!fs::exists(fs::path(db_folder) / "dimension.txt")) {
            cerr << "Error: dimension.txt not found in database folder: " << db_folder << endl;
        }
        if (!fs::exists(fs::path(db_folder) / "vector_norms.txt")) {
            cerr << "Error: vector_norms.txt not found in database folder: " << db_folder << endl;
        }
        return 1;
    }

    //load dimension from dimension.txt
    ifstream dim_file(dimension_file);
    dim_file >> dimension;
    dim_file.close();

    auto start_time = chrono::high_resolution_clock::now();

    // Get number of vectors
    int num_query_vectors = get_num_vectors(query_file, dimension);
    int num_db_vectors = get_num_vectors(vector_file, dimension);

    cout << "Query vectors: " << num_query_vectors << ", dimension: " << dimension << endl;
    cout << "Database vectors: " << num_db_vectors << ", dimension: " << dimension << endl;

    if (num_query_vectors <= 0 || num_db_vectors <= 0) {
        cerr << "Error: Invalid number of vectors" << endl;
        return 1;
    }

    // Load query vectors once
    cout << "Loading query vectors..." << endl;
    MatrixXll query_vectors = load_vectors_block(query_file, dimension, 0, num_query_vectors);
    
    // Compute query norms squared
    cout << "Computing query norms..." << endl;
    VectorXd query_norms_sq = compute_norms_squared(query_vectors);

    // Print query norms
    cout << "Query norms (squared):" << endl;
    for (int i = 0; i < query_norms_sq.size(); ++i) {
        cout << "  Query " << i << ": " << query_norms_sq(i) << endl;
    }

    // Initialize results storage
    vector<vector<QueryResult>> all_results(num_query_vectors);

    // Process database in chunks of 500k vectors
    const int CHUNK_SIZE = 5000;
    int num_chunks = (num_db_vectors + CHUNK_SIZE - 1) / CHUNK_SIZE;
    cout << "Processing " << num_chunks << " database chunks of " << CHUNK_SIZE << " vectors..." << endl;

    // Reduce inner thread count to avoid oversubscription
    int chunk_threads = max(1, num_threads / num_chunks);
    
    #pragma omp parallel for schedule(dynamic) num_threads(num_threads)
    for (int chunk_idx = 0; chunk_idx < num_chunks; ++chunk_idx) {
        int chunk_start = chunk_idx * CHUNK_SIZE;
        int chunk_end = min(chunk_start + CHUNK_SIZE, num_db_vectors);
        
        cout << "Loading database chunk [" << chunk_start << ", " << chunk_end << ")..." << endl;
        MatrixXll db_chunk = load_vectors_block(vector_file, dimension, chunk_start, chunk_end);
        
        // Compute norms for this chunk
        VectorXd db_norms_sq = compute_norms_squared(db_chunk);
        
        // Compute dot products for this chunk
        cout << "Computing dot products for chunk " << chunk_idx << "..." << endl;
        MatrixXll dot_products = compute_all_dot_products(query_vectors, db_chunk);

        // Process results for this chunk
        cout << "Processing results for chunk " << chunk_idx << "..." << endl;
        #pragma omp parallel for num_threads(chunk_threads)
        for (int query_idx = 0; query_idx < dot_products.rows(); ++query_idx) {
            vector<QueryResult> local_results;
            
            for (int db_idx = 0; db_idx < dot_products.cols(); ++db_idx) {
                double dot_prod = static_cast<double>(dot_products(query_idx, db_idx));
                double union_sum = query_norms_sq(query_idx) + db_norms_sq(db_idx) - dot_prod;
                double jaccard = (union_sum > 0) ? dot_prod / union_sum : 0.0;
                
                // Only keep results with Jaccard >= 0.05
                if (jaccard >= 0.05) {
                    local_results.push_back({chunk_start + db_idx, jaccard});
                }
            }
            
            // Thread-safe append to all_results
            #pragma omp critical
            {
                all_results[query_idx].insert(all_results[query_idx].end(), 
                                            local_results.begin(), local_results.end());
            }
        }
    }

    // Write results
    write_results(output_file, all_results, top_k, norms_file);

    auto end_time = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time);

    cout << "Total computation time: " << duration.count() << " ms" << endl;

    return 0;
}

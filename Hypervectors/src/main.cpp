#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_set>
#include <cstdint>
#include <cstdlib>
#include <filesystem>

#include "random_projection.h"

namespace fs = std::filesystem;

void write_binary_vector(const std::string& filename, const std::vector<int32_t>& vec) {
    std::ofstream outfile(filename, std::ios::binary);
    if (!outfile) {
        throw std::runtime_error("Error opening output file: " + filename);
    }
    
    // Write each element as int32
    for (int i = 0; i < (int)vec.size(); ++i) {
        int32_t val = static_cast<int32_t>(vec[i]);
        outfile.write(reinterpret_cast<const char*>(&val), sizeof(int32_t));
    }
    
    outfile.close();
}

int run_command(const std::string& cmd) {
    return system(cmd.c_str());
}

std::vector<unsigned long int> extract_array_from_json(const std::string& content, const std::string& array_name) {
    std::vector<unsigned long int> result;
    
    // Find the array in JSON
    size_t pos = content.find("\"" + array_name + "\"");
    if (pos == std::string::npos) {
        return result;
    }
    
    // Find the opening bracket
    size_t array_start = content.find('[', pos);
    if (array_start == std::string::npos) {
        return result;
    }
    
    // Find the closing bracket
    size_t array_end = array_start;
    int bracket_count = 1;
    for (size_t i = array_start + 1; i < content.length() && bracket_count > 0; ++i) {
        if (content[i] == '[') bracket_count++;
        else if (content[i] == ']') bracket_count--;
        if (bracket_count == 0) array_end = i;
    }
    
    // Extract the array content
    std::string array_content = content.substr(array_start + 1, array_end - array_start - 1);
    
    // Parse comma-separated numbers
    std::istringstream iss(array_content);
    std::string token;
    while (std::getline(iss, token, ',')) {
        // Trim whitespace and non-numeric characters
        token.erase(0, token.find_first_not_of(" \t\n\r[]{}"));
        token.erase(token.find_last_not_of(" \t\n\r[]{}") + 1);
        
        if (!token.empty()) {
            try {
                unsigned long int val = std::stoul(token);
                result.push_back(val);
            } catch (...) {
                // Skip malformed numbers
            }
        }
    }
    
    return result;
}

std::unordered_set<unsigned long int> extract_hashes_from_sketch(const std::string& sketch_file, const std::string& mode) {
    std::unordered_set<unsigned long int> hashes;
    
    // Parse JSON sketch file to extract mins and abundances arrays
    std::ifstream sketch_json(sketch_file);
    if (!sketch_json) {
        throw std::runtime_error("Failed to read sketch file: " + sketch_file);
    }
    
    // Read entire file
    std::string content((std::istreambuf_iterator<char>(sketch_json)),
                        std::istreambuf_iterator<char>());
    sketch_json.close();
    
    // Extract mins array
    std::vector<unsigned long int> mins = extract_array_from_json(content, "mins");
    if (mins.empty()) {
        // Return empty set if no hashes found (will be warned about in main)
        return hashes;
    }
    
    if (mode == "assembly") {
        // For assembly mode, use all hashes
        for (unsigned long int hash : mins) {
            hashes.insert(hash);
        }
    } else if (mode == "reads") {
        // For reads mode, filter by abundance (>= 2)
        std::vector<unsigned long int> abundances = extract_array_from_json(content, "abundances");
        
        if (abundances.empty()) {
            throw std::runtime_error("No abundances array found in sketch file. Make sure to use sketching mode with abundance tracking.");
        }
        
        if (mins.size() != abundances.size()) {
            throw std::runtime_error("Mismatch between mins and abundances array sizes");
        }
        
        // Keep only hashes with abundance >= 2
        for (size_t i = 0; i < mins.size(); ++i) {
            if (abundances[i] >= 2) {
                hashes.insert(mins[i]);
            }
        }
    } else {
        throw std::runtime_error("Unknown mode: " + mode + ". Use 'reads' or 'assembly'.");
    }
    
    // Return hashes (may be empty if all filtered out in reads mode)
    return hashes;
}

void print_usage(const char* program_name) {
    std::cout << "DNA to Vector Conversion Tool\n" << std::endl;
    std::cout << "Usage: " << program_name << " <input.fa> <output.bin> [options]\n" << std::endl;
    std::cout << "Arguments:\n"
              << "  input.fa       Input FASTA file with DNA sequences\n"
              << "  output.bin     Output binary file with projected vectors\n"
              << "\nOptions:\n"
              << "  -m, --mode <M> Processing mode: 'reads' or 'assembly' (default: assembly)\n"
              << "                 - 'assembly': use all k-mers\n"
              << "                 - 'reads': keep only k-mers with abundance >= 2\n"
              << "  -d, --dim <N>  Target dimension for random projection (default: 2048)\n"
              << "  -k <N>         K-mer size for sourmash (default: 31)\n"
              << "  -s <N>         Scaled factor for sourmash (default: 1000)\n"
              << "  -h, --help     Show this help message\n" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    // Parse arguments
    std::string input_file = argv[1];
    std::string output_file = argv[2];
    int dimension = 2048;
    int kmer_size = 31;
    int scaled = 1000;
    std::string mode = "assembly";
    
    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-m" || arg == "--mode") && i + 1 < argc) {
            mode = argv[++i];
        } else if ((arg == "-d" || arg == "--dim") && i + 1 < argc) {
            dimension = std::stoi(argv[++i]);
        } else if (arg == "-k" && i + 1 < argc) {
            kmer_size = std::stoi(argv[++i]);
        } else if (arg == "-s" && i + 1 < argc) {
            scaled = std::stoi(argv[++i]);
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
    }

    // Validate inputs
    if (!fs::exists(input_file)) {
        std::cerr << "Error: Input file not found: " << input_file << std::endl;
        return 1;
    }
    
    if (mode != "reads" && mode != "assembly") {
        std::cerr << "Error: Invalid mode '" << mode << "'. Use 'reads' or 'assembly'." << std::endl;
        return 1;
    }

    std::cout << "\n=== DNA to Vector Conversion Pipeline ===" << std::endl;
    std::cout << "Input file: " << input_file << std::endl;
    std::cout << "Output file: " << output_file << std::endl;
    std::cout << "Mode: " << mode << std::endl;
    std::cout << "Dimension: " << dimension << std::endl;
    std::cout << "K-mer size: " << kmer_size << std::endl;
    std::cout << "Scaled: " << scaled << std::endl;
    
    try {
        // Step 1: Run sourmash sketch
        std::cout << "\n[Step 1] Generating sourmash sketch..." << std::endl;
        std::string sketch_file = input_file + ".sig";
        std::string sketch_cmd = "sourmash sketch dna -p dna,k=" + std::to_string(kmer_size) + 
                                ",scaled=" + std::to_string(scaled) + ",abund \"" + 
                                input_file + "\" -o \"" + sketch_file + "\" 2>/dev/null";
        
        if (run_command(sketch_cmd) != 0) {
            throw std::runtime_error("Failed to generate sourmash sketch. Make sure sourmash is installed. Tried command: " + sketch_cmd);
        }
        
        if (!fs::exists(sketch_file)) {
            throw std::runtime_error("Sketch file was not created");
        }
        std::cout << "✓ Sketch created: " << sketch_file << std::endl;
        
        // Step 2: Extract hashes from sketch
        std::cout << "\n[Step 2] Extracting hashes from sketch..." << std::endl;
        std::unordered_set<unsigned long int> hashes = extract_hashes_from_sketch(sketch_file, mode);
        
        if (hashes.empty()) {
            std::cerr << "⚠ Warning: No hashes extracted from sketch" << std::endl;
        }
        std::cout << "✓ Extracted " << hashes.size() << " hashes" << std::endl;
        
        // Step 3: Transform hashes to vector using random projection
        std::cout << "\n[Step 3] Transforming to vector..." << std::endl;
        std::vector<int32_t> projected_vec = transform_set_into_vector(hashes, dimension);
        
        // Step 4: Write binary output
        std::cout << "\n[Step 4] Writing binary output..." << std::endl;
        write_binary_vector(output_file, projected_vec);
        std::cout << "✓ Projected vector written to: " << output_file << std::endl;
        std::cout << "  Vector dimension: " << projected_vec.size() << std::endl;
                
        // Clean up sketch file
        fs::remove(sketch_file);
        
        std::cout << "\n=== Pipeline Complete ===" << std::endl;
        std::cout << "Output: " << output_file << std::endl;
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\nError: " << e.what() << std::endl;
        return 1;
    }
}

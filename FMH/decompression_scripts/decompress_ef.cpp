#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

static const char MAGIC[4] = {'E', 'F', 'U', '1'};

struct Args {
    fs::path indir;
    fs::path outdir;
    int outbit = 64;
    size_t threads = std::thread::hardware_concurrency();
};

template <typename T>
void read_binary(std::ifstream& in, T& x) {
    in.read(reinterpret_cast<char*>(&x), sizeof(T));
}

Args parse_args(int argc, char* argv[]) {
    Args args;

    for (int i = 1; i < argc; i++) {
        std::string key = argv[i];

        if (key == "--indir" && i + 1 < argc) {
            args.indir = argv[++i];
        } else if (key == "--outdir" && i + 1 < argc) {
            args.outdir = argv[++i];
        } else if (key == "--outbit" && i + 1 < argc) {
            args.outbit = std::stoi(argv[++i]);
        } else if (key == "--threads" && i + 1 < argc) {
            args.threads = std::stoul(argv[++i]);
        } else {
            throw std::runtime_error(
                "Usage: ./decompress_ef "
                "--indir <dir> "
                "--outdir <dir> "
                "[--outbit 32|64] "
                "[--threads N]"
            );
        }
    }

    if (args.indir.empty() || args.outdir.empty()) {
        throw std::runtime_error(
            "Usage: ./decompress_ef "
            "--indir <dir> "
            "--outdir <dir> "
            "[--outbit 32|64] "
            "[--threads N]"
        );
    }

    if (args.outbit != 32 && args.outbit != 64) {
        throw std::runtime_error("--outbit must be 32 or 64");
    }

    if (args.threads == 0) {
        args.threads = 1;
    }

    args.indir = fs::absolute(args.indir);
    args.outdir = fs::absolute(args.outdir);

    return args;
}

uint64_t get_bit(const std::vector<uint8_t>& bytes, uint64_t pos) {
    return (bytes[pos / 8] >> (pos % 8)) & 1ULL;
}

uint64_t unpack_low_value(const std::vector<uint8_t>& low_bytes,
                          uint64_t index,
                          uint32_t l) {
    if (l == 0) {
        return 0;
    }

    uint64_t bit_pos = index * uint64_t(l);
    uint64_t value = 0;

    for (uint32_t b = 0; b < l; b++) {
        uint64_t p = bit_pos + b;
        uint64_t bit = (low_bytes[p / 8] >> (p % 8)) & 1ULL;
        value |= bit << b;
    }

    return value;
}

std::vector<uint64_t> decode_ef_file(const fs::path& file_path) {
    std::ifstream in(file_path, std::ios::binary);

    if (!in) {
        throw std::runtime_error("Cannot open EF file: " + file_path.string());
    }

    char magic[4];
    in.read(magic, 4);

    for (int i = 0; i < 4; i++) {
        if (magic[i] != MAGIC[i]) {
            throw std::runtime_error("Bad EF magic: " + file_path.string());
        }
    }

    uint64_t n = 0;
    uint64_t universe = 0;
    uint32_t l = 0;
    uint64_t low_size = 0;
    uint64_t high_size = 0;

    read_binary(in, n);
    read_binary(in, universe);
    read_binary(in, l);
    read_binary(in, low_size);
    read_binary(in, high_size);

    std::vector<uint8_t> low_bytes(low_size);
    std::vector<uint8_t> high_bytes(high_size);

    if (low_size > 0) {
        in.read(reinterpret_cast<char*>(low_bytes.data()), low_size);
    }

    if (high_size > 0) {
        in.read(reinterpret_cast<char*>(high_bytes.data()), high_size);
    }

    std::vector<uint64_t> values;
    values.reserve(n);

    uint64_t found = 0;
    uint64_t high_len = high_size * 8;

    for (uint64_t pos = 0; pos < high_len && found < n; pos++) {
        if (get_bit(high_bytes, pos)) {
            uint64_t high = pos - found;
            uint64_t low = unpack_low_value(low_bytes, found, l);
            uint64_t value = (high << l) | low;

            values.push_back(value);
            found++;
        }
    }

    if (values.size() != n) {
        throw std::runtime_error(
            "Decoded wrong number of values from: " + file_path.string()
        );
    }

    return values;
}

void write_uint_file(const fs::path& out_file,
                     const std::vector<uint64_t>& values,
                     int outbit) {
    fs::create_directories(out_file.parent_path());

    std::ofstream out(out_file, std::ios::binary);

    if (!out) {
        throw std::runtime_error("Cannot open output file: " + out_file.string());
    }

    if (outbit == 32) {
        for (uint64_t x : values) {
            if (x > UINT32_MAX) {
                throw std::runtime_error(
                    "Value does not fit in uint32 while writing: " + out_file.string()
                );
            }

            uint32_t y = static_cast<uint32_t>(x);
            out.write(reinterpret_cast<const char*>(&y), sizeof(uint32_t));
        }
    } else {
        if (!values.empty()) {
            out.write(
                reinterpret_cast<const char*>(values.data()),
                values.size() * sizeof(uint64_t)
            );
        }
    }
}

std::vector<fs::path> collect_ef_files_in_batch(const fs::path& batch_dir) {
    std::vector<fs::path> files;

    for (const auto& entry : fs::directory_iterator(batch_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        fs::path file = entry.path();

        if (file.extension() == ".ef") {
            files.push_back(file);
        }
    }

    std::sort(files.begin(), files.end());

    return files;
}

void process_one_file(const fs::path& input_file,
                      const fs::path& output_file,
                      int outbit,
                      std::mutex& print_mutex) {
    std::vector<uint64_t> values = decode_ef_file(input_file);

    write_uint_file(output_file, values, outbit);

    {
        std::lock_guard<std::mutex> lock(print_mutex);

        std::cout << "[OK] "
                  << input_file.filename().string()
                  << " -> "
                  << output_file
                  << " n=" << values.size()
                  << "\n";
    }
}

void process_one_batch(const fs::path& batch_input_dir,
                       const fs::path& batch_output_dir,
                       int outbit,
                       size_t threads) {
    std::vector<fs::path> files = collect_ef_files_in_batch(batch_input_dir);

    std::cout << "=======================================\n";
    std::cout << "Batch: " << batch_input_dir.filename().string() << "\n";
    std::cout << "Input: " << batch_input_dir << "\n";
    std::cout << "Output: " << batch_output_dir << "\n";
    std::cout << "EF files: " << files.size() << "\n";
    std::cout << "Threads: " << threads << "\n";
    std::cout << "Outbit: " << outbit << "\n";
    std::cout << "=======================================\n";

    if (files.empty()) {
        std::cout << "[SKIP batch] No .ef files found in "
                  << batch_input_dir
                  << "\n";
        return;
    }

    fs::create_directories(batch_output_dir);

    std::atomic<size_t> next_index{0};
    std::atomic<size_t> ok_count{0};
    std::atomic<size_t> error_count{0};

    std::mutex print_mutex;

    auto worker = [&]() {
        while (true) {
            size_t i = next_index.fetch_add(1);

            if (i >= files.size()) {
                break;
            }

            fs::path input_file = files[i];

            std::string suffix = outbit == 32 ? ".u32" : ".u64";
            fs::path output_file =
                batch_output_dir / (input_file.stem().string() + suffix);

            try {
                process_one_file(
                    input_file,
                    output_file,
                    outbit,
                    print_mutex
                );

                ok_count.fetch_add(1);

            } catch (const std::exception& e) {
                error_count.fetch_add(1);

                std::lock_guard<std::mutex> lock(print_mutex);

                std::cerr << "[ERROR] "
                          << input_file
                          << ": "
                          << e.what()
                          << "\n";
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(threads);

    for (size_t t = 0; t < threads; t++) {
        workers.emplace_back(worker);
    }

    for (auto& t : workers) {
        t.join();
    }

    std::cout << "Batch done: "
              << batch_input_dir.filename().string()
              << " successful=" << ok_count.load()
              << " errors=" << error_count.load()
              << " total=" << files.size()
              << "\n";
}

std::vector<fs::path> collect_batch_dirs(const fs::path& indir) {
    std::vector<fs::path> batch_dirs;

    for (const auto& entry : fs::directory_iterator(indir)) {
        if (entry.is_directory()) {
            batch_dirs.push_back(entry.path());
        }
    }

    std::sort(batch_dirs.begin(), batch_dirs.end());

    return batch_dirs;
}

int main(int argc, char* argv[]) {
    try {
        Args args = parse_args(argc, argv);

        fs::create_directories(args.outdir);

        std::vector<fs::path> batch_dirs = collect_batch_dirs(args.indir);

        std::cout << "Input root: " << args.indir << "\n";
        std::cout << "Output root: " << args.outdir << "\n";
        std::cout << "Batch folders found: " << batch_dirs.size() << "\n";
        std::cout << "Threads per batch: " << args.threads << "\n";
        std::cout << "Output bit width: " << args.outbit << "\n";

        if (batch_dirs.empty()) {
            std::cout << "No batch folders found. Treating input directory as one batch.\n";

            process_one_batch(
                args.indir,
                args.outdir,
                args.outbit,
                args.threads
            );

            return 0;
        }

        for (const fs::path& batch_dir : batch_dirs) {
            fs::path batch_name = batch_dir.filename();
            fs::path batch_outdir = args.outdir / batch_name;

            process_one_batch(
                batch_dir,
                batch_outdir,
                args.outbit,
                args.threads
            );
        }

        std::cout << "All batches done.\n";

    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }

    return 0;
}
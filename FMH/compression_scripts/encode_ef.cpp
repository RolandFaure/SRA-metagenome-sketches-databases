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

#include "elias_fano.hpp"

namespace fs = std::filesystem;

static const char MAGIC[4] = {'E', 'F', 'U', '1'};

struct Args {
    fs::path indir;
    fs::path outdir;
    fs::path sigdir;
    size_t threads = std::thread::hardware_concurrency();
};

Args parse_args(int argc, char* argv[]) {
    Args args;

    for (int i = 1; i < argc; i++) {
        std::string key = argv[i];

        if (key == "--indir" && i + 1 < argc) {
            args.indir = argv[++i];
        } else if (key == "--outdir" && i + 1 < argc) {
            args.outdir = argv[++i];
        } else if (key == "--threads" && i + 1 < argc) {
            args.threads = std::stoul(argv[++i]);
        } else if (key == "--sigdir" && i + 1 < argc) {
            args.sigdir = argv[++i];
        } else {
            throw std::runtime_error(
                "Usage: ./encode_ef --indir <dir> --outdir <dir> [--threads N] [--sigdir <dir>]"
            );
        }
    }

    if (args.indir.empty() || args.outdir.empty()) {
        throw std::runtime_error(
            "Usage: ./encode_ef --indir <dir> --outdir <dir> [--threads N] [--sigdir <dir>]"
        );
    }

    if (args.threads == 0) {
        args.threads = 1;
    }

    args.indir = fs::absolute(args.indir);
    args.outdir = fs::absolute(args.outdir);
    args.sigdir = fs::absolute(args.sigdir);

    return args;
}

template <typename T>
void write_binary(std::ofstream& out, const T& x) {
    out.write(reinterpret_cast<const char*>(&x), sizeof(T));
}

std::vector<uint64_t> read_u64_file(const fs::path& file_path) {
    std::ifstream in(file_path, std::ios::binary);

    if (!in) {
        throw std::runtime_error("Cannot open input file: " + file_path.string());
    }

    in.seekg(0, std::ios::end);
    std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);

    if (size % sizeof(uint64_t) != 0) {
        throw std::runtime_error("Bad uint64 file size: " + file_path.string());
    }

    size_t n = size / sizeof(uint64_t);
    std::vector<uint64_t> values(n);

    if (n > 0) {
        in.read(reinterpret_cast<char*>(values.data()), size);
    }

    return values;
}

uint32_t floor_log2_u64(uint64_t x) {
    uint32_t r = 0;

    while (x >>= 1) {
        r++;
    }

    return r;
}

std::vector<uint8_t> pack_low_bits(const std::vector<uint64_t>& values, uint32_t l) {
    if (l == 0 || values.empty()) {
        return {};
    }

    std::vector<uint8_t> out;
    out.reserve((values.size() * uint64_t(l) + 7) / 8);

    uint64_t buffer = 0;
    uint32_t bits_in_buffer = 0;
    uint64_t mask = (uint64_t(1) << l) - 1;

    for (uint64_t v : values) {
        uint64_t low = v & mask;

        buffer |= low << bits_in_buffer;
        bits_in_buffer += l;

        while (bits_in_buffer >= 8) {
            out.push_back(static_cast<uint8_t>(buffer & 0xFF));
            buffer >>= 8;
            bits_in_buffer -= 8;
        }
    }

    if (bits_in_buffer > 0) {
        out.push_back(static_cast<uint8_t>(buffer & 0xFF));
    }

    return out;
}

std::vector<uint8_t> build_high_bits(const std::vector<uint64_t>& values, uint32_t l) {
    uint64_t n = values.size();
    uint64_t max_high = values.back() >> l;
    uint64_t high_len = max_high + n + 1;

    std::vector<uint8_t> high_bytes((high_len + 7) / 8, 0);

    for (uint64_t i = 0; i < n; i++) {
        uint64_t high = values[i] >> l;
        uint64_t pos = high + i;

        high_bytes[pos / 8] |= uint8_t(1) << (pos % 8);
    }

    return high_bytes;
}

void save_flat_ef_file(const fs::path& out_file,
                       uint64_t n,
                       uint64_t universe,
                       uint32_t l,
                       const std::vector<uint8_t>& low_bytes,
                       const std::vector<uint8_t>& high_bytes) {
    fs::create_directories(out_file.parent_path());

    std::ofstream out(out_file, std::ios::binary);

    if (!out) {
        throw std::runtime_error("Cannot open output file: " + out_file.string());
    }

    out.write(MAGIC, 4);

    write_binary<uint64_t>(out, n);
    write_binary<uint64_t>(out, universe);
    write_binary<uint32_t>(out, l);

    uint64_t low_size = low_bytes.size();
    uint64_t high_size = high_bytes.size();

    write_binary<uint64_t>(out, low_size);
    write_binary<uint64_t>(out, high_size);

    if (!low_bytes.empty()) {
        out.write(reinterpret_cast<const char*>(low_bytes.data()), low_bytes.size());
    }

    if (!high_bytes.empty()) {
        out.write(reinterpret_cast<const char*>(high_bytes.data()), high_bytes.size());
    }
}

// ============================================================
// OPTIONAL: batch compression size reporting
// If you do not want this, comment out the call to
// append_batch_size_report() inside process_one_batch().
// ============================================================

uint64_t total_file_size_in_dir(const fs::path& dir, const std::string& extension) {
    uint64_t total = 0;

    if (!fs::exists(dir)) {
        return 0;
    }

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        fs::path file = entry.path();

        if (file.extension() == extension) {
            total += fs::file_size(file);
        }
    }

    return total;
}

size_t count_files_in_dir(const fs::path& dir, const std::string& extension) {
    size_t count = 0;

    if (!fs::exists(dir)) {
        return 0;
    }

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        fs::path file = entry.path();

        if (file.extension() == extension) {
            count++;
        }
    }

    return count;
}

void append_batch_size_report(const fs::path& report_file,
                              const std::string& batch_name,
                              const fs::path& batch_sig_dir,
                              const fs::path& batch_output_dir,
                              size_t ok_count,
                              size_t error_count) {
    uint64_t input_sig_bytes = total_file_size_in_dir(batch_sig_dir, ".sig");
    uint64_t output_ef_bytes = total_file_size_in_dir(batch_output_dir, ".ef");

    size_t input_sig_files = count_files_in_dir(batch_sig_dir, ".sig");
    size_t output_ef_files = count_files_in_dir(batch_output_dir, ".ef");

    double input_sig_mib = input_sig_bytes / (1024.0 * 1024.0);
    double output_ef_mib = output_ef_bytes / (1024.0 * 1024.0);

    double ratio = 0.0;
    if (input_sig_bytes > 0) {
        ratio = static_cast<double>(output_ef_bytes) /
                static_cast<double>(input_sig_bytes);
    }

    bool write_header = !fs::exists(report_file);

    std::ofstream out(report_file, std::ios::app);
    if (!out) {
        throw std::runtime_error("Cannot open batch size report: " + report_file.string());
    }

    if (write_header) {
        out << "batch"
            << "\tinput_sig_files"
            << "\toutput_ef_files"
            << "\tsuccessful_files"
            << "\terror_files"
            << "\tinput_sig_bytes"
            << "\toutput_ef_bytes"
            << "\tinput_sig_MiB"
            << "\toutput_ef_MiB"
            << "\tef_over_sig_ratio"
            << "\n";
    }

    out << batch_name
        << "\t" << input_sig_files
        << "\t" << output_ef_files
        << "\t" << ok_count
        << "\t" << error_count
        << "\t" << input_sig_bytes
        << "\t" << output_ef_bytes
        << "\t" << input_sig_mib
        << "\t" << output_ef_mib
        << "\t" << ratio
        << "\n";
}

// ============================================================
// END OPTIONAL REPORTING SECTION
// ============================================================

void process_one_file(const fs::path& input_file,
                      const fs::path& output_file,
                      std::mutex& print_mutex) {
    std::vector<uint64_t> values = read_u64_file(input_file);

    if (values.empty()) {
        std::lock_guard<std::mutex> lock(print_mutex);
        std::cerr << "[SKIP empty] " << input_file << "\n";
        return;
    }

    uint64_t n = values.size();
    uint64_t universe = values.back();

    uint32_t l = 0;
    if (universe > n) {
        l = floor_log2_u64(universe / n);
    }

    /*
      This builds the repo EF object only to validate access/queries
      and report ef.num_bytes(). The actual saved file is written below
      using low_bytes + high_bytes.
    */
    bits::elias_fano<true> ef;
    ef.encode(values.begin(), values.size(), universe);

    if (ef.access(0) != values[0]) {
        throw std::runtime_error("access(0) failed: " + input_file.string());
    }

    if (ef.access(n - 1) != values[n - 1]) {
        throw std::runtime_error("access(n-1) failed: " + input_file.string());
    }

    uint64_t mid_value = values[n / 2];

    auto succ = ef.next_geq(mid_value);
    auto pred = ef.prev_leq(mid_value);

    if (succ.val != mid_value || pred.val != mid_value) {
        throw std::runtime_error("next_geq / prev_leq failed: " + input_file.string());
    }

    std::vector<uint8_t> low_bytes = pack_low_bits(values, l);
    std::vector<uint8_t> high_bytes = build_high_bits(values, l);

    save_flat_ef_file(output_file, n, universe, l, low_bytes, high_bytes);

    uint64_t raw_bytes = n * sizeof(uint64_t);
    uint64_t payload_bytes = low_bytes.size() + high_bytes.size();
    uint64_t total_disk = fs::file_size(output_file);

    {
        std::lock_guard<std::mutex> lock(print_mutex);

        std::cout << "[OK] "
                  << input_file.filename().string()
                  << " n=" << n
                  << " l=" << l
                  << " raw_u64=" << raw_bytes
                  << " ef_hpp_mem=" << ef.num_bytes()
                  << " ef_payload_disk=" << payload_bytes
                  << " ef_total_disk=" << total_disk
                  << " first=" << ef.access(0)
                  << " last=" << ef.access(n - 1)
                  << " -> " << output_file
                  << "\n";
    }
}

std::vector<fs::path> collect_u64_files_in_batch(const fs::path& batch_dir) {
    std::vector<fs::path> files;

    for (const auto& entry : fs::directory_iterator(batch_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        fs::path file = entry.path();

        if (file.extension() == ".u64") {
            files.push_back(file);
        }
    }

    std::sort(files.begin(), files.end());

    return files;
}

void process_one_batch(const fs::path& batch_input_dir,
                       const fs::path& batch_output_dir,
                       const fs::path& batch_sig_dir,
                       size_t threads) {
    std::vector<fs::path> files = collect_u64_files_in_batch(batch_input_dir);

    std::cout << "=======================================\n";
    std::cout << "Batch: " << batch_input_dir.filename().string() << "\n";
    std::cout << "Input: " << batch_input_dir << "\n";
    std::cout << "Output: " << batch_output_dir << "\n";
    std::cout << "Files: " << files.size() << "\n";
    std::cout << "Threads: " << threads << "\n";
    std::cout << "=======================================\n";

    if (files.empty()) {
        std::cout << "[SKIP batch] No .u64 files found in " << batch_input_dir << "\n";
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
            fs::path output_file =
                batch_output_dir / (input_file.stem().string() + ".ef");

            try {
                process_one_file(input_file, output_file, print_mutex);
                ok_count.fetch_add(1);
            } catch (const std::exception& e) {
                error_count.fetch_add(1);

                std::lock_guard<std::mutex> lock(print_mutex);
                std::cerr << "[ERROR] "
                          << input_file
                          << ": " << e.what()
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

    // ============================================================
    // OPTIONAL: write original .sig vs compressed EF size to summary file
    // Comment out this block if you do not want batch-size reporting.
    // ============================================================
    {
    fs::path report_file =
        batch_output_dir.parent_path() / "batch_sig_vs_ef_sizes.tsv";

    append_batch_size_report(
        report_file,
        batch_input_dir.filename().string(),
        batch_sig_dir,
        batch_output_dir,
        ok_count.load(),
        error_count.load()
    );

    std::cout << "Batch .sig vs EF size written to: "
            << report_file
            << "\n";
    }
    // ============================================================

    std::cout << "Batch done: " << batch_input_dir.filename().string()
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

        if (batch_dirs.empty()) {
            std::cout << "No batch folders found. Treating input directory as one batch.\n";

            process_one_batch(
                args.indir,
                args.outdir,
                args.sigdir,
                args.threads
            );

            return 0;
        }

        for (const fs::path& batch_dir : batch_dirs) {
            fs::path batch_name = batch_dir.filename();
            fs::path batch_outdir = args.outdir / batch_name;
            fs::path batch_sig_dir = args.sigdir / batch_name;

            process_one_batch(
                batch_dir,
                batch_outdir,
                batch_sig_dir,
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
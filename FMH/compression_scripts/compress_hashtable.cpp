#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static const char MAGIC[4] = {'E', 'F', 'U', '1'};

struct Args {
    fs::path hash_table_dir;
    fs::path outdir = "hash_table_compressed";
};

Args parse_args(int argc, char* argv[]) {
    Args args;

    for (int i = 1; i < argc; i++) {
        std::string key = argv[i];

        if (key == "--hash_table_dir" && i + 1 < argc) {
            args.hash_table_dir = argv[++i];
        } else if (key == "--outdir" && i + 1 < argc) {
            args.outdir = argv[++i];
        } else {
            throw std::runtime_error(
                "Usage: ./compress_hash_table_ef "
                "--hash_table_dir <dir> "
                "[--outdir hash_table_compressed]"
            );
        }
    }

    if (args.hash_table_dir.empty()) {
        throw std::runtime_error("Missing --hash_table_dir");
    }

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
        throw std::runtime_error("File is not uint64 aligned: " + file_path.string());
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

void save_ef_file(const fs::path& out_file,
                  uint64_t n,
                  uint64_t universe,
                  uint32_t l,
                  const std::vector<uint8_t>& low_bytes,
                  const std::vector<uint8_t>& high_bytes) {
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

void compress_one_hash_table(const fs::path& input_file, const fs::path& outdir) {
    std::vector<uint64_t> values = read_u64_file(input_file);

    if (values.empty()) {
        std::cerr << "[SKIP empty] " << input_file << "\n";
        return;
    }

    //std::sort(values.begin(), values.end());
    //values.erase(std::unique(values.begin(), values.end()), values.end());

    uint64_t n = values.size();
    uint64_t universe = values.back();

    uint32_t l = 0;
    if (universe > n) {
        l = floor_log2_u64(universe / n);
    }

    std::vector<uint8_t> low_bytes = pack_low_bits(values, l);
    std::vector<uint8_t> high_bytes = build_high_bits(values, l);

    fs::path out_file = outdir / (input_file.stem().string() + ".ef");

    save_ef_file(out_file, n, universe, l, low_bytes, high_bytes);

    uint64_t raw_bytes = n * sizeof(uint64_t);
    uint64_t ef_payload = low_bytes.size() + high_bytes.size();

    std::cout << "[OK] "
              << input_file.filename()
              << " n=" << n
              << " l=" << l
              << " raw_bytes=" << raw_bytes
              << " ef_payload=" << ef_payload
              << " ef_disk=" << fs::file_size(out_file)
              << " -> " << out_file.filename()
              << "\n";
}

int main(int argc, char* argv[]) {
    try {
        Args args = parse_args(argc, argv);

        fs::create_directories(args.outdir);

        size_t count = 0;

        for (const auto& entry : fs::directory_iterator(args.hash_table_dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            fs::path file = entry.path();

            if (file.extension() == ".u64") {
                compress_one_hash_table(file, args.outdir);
                count++;
            }
        }

        std::cout << "Done. Hash table files compressed: " << count << "\n";

    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }

    return 0;
}
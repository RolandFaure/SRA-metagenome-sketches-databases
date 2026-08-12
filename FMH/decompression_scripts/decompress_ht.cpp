#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

static const char MAGIC[4] = {'E', 'F', 'U', '1'};

struct Args {
    fs::path indir;
    fs::path outdir;
    uint64_t progress_every = 100000000; // print every 100M decoded values
};

struct MappedFile {
    int fd = -1;
    uint8_t* data = nullptr;
    size_t size = 0;

    explicit MappedFile(const fs::path& path) {
        fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) {
            throw std::runtime_error("Cannot open file: " + path.string() +
                                     " errno=" + std::strerror(errno));
        }

        struct stat st;
        if (fstat(fd, &st) != 0) {
            close(fd);
            throw std::runtime_error("Cannot stat file: " + path.string());
        }

        size = static_cast<size_t>(st.st_size);

        if (size == 0) {
            close(fd);
            throw std::runtime_error("Empty file: " + path.string());
        }

        void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (mapped == MAP_FAILED) {
            close(fd);
            throw std::runtime_error("mmap failed for file: " + path.string() +
                                     " errno=" + std::strerror(errno));
        }

        data = static_cast<uint8_t*>(mapped);
    }

    ~MappedFile() {
        if (data && data != MAP_FAILED) {
            munmap(data, size);
        }
        if (fd >= 0) {
            close(fd);
        }
    }

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
};

template <typename T>
T read_value(const uint8_t* data, size_t size, size_t& offset) {
    if (offset + sizeof(T) > size) {
        throw std::runtime_error("Unexpected EOF while reading EF header");
    }

    T x;
    std::memcpy(&x, data + offset, sizeof(T));
    offset += sizeof(T);

    return x;
}

Args parse_args(int argc, char* argv[]) {
    Args args;

    for (int i = 1; i < argc; i++) {
        std::string key = argv[i];

        if (key == "--indir" && i + 1 < argc) {
            args.indir = argv[++i];
        } else if (key == "--outdir" && i + 1 < argc) {
            args.outdir = argv[++i];
        } else if (key == "--progress_every" && i + 1 < argc) {
            args.progress_every = std::stoull(argv[++i]);
        } else {
            throw std::runtime_error(
                "Usage: ./decompress_ht "
                "--indir <hash_table_compressed_dir> "
                "--outdir <hash_table_dir> "
                "[--progress_every N]"
            );
        }
    }

    if (args.indir.empty() || args.outdir.empty()) {
        throw std::runtime_error(
            "Usage: ./decompress_ht "
            "--indir <hash_table_compressed_dir> "
            "--outdir <hash_table_dir> "
            "[--progress_every N]"
        );
    }

    args.indir = fs::absolute(args.indir);
    args.outdir = fs::absolute(args.outdir);

    return args;
}

uint64_t unpack_low_value_fast(const uint8_t* low_bytes,
                               uint64_t low_size,
                               uint64_t index,
                               uint32_t l) {
    if (l == 0) {
        return 0;
    }

    uint64_t bit_pos = index * uint64_t(l);
    uint64_t byte_i = bit_pos / 8;
    uint32_t shift = bit_pos % 8;

    if (byte_i >= low_size) {
        throw std::runtime_error("Low-bit read outside low_bytes");
    }

    uint64_t x = 0;

    uint32_t available_bytes = 0;
    uint64_t remaining = low_size - byte_i;

    if (remaining >= 8) {
        available_bytes = 8;
    } else {
        available_bytes = static_cast<uint32_t>(remaining);
    }

    for (uint32_t b = 0; b < available_bytes; b++) {
        x |= uint64_t(low_bytes[byte_i + b]) << (8 * b);
    }

    uint64_t value = x >> shift;
    uint32_t bits_from_first_word = 64 - shift;

    if (bits_from_first_word < l) {
        uint32_t extra_bits_needed = l - bits_from_first_word;
        uint64_t next_byte_pos = byte_i + 8;

        if (next_byte_pos >= low_size) {
            throw std::runtime_error("Low-bit extra read outside low_bytes");
        }

        uint64_t extra = low_bytes[next_byte_pos];
        value |= extra << bits_from_first_word;

        // extra_bits_needed is at most 7 here because shift is 0..7.
        (void)extra_bits_needed;
    }

    if (l == 64) {
        return value;
    }

    uint64_t mask = (uint64_t(1) << l) - 1;
    return value & mask;
}

void flush_buffer(std::ofstream& out, std::vector<uint64_t>& buffer) {
    if (!buffer.empty()) {
        out.write(
            reinterpret_cast<const char*>(buffer.data()),
            buffer.size() * sizeof(uint64_t)
        );
        buffer.clear();
    }
}

void decode_ef_to_u64_streaming(const fs::path& ef_file,
                                const fs::path& out_file,
                                uint64_t progress_every) {
    MappedFile mf(ef_file);

    size_t offset = 0;

    if (mf.size < 4) {
        throw std::runtime_error("Bad EF file, too small: " + ef_file.string());
    }

    for (int i = 0; i < 4; i++) {
        if (mf.data[i] != static_cast<uint8_t>(MAGIC[i])) {
            throw std::runtime_error("Bad EF magic: " + ef_file.string());
        }
    }

    offset += 4;

    uint64_t n = read_value<uint64_t>(mf.data, mf.size, offset);
    uint64_t universe = read_value<uint64_t>(mf.data, mf.size, offset);
    uint32_t l = read_value<uint32_t>(mf.data, mf.size, offset);
    uint64_t low_size = read_value<uint64_t>(mf.data, mf.size, offset);
    uint64_t high_size = read_value<uint64_t>(mf.data, mf.size, offset);

    if (offset + low_size + high_size > mf.size) {
        throw std::runtime_error("EF payload size exceeds file size: " + ef_file.string());
    }

    const uint8_t* low_bytes = mf.data + offset;
    offset += low_size;

    const uint8_t* high_bytes = mf.data + offset;
    offset += high_size;

    fs::create_directories(out_file.parent_path());

    std::ofstream out(out_file, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Cannot open output file: " + out_file.string());
    }

    std::cout << "[INFO] Decoding hash table EF\n";
    std::cout << "       input=" << ef_file << "\n";
    std::cout << "       output=" << out_file << "\n";
    std::cout << "       n=" << n << "\n";
    std::cout << "       universe=" << universe << "\n";
    std::cout << "       l=" << l << "\n";
    std::cout << "       low_size=" << low_size << "\n";
    std::cout << "       high_size=" << high_size << "\n";

    std::vector<uint64_t> out_buffer;
    out_buffer.reserve(1 << 20); // 1,048,576 values = 8 MiB

    uint64_t found = 0;

    for (uint64_t byte_i = 0; byte_i < high_size && found < n; byte_i++) {
        uint8_t byte = high_bytes[byte_i];

        while (byte != 0 && found < n) {
            int bit = __builtin_ctz(static_cast<unsigned int>(byte));

            uint64_t pos = byte_i * 8ULL + static_cast<uint64_t>(bit);
            uint64_t high = pos - found;
            uint64_t low = unpack_low_value_fast(low_bytes, low_size, found, l);
            uint64_t value = (high << l) | low;

            out_buffer.push_back(value);

            if (out_buffer.size() >= (1 << 20)) {
                flush_buffer(out, out_buffer);
            }

            found++;

            if (progress_every > 0 && found % progress_every == 0) {
                std::cout << "[PROGRESS] decoded " << found
                          << " / " << n
                          << " values"
                          << "\n";
            }

            byte &= static_cast<uint8_t>(byte - 1);
        }
    }

    flush_buffer(out, out_buffer);
    out.close();

    if (found != n) {
        throw std::runtime_error(
            "Decoded wrong number of values from " + ef_file.string() +
            ". expected=" + std::to_string(n) +
            " found=" + std::to_string(found)
        );
    }

    uint64_t out_bytes = fs::file_size(out_file);

    std::cout << "[OK] "
              << ef_file.filename().string()
              << " -> "
              << out_file
              << " n=" << n
              << " output_bytes=" << out_bytes
              << "\n";
}

int main(int argc, char* argv[]) {
    try {
        Args args = parse_args(argc, argv);

        fs::create_directories(args.outdir);

        size_t count = 0;

        for (const auto& entry : fs::directory_iterator(args.indir)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            fs::path ef_file = entry.path();

            if (ef_file.extension() != ".ef") {
                continue;
            }

            fs::path out_file = args.outdir / (ef_file.stem().string() + ".u64");

            decode_ef_to_u64_streaming(
                ef_file,
                out_file,
                args.progress_every
            );

            count++;
        }

        std::cout << "Done. Hash table EF files decompressed: "
                  << count
                  << "\n";

    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }

    return 0;
}


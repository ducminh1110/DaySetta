#include "daysetta/render.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>
#include <string>
#include <vector>

namespace ds::render {
namespace {

bool write_bytes(std::FILE* file, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::size_t offset = 0;
    while (offset < size) {
        const std::size_t written =
            std::fwrite(bytes + offset, 1, size - offset, file);
        if (written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

Result<void> finish_file(std::FILE* file, const std::string& path, bool wrote) {
    const bool closed = std::fclose(file) == 0;
    if (!wrote || !closed) {
        std::remove(path.c_str());
        return Result<void>::fail(Code::io, "failed to write output file");
    }
    return Result<void>::ok();
}

Result<void> open_file(const std::string& path, std::FILE** file) {
    *file = std::fopen(path.c_str(), "wb");
    if (*file == nullptr) {
        return Result<void>::fail(Code::io, "failed to open output file");
    }
    return Result<void>::ok();
}

bool valid_frame(const Frame& frame) {
    if (frame.width == 0 || frame.height == 0) {
        return false;
    }
    if (frame.stride < static_cast<std::uint64_t>(frame.width) * 4u) {
        return false;
    }
    const std::uint64_t required =
        static_cast<std::uint64_t>(frame.stride) * frame.height;
    return frame.pixels.size() >= required;
}

}  // namespace

Result<void> write_ppm(const std::string& path, const Frame& frame) {
    if (!valid_frame(frame)) {
        return Result<void>::fail(Code::invalid, "invalid frame");
    }

    std::FILE* file = nullptr;
    if (Result<void> opened = open_file(path, &file); !opened) {
        return opened;
    }

    char header[160];
    const int header_length =
        std::snprintf(header, sizeof(header), "P6\n%u %u\n255\n",
                      frame.width, frame.height);
    if (header_length < 0 ||
        static_cast<std::size_t>(header_length) >= sizeof(header)) {
        std::fclose(file);
        std::remove(path.c_str());
        return Result<void>::fail(Code::io, "failed to format PPM header");
    }

    bool written = write_bytes(file, header,
                               static_cast<std::size_t>(header_length));
    std::vector<std::uint8_t> row;
    if (written) {
        try {
            row.resize(static_cast<std::size_t>(frame.width) * 3u);
        } catch (const std::bad_alloc&) {
            std::fclose(file);
            std::remove(path.c_str());
            return Result<void>::fail(Code::too_large, "PPM row is too large");
        }
    }

    for (std::uint32_t y = 0; written && y < frame.height; ++y) {
        const std::uint8_t* source =
            frame.pixels.data() + static_cast<std::size_t>(y) * frame.stride;
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            const std::uint8_t* pixel = source + static_cast<std::size_t>(x) * 4u;
            row[static_cast<std::size_t>(x) * 3u] = pixel[2];
            row[static_cast<std::size_t>(x) * 3u + 1u] = pixel[1];
            row[static_cast<std::size_t>(x) * 3u + 2u] = pixel[0];
        }
        written = write_bytes(file, row.data(), row.size());
    }
    return finish_file(file, path, written);
}

Result<void> write_pam(const std::string& path, const Frame& frame) {
    if (!valid_frame(frame)) {
        return Result<void>::fail(Code::invalid, "invalid frame");
    }

    std::FILE* file = nullptr;
    if (Result<void> opened = open_file(path, &file); !opened) {
        return opened;
    }

    char header[192];
    const int header_length = std::snprintf(
        header, sizeof(header),
        "P7\nWIDTH %u\nHEIGHT %u\nDEPTH 4\nMAXVAL 255\n"
        "TUPLTYPE RGBA_8888\nENDHDR\n",
        frame.width, frame.height);
    if (header_length < 0 ||
        static_cast<std::size_t>(header_length) >= sizeof(header)) {
        std::fclose(file);
        std::remove(path.c_str());
        return Result<void>::fail(Code::io, "failed to format PAM header");
    }

    bool written = write_bytes(file, header,
                               static_cast<std::size_t>(header_length));
    std::vector<std::uint8_t> row;
    if (written) {
        try {
            row.resize(static_cast<std::size_t>(frame.width) * 4u);
        } catch (const std::bad_alloc&) {
            std::fclose(file);
            std::remove(path.c_str());
            return Result<void>::fail(Code::too_large, "PAM row is too large");
        }
    }

    for (std::uint32_t y = 0; written && y < frame.height; ++y) {
        const std::uint8_t* source =
            frame.pixels.data() + static_cast<std::size_t>(y) * frame.stride;
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            const std::uint8_t* pixel = source + static_cast<std::size_t>(x) * 4u;
            std::uint8_t* destination = row.data() + static_cast<std::size_t>(x) * 4u;
            destination[0] = pixel[2];
            destination[1] = pixel[1];
            destination[2] = pixel[0];
            destination[3] = pixel[3];
        }
        written = write_bytes(file, row.data(), row.size());
    }
    return finish_file(file, path, written);
}

Result<void> write_bgra(const std::string& path, const Frame& frame) {
    if (!valid_frame(frame)) {
        return Result<void>::fail(Code::invalid, "invalid frame");
    }

    std::FILE* file = nullptr;
    if (Result<void> opened = open_file(path, &file); !opened) {
        return opened;
    }

    bool written = true;
    const std::size_t row_size = static_cast<std::size_t>(frame.width) * 4u;
    for (std::uint32_t y = 0; written && y < frame.height; ++y) {
        const std::uint8_t* source =
            frame.pixels.data() + static_cast<std::size_t>(y) * frame.stride;
        written = write_bytes(file, source, row_size);
    }
    return finish_file(file, path, written);
}

std::uint32_t checksum32(std::span<const std::uint8_t> data) {
    std::uint32_t hash = 2166136261u;
    for (const std::uint8_t byte : data) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

}  // namespace ds::render

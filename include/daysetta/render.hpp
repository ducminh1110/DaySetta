// ds-render: CPU reference compositor, SHA-256 and headless frame output.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "daysetta/result.hpp"
#include "daysetta/scene.hpp"

namespace ds::render {

struct Color {
    std::uint8_t b = 0;
    std::uint8_t g = 0;
    std::uint8_t r = 0;
    std::uint8_t a = 255;

    static Color from_rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        return Color{b, g, r, 255};
    }
};

struct Frame {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride = 0;
    std::vector<std::uint8_t> pixels;  // BGRA8888, premultiplied alpha

    bool valid() const {
        return width > 0 && height > 0 && stride >= width * 4 &&
               pixels.size() >= static_cast<std::size_t>(stride) * height;
    }
};

struct FrameStats {
    std::uint32_t windows_drawn = 0;
    std::uint32_t pixels_written = 0;
    std::uint64_t hash = 0;
};

class CpuCompositor {
public:
    // Renders a snapshot (bottom-to-top order) into a fresh frame.
    Result<Frame> render(const std::vector<scene::WindowSnapshot>& windows,
                         std::uint32_t width, std::uint32_t height,
                         Color clear) const;

    // Same, but only changes pixels inside `clip`.  The previous frame must be valid.
    Result<void> render_damage(Frame& frame,
                               const std::vector<scene::WindowSnapshot>& windows,
                               const proto::Rect& clip, Color clear) const;

    static void blend_bgra(std::uint8_t* dst, const std::uint8_t* src,
                           float opacity);
};

std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> data);
std::string sha256_hex(std::span<const std::uint8_t> data);

Result<void> write_ppm(const std::string& path, const Frame& frame);
Result<void> write_pam(const std::string& path, const Frame& frame);
Result<void> write_bgra(const std::string& path, const Frame& frame);

std::uint32_t checksum32(std::span<const std::uint8_t> data);

}  // namespace ds::render

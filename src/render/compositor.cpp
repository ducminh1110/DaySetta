#include "daysetta/render.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace ds::render {
namespace {

constexpr std::uint32_t kMaxDimension = 16384;

bool usable_surface(const scene::Surface& surface) {
    if (surface.format != proto::PixelFormat::bgra8888 &&
        surface.format != proto::PixelFormat::rgba8888) {
        return false;
    }
    if (surface.width == 0 || surface.height == 0 ||
        surface.width > kMaxDimension || surface.height > kMaxDimension) {
        return false;
    }
    if (surface.stride < static_cast<std::uint64_t>(surface.width) * 4u) {
        return false;
    }
    const std::uint64_t required =
        static_cast<std::uint64_t>(surface.stride) * surface.height;
    return surface.pixels.size() >= required;
}

bool usable_frame(const Frame& frame) {
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

std::uint8_t premultiply(std::uint8_t channel, std::uint8_t alpha) {
    return static_cast<std::uint8_t>(
        (static_cast<unsigned>(channel) * alpha + 127u) / 255u);
}

void fill_rect(Frame& frame, std::int64_t x0, std::int64_t y0,
               std::int64_t x1, std::int64_t y1, Color clear) {
    const std::uint8_t b = premultiply(clear.b, clear.a);
    const std::uint8_t g = premultiply(clear.g, clear.a);
    const std::uint8_t r = premultiply(clear.r, clear.a);
    const std::uint8_t a = clear.a;
    for (std::int64_t y = y0; y < y1; ++y) {
        std::uint8_t* row = frame.pixels.data() +
                            static_cast<std::size_t>(y) * frame.stride;
        for (std::int64_t x = x0; x < x1; ++x) {
            std::uint8_t* pixel = row + static_cast<std::size_t>(x) * 4u;
            pixel[0] = b;
            pixel[1] = g;
            pixel[2] = r;
            pixel[3] = a;
        }
    }
}

void draw_window(Frame& frame, const scene::WindowSnapshot& window,
                 std::int64_t clip_x0, std::int64_t clip_y0,
                 std::int64_t clip_x1, std::int64_t clip_y1) {
    if (!window.surface || !(window.opacity > 0.0f) ||
        window.frame.w <= 0 || window.frame.h <= 0) {
        return;
    }
    const scene::Surface& surface = *window.surface;
    if (!usable_surface(surface)) {
        return;
    }

    const float opacity = window.opacity > 1.0f ? 1.0f : window.opacity;
    const std::int64_t frame_left = window.frame.x;
    const std::int64_t frame_top = window.frame.y;
    std::int64_t left = std::max(clip_x0, std::max<std::int64_t>(0, frame_left));
    std::int64_t top = std::max(clip_y0, std::max<std::int64_t>(0, frame_top));
    std::int64_t right =
        std::min(clip_x1, frame_left + window.frame.w);
    std::int64_t bottom =
        std::min(clip_y1, frame_top + window.frame.h);
    right = std::min(right, frame_left + static_cast<std::int64_t>(surface.width));
    bottom = std::min(bottom,
                      frame_top + static_cast<std::int64_t>(surface.height));
    right = std::min(right, static_cast<std::int64_t>(frame.width));
    bottom = std::min(bottom, static_cast<std::int64_t>(frame.height));
    if (left >= right || top >= bottom) {
        return;
    }

    for (std::int64_t y = top; y < bottom; ++y) {
        const std::size_t source_y = static_cast<std::size_t>(y - frame_top);
        const std::uint8_t* source_row =
            surface.pixels.data() + source_y * surface.stride;
        std::uint8_t* destination_row =
            frame.pixels.data() + static_cast<std::size_t>(y) * frame.stride;
        for (std::int64_t x = left; x < right; ++x) {
            const std::size_t source_x = static_cast<std::size_t>(x - frame_left);
            const std::uint8_t* source = source_row + source_x * 4u;
            if (source[3] == 0) {
                continue;
            }
            std::uint8_t pixel[4] = {source[0], source[1], source[2], source[3]};
            if (surface.format == proto::PixelFormat::rgba8888) {
                std::swap(pixel[0], pixel[2]);
            }
            std::uint8_t* destination =
                destination_row + static_cast<std::size_t>(x) * 4u;
            CpuCompositor::blend_bgra(destination, pixel, opacity);
        }
    }
}

}  // namespace

Result<Frame> CpuCompositor::render(
    const std::vector<scene::WindowSnapshot>& windows, std::uint32_t width,
    std::uint32_t height, Color clear) const {
    if (width == 0 || height == 0) {
        return Result<Frame>::fail(Code::invalid, "frame dimensions must be positive");
    }
    if (width > kMaxDimension || height > kMaxDimension) {
        return Result<Frame>::fail(Code::too_large, "frame dimensions out of range");
    }

    const std::uint64_t byte_count =
        static_cast<std::uint64_t>(width) * height * 4u;
    if (byte_count > std::numeric_limits<std::size_t>::max()) {
        return Result<Frame>::fail(Code::too_large, "frame is too large");
    }

    Frame frame;
    frame.width = width;
    frame.height = height;
    frame.stride = width * 4u;
    try {
        frame.pixels.resize(static_cast<std::size_t>(byte_count));
    } catch (const std::bad_alloc&) {
        return Result<Frame>::fail(Code::too_large, "frame allocation failed");
    } catch (const std::length_error&) {
        return Result<Frame>::fail(Code::too_large, "frame is too large");
    }

    fill_rect(frame, 0, 0, width, height, clear);
    for (const scene::WindowSnapshot& window : windows) {
        draw_window(frame, window, 0, 0, width, height);
    }
    return frame;
}

Result<void> CpuCompositor::render_damage(
    Frame& frame, const std::vector<scene::WindowSnapshot>& windows,
    const proto::Rect& clip, Color clear) const {
    if (!usable_frame(frame)) {
        return Result<void>::fail(Code::invalid, "invalid frame");
    }
    if (clip.w <= 0 || clip.h <= 0) {
        return Result<void>::fail(Code::invalid, "invalid damage clip");
    }

    const std::int64_t clip_left = clip.x;
    const std::int64_t clip_top = clip.y;
    const std::int64_t left = std::max<std::int64_t>(0, clip_left);
    const std::int64_t top = std::max<std::int64_t>(0, clip_top);
    const std::int64_t right = std::min<std::int64_t>(
        frame.width, clip_left + static_cast<std::int64_t>(clip.w));
    const std::int64_t bottom = std::min<std::int64_t>(
        frame.height, clip_top + static_cast<std::int64_t>(clip.h));
    if (left >= right || top >= bottom) {
        return Result<void>::ok();
    }

    fill_rect(frame, left, top, right, bottom, clear);
    for (const scene::WindowSnapshot& window : windows) {
        draw_window(frame, window, left, top, right, bottom);
    }
    return Result<void>::ok();
}

void CpuCompositor::blend_bgra(std::uint8_t* dst, const std::uint8_t* src,
                               float opacity) {
    if (dst == nullptr || src == nullptr) {
        return;
    }
    if (std::isnan(opacity) || opacity <= 0.0f || src[3] == 0) {
        return;
    }
    if (opacity > 1.0f) {
        opacity = 1.0f;
    }

    const double factor =
        static_cast<double>(src[3]) * static_cast<double>(opacity) / 255.0;
    for (int channel = 0; channel < 4; ++channel) {
        const double value = static_cast<double>(src[channel]) *
                                 static_cast<double>(opacity) +
                             static_cast<double>(dst[channel]) * (1.0 - factor);
        const long rounded = std::lround(value);
        dst[channel] = static_cast<std::uint8_t>(
            std::clamp<long>(rounded, 0, 255));
    }
}

}  // namespace ds::render

#include "daysetta/render.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

#include "test.hpp"

namespace {

using ds::render::Color;
using ds::render::CpuCompositor;
using ds::render::Frame;
using ds::scene::Surface;
using ds::scene::WindowSnapshot;

std::shared_ptr<Surface> make_surface(
    std::uint32_t width, std::uint32_t height,
    ds::proto::PixelFormat format = ds::proto::PixelFormat::bgra8888) {
    auto surface = std::make_shared<Surface>();
    surface->width = width;
    surface->height = height;
    surface->stride = width * 4u;
    surface->format = format;
    surface->sequence = 1;
    surface->pixels.assign(
        static_cast<std::size_t>(surface->stride) * height, 0);
    return surface;
}

void put_pixel(Surface& surface, std::uint32_t x, std::uint32_t y,
               std::uint8_t b, std::uint8_t g, std::uint8_t r,
               std::uint8_t a) {
    std::uint8_t* pixel =
        surface.pixels.data() + static_cast<std::size_t>(y) * surface.stride +
        static_cast<std::size_t>(x) * 4u;
    pixel[0] = b;
    pixel[1] = g;
    pixel[2] = r;
    pixel[3] = a;
}

WindowSnapshot make_window(ds::scene::WindowId id, ds::proto::Rect frame,
                           std::int32_t level, float opacity,
                           std::shared_ptr<Surface> surface,
                           std::uint64_t commit_sequence = 1) {
    WindowSnapshot window;
    window.id = id;
    window.client = 1;
    window.frame = frame;
    window.level = level;
    window.visible = true;
    window.opacity = opacity;
    window.title = "window";
    window.surface = std::move(surface);
    window.commit_sequence = commit_sequence;
    return window;
}

std::span<const std::uint8_t> bytes_of(const std::string& text) {
    return std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

const std::uint8_t* pixel_at(const Frame& frame, std::uint32_t x,
                             std::uint32_t y) {
    return frame.pixels.data() + static_cast<std::size_t>(y) * frame.stride +
           static_cast<std::size_t>(x) * 4u;
}

bool pixel_equals(const Frame& frame, std::uint32_t x, std::uint32_t y,
                  std::uint8_t b, std::uint8_t g, std::uint8_t r,
                  std::uint8_t a) {
    const std::uint8_t* pixel = pixel_at(frame, x, y);
    return pixel[0] == b && pixel[1] == g && pixel[2] == r && pixel[3] == a;
}

bool pixel_equals(const Frame& left, std::uint32_t left_x,
                  std::uint32_t left_y, const Frame& right,
                  std::uint32_t right_x, std::uint32_t right_y) {
    const std::uint8_t* a = pixel_at(left, left_x, left_y);
    const std::uint8_t* b = pixel_at(right, right_x, right_y);
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

struct TempFile {
    std::string path;

    TempFile() {
        char name[] = "/tmp/daysetta_render_XXXXXX";
        const int descriptor = ::mkstemp(name);
        DS_CHECK(descriptor >= 0);
        ::close(descriptor);
        path = name;
    }

    ~TempFile() {
        if (!path.empty()) {
            std::remove(path.c_str());
        }
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
};

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    DS_CHECK(input.good());
    std::vector<std::uint8_t> result;
    char byte = 0;
    while (input.get(byte)) {
        result.push_back(static_cast<std::uint8_t>(byte));
    }
    return result;
}

}  // namespace

DS_TEST(compositor_clear_and_opaque_pixel) {
    CpuCompositor compositor;

    auto clear_result = compositor.render({}, 1, 1, Color{20, 40, 60, 128});
    DS_CHECK(clear_result.has_value());
    DS_CHECK_EQ(clear_result->width, 1u);
    DS_CHECK_EQ(clear_result->height, 1u);
    DS_CHECK_EQ(clear_result->stride, 4u);
    const std::vector<std::uint8_t> expected_clear = {10, 20, 30, 128};
    DS_CHECK(clear_result->pixels == expected_clear);

    auto surface = make_surface(1, 1);
    put_pixel(*surface, 0, 0, 10, 20, 30, 255);
    auto result = compositor.render(
        {make_window(1, {0, 0, 1, 1}, 0, 1.0f, surface)}, 1, 1,
        Color::from_rgb(0, 0, 0));
    DS_CHECK(result.has_value());
    const std::vector<std::uint8_t> expected = {10, 20, 30, 255};
    DS_CHECK(result->pixels == expected);
}

DS_TEST(compositor_alpha_blend_exact) {
    std::uint8_t destination[4] = {100, 100, 100, 255};
    const std::uint8_t source[4] = {64, 32, 16, 128};
    CpuCompositor::blend_bgra(destination, source, 0.5f);
    DS_CHECK_EQ(static_cast<int>(destination[0]), 107);
    DS_CHECK_EQ(static_cast<int>(destination[1]), 91);
    DS_CHECK_EQ(static_cast<int>(destination[2]), 83);
    DS_CHECK_EQ(static_cast<int>(destination[3]), 255);

    std::uint8_t unchanged[4] = {1, 2, 3, 4};
    const std::uint8_t transparent[4] = {9, 8, 7, 0};
    CpuCompositor::blend_bgra(unchanged, transparent, 1.0f);
    DS_CHECK_EQ(static_cast<int>(unchanged[0]), 1);
    DS_CHECK_EQ(static_cast<int>(unchanged[3]), 4);
    CpuCompositor::blend_bgra(unchanged, source, -1.0f);
    DS_CHECK_EQ(static_cast<int>(unchanged[0]), 1);
    CpuCompositor::blend_bgra(unchanged, source, std::nanf(""));
    DS_CHECK_EQ(static_cast<int>(unchanged[0]), 1);

    CpuCompositor compositor;
    auto surface = make_surface(1, 1);
    put_pixel(*surface, 0, 0, 0, 0, 255, 255);
    auto result = compositor.render(
        {make_window(1, {0, 0, 1, 1}, 0, 0.5f, surface)}, 1, 1,
        Color::from_rgb(0, 0, 0));
    DS_CHECK(result.has_value());
    const std::vector<std::uint8_t> expected = {0, 0, 128, 255};
    DS_CHECK(result->pixels == expected);

    auto transparent_surface = make_surface(1, 1);
    put_pixel(*transparent_surface, 0, 0, 50, 60, 70, 0);
    auto transparent_result = compositor.render(
        {make_window(2, {0, 0, 1, 1}, 0, 1.0f, transparent_surface)}, 1, 1,
        Color::from_rgb(1, 2, 3));
    DS_CHECK(transparent_result.has_value());
    DS_CHECK(pixel_equals(*transparent_result, 0, 0, 3, 2, 1, 255));
}

DS_TEST(compositor_clips_negative_and_oversized_windows) {
    CpuCompositor compositor;
    auto surface = make_surface(3, 3);
    for (std::uint32_t y = 0; y < 3; ++y) {
        for (std::uint32_t x = 0; x < 3; ++x) {
            put_pixel(*surface, x, y,
                      static_cast<std::uint8_t>(10 + x + 3 * y), 100, 200,
                      255);
        }
    }
    auto result = compositor.render(
        {make_window(1, {-1, -1, 3, 3}, 0, 1.0f, surface)}, 3, 2,
        Color::from_rgb(0, 0, 255));
    DS_CHECK(result.has_value());
    DS_CHECK(pixel_equals(*result, 0, 0, 14, 100, 200, 255));
    DS_CHECK(pixel_equals(*result, 1, 0, 15, 100, 200, 255));
    DS_CHECK(pixel_equals(*result, 0, 1, 17, 100, 200, 255));
    DS_CHECK(pixel_equals(*result, 1, 1, 18, 100, 200, 255));
    DS_CHECK(pixel_equals(*result, 2, 0, 255, 0, 0, 255));
    DS_CHECK(pixel_equals(*result, 2, 1, 255, 0, 0, 255));

    auto large_surface = make_surface(2, 2);
    put_pixel(*large_surface, 0, 0, 1, 2, 3, 255);
    auto large_result = compositor.render(
        {make_window(2, {1, 1, 100, 100}, 0, 1.0f, large_surface)}, 3, 2,
        Color::from_rgb(0, 0, 255));
    DS_CHECK(large_result.has_value());
    DS_CHECK(pixel_equals(*large_result, 1, 1, 1, 2, 3, 255));
    DS_CHECK(pixel_equals(*large_result, 0, 0, 255, 0, 0, 255));
    DS_CHECK(pixel_equals(*large_result, 2, 0, 255, 0, 0, 255));
    DS_CHECK(pixel_equals(*large_result, 0, 1, 255, 0, 0, 255));
}

DS_TEST(compositor_swaps_rgba_source) {
    CpuCompositor compositor;
    auto surface = make_surface(1, 1, ds::proto::PixelFormat::rgba8888);
    surface->pixels = {10, 20, 30, 255};
    auto result = compositor.render(
        {make_window(1, {0, 0, 1, 1}, 0, 1.0f, surface)}, 1, 1,
        Color::from_rgb(0, 0, 0));
    DS_CHECK(result.has_value());
    const std::vector<std::uint8_t> expected = {30, 20, 10, 255};
    DS_CHECK(result->pixels == expected);
}

DS_TEST(compositor_damage_only_changes_clip) {
    CpuCompositor compositor;
    auto rendered = compositor.render({}, 4, 4, Color::from_rgb(10, 20, 30));
    DS_CHECK(rendered.has_value());
    Frame frame = std::move(*rendered);
    const Frame before = frame;

    auto surface = make_surface(4, 4);
    for (std::uint32_t y = 0; y < 4; ++y) {
        for (std::uint32_t x = 0; x < 4; ++x) {
            put_pixel(*surface, x, y, 0, 255, 0, 255);
        }
    }
    const std::vector<WindowSnapshot> windows = {
        make_window(1, {0, 0, 4, 4}, 0, 1.0f, surface)};

    DS_CHECK(compositor
                 .render_damage(frame, windows, {1, 1, 2, 2},
                                Color::from_rgb(0, 0, 0))
                 .has_value());
    for (std::uint32_t y = 0; y < 4; ++y) {
        for (std::uint32_t x = 0; x < 4; ++x) {
            const bool inside = x >= 1 && x < 3 && y >= 1 && y < 3;
            if (inside) {
                DS_CHECK(pixel_equals(frame, x, y, 0, 255, 0, 255));
            } else {
                DS_CHECK(pixel_equals(frame, x, y, before, x, y));
            }
        }
    }

    const Frame after_window = frame;
    DS_CHECK(compositor
                 .render_damage(frame, {}, {0, 0, 1, 1},
                                Color::from_rgb(255, 0, 0))
                 .has_value());
    DS_CHECK(pixel_equals(frame, 0, 0, 0, 0, 255, 255));
    for (std::uint32_t y = 0; y < 4; ++y) {
        for (std::uint32_t x = 0; x < 4; ++x) {
            if (x == 0 && y == 0) {
                continue;
            }
            DS_CHECK(pixel_equals(frame, x, y, after_window, x, y));
        }
    }

    const Frame before_invalid = frame;
    DS_CHECK(!compositor
                  .render_damage(frame, windows, {0, 0, 0, 2},
                                 Color::from_rgb(0, 0, 0))
                  .has_value());
    DS_CHECK(frame.pixels == before_invalid.pixels);

    DS_CHECK(compositor
                 .render_damage(frame, windows, {20, 20, 2, 2},
                                Color::from_rgb(0, 0, 0))
                 .has_value());
    DS_CHECK(frame.pixels == before_invalid.pixels);
}

DS_TEST(compositor_sha256_and_checksum_vectors) {
    DS_CHECK_EQ(ds::render::sha256_hex(std::span<const std::uint8_t>()),
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const std::string abc = "abc";
    DS_CHECK_EQ(ds::render::sha256_hex(bytes_of(abc)),
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    DS_CHECK_EQ(ds::render::checksum32(std::span<const std::uint8_t>()),
                0x811c9dc5u);
    DS_CHECK_EQ(ds::render::checksum32(bytes_of(abc)), 0x1a47e90bu);
    DS_CHECK_EQ(ds::render::checksum32(bytes_of("hello")), 0x4f9f2cabu);
}

DS_TEST(compositor_output_ppm_pam_and_bgra) {
    Frame frame;
    frame.width = 2;
    frame.height = 1;
    frame.stride = 8;
    frame.pixels = {1, 2, 3, 255, 4, 5, 6, 128};

    TempFile ppm;
    DS_CHECK(ds::render::write_ppm(ppm.path, frame).has_value());
    const std::vector<std::uint8_t> ppm_bytes = read_file(ppm.path);
    const std::string ppm_header = "P6\n2 1\n255\n";
    const std::vector<std::uint8_t> expected_ppm = {
        'P', '6', '\n', '2', ' ', '1', '\n', '2', '5', '5', '\n',
        3,   2,   1,    6,   5,   4};
    DS_CHECK(ppm_bytes == expected_ppm);
    DS_CHECK(std::string(ppm_bytes.begin(),
                         ppm_bytes.begin() +
                             static_cast<std::ptrdiff_t>(ppm_header.size())) ==
             ppm_header);

    TempFile pam;
    DS_CHECK(ds::render::write_pam(pam.path, frame).has_value());
    const std::vector<std::uint8_t> pam_bytes = read_file(pam.path);
    const std::string pam_header =
        "P7\nWIDTH 2\nHEIGHT 1\nDEPTH 4\nMAXVAL 255\n"
        "TUPLTYPE RGBA_8888\nENDHDR\n";
    const std::vector<std::uint8_t> expected_pam = {
        'P', '7', '\n', 'W', 'I', 'D', 'T', 'H', ' ', '2', '\n', 'H',
        'E', 'I', 'G', 'H', 'T', ' ', '1', '\n', 'D', 'E', 'P', 'T',
        'H', ' ', '4', '\n', 'M', 'A', 'X', 'V', 'A', 'L', ' ', '2',
        '5', '5', '\n', 'T', 'U', 'P', 'L', 'T', 'Y', 'P', 'E', ' ',
        'R', 'G', 'B', 'A', '_', '8', '8', '8', '8', '\n', 'E', 'N',
        'D', 'H', 'D', 'R', '\n', 3,   2,   1,   255, 6,   5,   4,   128};
    DS_CHECK(pam_bytes == expected_pam);
    DS_CHECK(std::string(pam_bytes.begin(),
                         pam_bytes.begin() +
                             static_cast<std::ptrdiff_t>(pam_header.size())) ==
             pam_header);

    TempFile bgra;
    DS_CHECK(ds::render::write_bgra(bgra.path, frame).has_value());
    DS_CHECK(read_file(bgra.path) == frame.pixels);

    const std::string missing =
        "/tmp/daysetta_missing_" + std::to_string(::getpid()) + "/frame.ppm";
    DS_CHECK(ds::render::write_ppm(missing, frame).code() == ds::Code::io);
}

DS_TEST(compositor_rejects_bad_frame_size_and_frame) {
    CpuCompositor compositor;
    DS_CHECK(compositor.render({}, 0, 1, Color{}).code() == ds::Code::invalid);
    DS_CHECK(compositor.render({}, 1, 0, Color{}).code() == ds::Code::invalid);
    DS_CHECK(compositor.render({}, 16385, 1, Color{}).code() == ds::Code::too_large);
    DS_CHECK(compositor.render({}, 1, 16385, Color{}).code() == ds::Code::too_large);

    Frame invalid;
    DS_CHECK(compositor
                    .render_damage(invalid, {}, {0, 0, 1, 1}, Color{})
                    .code() == ds::Code::invalid);
}

DS_TEST(compositor_deterministic_scene_hash) {
    CpuCompositor compositor;
    std::vector<WindowSnapshot> windows;

    auto lower = make_surface(4, 4);
    for (std::uint32_t y = 0; y < 4; ++y) {
        for (std::uint32_t x = 0; x < 4; ++x) {
            put_pixel(*lower, x, y, static_cast<std::uint8_t>(x * 20),
                      static_cast<std::uint8_t>(y * 30),
                      static_cast<std::uint8_t>(x * 10 + y), 200);
        }
    }
    windows.push_back(make_window(1, {-1, -1, 4, 4}, 0, 0.75f, lower, 1));

    auto middle = make_surface(3, 5, ds::proto::PixelFormat::rgba8888);
    for (std::size_t i = 0; i < middle->pixels.size(); i += 4) {
        middle->pixels[i] = static_cast<std::uint8_t>(i);
        middle->pixels[i + 1] = 99;
        middle->pixels[i + 2] = static_cast<std::uint8_t>(255 - i);
        middle->pixels[i + 3] = 255;
    }
    windows.push_back(make_window(2, {2, 0, 3, 5}, 0, 0.5f, middle, 2));

    auto top = make_surface(2, 2);
    put_pixel(*top, 0, 0, 255, 0, 0, 128);
    put_pixel(*top, 1, 0, 0, 255, 0, 128);
    put_pixel(*top, 0, 1, 0, 0, 255, 128);
    put_pixel(*top, 1, 1, 255, 255, 255, 255);
    windows.push_back(make_window(3, {0, 2, 2, 2}, 1, 1.0f, top, 3));

    const Color clear = Color{17, 34, 51, 255};
    auto first = compositor.render(windows, 5, 4, clear);
    auto second = compositor.render(windows, 5, 4, clear);
    DS_CHECK(first.has_value());
    DS_CHECK(second.has_value());
    DS_CHECK(first->pixels == second->pixels);

    const std::string first_hash = ds::render::sha256_hex(first->pixels);
    const std::string second_hash = ds::render::sha256_hex(second->pixels);
    DS_CHECK_EQ(first_hash, second_hash);
    DS_CHECK_EQ(first_hash.size(), 64u);
}

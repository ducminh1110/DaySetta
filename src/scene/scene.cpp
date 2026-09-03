#include "daysetta/scene.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <utility>

namespace ds::scene {
namespace {

constexpr std::int64_t kMaxCoordinate = 1 << 20;
constexpr std::int32_t kMaxDimension = 16384;
constexpr std::int32_t kMaxLevel = 10000;
constexpr std::size_t kMaxTitle = 4096;
constexpr std::uint32_t kMaxStride = 65536;

void append_u64(std::string& out, std::uint64_t value) {
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    out.append(buffer, result.ptr);
}

void append_i32(std::string& out, std::int32_t value) {
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    out.append(buffer, result.ptr);
}

void append_float(std::string& out, float value) {
    if (!std::isfinite(value)) {
        out += '0';
        return;
    }
    char buffer[64];
    const auto result =
        std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general);
    if (result.ec != std::errc{}) {
        out += '0';
        return;
    }
    out.append(buffer, result.ptr);
}

void append_json_string(std::string& out, std::string_view value) {
    static constexpr char kHex[] = "0123456789abcdef";
    out.push_back('"');
    for (const unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out.push_back(kHex[c >> 4]);
                    out.push_back(kHex[c & 0x0f]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    out.push_back('"');
}

WindowSnapshot snapshot_of(const Window& window) {
    WindowSnapshot snapshot;
    snapshot.id = window.id;
    snapshot.client = window.client;
    snapshot.frame = window.frame;
    snapshot.level = window.level;
    snapshot.visible = window.visible;
    snapshot.opacity = window.opacity;
    snapshot.title = window.title;
    snapshot.surface = window.surface;
    snapshot.commit_sequence = window.commit_sequence;
    return snapshot;
}

bool snapshot_less(const WindowSnapshot& a, const WindowSnapshot& b) {
    return std::tie(a.level, a.commit_sequence, a.id) <
           std::tie(b.level, b.commit_sequence, b.id);
}

}  // namespace

ClientId Scene::add_client() {
    std::lock_guard<std::mutex> lock(mutex_);
    return next_client_++;
}

void Scene::remove_client(ClientId client) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = windows_.begin(); it != windows_.end();) {
        if (it->second.client == client) {
            it = windows_.erase(it);
            ++counters_.windows_destroyed;
        } else {
            ++it;
        }
    }
}

Result<WindowId> Scene::create_window(ClientId client, const proto::Rect& frame,
                                      std::int32_t level, std::string title) {
    if (!valid_rect(frame)) {
        return Result<WindowId>::fail(Code::invalid, "invalid window frame");
    }
    if (level < -kMaxLevel || level > kMaxLevel) {
        return Result<WindowId>::fail(Code::invalid, "window level out of range");
    }
    if (title.size() > kMaxTitle) {
        return Result<WindowId>::fail(Code::invalid, "window title is too long");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    Window window;
    window.id = next_window_++;
    window.client = client;
    window.frame = frame;
    window.level = level;
    window.title = std::move(title);
    window.created_sequence = next_sequence_++;
    const WindowId id = window.id;
    windows_.emplace(id, std::move(window));
    ++counters_.windows_created;
    return id;
}

Result<void> Scene::destroy_window(WindowId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = windows_.find(id);
    if (it == windows_.end()) {
        return Result<void>::fail(Code::not_found, "window not found");
    }
    windows_.erase(it);
    ++counters_.windows_destroyed;
    return Result<void>::ok();
}

Result<void> Scene::set_frame(WindowId id, const proto::Rect& frame) {
    if (!valid_rect(frame)) {
        return Result<void>::fail(Code::invalid, "invalid window frame");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = windows_.find(id);
    if (it == windows_.end()) {
        return Result<void>::fail(Code::not_found, "window not found");
    }
    it->second.frame = frame;
    return Result<void>::ok();
}

Result<void> Scene::set_visible(WindowId id, bool visible) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = windows_.find(id);
    if (it == windows_.end()) {
        return Result<void>::fail(Code::not_found, "window not found");
    }
    it->second.visible = visible;
    return Result<void>::ok();
}

Result<void> Scene::set_opacity(WindowId id, float opacity) {
    if (std::isnan(opacity)) {
        return Result<void>::fail(Code::invalid, "opacity is not a number");
    }
    if (opacity < 0.0f) {
        opacity = 0.0f;
    } else if (opacity > 1.0f) {
        opacity = 1.0f;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = windows_.find(id);
    if (it == windows_.end()) {
        return Result<void>::fail(Code::not_found, "window not found");
    }
    it->second.opacity = opacity;
    return Result<void>::ok();
}

Result<void> Scene::set_title(WindowId id, std::string title) {
    if (title.size() > kMaxTitle) {
        return Result<void>::fail(Code::invalid, "window title is too long");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = windows_.find(id);
    if (it == windows_.end()) {
        return Result<void>::fail(Code::not_found, "window not found");
    }
    it->second.title = std::move(title);
    return Result<void>::ok();
}

Result<void> Scene::order(WindowId id, proto::WindowOrder order) {
    if (order != proto::WindowOrder::raise && order != proto::WindowOrder::lower) {
        return Result<void>::fail(Code::invalid, "invalid window order");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = windows_.find(id);
    if (it == windows_.end()) {
        return Result<void>::fail(Code::not_found, "window not found");
    }

    // New sequence numbers are issued for order changes too.  Raise takes the next
    // (largest) number.  Lower rebuilds the level's order with the selected window
    // first, keeping the relative order of every other window in that level.
    Window& selected = it->second;
    const std::uint64_t selected_sequence = next_sequence_++;
    selected.created_sequence = selected_sequence;

    if (order == proto::WindowOrder::raise) {
        selected.commit_sequence = selected_sequence;
        return Result<void>::ok();
    }

    std::vector<WindowId> level_windows;
    for (const auto& [window_id, window] : windows_) {
        if (window.level == selected.level && window_id != id) {
            level_windows.push_back(window_id);
        }
    }
    std::sort(level_windows.begin(), level_windows.end(),
              [this](WindowId a, WindowId b) {
                  const Window& left = windows_.at(a);
                  const Window& right = windows_.at(b);
                  return std::tie(left.commit_sequence, left.id) <
                         std::tie(right.commit_sequence, right.id);
              });

    selected.commit_sequence = selected_sequence;
    for (const WindowId window_id : level_windows) {
        windows_.at(window_id).commit_sequence = next_sequence_++;
    }
    return Result<void>::ok();
}

Result<std::uint64_t> Scene::commit(WindowId id,
                                    std::shared_ptr<const Surface> surface) {
    if (!surface) {
        return Result<std::uint64_t>::fail(Code::invalid, "surface is null");
    }
    std::string why;
    if (!valid_surface(*surface, &why)) {
        return Result<std::uint64_t>::fail(Code::invalid,
                                           why.empty() ? "invalid surface" : why);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = windows_.find(id);
    if (it == windows_.end()) {
        return Result<std::uint64_t>::fail(Code::not_found, "window not found");
    }
    const std::uint64_t sequence = next_sequence_++;
    it->second.surface = std::move(surface);
    it->second.commit_sequence = sequence;
    ++counters_.commits;
    return sequence;
}

std::vector<WindowSnapshot> Scene::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<WindowSnapshot> result;
    result.reserve(windows_.size());
    for (const auto& [id, window] : windows_) {
        (void)id;
        if (!window.visible) {
            continue;
        }
        result.push_back(snapshot_of(window));
    }
    std::sort(result.begin(), result.end(), snapshot_less);
    return result;
}

std::vector<Window> Scene::windows() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Window> result;
    result.reserve(windows_.size());
    for (const auto& [id, window] : windows_) {
        (void)id;
        result.push_back(window);
    }
    return result;
}

Result<WindowSnapshot> Scene::find(WindowId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = windows_.find(id);
    if (it == windows_.end()) {
        return Result<WindowSnapshot>::fail(Code::not_found, "window not found");
    }
    return snapshot_of(it->second);
}

WindowId Scene::hit_test(std::int32_t x, std::int32_t y) const {
    const std::vector<WindowSnapshot> ordered = snapshot();
    for (auto it = ordered.rbegin(); it != ordered.rend(); ++it) {
        if (!it->surface) {
            continue;
        }
        const std::int64_t left = it->frame.x;
        const std::int64_t top = it->frame.y;
        const std::int64_t right = left + it->frame.w;
        const std::int64_t bottom = top + it->frame.h;
        if (x >= left && x < right && y >= top && y < bottom) {
            return it->id;
        }
    }
    return 0;
}

SceneCounters Scene::counters() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return counters_;
}

void Scene::note_frame_rendered() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++counters_.frames_rendered;
}

void Scene::note_input_event() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++counters_.input_events;
}

std::string Scene::dump_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string json;
    json.reserve(256 + windows_.size() * 256);
    json += "{\"next_window\":";
    append_u64(json, next_window_);
    json += ",\"count\":";
    append_u64(json, static_cast<std::uint64_t>(windows_.size()));
    json += ",\"windows\":[";
    bool first = true;
    for (const auto& [id, window] : windows_) {
        (void)id;
        if (!first) {
            json.push_back(',');
        }
        first = false;
        json += "{\"id\":";
        append_u64(json, window.id);
        json += ",\"client\":";
        append_u64(json, window.client);
        json += ",\"x\":";
        append_i32(json, window.frame.x);
        json += ",\"y\":";
        append_i32(json, window.frame.y);
        json += ",\"w\":";
        append_i32(json, window.frame.w);
        json += ",\"h\":";
        append_i32(json, window.frame.h);
        json += ",\"level\":";
        append_i32(json, window.level);
        json += ",\"visible\":";
        json += window.visible ? "true" : "false";
        json += ",\"opacity\":";
        append_float(json, window.opacity);
        json += ",\"title\":";
        append_json_string(json, window.title);
        json += ",\"surface\":";
        if (window.surface) {
            json += "{\"id\":";
            append_u64(json, window.surface->id);
            json += ",\"width\":";
            append_u64(json, window.surface->width);
            json += ",\"height\":";
            append_u64(json, window.surface->height);
            json += ",\"sequence\":";
            append_u64(json, window.surface->sequence);
            json += ",\"format\":";
            append_u64(json, static_cast<std::uint64_t>(window.surface->format));
            json.push_back('}');
        } else {
            json += "null";
        }
        json += ",\"commit_sequence\":";
        append_u64(json, window.commit_sequence);
        json.push_back('}');
    }
    json += "]}";
    return json;
}

bool Scene::valid_rect(const proto::Rect& r) {
    const std::int64_t x = r.x;
    const std::int64_t y = r.y;
    return r.w > 0 && r.h > 0 && r.w <= kMaxDimension && r.h <= kMaxDimension &&
           x >= -kMaxCoordinate && x <= kMaxCoordinate &&
           y >= -kMaxCoordinate && y <= kMaxCoordinate;
}

bool Scene::valid_surface(const Surface& surface, std::string* why) {
    const auto fail = [why](const char* message) {
        if (why != nullptr) {
            *why = message;
        }
        return false;
    };

    if (surface.format != proto::PixelFormat::bgra8888 &&
        surface.format != proto::PixelFormat::rgba8888) {
        return fail("unsupported surface format");
    }
    if (surface.width == 0 || surface.height == 0 ||
        surface.width > static_cast<std::uint32_t>(kMaxDimension) ||
        surface.height > static_cast<std::uint32_t>(kMaxDimension)) {
        return fail("surface dimensions out of range");
    }
    const std::uint64_t minimum_stride =
        static_cast<std::uint64_t>(surface.width) * 4u;
    if (surface.stride < minimum_stride || surface.stride > kMaxStride) {
        return fail("surface stride out of range");
    }
    const std::uint64_t required =
        static_cast<std::uint64_t>(surface.stride) * surface.height;
    if (surface.pixels.size() < required) {
        return fail("surface buffer is too small");
    }
    if (surface.sequence == 0) {
        return fail("surface sequence must be positive");
    }
    if (why != nullptr) {
        why->clear();
    }
    return true;
}

}  // namespace ds::scene

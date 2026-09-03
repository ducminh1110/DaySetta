// ds-scene: windows, surfaces, ordering and snapshots.  Pure data, no pixels drawn here.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "daysetta/protocol.hpp"
#include "daysetta/result.hpp"

namespace ds::scene {

using SurfaceId = std::uint64_t;
using WindowId = std::uint64_t;
using ClientId = std::uint64_t;

struct Surface {
    SurfaceId id = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride = 0;
    proto::PixelFormat format = proto::PixelFormat::bgra8888;
    std::vector<std::uint8_t> pixels;
    std::uint64_t sequence = 0;
    std::vector<proto::Rect> damage;
};

struct Window {
    WindowId id = 0;
    ClientId client = 0;
    proto::Rect frame;
    std::int32_t level = 0;
    bool visible = false;
    float opacity = 1.0f;
    std::string title;
    std::shared_ptr<const Surface> surface;
    std::uint64_t commit_sequence = 0;
    std::uint64_t created_sequence = 0;
};

struct WindowSnapshot {
    WindowId id = 0;
    ClientId client = 0;
    proto::Rect frame;
    std::int32_t level = 0;
    bool visible = false;
    float opacity = 1.0f;
    std::string title;
    std::shared_ptr<const Surface> surface;
    std::uint64_t commit_sequence = 0;
};

struct SceneCounters {
    std::uint64_t windows_created = 0;
    std::uint64_t windows_destroyed = 0;
    std::uint64_t commits = 0;
    std::uint64_t frames_rendered = 0;
    std::uint64_t input_events = 0;
};

class Scene {
public:
    Scene() = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    ClientId add_client();
    void remove_client(ClientId client);

    Result<WindowId> create_window(ClientId client, const proto::Rect& frame,
                                   std::int32_t level, std::string title);
    Result<void> destroy_window(WindowId id);
    Result<void> set_frame(WindowId id, const proto::Rect& frame);
    Result<void> set_visible(WindowId id, bool visible);
    Result<void> set_opacity(WindowId id, float opacity);
    Result<void> set_title(WindowId id, std::string title);
    Result<void> order(WindowId id, proto::WindowOrder order);

    // Takes ownership of the surface on success.  The commit sequence is assigned by
    // the scene, never by the client.
    Result<std::uint64_t> commit(WindowId id, std::shared_ptr<const Surface> surface);

    // Returns an immutable snapshot ordered bottom-to-top: level ascending, then
    // commit sequence ascending, then window id.
    std::vector<WindowSnapshot> snapshot() const;
    std::vector<Window> windows() const;

    Result<WindowSnapshot> find(WindowId id) const;
    // Topmost visible window containing the point, or 0.
    WindowId hit_test(std::int32_t x, std::int32_t y) const;

    SceneCounters counters() const;
    void note_frame_rendered();
    void note_input_event();

    // JSON with stable field order; used by daysetta-ctl and tests.
    std::string dump_json() const;

    static bool valid_rect(const proto::Rect& r);
    static bool valid_surface(const Surface& s, std::string* why);

private:
    mutable std::mutex mutex_;
    std::map<WindowId, Window> windows_;
    WindowId next_window_ = 1;
    ClientId next_client_ = 1;
    std::uint64_t next_sequence_ = 1;
    SceneCounters counters_;
};

}  // namespace ds::scene

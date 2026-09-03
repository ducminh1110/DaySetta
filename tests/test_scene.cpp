#include "daysetta/scene.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "test.hpp"

namespace {

using ds::Code;
using ds::scene::Scene;
using ds::scene::Surface;
using ds::scene::WindowId;

std::shared_ptr<Surface> make_surface(
    std::uint32_t width = 2, std::uint32_t height = 1,
    ds::proto::PixelFormat format = ds::proto::PixelFormat::bgra8888,
    std::uint64_t sequence = 1) {
    auto surface = std::make_shared<Surface>();
    surface->width = width;
    surface->height = height;
    surface->stride = width * 4u;
    surface->format = format;
    surface->sequence = sequence;
    surface->pixels.assign(
        static_cast<std::size_t>(surface->stride) * height, 0x7fu);
    return surface;
}

std::vector<WindowId> snapshot_ids(const Scene& scene) {
    std::vector<WindowId> result;
    for (const auto& window : scene.snapshot()) {
        result.push_back(window.id);
    }
    return result;
}

void show_all(Scene& scene, const std::vector<WindowId>& ids) {
    for (const WindowId id : ids) {
        DS_CHECK(scene.set_visible(id, true).has_value());
    }
}

}  // namespace

DS_TEST(scene_ids_are_monotonic_and_not_reused) {
    Scene scene;
    const auto client1 = scene.add_client();
    const auto client2 = scene.add_client();
    DS_CHECK_EQ(client1, 1u);
    DS_CHECK_EQ(client2, 2u);

    auto first = scene.create_window(client1, {0, 0, 10, 10}, 0, "first");
    DS_CHECK(first.has_value());
    DS_CHECK_EQ(*first, 1u);

    auto second = scene.create_window(client2, {10, 0, 10, 10}, 0, "second");
    DS_CHECK(second.has_value());
    DS_CHECK_EQ(*second, 2u);

    DS_CHECK(scene.destroy_window(*first).has_value());
    auto third = scene.create_window(client1, {20, 0, 10, 10}, 0, "third");
    DS_CHECK(third.has_value());
    DS_CHECK_EQ(*third, 3u);

    DS_CHECK(scene.destroy_window(*second).has_value());
    DS_CHECK(scene.destroy_window(*third).has_value());
    auto fourth = scene.create_window(client2, {30, 0, 10, 10}, 0, "fourth");
    DS_CHECK(fourth.has_value());
    DS_CHECK_EQ(*fourth, 4u);

    const auto counters = scene.counters();
    DS_CHECK_EQ(counters.windows_created, 4u);
    DS_CHECK_EQ(counters.windows_destroyed, 3u);
}

DS_TEST(scene_orders_by_level_then_commit) {
    Scene scene;
    const auto client = scene.add_client();

    const auto a = *scene.create_window(client, {0, 0, 10, 10}, 0, "a");
    const auto b = *scene.create_window(client, {10, 0, 10, 10}, 0, "b");
    const auto c = *scene.create_window(client, {20, 0, 10, 10}, 1, "c");
    show_all(scene, {a, b, c});

    const std::uint64_t commit_a = *scene.commit(a, make_surface());
    const std::uint64_t commit_b = *scene.commit(b, make_surface());
    const std::uint64_t commit_c = *scene.commit(c, make_surface());
    DS_CHECK(commit_a < commit_b);
    DS_CHECK(commit_b < commit_c);

    std::vector<WindowId> expected = {a, b, c};
    DS_CHECK(snapshot_ids(scene) == expected);

    const std::uint64_t recommit_a = *scene.commit(a, make_surface());
    DS_CHECK(recommit_a > commit_c);
    expected = {b, a, c};
    DS_CHECK(snapshot_ids(scene) == expected);

    const auto snapshot = scene.snapshot();
    DS_CHECK_EQ(snapshot.size(), 3u);
    DS_CHECK(snapshot[0].surface != nullptr);
    DS_CHECK_EQ(snapshot[0].commit_sequence, commit_b);
    DS_CHECK_EQ(snapshot[1].commit_sequence, recommit_a);
}

DS_TEST(scene_visibility_and_hit_test) {
    Scene scene;
    const auto client = scene.add_client();

    const auto bottom = *scene.create_window(client, {0, 0, 10, 10}, 0, "bottom");
    const auto top = *scene.create_window(client, {5, 5, 10, 10}, 0, "top");
    DS_CHECK(scene.commit(bottom, make_surface()).has_value());
    DS_CHECK(scene.commit(top, make_surface()).has_value());

    DS_CHECK_EQ(scene.snapshot().size(), 0u);
    DS_CHECK_EQ(scene.hit_test(6, 6), 0u);

    DS_CHECK(scene.set_visible(bottom, true).has_value());
    DS_CHECK_EQ(scene.hit_test(1, 1), bottom);
    DS_CHECK_EQ(scene.hit_test(6, 6), bottom);

    DS_CHECK(scene.set_visible(top, true).has_value());
    DS_CHECK_EQ(scene.hit_test(6, 6), top);
    DS_CHECK_EQ(scene.hit_test(14, 14), top);
    DS_CHECK_EQ(scene.hit_test(15, 15), 0u);

    DS_CHECK(scene.set_visible(top, false).has_value());
    DS_CHECK_EQ(scene.hit_test(6, 6), bottom);
    DS_CHECK_EQ(scene.snapshot().size(), 1u);

    const auto empty_top =
        *scene.create_window(client, {0, 0, 20, 20}, 2, "empty-top");
    DS_CHECK(scene.set_visible(empty_top, true).has_value());
    DS_CHECK_EQ(scene.hit_test(6, 6), bottom);
    DS_CHECK_EQ(scene.hit_test(19, 19), 0u);

    const auto snapshot = scene.snapshot();
    DS_CHECK_EQ(snapshot.size(), 2u);
    DS_CHECK_EQ(snapshot.back().id, empty_top);
    DS_CHECK(snapshot.back().surface == nullptr);
}

DS_TEST(scene_order_raises_and_lowers) {
    Scene scene;
    const auto client = scene.add_client();
    const auto a = *scene.create_window(client, {0, 0, 10, 10}, 0, "a");
    const auto b = *scene.create_window(client, {10, 0, 10, 10}, 0, "b");
    const auto c = *scene.create_window(client, {20, 0, 10, 10}, 0, "c");
    show_all(scene, {a, b, c});
    DS_CHECK(scene.commit(a, make_surface()).has_value());
    DS_CHECK(scene.commit(b, make_surface()).has_value());
    DS_CHECK(scene.commit(c, make_surface()).has_value());

    std::vector<WindowId> expected = {a, b, c};
    DS_CHECK(snapshot_ids(scene) == expected);

    DS_CHECK(scene.order(a, ds::proto::WindowOrder::raise).has_value());
    expected = {b, c, a};
    DS_CHECK(snapshot_ids(scene) == expected);

    DS_CHECK(scene.order(a, ds::proto::WindowOrder::lower).has_value());
    expected = {a, b, c};
    DS_CHECK(snapshot_ids(scene) == expected);

    DS_CHECK(scene.order(c, ds::proto::WindowOrder::lower).has_value());
    expected = {c, a, b};
    DS_CHECK(snapshot_ids(scene) == expected);

    DS_CHECK(scene.order(c, ds::proto::WindowOrder::raise).has_value());
    expected = {a, b, c};
    DS_CHECK(snapshot_ids(scene) == expected);

    DS_CHECK(scene.order(999, ds::proto::WindowOrder::raise).code() ==
             Code::not_found);
    DS_CHECK(scene.order(
                    a, static_cast<ds::proto::WindowOrder>(77)).code() ==
             Code::invalid);
}

DS_TEST(scene_rejects_invalid_rect_level_title_and_surface) {
    Scene scene;
    const auto client = scene.add_client();

    const auto invalid_rect = [&](const ds::proto::Rect& rect) {
        auto result = scene.create_window(client, rect, 0, "bad");
        DS_CHECK(!result.has_value());
        DS_CHECK(result.code() == Code::invalid);
    };
    invalid_rect({0, 0, 0, 10});
    invalid_rect({0, 0, 10, 0});
    invalid_rect({0, 0, -1, 10});
    invalid_rect({0, 0, 16385, 10});
    invalid_rect({0, 0, 10, 16385});
    invalid_rect({(1 << 20) + 1, 0, 10, 10});
    invalid_rect({0, -((1 << 20) + 1), 10, 10});

    DS_CHECK(scene.create_window(client, {0, 0, 10, 10}, 10001, "x").code() == Code::invalid);
    DS_CHECK(scene.create_window(client, {0, 0, 10, 10}, -10001, "x").code() == Code::invalid);
    DS_CHECK(
        scene.create_window(client, {0, 0, 10, 10}, 0, std::string(4097, 'x'))
            .code() == Code::invalid);

    auto window = scene.create_window(client, {0, 0, 10, 10}, 0, "good");
    DS_CHECK(window.has_value());
    DS_CHECK(scene.set_frame(*window, {0, 0, 0, 0}).code() == Code::invalid);
    DS_CHECK(scene.set_title(*window, std::string(4097, 'x')).code() == Code::invalid);
    DS_CHECK(scene.set_opacity(*window, std::nanf("")).code() == Code::invalid);
    DS_CHECK(scene.set_opacity(999, 0.5f).code() == Code::not_found);

    std::string why;
    DS_CHECK(Scene::valid_surface(*make_surface(), &why));
    DS_CHECK(why.empty());

    auto bad_format = make_surface();
    bad_format->format = static_cast<ds::proto::PixelFormat>(99);
    DS_CHECK(!Scene::valid_surface(*bad_format, &why));
    DS_CHECK(!why.empty());

    auto bad_width = make_surface();
    bad_width->width = 0;
    DS_CHECK(!Scene::valid_surface(*bad_width, &why));

    auto bad_height = make_surface();
    bad_height->height = 0;
    DS_CHECK(!Scene::valid_surface(*bad_height, &why));

    auto bad_stride_small = make_surface();
    bad_stride_small->stride -= 1;
    DS_CHECK(!Scene::valid_surface(*bad_stride_small, &why));

    auto bad_stride_large = make_surface(1, 1);
    bad_stride_large->stride = 65537;
    bad_stride_large->pixels.resize(65537);
    DS_CHECK(!Scene::valid_surface(*bad_stride_large, &why));

    auto bad_pixels = make_surface(4, 4);
    bad_pixels->pixels.resize(bad_pixels->pixels.size() - 1);
    DS_CHECK(!Scene::valid_surface(*bad_pixels, &why));

    auto bad_sequence = make_surface();
    bad_sequence->sequence = 0;
    DS_CHECK(!Scene::valid_surface(*bad_sequence, &why));

    DS_CHECK(scene.commit(*window, nullptr).code() == Code::invalid);
    DS_CHECK(scene.commit(*window, bad_format).code() == Code::invalid);
    DS_CHECK(scene.find(*window)->surface == nullptr);
    DS_CHECK(scene.commit(999, make_surface()).code() == Code::not_found);
}

DS_TEST(scene_opacity_clamp_and_json_escaping) {
    Scene scene;
    const auto client = scene.add_client();
    std::string title = "a\"b\\c\n\t";
    title.push_back('\x01');

    const auto first =
        *scene.create_window(client, {-2, 3, 10, 20}, -5, title);
    const auto second =
        *scene.create_window(client, {4, 5, 6, 7}, 0, "plain");
    DS_CHECK(scene.set_visible(first, true).has_value());

    auto surface = make_surface(2, 1, ds::proto::PixelFormat::rgba8888, 9);
    surface->id = 77;
    DS_CHECK(scene.commit(first, surface).has_value());

    DS_CHECK(scene.set_opacity(first, 1.5f).has_value());
    DS_CHECK(scene.find(first)->opacity == 1.0f);
    DS_CHECK(scene.set_opacity(first, -0.25f).has_value());
    DS_CHECK(scene.find(first)->opacity == 0.0f);
    DS_CHECK(scene.set_opacity(first, std::nanf("")).code() == Code::invalid);
    DS_CHECK(scene.find(first)->opacity == 0.0f);

    const std::string json = scene.dump_json();
    DS_CHECK(json.rfind("{\"next_window\":3,\"count\":2,\"windows\":[", 0) ==
             0);
    DS_CHECK(json.find(
                 R"("title":"a\"b\\c\n\t\u0001")") != std::string::npos);
    DS_CHECK(json.find(
                 "\"surface\":{\"id\":77,\"width\":2,\"height\":1,"
                 "\"sequence\":9,\"format\":2}") != std::string::npos);
    DS_CHECK(json.find("\"opacity\":0") != std::string::npos);
    DS_CHECK(json.find("\"visible\":true") != std::string::npos);
    DS_CHECK(json.find("\"commit_sequence\":") != std::string::npos);
    DS_CHECK(json.find("\"surface\":null") != std::string::npos);
    DS_CHECK_EQ(json.back(), '}');
    DS_CHECK(json.find("\"id\":") < json.find("\"client\":"));
    DS_CHECK(json.find("\"visible\":") < json.find("\"opacity\":"));
    DS_CHECK(json.find("\"surface\":") < json.find("\"commit_sequence\":"));
    (void)second;
}

DS_TEST(scene_counters_and_client_removal) {
    Scene scene;
    const auto client1 = scene.add_client();
    const auto client2 = scene.add_client();
    const auto a = *scene.create_window(client1, {0, 0, 4, 4}, 0, "a");
    const auto b = *scene.create_window(client1, {1, 1, 4, 4}, 0, "b");
    const auto c = *scene.create_window(client2, {2, 2, 4, 4}, 0, "c");
    DS_CHECK(scene.commit(a, make_surface()).has_value());
    scene.note_frame_rendered();
    scene.note_frame_rendered();
    scene.note_input_event();

    auto counters = scene.counters();
    DS_CHECK_EQ(counters.windows_created, 3u);
    DS_CHECK_EQ(counters.windows_destroyed, 0u);
    DS_CHECK_EQ(counters.commits, 1u);
    DS_CHECK_EQ(counters.frames_rendered, 2u);
    DS_CHECK_EQ(counters.input_events, 1u);

    scene.remove_client(client1);
    counters = scene.counters();
    DS_CHECK_EQ(counters.windows_destroyed, 2u);
    DS_CHECK_EQ(scene.windows().size(), 1u);
    DS_CHECK(scene.find(a).code() == Code::not_found);
    DS_CHECK(scene.find(b).code() == Code::not_found);
    DS_CHECK(scene.find(c).has_value());

    scene.remove_client(client1);
    DS_CHECK_EQ(scene.counters().windows_destroyed, 2u);

    DS_CHECK(scene.destroy_window(c).has_value());
    DS_CHECK_EQ(scene.counters().windows_destroyed, 3u);
    DS_CHECK_EQ(scene.add_client(), 3u);
}

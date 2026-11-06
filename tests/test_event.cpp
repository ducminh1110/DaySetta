#include <utility>

#include "daysetta/event.hpp"
#include "test.hpp"

namespace {

ds::event::Event make(std::int32_t a) {
    ds::event::Event e;
    e.type = ds::event::Type::key_down;
    e.a = a;
    return e;
}

ds::event::Ring make_ring() {
    auto ring = ds::event::Ring::create(8, true);
    if (!ring) throw ds::test::Failure("Ring::create: " + ring.error().message);
    return std::move(ring.value());
}

}  // namespace

DS_TEST(event_ring_move) {
    using ds::event::Event;
    using ds::event::Ring;
    Event out;

    // Move construction: source becomes empty, destination keeps the data.
    Ring a = make_ring();
    DS_CHECK(a.push(make(1)));
    Ring b(std::move(a));
    DS_CHECK(!a.valid());
    DS_CHECK(!a.push(make(9)));
    DS_CHECK(!a.pop(&out));
    DS_CHECK(b.valid());
    DS_CHECK(b.pop(&out));
    DS_CHECK_EQ(out.a, 1);
    DS_CHECK(b.push(make(2)));  // mapping still writable after the move

    // Move assignment into a ring that already owns a mapping.
    Ring c = make_ring();
    DS_CHECK(c.push(make(3)));
    c = std::move(b);
    DS_CHECK(!b.valid());
    DS_CHECK(c.valid());
    DS_CHECK(c.pop(&out));
    DS_CHECK_EQ(out.a, 2);
    DS_CHECK(c.push(make(4)));

    // Self move-assignment keeps the mapping.
    Ring& alias = c;
    c = std::move(alias);
    DS_CHECK(c.valid());
    DS_CHECK(c.pop(&out));
    DS_CHECK_EQ(out.a, 4);

    // Assigning into an empty ring.
    Ring d;
    d = std::move(c);
    DS_CHECK(!c.valid());
    DS_CHECK(d.push(make(5)));
    DS_CHECK(d.pop(&out));
    DS_CHECK_EQ(out.a, 5);
}

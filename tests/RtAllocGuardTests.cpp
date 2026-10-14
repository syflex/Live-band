#include "common/RtAllocGuard.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

namespace rt = lb::rt;

TEST_CASE("RtAllocGuard is active in the test executable")
{
    CHECK(rt::isGuardActive());
}

TEST_CASE("RtAllocGuard counts allocations only inside a real-time section")
{
    const auto before = rt::allocationCount();

    auto outside = std::make_unique<int>(1);
    CHECK(rt::allocationCount() == before);

    {
        rt::ScopedRealtimeSection section;
        CHECK(rt::isInRealtimeSection());
        auto inside = std::make_unique<int>(2);
        std::vector<float> alsoInside(16);
    }

    CHECK_FALSE(rt::isInRealtimeSection());
    CHECK(rt::allocationCount() == before + 2);
}

TEST_CASE("RtAllocGuard sections nest")
{
    rt::ScopedRealtimeSection outer;
    {
        rt::ScopedRealtimeSection inner;
        CHECK(rt::isInRealtimeSection());
    }
    CHECK(rt::isInRealtimeSection());
}

#include "common/RtAllocGuard.h"

#include <atomic>

namespace lb::rt
{
namespace
{
thread_local bool inRealtimeSection = false;
std::atomic<std::uint64_t> realtimeAllocations { 0 };
std::atomic<bool> guardActive { false };
} // namespace

ScopedRealtimeSection::ScopedRealtimeSection() noexcept
    : wasInSection_(inRealtimeSection)
{
    inRealtimeSection = true;
}

ScopedRealtimeSection::~ScopedRealtimeSection() noexcept
{
    inRealtimeSection = wasInSection_;
}

bool isInRealtimeSection() noexcept
{
    return inRealtimeSection;
}

std::uint64_t allocationCount() noexcept
{
    return realtimeAllocations.load(std::memory_order_relaxed);
}

bool isGuardActive() noexcept
{
    return guardActive.load(std::memory_order_relaxed);
}

namespace detail
{
void onAllocation() noexcept
{
    if (inRealtimeSection)
        realtimeAllocations.fetch_add(1, std::memory_order_relaxed);
}

void markGuardActive() noexcept
{
    guardActive.store(true, std::memory_order_relaxed);
}
} // namespace detail

} // namespace lb::rt

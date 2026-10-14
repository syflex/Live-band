#pragma once

#include <cstdint>

namespace lb::rt
{

// Marks the current thread as running real-time audio code for the lifetime of the scope.
//
// When RtAllocGuardNew.cpp is linked into the program (Debug builds of the app, and the
// unit tests), every heap allocation made inside such a scope is counted, so the UI can
// show the mistake the first time it happens. Without that file the scope costs one
// thread-local write and the count stays at zero.
class ScopedRealtimeSection
{
public:
    ScopedRealtimeSection() noexcept;
    ~ScopedRealtimeSection() noexcept;

    ScopedRealtimeSection(const ScopedRealtimeSection&) = delete;
    ScopedRealtimeSection& operator=(const ScopedRealtimeSection&) = delete;

private:
    bool wasInSection_;
};

bool isInRealtimeSection() noexcept;

// Total allocations seen inside real-time sections, on any thread, since the program started.
std::uint64_t allocationCount() noexcept;

// True when the counting operator new is linked in.
bool isGuardActive() noexcept;

namespace detail
{
// Called by the replacement operator new for every allocation.
void onAllocation() noexcept;
void markGuardActive() noexcept;
} // namespace detail

} // namespace lb::rt

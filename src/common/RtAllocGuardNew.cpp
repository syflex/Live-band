// Replacement global operator new/delete that reports allocations to RtAllocGuard.
//
// Link this file into an executable (never into a library) to count heap allocations made
// inside lb::rt::ScopedRealtimeSection. The app builds it into Debug only; the unit tests
// always include it. Aligned new/delete are left to the standard library, which pairs them
// with its own aligned delete.

#include "common/RtAllocGuard.h"

#include <cstdlib>
#include <new>

namespace
{
void* allocateOrThrow(std::size_t size)
{
    lb::rt::detail::onAllocation();
    if (void* p = std::malloc(size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void* allocateOrNull(std::size_t size) noexcept
{
    lb::rt::detail::onAllocation();
    return std::malloc(size == 0 ? 1 : size);
}

struct GuardRegistration
{
    GuardRegistration() noexcept { lb::rt::detail::markGuardActive(); }
};

const GuardRegistration registration;
} // namespace

void* operator new(std::size_t size) { return allocateOrThrow(size); }
void* operator new[](std::size_t size) { return allocateOrThrow(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return allocateOrNull(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return allocateOrNull(size); }

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

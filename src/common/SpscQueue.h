#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <type_traits>

#if defined(_MSC_VER)
 #pragma warning(push)
 #pragma warning(disable : 4324) // structure was padded due to alignment specifier
#endif

namespace lb
{

// Wait-free ring buffer for exactly one producer thread and one consumer thread.
// Storage is allocated once in the constructor; push() and pop() never allocate,
// lock or block, so either side may be the audio thread.
template <typename T>
class SpscQueue
{
    static_assert(std::is_trivially_copyable_v<T>, "SpscQueue elements must be trivially copyable");

public:
    explicit SpscQueue(std::size_t minCapacity)
        : capacity_(roundUpToPowerOfTwo(minCapacity < 2 ? 2 : minCapacity)),
          mask_(capacity_ - 1),
          slots_(std::make_unique<T[]>(capacity_))
    {
    }

    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    // Producer only. Returns false, leaving the queue unchanged, when it is full.
    bool push(const T& item) noexcept
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);

        if (tail - headCache_ == capacity_)
        {
            headCache_ = head_.load(std::memory_order_acquire);
            if (tail - headCache_ == capacity_)
                return false;
        }

        slots_[tail & mask_] = item;
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    // Consumer only. Returns false when the queue is empty.
    bool pop(T& out) noexcept
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);

        if (head == tailCache_)
        {
            tailCache_ = tail_.load(std::memory_order_acquire);
            if (head == tailCache_)
                return false;
        }

        out = slots_[head & mask_];
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    std::size_t capacity() const noexcept { return capacity_; }

    // Exact only when called from one of the two owning threads while the other is idle.
    std::size_t sizeApprox() const noexcept
    {
        return tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_acquire);
    }

private:
    static std::size_t roundUpToPowerOfTwo(std::size_t value) noexcept
    {
        std::size_t result = 1;
        while (result < value)
            result <<= 1;
        return result;
    }

    static constexpr std::size_t cacheLineSize = 64;

    const std::size_t capacity_;
    const std::size_t mask_;
    std::unique_ptr<T[]> slots_;

    // Consumer-owned line: its read position and its cached copy of the producer's position.
    alignas(cacheLineSize) std::atomic<std::size_t> head_ { 0 };
    std::size_t tailCache_ = 0;

    // Producer-owned line.
    alignas(cacheLineSize) std::atomic<std::size_t> tail_ { 0 };
    std::size_t headCache_ = 0;
};

} // namespace lb

#if defined(_MSC_VER)
 #pragma warning(pop)
#endif

#include "common/SpscQueue.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <thread>

using lb::SpscQueue;

TEST_CASE("SpscQueue rounds capacity up to a power of two")
{
    CHECK(SpscQueue<int>(1).capacity() == 2);
    CHECK(SpscQueue<int>(5).capacity() == 8);
    CHECK(SpscQueue<int>(64).capacity() == 64);
}

TEST_CASE("SpscQueue keeps FIFO order and reports full and empty")
{
    SpscQueue<int> queue(4);
    int value = 0;

    CHECK_FALSE(queue.pop(value));

    for (int i = 0; i < 4; ++i)
        CHECK(queue.push(i));
    CHECK_FALSE(queue.push(99));
    CHECK(queue.sizeApprox() == 4);

    for (int i = 0; i < 4; ++i)
    {
        REQUIRE(queue.pop(value));
        CHECK(value == i);
    }
    CHECK_FALSE(queue.pop(value));
}

TEST_CASE("SpscQueue works across wrap-around")
{
    SpscQueue<int> queue(4);
    int value = 0;

    for (int round = 0; round < 10; ++round)
    {
        REQUIRE(queue.push(round * 2));
        REQUIRE(queue.push(round * 2 + 1));
        REQUIRE(queue.pop(value));
        CHECK(value == round * 2);
        REQUIRE(queue.pop(value));
        CHECK(value == round * 2 + 1);
    }
}

TEST_CASE("SpscQueue delivers every item in order between two threads")
{
    constexpr std::uint32_t count = 200000;
    SpscQueue<std::uint32_t> queue(256);

    std::thread producer([&] {
        for (std::uint32_t i = 0; i < count;)
            if (queue.push(i))
                ++i;
    });

    std::uint32_t expected = 0;
    bool inOrder = true;
    while (expected < count)
    {
        std::uint32_t value = 0;
        if (queue.pop(value))
        {
            inOrder = inOrder && (value == expected);
            ++expected;
        }
    }

    producer.join();
    CHECK(inOrder);
}

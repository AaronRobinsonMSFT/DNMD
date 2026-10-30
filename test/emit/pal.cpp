#include "pal.hpp"
#include <gtest/gtest.h>
#include <array>

TEST(CppPal, Sha1HashesEmptySpan)
{
    std::array<uint8_t, pal::SHA1_HASH_SIZE> digest{};
    constexpr std::array<uint8_t, pal::SHA1_HASH_SIZE> expected{
        0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d, 0x32, 0x55,
        0xbf, 0xef, 0x95, 0x60, 0x18, 0x90, 0xaf, 0xd8, 0x07, 0x09};
    ASSERT_TRUE(pal::ComputeSha1Hash({}, digest));
    EXPECT_EQ(expected, digest);
}

TEST(CppPal, Sha1HashesOnlyTheSuppliedSpan)
{
    constexpr std::array<uint8_t, 5> bytes{'!', 'a', 'b', 'c', '?'};
    std::array<uint8_t, pal::SHA1_HASH_SIZE> digest{};
    constexpr std::array<uint8_t, pal::SHA1_HASH_SIZE> expected{
        0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
        0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d};
    ASSERT_TRUE(pal::ComputeSha1Hash({bytes.data() + 1, 3}, digest));
    EXPECT_EQ(expected, digest);
}

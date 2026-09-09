#include "canopen/pdo.hpp"
#include <gtest/gtest.h>
TEST(PDO, EncodesLittleEndianBitsWithoutAliasing) {
    canopen::ObjectDictionary od;
    ASSERT_TRUE(od.add(
        {{0x2000, 0}, 16, true, canopen::ObjectAccess::ReadWrite, {std::byte{0}, std::byte{0}}}));
    canopen::ProcessImage image;
    canopen::PdoPlan plan;
    ASSERT_TRUE(plan.build({0x180, 1, {{{0x2000, 0}, 16, true}}}, od, image));
    ASSERT_TRUE(image.freeze());
    ASSERT_TRUE(image.write(0, 0xFF80));
    can::Frame f;
    ASSERT_TRUE(plan.encode(image, f));
    EXPECT_EQ(static_cast<unsigned char>(f.data[0]), 0x80);
    EXPECT_EQ(static_cast<unsigned char>(f.data[1]), 0xFF);
    canopen::ProcessImage rx;
    canopen::PdoPlan decode;
    ASSERT_TRUE(decode.build({0x180, 1, {{{0x2000, 0}, 16, true}}}, od, rx));
    ASSERT_TRUE(rx.freeze());
    ASSERT_TRUE(decode.decode(f, rx));
    EXPECT_EQ(rx.read(0).value(), 0xFF80U);
}

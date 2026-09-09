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

TEST(PDO, ReusesOneProcessSlotAcrossMultipleMappings) {
    canopen::ObjectDictionary od;
    const canopen::ObjectKey key{0x2000, 0};
    ASSERT_TRUE(
        od.add({key, 16, false, canopen::ObjectAccess::ReadWrite, {std::byte{0}, std::byte{0}}}));
    canopen::ProcessImage image;
    canopen::PdoPlan first;
    canopen::PdoPlan second;
    ASSERT_TRUE(first.build({0x180, 1, {{key, 16, false}}}, od, image));
    ASSERT_TRUE(second.build({0x280, 1, {{key, 16, false}}}, od, image));
    ASSERT_TRUE(image.freeze());
    auto slot = image.find(key);
    ASSERT_TRUE(slot);
    ASSERT_TRUE(image.write(*slot, 0x1234));
    can::Frame first_frame;
    can::Frame second_frame;
    ASSERT_TRUE(first.encode(image, first_frame));
    ASSERT_TRUE(second.encode(image, second_frame));
    EXPECT_EQ(first_frame.data[0], second_frame.data[0]);
    EXPECT_EQ(first_frame.data[1], second_frame.data[1]);
}

TEST(PDO, RejectsUnsupportedTransmissionTypesDuringInitialization) {
    canopen::ObjectDictionary od;
    ASSERT_TRUE(od.add({{0x2000, 0}, 8, false, canopen::ObjectAccess::ReadWrite, {std::byte{0}}}));
    canopen::ProcessImage image;
    canopen::PdoPlan plan;
    auto result = plan.build({0x180, 254, {{{0x2000, 0}, 8, false}}}, od, image);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, canopen::ErrorCode::UnsupportedTransmissionType);
}

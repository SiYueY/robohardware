#include "canopen/process_image.hpp"

#include <gtest/gtest.h>

TEST(ProcessImage, BindsEachObjectKeyToOneSlot) {
    canopen::ProcessImage image;
    const canopen::ObjectKey controlword{0x6040, 0};
    auto first = image.add(controlword, 16, false);
    auto second = image.add(controlword, 16, false);
    auto statusword = image.add({0x6041, 0}, 16, false);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(statusword);
    EXPECT_EQ(first.value(), second.value());
    EXPECT_NE(first.value(), statusword.value());
    EXPECT_EQ(image.find(controlword), first.value());
}

TEST(ProcessImage, RejectsConflictingShapeForSameObject) {
    canopen::ProcessImage image;
    ASSERT_TRUE(image.add({0x2000, 0}, 16, false));
    EXPECT_FALSE(image.add({0x2000, 0}, 32, false));
}

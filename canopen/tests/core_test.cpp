#include "canopen/network.hpp"
#include <gtest/gtest.h>
TEST(CanopenCore, RejectsInvalidNodeId) {
    canopen::Node node({0, {}, {}, {}, {}});
    EXPECT_FALSE(node.initialize());
}
TEST(CanopenCore, RouteTableIsFrozenAndDetectsConflict) {
    canopen::RouteTable t;
    EXPECT_TRUE(t.add(1, {canopen::RouteKind::Sync, 0, 0}));
    EXPECT_FALSE(t.add(1, {canopen::RouteKind::Sync, 0, 0}));
    EXPECT_TRUE(t.freeze());
    EXPECT_FALSE(t.add(2, {canopen::RouteKind::Sync, 0, 0}));
}

#include "canopen/cia402/cia402.hpp"
#include <gtest/gtest.h>
TEST(Cia402, EnableSequenceAndFaultReset) {
    canopen::ProcessImage pi;
    ASSERT_TRUE(pi.add({0x6040, 0}, 16, false));
    ASSERT_TRUE(pi.add({0x6041, 0}, 16, false));
    ASSERT_TRUE(pi.add({0x6060, 0}, 8, true));
    ASSERT_TRUE(pi.add({0x6061, 0}, 8, true));
    ASSERT_TRUE(pi.add({0x607A, 0}, 32, true));
    ASSERT_TRUE(pi.add({0x6064, 0}, 32, true));
    ASSERT_TRUE(pi.add({0x60FF, 0}, 32, true));
    ASSERT_TRUE(pi.add({0x606C, 0}, 32, true));
    ASSERT_TRUE(pi.add({0x6071, 0}, 16, true));
    ASSERT_TRUE(pi.add({0x6077, 0}, 16, true));
    ASSERT_TRUE(pi.freeze());
    auto resolved = canopen::cia402::ProcessDataBinding::resolve(pi);
    ASSERT_TRUE(resolved);
    auto b = resolved.value();
    canopen::cia402::Slave slave(b);
    canopen::cia402::Master master(b);
    ASSERT_TRUE(pi.write(b.statusword, 0x40));
    master.request(pi, canopen::cia402::PdsState::OperationEnabled);
    slave.update(pi);
    EXPECT_EQ(master.state(pi), canopen::cia402::PdsState::ReadyToSwitchOn);
    master.request(pi, canopen::cia402::PdsState::OperationEnabled);
    slave.update(pi);
    master.request(pi, canopen::cia402::PdsState::OperationEnabled);
    slave.update(pi);
    EXPECT_EQ(master.state(pi), canopen::cia402::PdsState::OperationEnabled);
    master.request(pi, canopen::cia402::PdsState::QuickStopActive);
    slave.update(pi);
    EXPECT_EQ(master.state(pi), canopen::cia402::PdsState::QuickStopActive);
    slave.inject_fault();
    slave.update(pi);
    EXPECT_EQ(master.state(pi), canopen::cia402::PdsState::Fault);
    master.request(pi, canopen::cia402::PdsState::SwitchOnDisabled, true);
    slave.update(pi);
    EXPECT_EQ(master.state(pi), canopen::cia402::PdsState::SwitchOnDisabled);
}

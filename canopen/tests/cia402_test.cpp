#include "canopen/cia402/cia402.hpp"
#include <gtest/gtest.h>
TEST(Cia402, EnableSequenceAndFaultReset) {
    canopen::ProcessImage pi;
    for (int i = 0; i < 6; ++i) ASSERT_TRUE(pi.add(16, false));
    ASSERT_TRUE(pi.freeze());
    canopen::cia402::ProcessDataBinding b{0, 1, 2, 3, 4, 5};
    canopen::cia402::VirtualMotor motor;
    canopen::cia402::Slave slave(b, &motor);
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
    ASSERT_TRUE(pi.write(b.target, 42));
    slave.update(pi);
    EXPECT_EQ(motor.position(), 42);
    EXPECT_EQ(pi.read(b.feedback).value(), 42U);
    motor.inject_fault();
    slave.update(pi);
    EXPECT_EQ(master.state(pi), canopen::cia402::PdsState::Fault);
    master.request(pi, canopen::cia402::PdsState::SwitchOnDisabled, true);
    slave.update(pi);
    EXPECT_FALSE(motor.faulted());
}

#include <realtime/periodic_schedule.hpp>
#include <cassert>
int main() {
    realtime::PeriodicSchedule schedule;
    auto unconfigured = schedule.wait_next();
    assert(!unconfigured && unconfigured.error() == realtime::Error::NotConfigured);
    auto invalid = schedule.configure(
        realtime::TimePoint{}, realtime::Duration::zero(), realtime::MissedPeriodPolicy::CatchUp);
    assert(!invalid && invalid.error() == realtime::Error::InvalidArgument);
    assert(!schedule.is_configured());
}

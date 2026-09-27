#pragma once
#include "protocol.hpp"
#include <deque>
namespace sensor {
class SensorState {
public:
    explicit SensorState(std::uint16_t period): period_(period) {}
    Message command(const Message&);
    bool fault() const { return fault_; }
    std::uint16_t period() const { return period_; }
private:
    struct Cached { Message request; Message response; };
    bool fault_=false;
    std::uint16_t period_;
    std::deque<Cached> cache_;
};
}

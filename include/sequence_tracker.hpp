#pragma once
#include <array>
#include <cstdint>
namespace sensor {
class SequenceTracker {
public:
    bool observe(std::uint32_t);
    std::uint64_t received() const { return received_; }
    std::uint64_t lost() const { return lost_; }
    std::uint64_t duplicates() const { return duplicates_; }
    double loss_percent() const;
private:
    static constexpr std::uint32_t window=4096;
    std::array<std::uint64_t,window> seen_{};
    std::uint64_t highest_=0, received_=0, lost_=0, duplicates_=0;
    std::uint32_t last_=0;
};
}

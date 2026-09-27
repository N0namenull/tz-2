#include "sequence_tracker.hpp"
namespace sensor {
bool SequenceTracker::observe(std::uint32_t seq) {
    if (!highest_) {
        last_=seq; highest_=1; received_=1; seen_[1]=1; return true;
    }
    const std::uint32_t ahead=seq-last_;
    std::uint64_t position=highest_;
    if (ahead && ahead<0x80000000u) {
        highest_+=ahead; lost_+=ahead-1; last_=seq; position=highest_;
    } else {
        const std::uint32_t behind=last_-seq;
        if (behind>=window || behind>=highest_) { ++duplicates_; return false; }
        position=highest_-behind;
        if (seen_[position%window]==position) { ++duplicates_; return false; }
        --lost_;
    }
    seen_[position%window]=position; ++received_; return true;
}
double SequenceTracker::loss_percent() const {
    const auto total=received_+lost_;
    return total ? 100.0*static_cast<double>(lost_)/static_cast<double>(total) : 0.0;
}
}

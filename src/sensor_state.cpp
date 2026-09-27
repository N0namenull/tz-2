#include "sensor_state.hpp"
namespace sensor {
static Message response(const Message& cmd, std::uint8_t reason) {
    Bytes b; put16(b,cmd.id); put32(b,cmd.seq);
    if (reason) b.push_back(reason);
    return {static_cast<std::uint16_t>(reason ? 21 : 20),0,std::move(b)};
}
Message SensorState::command(const Message& cmd) {
    for (const auto& item: cache_) {
        if (item.request.seq==cmd.seq) {
            if (item.request.id==cmd.id && item.request.payload==cmd.payload) return item.response;
            return response(cmd,3);
        }
    }
    std::uint8_t reason=0;
    switch (cmd.id) {
    case 10:
        if (cmd.payload.size()!=2) reason=1;
        else if (fault_) reason=2;
        else if (get16(cmd.payload,0)<50 || get16(cmd.payload,0)>5000) reason=1;
        else period_=get16(cmd.payload,0);
        break;
    case 11:
        if (!cmd.payload.empty()) reason=1; else fault_=true;
        break;
    case 12:
        if (!cmd.payload.empty()) reason=1; else { fault_=false; period_=200; }
        break;
    default: reason=3; break;
    }
    auto reply=response(cmd,reason);
    cache_.push_back({cmd,reply}); if (cache_.size()>64) cache_.pop_front();
    return reply;
}
}

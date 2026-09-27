#include "protocol.hpp"
#include "sensor_state.hpp"
#include "sequence_tracker.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

void require(bool value, const char* name) {
    if (!value) throw std::runtime_error(name);
}

int main() {
    try {
        using namespace sensor;
        // Independent wire fixture: magic S N E S in little-endian, heartbeat seq=1.
        const Bytes heartbeat{0x53,0x4e,0x45,0x53, 1,0, 1,0,0,0, 1,0, 0, 0x3c,1};
        require(encode({1,1,{0}}) == heartbeat, "heartbeat wire fixture");
        require(decode(heartbeat).has_value(), "valid heartbeat");
        for (std::size_t n=0; n<heartbeat.size(); ++n) {
            require(!decode(Bytes(heartbeat.begin(), heartbeat.begin()+n)), "truncated frame");
        }
        for (std::size_t i=0; i<heartbeat.size(); ++i) {
            auto bad=heartbeat; bad[i]^=1;
            require(!decode(bad), "single byte corruption rejected");
        }
        auto extra=heartbeat; extra.push_back(0);
        require(!decode(extra), "trailing data rejected");
        auto valid_checksum=[](Bytes& bytes) {
            unsigned sum=0;
            for (std::size_t i=0; i<bytes.size()-2; ++i) sum+=bytes[i];
            bytes[bytes.size()-2]=static_cast<std::uint8_t>(sum);
            bytes.back()=static_cast<std::uint8_t>(sum>>8);
        };
        auto wrong_magic=heartbeat; wrong_magic[0]=0; valid_checksum(wrong_magic);
        require(!decode(wrong_magic), "magic rejected independently of checksum");
        auto wrong_length=heartbeat; wrong_length[10]=2; valid_checksum(wrong_length);
        require(!decode(wrong_length), "payload length rejected independently of checksum");
        require(!decode(Bytes(65507,255)), "oversized garbage rejected");
        const Bytes sample{0,0,0xc8,0x41, 0,0,0x48,0x42, 0x78,0x56,0x34,0x12};
        require(sample_payload(25.0f,50.0f,0x12345678)==sample, "IEEE754 little endian sample");
        auto value=read_sample(sample);
        require(value && value->temperature==25.0f && value->humidity==50.0f &&
                value->uptime==0x12345678, "sample fields");
        require(!read_sample(Bytes(11)), "bad sample size");
        Bytes nan=sample; nan[0]=0; nan[1]=0; nan[2]=0xc0; nan[3]=0x7f;
        require(!read_sample(nan), "nonfinite sample");
        auto long_frame=encode({99,123,Bytes(300,255)});
        require(decode(long_frame).has_value(), "checksum wraps at 65536");
        SensorState state(200);
        auto send=[&](std::uint16_t id, std::uint32_t seq, Bytes payload) {
            return state.command({id,seq,std::move(payload)});
        };
        require(send(10,1,{49,0}).id==21, "rate below range NACK");
        require(send(10,2,{50,0}).id==20 && state.period()==50, "rate lower boundary");
        require(send(10,3,{0x88,0x13}).id==20 && state.period()==5000, "rate upper boundary");
        require(send(10,4,{0x89,0x13}).payload.back()==1, "rate above range");
        require(send(11,5,{}).id==20 && state.fault(), "fault enters state");
        require(send(10,6,{0xf4,1}).payload.back()==2, "rate in fault BUSY");
        require(send(10,60,{49,0}).payload.back()==2, "FAULT takes precedence over rate bounds");
        require(send(10,61,{}).payload.back()==1, "invalid command shape in FAULT");
        require(send(12,7,{}).id==20 && !state.fault() && state.period()==200, "reset defaults");
        require(send(11,5,{}).id==20 && !state.fault(), "retry cached without reapplying fault");
        require(send(12,7,{1}).payload.back()==3, "same seq different command rejected");
        require(send(12,8,{1}).payload.back()==1, "malformed command arguments");
        SequenceTracker stats;
        require(stats.observe(100), "first received baselines session");
        require(stats.observe(102) && stats.lost()==1, "gap detected");
        require(stats.observe(101) && stats.lost()==0, "reordering repairs loss");
        require(!stats.observe(101) && stats.duplicates()==1, "duplicate ignored");
        require(stats.observe(104) && stats.lost()==1 && stats.received()==4, "second gap");
        require(std::abs(stats.loss_percent()-20.0)<0.001, "loss denominator");
        SequenceTracker wrap;
        wrap.observe(0xfffffffe); wrap.observe(0xffffffff); wrap.observe(0); wrap.observe(1);
        require(wrap.lost()==0 && wrap.received()==4, "sequence wrap");
        SequenceTracker window;
        window.observe(1); window.observe(5000);
        require(!window.observe(2) && window.lost()==4998, "too old packet bounded memory");
        std::cout << "PASS: protocol, state, deduplication, sequence statistics\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n'; return 1;
    }
}

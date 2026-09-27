#include "udp_socket.hpp"
#include "console_input.hpp"
#include "options.hpp"
#include "sensor_state.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;
int main(int argc, char** argv) {
    try {
        using namespace sensor;
        const auto options=parse_options(argc,argv,false);
        if (options.help) { usage(false); return 0; }
        std::cout << std::unitbuf;
        install_stop_handler();
        UdpSocket socket(options.address,options.port,options.peer_address,options.peer_port);
        SensorState state(options.period);
        std::uint32_t sequence=1;
        std::uint64_t invalid=0, foreign=0;
        const auto start=Clock::now(); auto next_sample=start, next_heartbeat=start;
        auto send=[&](Message message) { message.seq=sequence++; socket.send(encode(message)); };
        std::cout << "SensorSim listening " << options.address << ':' << options.port
                  << " peer=" << options.peer_address << ':' << options.peer_port
                  << " period_ms=" << state.period() << '\n';
        while (!stop_requested()) {
            if (options.run_for_ms && Clock::now()-start>=std::chrono::milliseconds(options.run_for_ms)) break;
            // Bound each receive batch so traffic cannot starve timers or shutdown.
            for (int i=0; i<64; ++i) {
                auto datagram=socket.receive(); if (!datagram) break;
                if (!datagram->trusted) { ++foreign; continue; }
                auto cmd=decode(datagram->bytes);
                if (!cmd || cmd->id<10 || cmd->id>12) { ++invalid; continue; }
                const auto old_period=state.period(); const bool old_fault=state.fault();
                auto reply=state.command(*cmd);
                std::cout << (reply.id==20 ? "ACK" : "NACK") << " cmd=" << cmd->id
                          << " cmd_seq=" << cmd->seq << " period_ms=" << state.period()
                          << " status=" << (state.fault() ? "FAULT" : "OK") << '\n';
                send(std::move(reply));
                if (old_period!=state.period() || old_fault!=state.fault())
                    next_sample=Clock::now()+std::chrono::milliseconds(state.period());
            }
            const auto now=Clock::now();
            const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(now-start).count();
            if (now>=next_heartbeat) {
                send({1,0,{static_cast<std::uint8_t>(state.fault() ? 2 : 0)}});
                next_heartbeat=now+1s;
            }
            if (!state.fault() && now>=next_sample) {
                const double seconds=static_cast<double>(elapsed)/1000.0;
                send({2,0,sample_payload(static_cast<float>(25.0+5.0*std::sin(seconds/20.0)),
                                         static_cast<float>(50.0+10.0*std::sin(seconds/15.0)),
                                         static_cast<std::uint32_t>(elapsed))});
                next_sample=now+std::chrono::milliseconds(state.period());
            }
            socket.wait(10);
        }
        std::cout << "Stopped invalid=" << invalid << " foreign=" << foreign << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << "SensorSim: " << e.what() << '\n'; return 1; }
}

#include "udp_socket.hpp"
#include "console_input.hpp"
#include "options.hpp"
#include "sequence_tracker.hpp"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;
namespace {
struct Pending {
    sensor::Message message;
    sensor::Bytes bytes;
    Clock::time_point deadline;
    int attempts=1;
};
std::string timestamp() {
    SYSTEMTIME t{}; GetSystemTime(&t);
    std::ostringstream out;
    out << std::setfill('0') << std::setw(4) << t.wYear << '-' << std::setw(2) << t.wMonth
        << '-' << std::setw(2) << t.wDay << 'T' << std::setw(2) << t.wHour << ':'
        << std::setw(2) << t.wMinute << ':' << std::setw(2) << t.wSecond << '.'
        << std::setw(3) << t.wMilliseconds << 'Z';
    return out.str();
}
const char* reason_name(std::uint8_t reason) {
    return reason==1 ? "INVALID_ARGS" : reason==2 ? "BUSY" : "OTHER";
}
}
int main(int argc, char** argv) {
    try {
        using namespace sensor;
        const auto options=parse_options(argc,argv,true);
        if (options.help) { usage(true); return 0; }
        std::cout.imbue(std::locale::classic());
        std::cout << std::unitbuf << std::fixed << std::setprecision(2);
        install_stop_handler();
        UdpSocket socket(options.address,options.port,options.peer_address,options.peer_port);
        std::ofstream csv;
        csv.exceptions(std::ios::failbit | std::ios::badbit);
        csv.imbue(std::locale::classic());
        csv.open(options.csv,std::ios::out | std::ios::trunc);
        csv << "timestamp,seq,temperature_c,humidity_pct,uptime_ms\n";
        csv.flush();
        ConsoleInput input;
        SequenceTracker stats;
        std::uint64_t invalid=0, foreign=0;
        std::uint32_t command_seq=1;
        std::optional<Pending> pending;
        std::optional<Clock::time_point> heartbeat;
        const auto start=Clock::now(); auto next_status=start+1s;
        auto status=[&] {
            std::cout << "STATUS rx=" << stats.received() << " loss=" << stats.lost()
                      << " loss_rate=" << stats.loss_percent() << "% duplicates_or_old=" << stats.duplicates()
                      << " invalid=" << invalid << " foreign=" << foreign << " heartbeat_age_ms=";
            if (heartbeat) std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-*heartbeat).count();
            else std::cout << "never";
            std::cout << " pending=" << (pending ? "yes" : "no") << '\n';
        };
        std::cout << "Collector listening " << options.address << ':' << options.port
                  << " peer=" << options.peer_address << ':' << options.peer_port << " csv=" << options.csv
                  << "\nCommands: status | rate <ms> | fault | reset | quit\n";
        bool running=true;

        // https://www.youtube.com/watch?v=dQw4w9WgXcQ
        while (running && !stop_requested()) {
            if (options.run_for_ms && Clock::now()-start>=std::chrono::milliseconds(options.run_for_ms)) break;
            for (int i=0; i<64; ++i) {
                auto datagram=socket.receive(); if (!datagram) break;
                if (!datagram->trusted) { ++foreign; continue; }
                auto m=decode(datagram->bytes);
                if (!m || !telemetry_valid(*m)) { ++invalid; continue; }
                if (!stats.observe(m->seq)) continue;
                if (m->id==1) {
                    heartbeat=Clock::now();
                    const char* names[]={"OK","DEGRADED","FAULT"};
                    std::cout << "HEARTBEAT status=" << names[m->payload[0]] << " seq=" << m->seq << '\n';
                } else if (m->id==2) {
                    const auto sample=*read_sample(m->payload);
                    csv << timestamp() << ',' << m->seq << ',' << std::fixed << std::setprecision(3)
                        << sample.temperature << ',' << sample.humidity << ',' << sample.uptime << '\n';
                    csv.flush();
                    std::cout << "SAMPLE seq=" << m->seq << " temperature_c=" << sample.temperature
                              << " humidity_pct=" << sample.humidity << " uptime_ms=" << sample.uptime << '\n';
                } else {
                    const auto id=get16(m->payload,0); const auto seq=get32(m->payload,2);
                    const bool matched=pending && pending->message.id==id && pending->message.seq==seq;
                    std::cout << (m->id==20 ? "ACK" : "NACK") << " cmd=" << id << " cmd_seq=" << seq;
                    if (m->id==21) std::cout << " reason=" << reason_name(m->payload[6]);
                    std::cout << (matched ? " matched" : " unsolicited") << '\n';
                    if (matched) pending.reset();
                }
            }
            // Limit input processing too: a pipe full of commands must not starve telemetry.
            for (int i=0; i<16; ++i) {
                auto line=input.line(); if (!line) break;
                std::istringstream words(*line); std::string command,arg,extra;
                if (!(words>>command)) continue;
                std::cout << "COMMAND " << *line << '\n';
                if (command=="rate") {
                    if (!(words>>arg) || (words>>extra)) { std::cout << "Usage: rate <ms>\n"; continue; }
                } else if (words>>extra) { std::cout << "Unexpected argument\n"; continue; }
                if (command=="quit") { running=false; break; }
                if (command=="status") { status(); continue; }
                Message m{0,0,{}};
                if (command=="rate") {
                    try { put16(m.payload,static_cast<std::uint16_t>(number(arg,0,65535))); }
                    catch (const std::invalid_argument& e) { std::cout << e.what() << '\n'; continue; }
                    m.id=10;
                } else if (command=="fault") m.id=11;
                else if (command=="reset") m.id=12;
                else { std::cout << "Unknown command. Use status | rate <ms> | fault | reset | quit\n"; continue; }
                if (pending) { std::cout << "Command pending; wait for ACK/NACK or TIMEOUT\n"; continue; }
                m.seq=command_seq++;
                auto bytes=encode(m); socket.send(bytes);
                pending=Pending{std::move(m),std::move(bytes),Clock::now()+600ms,1};
            }
            if (input.ended()) running=false;
            const auto now=Clock::now();
            if (running && pending && now>=pending->deadline) {
                if (pending->attempts<3) {
                    socket.send(pending->bytes); ++pending->attempts; pending->deadline=now+600ms;
                    std::cout << "RETRY cmd=" << pending->message.id << " cmd_seq=" << pending->message.seq
                              << " attempt=" << pending->attempts << '\n';
                } else {
                    std::cout << "TIMEOUT cmd=" << pending->message.id << " cmd_seq=" << pending->message.seq
                              << " result unknown\n"; pending.reset();
                }
            }
            if (now>=next_status) { status(); next_status=now+1s; }
            if (running) socket.wait(10);
        }
        if (pending) std::cout << "Stopped with an unconfirmed command; result unknown\n";
        status(); csv.close(); std::cout << "Stopped\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "Collector: " << e.what() << '\n'; return 1; }
}

#include "options.hpp"
#include <charconv>
#include <iostream>
#include <stdexcept>
namespace sensor {
std::uint32_t number(const std::string& s, std::uint32_t minimum, std::uint32_t maximum) {
    std::uint32_t value=0;
    const auto result=std::from_chars(s.data(),s.data()+s.size(),value);
    if (result.ec!=std::errc{} || result.ptr!=s.data()+s.size() || value<minimum || value>maximum)
        throw std::invalid_argument("Invalid number: "+s+" (expected "+std::to_string(minimum)+".."+std::to_string(maximum)+")");
    return value;
}
Options parse_options(int argc, char** argv, bool collector) {
    Options o; if (collector) { o.port=24551; o.peer_port=24550; }
    for (int i=1; i<argc; ++i) {
        const std::string flag=argv[i];
        if (flag=="--help" || flag=="-h") { o.help=true; continue; }
        if (i+1==argc) throw std::invalid_argument("Missing value for "+flag);
        const std::string value=argv[++i];
        if (flag=="--listen-address") o.address=value;
        else if (flag=="--peer-address") o.peer_address=value;
        else if (flag=="--listen-port") o.port=static_cast<std::uint16_t>(number(value,1,65535));
        else if (flag=="--peer-port") o.peer_port=static_cast<std::uint16_t>(number(value,1,65535));
        else if (flag=="--period-ms" && !collector) o.period=static_cast<std::uint16_t>(number(value,50,5000));
        else if (flag=="--csv" && collector) { if (value.empty()) throw std::invalid_argument("Empty CSV path"); o.csv=value; }
        else if (flag=="--run-for-ms") o.run_for_ms=number(value,1,86400000);
        else throw std::invalid_argument("Unknown option: "+flag);
    }
    if (o.address!="127.0.0.1" || o.peer_address!="127.0.0.1") throw std::invalid_argument("Only 127.0.0.1 is allowed");
    if (o.port==o.peer_port) throw std::invalid_argument("Listen and peer ports must differ");
    return o;
}
void usage(bool collector) {
    std::cout << (collector ? "Collector" : "SensorSim")
        << " [--listen-address 127.0.0.1] [--listen-port PORT]\n"
           "  [--peer-address 127.0.0.1] [--peer-port PORT] [--run-for-ms MS]\n"
        << (collector ? "  [--csv telemetry.csv]\nCommands: status | rate <0..65535> | fault | reset | quit\n"
                      : "  [--period-ms 50..5000]\nStop: Ctrl+C\n");
}
}

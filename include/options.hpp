#pragma once
#include <cstdint>
#include <string>
namespace sensor {
struct Options {
    std::string address="127.0.0.1", peer_address="127.0.0.1", csv="telemetry.csv";
    std::uint16_t port=24550, peer_port=24551, period=200;
    std::uint32_t run_for_ms=0;
    bool help=false;
};
std::uint32_t number(const std::string&, std::uint32_t minimum, std::uint32_t maximum);
Options parse_options(int argc, char** argv, bool collector);
void usage(bool collector);
}

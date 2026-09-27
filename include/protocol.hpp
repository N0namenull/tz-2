#pragma once
#include <cstdint>
#include <optional>
#include <vector>
namespace sensor {
using Bytes=std::vector<std::uint8_t>;
struct Message { std::uint16_t id; std::uint32_t seq; Bytes payload; };
struct Sample { float temperature; float humidity; std::uint32_t uptime; };
void put16(Bytes&, std::uint16_t);
void put32(Bytes&, std::uint32_t);
std::uint16_t get16(const Bytes&, std::size_t);
std::uint32_t get32(const Bytes&, std::size_t);
Bytes encode(const Message&);
std::optional<Message> decode(const Bytes&);
Bytes sample_payload(float, float, std::uint32_t);
std::optional<Sample> read_sample(const Bytes&);
bool telemetry_valid(const Message&);
}

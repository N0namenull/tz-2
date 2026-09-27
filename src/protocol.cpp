#include "protocol.hpp"
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace sensor {
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,
              "Protocol requires IEEE754 binary32");
void put16(Bytes& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v)); b.push_back(static_cast<std::uint8_t>(v>>8));
}
void put32(Bytes& b, std::uint32_t v) {
    for (unsigned shift=0; shift<32; shift+=8) b.push_back(static_cast<std::uint8_t>(v>>shift));
}
std::uint16_t get16(const Bytes& b, std::size_t p) {
    return static_cast<std::uint16_t>(b.at(p) | (static_cast<unsigned>(b.at(p+1))<<8));
}
std::uint32_t get32(const Bytes& b, std::size_t p) {
    std::uint32_t v=0;
    for (unsigned i=0; i<4; ++i) v|=static_cast<std::uint32_t>(b.at(p+i))<<(8*i);
    return v;
}
static std::uint16_t checksum(const Bytes& b, std::size_t length) {
    std::uint32_t sum=0;
    for (std::size_t i=0; i<length; ++i) sum+=b[i];
    return static_cast<std::uint16_t>(sum);
}
Bytes encode(const Message& m) {
    if (m.payload.size()>65493) throw std::invalid_argument("Payload exceeds UDP limit");
    Bytes b; b.reserve(14+m.payload.size());
    put32(b,0x53454e53); put16(b,m.id); put32(b,m.seq);
    put16(b,static_cast<std::uint16_t>(m.payload.size()));
    b.insert(b.end(),m.payload.begin(),m.payload.end()); put16(b,checksum(b,b.size()));
    return b;
}
std::optional<Message> decode(const Bytes& b) {
    if (b.size()<14 || b.size()>65507 || get32(b,0)!=0x53454e53) return {};
    if (b.size()!=14u+get16(b,10) || checksum(b,b.size()-2)!=get16(b,b.size()-2)) return {};
    return Message{get16(b,4),get32(b,6),Bytes(b.begin()+12,b.end()-2)};
}
Bytes sample_payload(float temperature, float humidity, std::uint32_t uptime) {
    Bytes b; std::uint32_t bits=0;
    std::memcpy(&bits,&temperature,4); put32(b,bits);
    std::memcpy(&bits,&humidity,4); put32(b,bits); put32(b,uptime); return b;
}
std::optional<Sample> read_sample(const Bytes& b) {
    if (b.size()!=12) return {};
    Sample s{}; auto bits=get32(b,0); std::memcpy(&s.temperature,&bits,4);
    bits=get32(b,4); std::memcpy(&s.humidity,&bits,4); s.uptime=get32(b,8);
    if (!std::isfinite(s.temperature) || !std::isfinite(s.humidity)) return {};
    return s;
}
bool telemetry_valid(const Message& m) {
    switch (m.id) {
    case 1: return m.payload.size()==1 && m.payload[0]<=2;
    case 2: return read_sample(m.payload).has_value();
    case 20: return m.payload.size()==6 && get16(m.payload,0)>=10 && get16(m.payload,0)<=12;
    case 21: return m.payload.size()==7 && get16(m.payload,0)>=10 && get16(m.payload,0)<=12 &&
                    m.payload[6]>=1 && m.payload[6]<=3;
    default: return false;
    }
}
}

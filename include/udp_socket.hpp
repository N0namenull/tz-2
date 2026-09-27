#pragma once
#include "protocol.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
namespace sensor {
class Winsock {
public:
    Winsock();
    ~Winsock();
    Winsock(const Winsock&)=delete;
    Winsock& operator=(const Winsock&)=delete;
};
struct Datagram { Bytes bytes; bool trusted; };
class UdpSocket {
public:
    UdpSocket(const std::string& address, std::uint16_t port,
              const std::string& peer_address, std::uint16_t peer_port);
    ~UdpSocket();
    UdpSocket(const UdpSocket&)=delete;
    UdpSocket& operator=(const UdpSocket&)=delete;
    void send(const Bytes&);
    std::optional<Datagram> receive();
    void wait(int milliseconds);
private:
    Winsock runtime_;
    SOCKET socket_=INVALID_SOCKET;
    sockaddr_in peer_{};
};
}

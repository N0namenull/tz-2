#include "udp_socket.hpp"
#include <array>
#include <stdexcept>
namespace sensor {
static std::runtime_error error(const char* operation) {
    return std::runtime_error(std::string(operation)+" failed (Winsock "+std::to_string(WSAGetLastError())+")");
}
Winsock::Winsock() {
    WSADATA data{}; const int result=WSAStartup(MAKEWORD(2,2),&data);
    if (result) throw std::runtime_error("WSAStartup failed: "+std::to_string(result));
}
Winsock::~Winsock() { WSACleanup(); }
static sockaddr_in endpoint(const std::string& address, std::uint16_t port) {
    sockaddr_in ep{}; ep.sin_family=AF_INET; ep.sin_port=htons(port);
    if (address!="127.0.0.1" || InetPtonA(AF_INET,address.c_str(),&ep.sin_addr)!=1)
        throw std::invalid_argument("Only 127.0.0.1 is allowed");
    return ep;
}
UdpSocket::UdpSocket(const std::string& address, std::uint16_t port,
                     const std::string& peer_address, std::uint16_t peer_port) {
    const auto local=endpoint(address,port); peer_=endpoint(peer_address,peer_port);
    socket_=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if (socket_==INVALID_SOCKET) throw error("socket");
    try {
        // Fail on occupied ports instead of silently sharing a unicast endpoint.
        BOOL exclusive=TRUE;
        if (setsockopt(socket_,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,
                       reinterpret_cast<const char*>(&exclusive),sizeof(exclusive))==SOCKET_ERROR)
            throw error("SO_EXCLUSIVEADDRUSE");
        if (bind(socket_,reinterpret_cast<const sockaddr*>(&local),sizeof(local))==SOCKET_ERROR)
            throw error("bind");
        u_long nonblocking=1;
        if (ioctlsocket(socket_,FIONBIO,&nonblocking)==SOCKET_ERROR) throw error("FIONBIO");
        BOOL reset=FALSE; DWORD returned=0;
        // A missing peer is normal for UDP. Do not turn ICMP port-unreachable into recv failures.
        if (WSAIoctl(socket_,_WSAIOW(IOC_VENDOR,12),&reset,sizeof(reset),nullptr,0,
                     &returned,nullptr,nullptr)==SOCKET_ERROR) throw error("SIO_UDP_CONNRESET");
    } catch (...) { closesocket(socket_); socket_=INVALID_SOCKET; throw; }
}
UdpSocket::~UdpSocket() { if (socket_!=INVALID_SOCKET) closesocket(socket_); }
void UdpSocket::send(const Bytes& b) {
    const int size=static_cast<int>(b.size());
    const int result=sendto(socket_,reinterpret_cast<const char*>(b.data()),size,0,
                            reinterpret_cast<const sockaddr*>(&peer_),sizeof(peer_));
    if (result==SOCKET_ERROR) throw error("sendto");
    if (result!=size) throw std::runtime_error("Partial UDP send");
}
std::optional<Datagram> UdpSocket::receive() {
    std::array<std::uint8_t,65535> buffer{}; sockaddr_in from{}; int length=sizeof(from);
    const int count=recvfrom(socket_,reinterpret_cast<char*>(buffer.data()),static_cast<int>(buffer.size()),
                             0,reinterpret_cast<sockaddr*>(&from),&length);
    if (count==SOCKET_ERROR) {
        const int code=WSAGetLastError();
        if (code==WSAEWOULDBLOCK || code==WSAECONNRESET) return {};
        throw error("recvfrom");
    }
    return Datagram{Bytes(buffer.begin(),buffer.begin()+count),
        from.sin_family==AF_INET && from.sin_addr.s_addr==peer_.sin_addr.s_addr && from.sin_port==peer_.sin_port};
}
void UdpSocket::wait(int milliseconds) {
    fd_set readable; FD_ZERO(&readable); FD_SET(socket_,&readable);
    timeval timeout{milliseconds/1000,(milliseconds%1000)*1000};
    if (select(0,&readable,nullptr,nullptr,&timeout)==SOCKET_ERROR) throw error("select");
}
}

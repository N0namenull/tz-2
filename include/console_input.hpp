#pragma once
#include <windows.h>
#include <deque>
#include <optional>
#include <string>
namespace sensor {
// All stdin and network work runs on one thread. No blocking getline to join at shutdown.
class ConsoleInput {
public:
    ConsoleInput();
    ~ConsoleInput();
    ConsoleInput(const ConsoleInput&)=delete;
    ConsoleInput& operator=(const ConsoleInput&)=delete;
    std::optional<std::string> line();
    bool ended() const { return eof_ && lines_.empty(); }
private:
    void accept(char);
    HANDLE handle_=INVALID_HANDLE_VALUE;
    DWORD original_mode_=0, type_=0;
    bool console_=false, eof_=false;
    std::string buffer_;
    std::deque<std::string> lines_;
};
void install_stop_handler();
bool stop_requested();
}

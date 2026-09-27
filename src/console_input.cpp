#include "console_input.hpp"
#include <iostream>
#include <stdexcept>
namespace sensor {
static volatile LONG stopping=0;
static BOOL WINAPI stop_handler(DWORD event) {
    if (event==CTRL_C_EVENT || event==CTRL_BREAK_EVENT || event==CTRL_CLOSE_EVENT) {
        InterlockedExchange(&stopping,1); return TRUE;
    }
    return FALSE;
}
void install_stop_handler() {
    if (!SetConsoleCtrlHandler(stop_handler,TRUE)) throw std::runtime_error("Cannot install Ctrl+C handler");
}
bool stop_requested() { return InterlockedCompareExchange(&stopping,0,0)!=0; }
ConsoleInput::ConsoleInput() {
    handle_=GetStdHandle(STD_INPUT_HANDLE);
    if (handle_==INVALID_HANDLE_VALUE || handle_==nullptr) { eof_=true; return; }
    type_=GetFileType(handle_);
    console_=GetConsoleMode(handle_,&original_mode_)!=0;
    if (console_ && !SetConsoleMode(handle_,ENABLE_PROCESSED_INPUT))
        throw std::runtime_error("Cannot set console input mode");
}
ConsoleInput::~ConsoleInput() { if (console_) SetConsoleMode(handle_,original_mode_); }
void ConsoleInput::accept(char c) {
    if (c=='\r') return;
    if (c=='\n') { lines_.push_back(buffer_); buffer_.clear(); return; }
    if (buffer_.size()>=4096) throw std::runtime_error("Input line exceeds 4096 bytes");
    buffer_.push_back(c);
}
std::optional<std::string> ConsoleInput::line() {
    if (lines_.empty() && !eof_) {
        if (console_) {
            DWORD count=0;
            if (!GetNumberOfConsoleInputEvents(handle_,&count)) throw std::runtime_error("Console input failed");
            for (DWORD i=0; i<count && i<128; ++i) {
                INPUT_RECORD record{}; DWORD read=0;
                if (!ReadConsoleInputA(handle_,&record,1,&read)) throw std::runtime_error("ReadConsoleInput failed");
                if (record.EventType!=KEY_EVENT || !record.Event.KeyEvent.bKeyDown) continue;
                const auto& key=record.Event.KeyEvent;
                for (WORD repeat=0; repeat<key.wRepeatCount; ++repeat) {
                    const char c=key.uChar.AsciiChar;
                    if (c=='\r') { accept('\n'); std::cout << '\n'; }
                    else if (c=='\b') { if (!buffer_.empty()) { buffer_.pop_back(); std::cout << "\b \b"; } }
                    else if (c>=32 && c<=126) { accept(c); std::cout << c; }
                }
            }
        } else {
            DWORD available=256;
            if (type_==FILE_TYPE_PIPE && !PeekNamedPipe(handle_,nullptr,0,nullptr,&available,nullptr)) {
                if (GetLastError()==ERROR_BROKEN_PIPE) eof_=true;
                else throw std::runtime_error("PeekNamedPipe failed");
            }
            if (!eof_ && available) {
                char chars[256]; DWORD read=0;
                if (!ReadFile(handle_,chars,(available<256 ? available : 256),&read,nullptr)) {
                    if (GetLastError()==ERROR_BROKEN_PIPE) eof_=true;
                    else throw std::runtime_error("ReadFile stdin failed");
                } else if (read==0) eof_=true;
                else for (DWORD i=0; i<read; ++i) accept(chars[i]);
            }
            if (eof_ && !buffer_.empty()) { lines_.push_back(buffer_); buffer_.clear(); }
        }
    }
    if (lines_.empty()) return {};
    auto result=std::move(lines_.front()); lines_.pop_front(); return result;
}
}

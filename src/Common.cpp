#include "Common.hpp"

#include <iostream>
#include <string>

#define GREEN "\033[32m"
#define RED "\033[31m"
#define BOLD "\033[1m"
#define RESET "\033[0m"

namespace {

void PrintLog(const char* color, const char* level, const std::string& msg) {
    std::cout << BOLD << color << level << ": " << RESET << msg;
    if (msg.empty() || msg.back() != '\n') {
        std::cout << '\n';
    }
}

} // namespace

void Log::Info(const std::string& msg) {
    if (!Log::enableInfo) {
        return;
    }
    PrintLog(GREEN, "INFO", msg);
}

void Log::Error(const std::string& msg) {
    PrintLog(RED, "ERROR", msg);
}

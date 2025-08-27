#pragma once
#include <utils/flog.h>
#include <chrono>

// Define SCAN_DEBUG macro that compiles away when debug logs are disabled
#if defined(SCANNER_DEBUG_LOGS)
  #define SCAN_DEBUG(...) flog::debug(__VA_ARGS__)
#else
  #define SCAN_DEBUG(...) ((void)0)
#endif

// Time-based throttle helper for debug logs
struct Throttle {
    std::chrono::steady_clock::time_point next{};
    std::chrono::milliseconds period;
    
    explicit Throttle(std::chrono::milliseconds p) 
        : period(p), next(std::chrono::steady_clock::now()) {}
    
    bool ready() {
        auto now = std::chrono::steady_clock::now();
        if (now >= next) { 
            next = now + period; 
            return true; 
        }
        return false;
    }
};


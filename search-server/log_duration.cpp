#include <chrono>
#include <iostream>

#include "log_duration.h"

LogDuration(const std::string& id)
        : id_(id), stream_(std::cerr) {
    }

LogDuration(const std::string& id, std::ostream& stream)
        : id_(id), stream_(stream) {
    }

~LogDuration() {
        using namespace std::chrono;
        using namespace std::literals;
        using Clock = std::chrono::steady_clock;
        
        const auto end_time = Clock::now();
        const auto dur = end_time - start_time_;
        stream_ << id_ << ": "s << duration_cast<milliseconds>(dur).count() << " ms"s << std::endl;
    }
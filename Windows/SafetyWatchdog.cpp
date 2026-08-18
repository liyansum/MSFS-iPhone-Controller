#include "SafetyWatchdog.h"
#include "Protocol.h"
#include <chrono>

namespace {
long long NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
} // namespace

void SafetyWatchdog::Start(std::function<void()> onTimeout) {
    onTimeout_ = std::move(onTimeout);
    if (running_.load()) return;
    running_ = true;
    thread_ = std::thread(&SafetyWatchdog::Loop, this);
}

void SafetyWatchdog::Stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void SafetyWatchdog::Touch() {
    lastControlMs_ = NowMs();
    expired_ = false;
}

void SafetyWatchdog::Loop() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        long long last = lastControlMs_.load();
        if (last == 0 || expired_.load()) continue;
        if (NowMs() - last > proto::kControlTimeoutMs) {
            expired_ = true;
            if (onTimeout_) onTimeout_();
        }
    }
}

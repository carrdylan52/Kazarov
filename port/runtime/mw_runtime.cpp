#include "mw_runtime.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "platform/audio.hpp"
#include "sce_core/mc_store.hpp"

namespace runtime {

namespace {

std::vector<void (*)()> &Handlers() {
    static std::vector<void (*)()> handlers;
    return handlers;
}

} // namespace

int Fptosi(float value) {
    if (std::isnan(value)) {
        return 0;
    }
    if (value >= 2147483648.0f) {
        return INT32_MAX;
    }
    if (value <= -2147483648.0f) {
        return INT32_MIN;
    }
    return static_cast<int>(value);
}

unsigned int Fptoui(float value) {
    if (std::isnan(value) || std::signbit(value)) {
        return 0;
    }
    if (value >= 4294967296.0f) {
        return UINT32_MAX;
    }
    return static_cast<unsigned int>(value);
}

[[noreturn]] void AssertFailed(const char *file, int line, const char *expression, int status) {
    std::fflush(stdout);
    std::fprintf(stderr, "%s:%d: assertion failed: %s\n", file, line, expression);
    std::fflush(nullptr);
    // Not exit: static destructors would tear down the renderer under a frame the game may have open.
    std::_Exit(status);
}

void AtExit(void (*handler)()) {
    Handlers().push_back(handler);
}

[[noreturn]] void Exit(int status) {
    std::vector<void (*)()> &handlers = Handlers();
    while (!handlers.empty()) {
        void (*handler)() = handlers.back();
        handlers.pop_back();
        handler();
    }
    sce_core::mc::CloseAll();
    AudioOutputStop();
    std::fflush(nullptr);
    std::exit(status);
}

} // namespace runtime

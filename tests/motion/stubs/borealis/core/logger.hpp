#pragma once
namespace brls {
struct Logger {
    template<typename... T> static void info(const char*, T...) {}
    template<typename... T> static void warning(const char*, T...) {}
};
}

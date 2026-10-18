// A tiny test harness: TEST(name) { CHECK(...); }. Run with `make -C host test`.
#pragma once

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

namespace testing {

struct Case {
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& cases() {
    static std::vector<Case> c;
    return c;
}
inline int& failures() {
    static int f = 0;
    return f;
}
inline const char*& current() {
    static const char* c = "";
    return c;
}

struct Registrar {
    Registrar(const char* n, void (*f)()) { cases().push_back({n, f}); }
};

inline void fail(const char* file, int line, const std::string& msg) {
    ++failures();
    printf("  FAIL %s (%s:%d): %s\n", current(), file, line, msg.c_str());
}

} // namespace testing

#define TEST(name)                                                    \
    static void name();                                               \
    static testing::Registrar registrar_##name(#name, name);          \
    static void name()

#define CHECK(cond)                                                   \
    do {                                                              \
        if (!(cond)) testing::fail(__FILE__, __LINE__, #cond);        \
    } while (0)

#define CHECK_EQ(a, b)                                                                              \
    do {                                                                                            \
        auto va_ = (a);                                                                             \
        auto vb_ = (b);                                                                             \
        if (!(va_ == vb_))                                                                          \
            testing::fail(__FILE__, __LINE__, std::string(#a " == " #b " (got ") +                 \
                                                  std::to_string((long long)va_) + " vs " +         \
                                                  std::to_string((long long)vb_) + ")");            \
    } while (0)

#define CHECK_STR(a, b)                                                                             \
    do {                                                                                            \
        std::string va_ = (a);                                                                      \
        std::string vb_ = (b);                                                                      \
        if (va_ != vb_)                                                                             \
            testing::fail(__FILE__, __LINE__, std::string(#a " == " #b " (got \"") + va_ + "\" vs \"" + vb_ + "\")"); \
    } while (0)

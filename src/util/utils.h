#pragma once
#include <iostream>
#include <stdexcept>
#include <cstdint>
#include <algorithm>

namespace util {

static inline void BUG_ON(bool cond, const char *msg) {
    if (cond) {
        throw std::runtime_error(msg);
    }
}

template <typename T, typename TypeA, typename TypeB>
static inline T min_t(TypeA a, TypeB b) {
    return (static_cast<T>(a) < static_cast<T>(b)) ? static_cast<T>(a) : static_cast<T>(b);
}

uint64_t get_urandom();

} // namespace util
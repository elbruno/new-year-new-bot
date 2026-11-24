#pragma once

#include <stddef.h>
#include <stdint.h>

namespace lab::setup_password {

inline constexpr char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
inline constexpr size_t kLength = 8;
inline constexpr size_t kBufferSize = kLength + 1;

static_assert(sizeof(kAlphabet) - 1 == 32);

constexpr bool isValidDevelopmentPassword(const char* value) {
    if (!value) return false;
    size_t length = 0;
    while (length < kLength && value[length] != '\0') {
        const unsigned char character = static_cast<unsigned char>(value[length]);
        if (character < 33 || character > 126) return false;
        ++length;
    }
    return length == kLength && value[length] == '\0';
}

template <typename NextRandom>
bool create(char* output, size_t capacity, const char* developmentPassword, NextRandom nextRandom) {
    if (!output || capacity < kBufferSize) return false;
    output[0] = '\0';

    if (developmentPassword) {
        if (!isValidDevelopmentPassword(developmentPassword)) return false;
        for (size_t index = 0; index < kLength; ++index)
            output[index] = developmentPassword[index];
    } else {
        for (size_t index = 0; index < kLength; ++index)
            output[index] = kAlphabet[nextRandom() & 0x1f];
    }

    output[kLength] = '\0';
    return true;
}

}  // namespace lab::setup_password

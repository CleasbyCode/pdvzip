#pragma once

#include <vector>
#include <array>
#include <cstdint>
#include <cstddef>
#include <algorithm> 

template <typename T, size_t N>
size_t searchFunc(std::vector<uint8_t>& vec, size_t start_index, const uint8_t INCREMENT_SEARCH_VAL, const std::array<T, N>& SIG) {
    return std::search(vec.begin() + start_index + INCREMENT_SEARCH_VAL, vec.end(), SIG.begin(), SIG.end()) - vec.begin();
}

#pragma once
/// @file Mp3Duration.hpp
/// @brief Быстрая оценка длительности MP3 без полного прохода.

#include <cstdint>

namespace ae2 {
class FsAdapter;

namespace Mp3Duration {

struct Result {
    uint32_t durationSec = 0;
    uint32_t sampleRate  = 0;
    uint8_t  channels    = 0;
    bool     isExact     = false;
};

/// Оценить длительность. Xing/VBRI → точно. Иначе — средний битрейт.
Result estimate(FsAdapter& fs, uint32_t fileSize);

} // namespace Mp3Duration
} // namespace ae2

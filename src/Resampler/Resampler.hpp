#pragma once
/// @file Resampler.hpp
/// @brief Ресемплер с поддержкой split-destination для прямой записи в ring.

#include "AudioEngineV2/Types.hpp"
#include <cstdint>

namespace ae2 {

class Resampler {
public:
    enum class Algorithm : uint8_t { Nearest, Linear };

    void setAlgorithm(Algorithm alg) { alg_ = alg; }
    void setRates(uint32_t inRate, uint32_t outRate);

    /// Число выходных сэмплов для inLen входных.
    uint32_t outputLength(uint32_t inLen) const;

    /// Ресемплировать src → два сегмента dst. Возвращает число записанных.
    uint32_t process(const s16* src, uint32_t srcLen,
                     s16* dst1, uint32_t dst1Cap,
                     s16* dst2, uint32_t dst2Cap) const;

private:
    uint32_t inRate_  = 44100;
    uint32_t outRate_ = 128000;
    Algorithm alg_ = Algorithm::Linear;
};

} // namespace ae2

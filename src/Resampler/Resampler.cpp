/// @file Resampler.cpp
#include "Resampler.hpp"

namespace ae2 {

void Resampler::setRates(uint32_t inRate, uint32_t outRate) {
    if (inRate  == 0) inRate  = 44100;
    if (outRate == 0) outRate = 128000;
    inRate_    = inRate;
    outRate_   = outRate;
    /* Предвычисляем Q16 шаг: сколько входных сэмплов (×2^16) приходится на
     * один выходной сэмпл. Используется в process() вместо умножения double. */
    phaseStep_ = (uint32_t)(((uint64_t)inRate << 16) / outRate);
}

uint32_t Resampler::outputLength(uint32_t inLen) const {
    if (inRate_ == 0) return inLen;
    return (uint32_t)(((uint64_t)inLen * outRate_ + inRate_ - 1) / inRate_);
}

uint32_t Resampler::process(const s16* src, uint32_t srcLen,
                            s16* dst1, uint32_t dst1Cap,
                            s16* dst2, uint32_t dst2Cap) const {
    if (!src || srcLen == 0) return 0;

    uint32_t outTotal = outputLength(srcLen);
    uint32_t maxOut = dst1Cap + dst2Cap;
    if (outTotal > maxOut) outTotal = maxOut;

    /* Фазовый аккумулятор Q16: phase = i * phaseStep_.
     * idx  = phase >> 16  — целая часть (индекс входного сэмпла).
     * frac = (phase & 0xFFFF) >> 1  — 15-бит дробь [0..32767].
     * Линейная интерполяция: sample = src[idx] + diff*frac >> 15,
     * все операции в int32 (diff ∈ [-65535..65535], frac ≤ 32767 → max |prod| < 2^31). */
    uint64_t phase = 0;

    for (uint32_t i = 0; i < outTotal; ++i) {
        uint32_t idx = (uint32_t)(phase >> 16);

        s16 sample;
        if (alg_ == Algorithm::Linear && idx + 1 < srcLen) {
            const int32_t frac = (int32_t)((phase & 0xFFFFu) >> 1);  /* [0..32767] */
            const int32_t diff = (int32_t)src[idx + 1] - (int32_t)src[idx];
            sample = (s16)((int32_t)src[idx] + ((diff * frac) >> 15));
        } else {
            if (idx >= srcLen) idx = srcLen - 1;
            sample = src[idx];
        }

        if (i < dst1Cap) {
            dst1[i] = sample;
        } else {
            dst2[i - dst1Cap] = sample;
        }

        phase += phaseStep_;
    }
    return outTotal;
}

} // namespace ae2

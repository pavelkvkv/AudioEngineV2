/// @file Resampler.cpp
#include "Resampler.hpp"
#include <cmath>

namespace ae2 {

void Resampler::setRates(uint32_t inRate, uint32_t outRate) {
    if (inRate  == 0) inRate  = 44100;
    if (outRate == 0) outRate = 128000;
    inRate_  = inRate;
    outRate_ = outRate;
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

    /* ratio = inRate / outRate (сколько входных на 1 выходной) */
    double ratio = (double)inRate_ / (double)outRate_;

    for (uint32_t i = 0; i < outTotal; ++i) {
        double srcPos = (double)i * ratio;
        uint32_t idx = (uint32_t)srcPos;

        s16 sample;
        if (alg_ == Algorithm::Linear && idx + 1 < srcLen) {
            double frac = srcPos - (double)idx;
            sample = (s16)((double)src[idx] * (1.0 - frac) + (double)src[idx + 1] * frac);
        } else {
            if (idx >= srcLen) idx = srcLen - 1;
            sample = src[idx];
        }

        if (i < dst1Cap) {
            dst1[i] = sample;
        } else {
            dst2[i - dst1Cap] = sample;
        }
    }
    return outTotal;
}

} // namespace ae2

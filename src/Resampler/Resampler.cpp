/// @file Resampler.cpp
#include "Resampler.hpp"
#include <cstring>

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
    if (inRate_ == outRate_) return inLen;
    return (uint32_t)(((uint64_t)inLen * outRate_ + inRate_ - 1) / inRate_);
}

/* ── Вспомогательная: линейная интерполяция для одного сегмента ── */
static inline void resampleLinear_(const s16* src, uint32_t srcLen,
                                   s16* dst, uint32_t count,
                                   uint64_t& phase, uint32_t step) {
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t idx = (uint32_t)(phase >> 16);
        if (idx + 1 < srcLen) {
            const int32_t frac = (int32_t)((phase & 0xFFFFu) >> 1);
            const int32_t diff = (int32_t)src[idx + 1] - (int32_t)src[idx];
            dst[i] = (s16)((int32_t)src[idx] + ((diff * frac) >> 15));
        } else {
            if (idx >= srcLen) idx = srcLen - 1;
            dst[i] = src[idx];
        }
        phase += step;
    }
}

/* ── Вспомогательная: nearest для одного сегмента ── */
static inline void resampleNearest_(const s16* src, uint32_t srcLen,
                                    s16* dst, uint32_t count,
                                    uint64_t& phase, uint32_t step) {
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t idx = (uint32_t)(phase >> 16);
        if (idx >= srcLen) idx = srcLen - 1;
        dst[i] = src[idx];
        phase += step;
    }
}

uint32_t Resampler::process(const s16* src, uint32_t srcLen,
                            s16* dst1, uint32_t dst1Cap,
                            s16* dst2, uint32_t dst2Cap) const {
    if (!src || srcLen == 0) return 0;

    /* ── Быстрый путь: passthrough (inRate == outRate) ── */
    if (inRate_ == outRate_) {
        uint32_t maxOut = dst1Cap + dst2Cap;
        uint32_t total = (srcLen > maxOut) ? maxOut : srcLen;
        uint32_t n1 = (total > dst1Cap) ? dst1Cap : total;
        uint32_t n2 = total - n1;
        std::memcpy(dst1, src, n1 * sizeof(s16));
        if (n2 > 0 && dst2)
            std::memcpy(dst2, src + n1, n2 * sizeof(s16));
        return total;
    }

    uint32_t outTotal = outputLength(srcLen);
    uint32_t maxOut = dst1Cap + dst2Cap;
    if (outTotal > maxOut) outTotal = maxOut;

    /* Разделяем вывод на два сегмента, убираем ветвление из горячего цикла */
    uint32_t seg1 = (outTotal > dst1Cap) ? dst1Cap : outTotal;
    uint32_t seg2 = outTotal - seg1;
    uint64_t phase = 0;

    if (alg_ == Algorithm::Linear) {
        resampleLinear_(src, srcLen, dst1, seg1, phase, phaseStep_);
        if (seg2 > 0 && dst2)
            resampleLinear_(src, srcLen, dst2, seg2, phase, phaseStep_);
    } else {
        resampleNearest_(src, srcLen, dst1, seg1, phase, phaseStep_);
        if (seg2 > 0 && dst2)
            resampleNearest_(src, srcLen, dst2, seg2, phase, phaseStep_);
    }
    return outTotal;
}

} // namespace ae2

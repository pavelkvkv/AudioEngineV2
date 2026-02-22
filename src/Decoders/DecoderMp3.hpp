#pragma once
#include "DecoderBase.hpp"

#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3.h"

namespace ae2 {

class DecoderMp3 final : public DecoderBase {
public:
    bool     open(FsAdapter& fs) override;
    uint32_t decode(s16* buf, uint32_t maxSamples) override;
    void     seek(uint32_t sec) override;
    uint32_t position() const override;
    uint32_t duration() const override;
    uint32_t sampleRate() const override { return sampleRate_; }
    void     close() override;

private:
    FsAdapter* fs_ = nullptr;
    mp3dec_t mp3d_{};

    static constexpr uint32_t kInBufSize = 16384;
    uint8_t  inBuf_[kInBufSize]{};
    uint32_t inBufLen_  = 0;
    uint32_t inBufPos_  = 0;

    uint32_t sampleRate_  = 44100;
    uint32_t channels_    = 2;
    uint32_t duration_    = 0;
    uint64_t totalSamplesDecoded_ = 0;

    bool refillInput_();
};

} // namespace ae2

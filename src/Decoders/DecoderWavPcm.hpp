#pragma once
#include "DecoderBase.hpp"

namespace ae2 {

class DecoderWavPcm final : public DecoderBase {
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
    uint16_t channels_      = 1;
    uint16_t bitsPerSample_  = 16;
    uint32_t sampleRate_     = 44100;
    uint32_t dataOffset_     = 0;
    uint32_t dataSize_       = 0;
    uint32_t bytesRead_      = 0;

    uint32_t bytesPerFrame_() const { return channels_ * (bitsPerSample_ / 8); }
};

} // namespace ae2

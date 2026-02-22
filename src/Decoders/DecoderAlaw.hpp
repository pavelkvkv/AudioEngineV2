#pragma once
#include "DecoderBase.hpp"

namespace ae2 {

class DecoderAlaw final : public DecoderBase {
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
    uint16_t channels_   = 1;
    uint32_t sampleRate_ = 8000;
    uint32_t dataOffset_ = 0;
    uint32_t dataSize_   = 0;
    uint32_t bytesRead_  = 0;

    static s16 decodeSample(uint8_t alaw);
};

} // namespace ae2

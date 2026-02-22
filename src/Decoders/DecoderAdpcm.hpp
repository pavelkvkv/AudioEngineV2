#pragma once
#include "DecoderBase.hpp"

namespace ae2 {

class DecoderAdpcm final : public DecoderBase {
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
    uint16_t channels_        = 1;
    uint32_t sampleRate_      = 22050;
    uint16_t blockAlign_      = 256;
    uint16_t samplesPerBlock_ = 0;
    uint32_t dataOffset_      = 0;
    uint32_t dataSize_        = 0;
    uint32_t blocksRead_      = 0;
    uint32_t totalBlocks_     = 0;

    struct AdpcmState { s16 predictor; uint8_t stepIndex; };
    static s16 decodeNibble(uint8_t nibble, AdpcmState& state);
};

} // namespace ae2

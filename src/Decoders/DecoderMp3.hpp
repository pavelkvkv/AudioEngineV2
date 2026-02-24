#pragma once
/// @file DecoderMp3.hpp
/// @brief MP3-декодер на базе Helix fixed-point decoder (RealNetworks).
///
/// Helix специально оптимизирован для ARM: fixed-point DSP, ~6KB RAM,
/// в 2-5x быстрее minimp3 на Cortex-M4.

#include "DecoderBase.hpp"
#include "mp3dec.h"   // Helix public API

namespace ae2 {

class DecoderMp3 final : public DecoderBase {
public:
    ~DecoderMp3() override { close(); }

    bool     open(FsAdapter& fs) override;
    uint32_t decode(s16* buf, uint32_t maxSamples) override;
    void     seek(uint32_t sec) override;
    uint32_t position() const override;
    uint32_t duration() const override;
    uint32_t sampleRate() const override { return sampleRate_; }
    void     close() override;

private:
    FsAdapter* fs_ = nullptr;
    HMP3Decoder hDec_ = nullptr;   ///< Helix decoder handle

    static constexpr uint32_t kInBufSize = 4096;  ///< Входной буфер (Helix нужен меньший)
    uint8_t  inBuf_[kInBufSize]{};
    uint32_t inBufLen_  = 0;
    uint32_t inBufPos_  = 0;

    uint32_t sampleRate_  = 44100;
    uint32_t channels_    = 2;
    uint32_t duration_    = 0;
    uint64_t totalSamplesDecoded_ = 0;

    /// Буфер остатка фрейма (макс. 1152 mono сэмплов для MPEG1 Layer3)
    static constexpr uint32_t kMaxFrameMono = 1152;
    s16 leftover_[kMaxFrameMono]{};
    uint32_t leftoverLen_ = 0;
    uint32_t leftoverPos_ = 0;

    bool refillInput_();
    int  findSyncAndDecode_(s16* pcm, MP3FrameInfo& info);
};

} // namespace ae2

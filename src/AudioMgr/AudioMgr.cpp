/// @file AudioMgr.cpp
/// @brief Реализация единого аудиоменеджера.
#include "AudioEngineV2/AudioMgr.hpp"
#include "AudioHw/AudioHw.hpp"
#include "Resampler/Resampler.hpp"
#include "FsAdapter/FsAdapter.hpp"
#include "CodecDetect/CodecDetect.hpp"
#include "Mp3Duration/Mp3Duration.hpp"
#include "Decoders/DecoderBase.hpp"
#include "Decoders/DecoderWavPcm.hpp"
#include "Decoders/DecoderMp3.hpp"
#include "Decoders/DecoderAdpcm.hpp"
#include "Decoders/DecoderAlaw.hpp"
#include "Decoders/DecoderUlaw.hpp"

#include <cstring>
#include <cstdio>
#include <algorithm>

/* Fallback arm_scale_q15 if arm_math.h is not available */
#if defined(__has_include)
#  if __has_include("arm_math.h")
#    include "arm_math.h"
#    define HAS_ARM_MATH 1
#  endif
#endif
#ifndef HAS_ARM_MATH
static inline void arm_scale_q15(const int16_t* src, int16_t scaleFract, int8_t shift,
                                  int16_t* dst, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        int32_t v = ((int32_t)src[i] * (int32_t)scaleFract);
        if (shift >= 0) v >>= (15 - shift); else v >>= (15 + (-shift));
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        dst[i] = (int16_t)v;
    }
}
#endif

namespace ae2 {

/* ═══ Singleton ═══ */

AudioMgr& AudioMgr::instance() {
    static AudioMgr mgr;
    return mgr;
}

AudioMgr::AudioMgr() {
    sources_[(int)SrcId::Disabled].priority = 0;
    sources_[(int)SrcId::Player].priority   = 1;
    sources_[(int)SrcId::Player].volume     = 7;
    sources_[(int)SrcId::AdcDirect].priority = 2;
    sources_[(int)SrcId::FrontExternal].priority = 1;
    sources_[(int)SrcId::Diag].priority     = 3;

    fs_ = new FsAdapter(fsBuf_, sizeof(fsBuf_));
    resampler_ = new Resampler();
    cmdQueue_ = xQueueCreate(kCmdQueueDepth, sizeof(Cmd));
    AudioHw::instance().start();
    xTaskCreate(taskEntry_, "AudioMgr", 4096, this, 5, &task_);
    initialized_ = true;
}

/* ═══ sendCmd_ ═══ */

void AudioMgr::sendCmd_(QueueHandle_t q, const Cmd& cmd) {
    if (!q) return;
    xQueueSend(q, &cmd, pdMS_TO_TICKS(50));
}

/* ═══ Thread-safe API ═══ */

void AudioMgr::play()  { Cmd c{}; c.type = Cmd::Play;  sendCmd_(cmdQueue_, c); }
void AudioMgr::pause() { Cmd c{}; c.type = Cmd::Pause; sendCmd_(cmdQueue_, c); }
void AudioMgr::stop()  { Cmd c{}; c.type = Cmd::Stop;  sendCmd_(cmdQueue_, c); }

void AudioMgr::addFile(const char* path, uint32_t startSec, Output out, bool front) {
    if (!path) return;
    Cmd c{};
    c.type = front ? Cmd::AddFileFront : Cmd::AddFile;
    std::strncpy(c.file.path, path, sizeof(c.file.path) - 1);
    c.file.path[sizeof(c.file.path)-1] = '\0';
    c.file.startSec = startSec;
    c.file.output   = (uint8_t)out;
    sendCmd_(cmdQueue_, c);
}

void AudioMgr::clearQueue() { Cmd c{}; c.type = Cmd::ClearQueue; sendCmd_(cmdQueue_, c); }

void AudioMgr::seek(uint32_t sec) {
    Cmd c{}; c.type = Cmd::Seek; c.seek.sec = sec; sendCmd_(cmdQueue_, c);
}
void AudioMgr::forward(uint32_t sec) {
    Cmd c{}; c.type = Cmd::Forward; c.seek.sec = sec; sendCmd_(cmdQueue_, c);
}
void AudioMgr::rewind(uint32_t sec) {
    Cmd c{}; c.type = Cmd::Rewind; c.seek.sec = sec; sendCmd_(cmdQueue_, c);
}

void AudioMgr::requestActivate(SrcId id) {
    Cmd c{}; c.type = Cmd::Activate; c.source.srcId = (uint8_t)id; sendCmd_(cmdQueue_, c);
}
void AudioMgr::requestDeactivate(SrcId id) {
    Cmd c{}; c.type = Cmd::Deactivate; c.source.srcId = (uint8_t)id; sendCmd_(cmdQueue_, c);
}
void AudioMgr::setVolume(SrcId id, uint8_t vol) {
    Cmd c{}; c.type = Cmd::SetVolume;
    c.volume.srcId = (uint8_t)id; c.volume.vol = vol; sendCmd_(cmdQueue_, c);
}
void AudioMgr::setSampleRate(uint32_t rate) {
    Cmd c{}; c.type = Cmd::SetSampleRate; c.sampleRate.rate = rate; sendCmd_(cmdQueue_, c);
}
void AudioMgr::volumeChanged() {
    Cmd c{}; c.type = Cmd::VolumeChanged; sendCmd_(cmdQueue_, c);
}

void AudioMgr::registerSource(SrcId id, uint8_t priority, ExternalFeed feed) {
    uint8_t idx = (uint8_t)id;
    if (idx >= kMaxSources) return;
    sources_[idx].priority = priority;
    sources_[idx].feed = feed;
}

void AudioMgr::unregisterSource(SrcId id) {
    uint8_t idx = (uint8_t)id;
    if (idx >= kMaxSources) return;
    sources_[idx].feed = {nullptr, nullptr};
    sources_[idx].wantPlay = false;
    sources_[idx].active = false;
}

AudioMgr::PlayerStatus AudioMgr::playerStatus() const {
    PlayerStatus copy;
    std::memcpy(&copy, (const void*)&status_, sizeof(copy));
    return copy;
}

SrcId AudioMgr::currentSource() const { return currentSrcAtomic_; }

/* ═══ Queue ═══ */

bool AudioMgr::queuePush_(const char* path, uint32_t startSec, Output out) {
    if (queueCount_ >= kMaxQueue) return false;
    auto& e = queue_[queueTail_];
    std::strncpy(e.path, path, sizeof(e.path) - 1);
    e.path[sizeof(e.path)-1] = '\0';
    e.startSec = startSec; e.output = out; e.used = true;
    queueTail_ = (queueTail_ + 1) % kMaxQueue;
    queueCount_++;
    return true;
}

bool AudioMgr::queuePushFront_(const char* path, uint32_t startSec, Output out) {
    if (queueCount_ >= kMaxQueue) return false;
    queueHead_ = (queueHead_ == 0) ? (kMaxQueue - 1) : (queueHead_ - 1);
    auto& e = queue_[queueHead_];
    std::strncpy(e.path, path, sizeof(e.path) - 1);
    e.path[sizeof(e.path)-1] = '\0';
    e.startSec = startSec; e.output = out; e.used = true;
    queueCount_++;
    return true;
}

bool AudioMgr::queuePop_(QueueEntry& out) {
    if (queueCount_ == 0) return false;
    out = queue_[queueHead_];
    queue_[queueHead_].used = false;
    queueHead_ = (queueHead_ + 1) % kMaxQueue;
    queueCount_--;
    return true;
}

void AudioMgr::queueClear_() {
    for (auto& e : queue_) e.used = false;
    queueHead_ = queueTail_ = queueCount_ = 0;
}

/* ═══ Process commands ═══ */

void AudioMgr::processCommands_() {
    Cmd cmd;
    while (xQueueReceive(cmdQueue_, &cmd, 0) == pdPASS) {
        switch (cmd.type) {
        case Cmd::Play:
            if (playerState_ == PlayerState::Paused) {
                playerState_ = PlayerState::Playing;
                sources_[(int)SrcId::Player].wantPlay = true;
            } else if (playerState_ == PlayerState::Stopped && queueCount_ > 0) {
                sources_[(int)SrcId::Player].wantPlay = true;
                startNextTrack_();
            }
            break;

        case Cmd::Pause:
            if (playerState_ == PlayerState::Playing)
                playerState_ = PlayerState::Paused;
            break;

        case Cmd::Stop:
            destroyDecoder(decoder_);
            fs_->close();
            playerState_ = PlayerState::Stopped;
            sources_[(int)SrcId::Player].wantPlay = false;
            if (currentSrc_ == SrcId::Player) {
                AudioHw::instance().flush(true);
                currentSrc_ = SrcId::Disabled;
                currentSrcAtomic_ = SrcId::Disabled;
            }
            break;

        case Cmd::AddFile:
            queuePush_(cmd.file.path, cmd.file.startSec, (Output)cmd.file.output);
            if (playerState_ == PlayerState::Stopped) {
                sources_[(int)SrcId::Player].wantPlay = true;
                startNextTrack_();
            }
            break;

        case Cmd::AddFileFront:
            destroyDecoder(decoder_);
            fs_->close();
            queuePushFront_(cmd.file.path, cmd.file.startSec, (Output)cmd.file.output);
            sources_[(int)SrcId::Player].wantPlay = true;
            startNextTrack_();
            break;

        case Cmd::ClearQueue:
            destroyDecoder(decoder_);
            fs_->close();
            queueClear_();
            playerState_ = PlayerState::Stopped;
            sources_[(int)SrcId::Player].wantPlay = false;
            break;

        case Cmd::Seek:
            if (decoder_) decoder_->seek(cmd.seek.sec);
            break;

        case Cmd::Forward:
            if (decoder_) { uint32_t c = decoder_->position(); decoder_->seek(c + cmd.seek.sec); }
            break;

        case Cmd::Rewind:
            if (decoder_) {
                uint32_t c = decoder_->position();
                decoder_->seek((c > cmd.seek.sec) ? (c - cmd.seek.sec) : 0);
            }
            break;

        case Cmd::Activate: {
            uint8_t idx = cmd.source.srcId;
            if (idx < kMaxSources) sources_[idx].wantPlay = true;
        } break;

        case Cmd::Deactivate: {
            uint8_t idx = cmd.source.srcId;
            if (idx < kMaxSources) {
                sources_[idx].wantPlay = false;
                sources_[idx].active = false;
                if (currentSrc_ == (SrcId)idx) {
                    AudioHw::instance().flush(true);
                    currentSrc_ = SrcId::Disabled;
                    currentSrcAtomic_ = SrcId::Disabled;
                }
            }
        } break;

        case Cmd::SetVolume: {
            uint8_t idx = cmd.volume.srcId;
            if (idx < kMaxSources) {
                uint8_t v = cmd.volume.vol;
                if (v > 10) v = 10;
                sources_[idx].volume = v;
            }
        } break;

        case Cmd::SetSampleRate:
            AudioHw::instance().setSampleRate(cmd.sampleRate.rate);
            break;

        case Cmd::VolumeChanged:
            break;
        }
    }
    routerUpdate_();
    updateStatus_();
}

/* ═══ Router ═══ */

void AudioMgr::routerUpdate_() {
    SrcId best = SrcId::Disabled;
    uint8_t bestPrio = 0;
    for (uint8_t i = 1; i < kMaxSources; ++i) {
        if (sources_[i].wantPlay && sources_[i].priority > bestPrio) {
            bestPrio = sources_[i].priority;
            best = (SrcId)i;
        }
    }
    if (best != currentSrc_) switchSource_(best);
}

void AudioMgr::switchSource_(SrcId newId) {
    if (currentSrc_ != SrcId::Disabled) {
        sources_[(int)currentSrc_].active = false;
        if (currentSrc_ == SrcId::Player && playerState_ == PlayerState::Playing)
            playerState_ = PlayerState::Paused;
        AudioHw::instance().flush(true);
    }
    currentSrc_ = newId;
    currentSrcAtomic_ = newId;
    if (newId != SrcId::Disabled) {
        sources_[(int)newId].active = true;
        if (newId == SrcId::Player && playerState_ == PlayerState::Paused)
            playerState_ = PlayerState::Playing;
    }
}

/* ═══ Next track ═══ */

void AudioMgr::startNextTrack_() {
    destroyDecoder(decoder_);
    fs_->close();

    QueueEntry entry;
    if (!queuePop_(entry)) {
        playerState_ = PlayerState::Stopped;
        sources_[(int)SrcId::Player].wantPlay = false;
        return;
    }

    if (!fs_->open(std::string(entry.path))) {
        startNextTrack_();
        return;
    }

    auto codecType = CodecDetect::detect(*fs_);
    switch (codecType) {
        case CodecDetect::Type::WavPcm:   emplaceDecoder<DecoderWavPcm>(decoderMem_, decoder_); break;
        case CodecDetect::Type::Mp3:      emplaceDecoder<DecoderMp3>(decoderMem_, decoder_); break;
        case CodecDetect::Type::WavAdpcm: emplaceDecoder<DecoderAdpcm>(decoderMem_, decoder_); break;
        case CodecDetect::Type::WavAlaw:  emplaceDecoder<DecoderAlaw>(decoderMem_, decoder_); break;
        case CodecDetect::Type::WavUlaw:  emplaceDecoder<DecoderUlaw>(decoderMem_, decoder_); break;
        default: startNextTrack_(); return;
    }

    if (!decoder_->open(*fs_)) {
        destroyDecoder(decoder_);
        startNextTrack_();
        return;
    }

    if (entry.startSec > 0) decoder_->seek(entry.startSec);
    playerState_ = PlayerState::Playing;
    sources_[(int)SrcId::Player].wantPlay = true;
    sources_[(int)SrcId::Player].output = entry.output;

    auto name = fs_->name();
    std::strncpy((char*)status_.filename, name.c_str(), sizeof(status_.filename) - 1);
    ((char*)status_.filename)[sizeof(status_.filename)-1] = '\0';
}

/* ═══ Pipeline tick ═══ */

void AudioMgr::pipelineTick_() {
    auto& hw = AudioHw::instance();
    auto* resamp = static_cast<Resampler*>(resampler_);

    uint32_t decoded = 0;
    uint32_t srcSampleRate = hw.sampleRate();

    if (currentSrc_ == SrcId::Player) {
        if (playerState_ != PlayerState::Playing || !decoder_) return;
        decoded = decoder_->decode(decodeBuf_, 1024);
        if (decoded == 0) { startNextTrack_(); return; }
        srcSampleRate = decoder_->sampleRate();
    } else {
        uint8_t idx = (uint8_t)currentSrc_;
        if (idx >= kMaxSources || !sources_[idx].feed.feed) return;
        decoded = sources_[idx].feed.feed(
            sources_[idx].feed.ctx, decodeBuf_, 1024, &srcSampleRate);
        if (decoded == 0) return;
    }

    /* Volume */
    uint8_t volIdx = sources_[(int)currentSrc_].volume;
    if (volIdx < 7) {
        int16_t scale = kVolumeTable[volIdx];
        arm_scale_q15(decodeBuf_, scale, 0, decodeBuf_, decoded);
    }

    /* Resample + write */
    resamp->setRates(srcSampleRate, hw.sampleRate());
    uint32_t outLen = resamp->outputLength(decoded);
    if (outLen == 0) return;

    auto wr = hw.acquireWrite(outLen, pdMS_TO_TICKS(100));
    if (wr.cap1 + wr.cap2 == 0) return;

    uint32_t toWrite = std::min(outLen, wr.cap1 + wr.cap2);
    resamp->process(decodeBuf_, decoded, wr.ptr1, wr.cap1, wr.ptr2, wr.cap2);
    hw.commitWrite(toWrite);
}

/* ═══ Status update ═══ */

void AudioMgr::updateStatus_() {
    auto& st = *(PlayerStatus*)&status_;
    st.playing  = (playerState_ == PlayerState::Playing);
    st.paused   = (playerState_ == PlayerState::Paused);
    st.fileReady = (decoder_ != nullptr && decoder_->status() != DecoderBase::Status::Closed);

    if (decoder_) {
        st.position = decoder_->position();
        st.duration = decoder_->duration();
        st.positionPercent = (st.duration > 0) ? (uint8_t)(st.position * 100 / st.duration) : 0;
    } else {
        st.position = st.duration = 0;
        st.positionPercent = 0;
    }
}

/* ═══ Main loop ═══ */

void AudioMgr::taskEntry_(void* arg) { static_cast<AudioMgr*>(arg)->taskLoop_(); }

void AudioMgr::taskLoop_() {
    for (;;) {
        processCommands_();
        if (currentSrc_ == SrcId::Disabled) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
            continue;
        }
        pipelineTick_();
        vTaskDelay(1);
    }
}

} // namespace ae2

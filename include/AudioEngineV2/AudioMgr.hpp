#pragma once
/// @file AudioMgr.hpp
/// @brief Единый аудиоменеджер: роутер + плеер + пайплайн.

#include "AudioEngineV2/Types.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <cstddef>

namespace ae2 {

class DecoderBase;
class FsAdapter;
class Resampler;

class AudioMgr {
public:
    static AudioMgr& instance();

    /* ── Внешние источники ── */
    struct ExternalFeed {
        uint32_t (*feed)(void* ctx, s16* buf, uint32_t maxSamples,
                         uint32_t* outSampleRate);
        void* ctx;
    };

    void registerSource(SrcId id, uint8_t priority, ExternalFeed feed);
    void unregisterSource(SrcId id);

    /* ── Команды (thread-safe, из любого таска) ── */
    void play();
    void pause();
    void stop();
    void addFile(const char* path, uint32_t startSec = 0,
                 Output out = Output::FrontSpeaker, bool front = false);
    void clearQueue();
    void seek(uint32_t sec);
    void forward(uint32_t sec = 10);
    void rewind(uint32_t sec = 10);
    void requestActivate(SrcId id);
    void requestDeactivate(SrcId id);
    void setVolume(SrcId id, uint8_t vol);
    void setSampleRate(uint32_t rate);
    void volumeChanged();

    /* ── Статус (lock-free, один writer) ── */
    struct PlayerStatus {
        char     filename[64]{};
        uint32_t position  = 0;
        uint32_t duration  = 0;
        uint8_t  positionPercent = 0;
        bool     playing   = false;
        bool     paused    = false;
        bool     fileReady = false;
    };
    PlayerStatus playerStatus() const;
    SrcId   currentSource() const;
    bool    isInitialized() const { return initialized_; }
    uint32_t queueSize() const { return queueCount_; }

    AudioMgr(const AudioMgr&) = delete;
    AudioMgr& operator=(const AudioMgr&) = delete;

    /* ── Cmd — публичный для sendCmd_ (используется только внутри) ── */
    struct Cmd {
        enum Type : uint8_t {
            Play, Pause, Stop, AddFile, AddFileFront, ClearQueue,
            Seek, Forward, Rewind,
            Activate, Deactivate,
            SetVolume, SetSampleRate,
            VolumeChanged
        };
        Type type;
        union {
            struct { char path[240]; uint32_t startSec; uint8_t output; } file;
            struct { uint8_t srcId; } source;
            struct { uint8_t srcId; uint8_t vol; } volume;
            struct { uint32_t sec; } seek;
            struct { uint32_t rate; } sampleRate;
        };
    };

private:
    AudioMgr();
    ~AudioMgr() = default;

    static void sendCmd_(QueueHandle_t q, const Cmd& cmd);

    QueueHandle_t cmdQueue_ = nullptr;
    static constexpr uint32_t kCmdQueueDepth = 32;

    /* ── Таск ── */
    TaskHandle_t task_ = nullptr;
    static void taskEntry_(void* arg);
    void taskLoop_();

    /* ── Источники ── */
    struct SourceInfo {
        uint8_t  priority  = 0;
        bool     wantPlay  = false;
        bool     active    = false;
        uint8_t  volume    = 7;
        Output   output    = Output::FrontSpeaker;
        ExternalFeed feed{nullptr, nullptr};
    };
    static constexpr uint32_t kMaxSources = (uint32_t)SrcId::Count;
    SourceInfo sources_[kMaxSources]{};
    SrcId currentSrc_ = SrcId::Disabled;

    /* ── Плеер ── */
    enum class PlayerState : uint8_t { Stopped, PlayWaiting, Playing, Paused };
    PlayerState playerState_ = PlayerState::Stopped;

    struct QueueEntry {
        char     path[240]{};
        uint32_t startSec = 0;
        Output   output   = Output::FrontSpeaker;
        bool     used     = false;
    };
    static constexpr uint32_t kMaxQueue = 16;
    QueueEntry queue_[kMaxQueue]{};
    uint32_t queueHead_ = 0;
    uint32_t queueTail_ = 0;
    uint32_t queueCount_ = 0;

    bool queuePush_(const char* path, uint32_t startSec, Output out);
    bool queuePushFront_(const char* path, uint32_t startSec, Output out);
    bool queuePop_(QueueEntry& out);
    void queueClear_();

    /* ── Декодер ── */
    alignas(16) uint8_t decoderMem_[80000]{};
    DecoderBase* decoder_  = nullptr;
    uint8_t fsBuf_[8192]{};
    FsAdapter* fs_ = nullptr;

    /* ── Буферы пайплайна ── */
    s16 decodeBuf_[2048]{};

    /* ── Ресемплер ── */
    void* resampler_ = nullptr;

    /* ── Процессинг ── */
    void processCommands_();
    void routerUpdate_();
    void switchSource_(SrcId newId);
    void startNextTrack_();
    void pipelineTick_();

    /* ── Статус ── */
    volatile PlayerStatus status_{};
    volatile SrcId currentSrcAtomic_ = SrcId::Disabled;
    void updateStatus_();

    bool initialized_ = false;
    TickType_t lastProgressLog_ = 0;  ///< Тик последнего лога прогресса
};

} // namespace ae2

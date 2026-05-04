#pragma once
/// @file AudioMgr.hpp
/// @brief Единый аудиоменеджер: роутер + плеер + пайплайн.

#include "AudioEngineV2/Types.hpp"
#include "PlayerSpiProtocol.hpp"
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
    void removeFromQueue(uint32_t trackId);
    void seek(uint32_t sec);
    void forward(uint32_t sec = 10);
    void rewind(uint32_t sec = 10);
    void requestActivate(SrcId id, Output out = Output::FrontSpeaker);
    void requestDeactivate(SrcId id);
    void setVolume(SrcId id, uint8_t vol);
    void setSampleRate(uint32_t rate);
    void volumeChanged();

    /* ── Callback на изменение состояния заднего выхода ── */
    using RearOutputCb = void(*)(bool active);
    void setRearOutputCb(RearOutputCb cb);

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
	[[nodiscard]] PlayerStatus playerStatus() const;
	[[nodiscard]] SrcId currentSource() const;
	[[nodiscard]] bool isInitialized() const { return initialized_; }
	[[nodiscard]] uint32_t queueSize() const { return queueCount_; }

    /// Заполняет снэпшот очереди в формате протокола SPI.
    /// @return количество записанных элементов
    uint8_t getQueueSnapshot(PlayerQueueEntry* out, uint8_t maxEntries) const;

	AudioMgr(const AudioMgr&) = delete;
    AudioMgr& operator=(const AudioMgr&) = delete;

    /* ── Cmd — публичный для sendCmd_ (используется только внутри) ── */
    struct Cmd {
        enum Type : uint8_t {
            Play, Pause, Stop, AddFile, AddFileFront, ClearQueue,
            Seek, Forward, Rewind,
            Activate, Deactivate,
            SetVolume, SetSampleRate,
            VolumeChanged,
            RemoveQueueItem
        };
        Type type;
        union {
            struct { char path[128]; uint32_t startSec; uint8_t output; } file;
            struct { uint8_t srcId; uint8_t output; } source;
            struct { uint8_t srcId; uint8_t vol; } volume;
            struct { uint32_t sec; } seek;
            struct { uint32_t rate; } sampleRate;
            struct { uint32_t trackId; } remove;
        };
    };

private:
    AudioMgr();
    ~AudioMgr() = default;

    static void sendCmd_(QueueHandle_t q, const Cmd& cmd);

    QueueHandle_t cmdQueue_ = nullptr;
    static constexpr uint32_t kCmdQueueDepth = 32;
    StaticQueue_t cmdQueueBuf_{};
    uint8_t cmdQueueStorage_[kCmdQueueDepth * sizeof(Cmd)]{};

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
		ExternalFeed feed{.feed = nullptr, .ctx = nullptr};
	};
    static constexpr uint32_t kMaxSources = (uint32_t)SrcId::Count;
    SourceInfo sources_[kMaxSources]{};
    SrcId currentSrc_ = SrcId::Disabled;

    /* ── Плеер ── */
    enum class PlayerState : uint8_t { Stopped, PlayWaiting, Playing, Paused };
    PlayerState playerState_ = PlayerState::Stopped;

    struct QueueEntry {
        char     path[128]{};
        uint32_t startSec = 0;
        Output   output   = Output::FrontSpeaker;
        bool     used     = false;
        uint32_t trackId  = 0;  ///< Стабильный ID трека (0 = невалидный)
    };
    static constexpr uint32_t kMaxQueue = 16;
    QueueEntry queue_[kMaxQueue]{};
    uint32_t queueHead_ = 0;
    uint32_t queueTail_ = 0;
    uint32_t queueCount_ = 0;

    uint32_t nextTrackId_ = 1;  ///< Следующий ID трека (инкрементный)
    bool queuePush_(const char* path, uint32_t startSec, Output out);
    bool queuePushFront_(const char* path, uint32_t startSec, Output out);
    bool queuePop_(QueueEntry& out);
    void queueClear_();
    bool queueRemoveById_(uint32_t trackId);

    /* ── Текущий трек ── */
    char   currentPath_[128]{};                 ///< Путь текущего воспроизводимого файла
    Output currentOutput_{Output::FrontSpeaker}; ///< Выход текущего воспроизводимого файла

    /* ── Декодер ── */
    alignas(16) uint8_t decoderMem_[8192]{};
    DecoderBase* decoder_  = nullptr;
    uint8_t fsBuf_[4096]{};
    alignas(8) uint8_t fsMem_[1152]{};  ///< placement-хранилище для FsAdapter
    FsAdapter* fs_ = nullptr;

    /* ── Буферы пайплайна ── */
    s16 decodeBuf_[2048]{};
    uint32_t residualCount_{0};      ///< необработанных сэмплов с прошлого тика
    uint32_t residualOffset_{0};     ///< смещение в decodeBuf_
    uint32_t residualSampleRate_{0}; ///< частота дискретизации остатка

    /* ── Диагностика пайплайна ── */
    struct PipeStats {
        uint32_t loopIter    = 0; ///< итерации главного цикла
        uint32_t decodes     = 0; ///< вызовы decode
        uint32_t residuals   = 0; ///< использования остатка
        uint32_t truncations = 0; ///< раз usable < decoded
        uint32_t timeouts    = 0; ///< таймаут acquireWrite
        uint32_t waitTicks   = 0; ///< суммарное ожидание в acquireWrite (тики)
        uint32_t maxWait     = 0; ///< макс. ожидание за период (тики)
        uint32_t samplesIn   = 0; ///< входных сэмплов обработано
        uint32_t samplesOut  = 0; ///< выходных сэмплов записано
    } pipeStats_;
    TickType_t lastPipeStatsLog_ = 0;

    /* ── Ресемплер ── */
    alignas(4) uint8_t resamplerMem_[32]{};  ///< placement-хранилище для Resampler
    void* resampler_ = nullptr;

    /* ── Процессинг ── */
    void processCommands_();
    void routerUpdate_();
    void switchSource_(SrcId newId);
    void startNextTrack_();
    void pipelineTick_();

    /// True, если в данный момент DAC занят не-Player источником
    /// (роутер вытеснил плеер по приоритету, например AdcDirect).
    /// В этом состоянии команды плеера должны откладывать побочные
    /// эффекты (открытие декодера, переход playerState_ в Playing) —
    /// switchSource_ выполнит их сам, когда источник вернётся.
    [[nodiscard]] bool isPlayerPreempted_() const noexcept {
        return currentSrc_ != SrcId::Disabled && currentSrc_ != SrcId::Player;
    }

    /* ── Статус ── */
    volatile PlayerStatus status_{};
    volatile SrcId currentSrcAtomic_ = SrcId::Disabled;
    volatile uint8_t queueSnapshotCount_ = 0;
    PlayerQueueEntry queueSnapshot_[PLAYER_MAX_QUEUE]{};
    uint32_t currentTrackId_ = 0;  ///< trackId текущего воспроизводимого трека
    void updateStatus_();

    bool initialized_ = false;
    TickType_t lastProgressLog_ = 0;  ///< Тик последнего лога прогресса

    /* ── Уведомление о заднем выходе ── */
    RearOutputCb rearOutputCb_ = nullptr;
    bool rearOutputActive_ = false;
    void notifyRearOutput_(bool active);
};

} // namespace ae2

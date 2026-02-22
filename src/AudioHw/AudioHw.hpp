#pragma once
/// @file AudioHw.hpp
/// @brief Аппаратный слой: кольцевой буфер + DMA-эмуляция (на хосте — drain-тред).

#include "AudioEngineV2/Types.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <atomic>
#include <cstddef>
#include <cstring>

namespace ae2 {

class AudioHw final {
public:
    static AudioHw& instance();

    /* ── Конфигурация ── */
    void setSampleRate(uint32_t rate);
    uint32_t sampleRate() const { return sampleRate_; }

    void start();
    void stop();
    bool isStarted() const { return started_; }

    /* ── Прямая запись ── */
    struct WriteRegion {
        s16*     ptr1  = nullptr;
        uint32_t cap1  = 0;
        s16*     ptr2  = nullptr;
        uint32_t cap2  = 0;
    };

    /// Заблокироваться до появления >= minSamples свободного места.
    WriteRegion acquireWrite(uint32_t minSamples, TickType_t timeout);
    /// Продвинуть write pointer.
    void commitWrite(uint32_t written);
    /// Сбросить буфер (с опциональным fade-out).
    void flush(bool fadeOut = true);
    /// Сколько свободного места.
    uint32_t freeSpace() const;

    static constexpr uint32_t RingSize = 16384;

    // Не копируем
    AudioHw(const AudioHw&) = delete;
    AudioHw& operator=(const AudioHw&) = delete;

private:
    AudioHw();
    ~AudioHw() = default;

    s16 ring_[RingSize]{};
    std::atomic<uint32_t> writePos_{0};
    std::atomic<uint32_t> readPos_{0};
    uint32_t sampleRate_{128000};
    bool started_{false};

    /* Drain-тред (эмуляция DMA-потребления на хосте) */
    TaskHandle_t drainTask_{nullptr};
    static void drainEntry_(void* arg);
    void drain_();

    /* Тишина: при flush обнуляем */
    static constexpr uint32_t FadeSamples = 200;
};

} // namespace ae2

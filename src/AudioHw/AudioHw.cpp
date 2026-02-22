/// @file AudioHw.cpp
#include "AudioHw.hpp"
#include <algorithm>

namespace ae2 {

AudioHw& AudioHw::instance() {
    static AudioHw hw;
    return hw;
}

AudioHw::AudioHw() {
    std::memset(ring_, 0, sizeof(ring_));
}

void AudioHw::setSampleRate(uint32_t rate) {
    if (rate == 0) rate = 128000;
    sampleRate_ = rate;
}

void AudioHw::start() {
    if (started_) return;
    writePos_.store(0, std::memory_order_relaxed);
    readPos_.store(0, std::memory_order_relaxed);
    started_ = true;
    if (!drainTask_) {
        xTaskCreate(drainEntry_, "AeHwDrain", 1024, this, 2, &drainTask_);
    }
}

void AudioHw::stop() {
    started_ = false;
}

uint32_t AudioHw::freeSpace() const {
    uint32_t w = writePos_.load(std::memory_order_acquire);
    uint32_t r = readPos_.load(std::memory_order_acquire);
    uint32_t used = (w >= r) ? (w - r) : (RingSize - r + w);
    return RingSize - 1 - used;  // -1 чтобы отличить полный от пустого
}

AudioHw::WriteRegion AudioHw::acquireWrite(uint32_t minSamples, TickType_t timeout) {
    WriteRegion wr;
    TickType_t waited = 0;
    while (freeSpace() < minSamples) {
        if (!started_ || waited >= timeout) return wr;
        vTaskDelay(1);
        waited++;
    }
    uint32_t w = writePos_.load(std::memory_order_relaxed);
    uint32_t avail = freeSpace();
    if (avail == 0) return wr;

    uint32_t toEnd = RingSize - w;
    if (toEnd >= avail) {
        wr.ptr1 = ring_ + w;
        wr.cap1 = avail;
    } else {
        wr.ptr1 = ring_ + w;
        wr.cap1 = toEnd;
        wr.ptr2 = ring_;
        wr.cap2 = avail - toEnd;
    }
    return wr;
}

void AudioHw::commitWrite(uint32_t written) {
    uint32_t w = writePos_.load(std::memory_order_relaxed);
    writePos_.store((w + written) % RingSize, std::memory_order_release);
}

void AudioHw::flush(bool fadeOut) {
    if (fadeOut) {
        /* Быстрый fade-out: обнуляем последние FadeSamples записанных */
        uint32_t w = writePos_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < FadeSamples && i < RingSize; ++i) {
            uint32_t idx = (w >= i + 1) ? (w - i - 1) : (RingSize + w - i - 1);
            int32_t scale = (int32_t)(FadeSamples - i);
            ring_[idx] = (s16)((int32_t)ring_[idx] * scale / (int32_t)FadeSamples);
        }
    }
    /* Сбрасываем write к read */
    uint32_t r = readPos_.load(std::memory_order_acquire);
    writePos_.store(r, std::memory_order_release);
}

/* ── Drain-тред ── */

void AudioHw::drainEntry_(void* arg) {
    static_cast<AudioHw*>(arg)->drain_();
}

void AudioHw::drain_() {
    for (;;) {
        if (!started_) {
            vTaskDelay(10);
            continue;
        }
        /* Эмулируем потребление ~1мс данных */
        uint32_t samplesToConsume = sampleRate_ / 1000;
        if (samplesToConsume < 1) samplesToConsume = 1;

        uint32_t w = writePos_.load(std::memory_order_acquire);
        uint32_t r = readPos_.load(std::memory_order_relaxed);
        uint32_t avail = (w >= r) ? (w - r) : (RingSize - r + w);

        uint32_t consume = std::min(samplesToConsume, avail);
        if (consume > 0) {
            readPos_.store((r + consume) % RingSize, std::memory_order_release);
        }
        vTaskDelay(1);
    }
}

} // namespace ae2

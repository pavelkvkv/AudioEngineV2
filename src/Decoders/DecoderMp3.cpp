/// @file DecoderMp3.cpp
#include "DecoderMp3.hpp"
#include "FsAdapter/FsAdapter.hpp"
#include "Mp3Duration/Mp3Duration.hpp"
#include <cstring>
#include <algorithm>

namespace ae2 {

bool DecoderMp3::open(FsAdapter& fs) {
    close();
    fs_ = &fs;
    mp3dec_init(&mp3d_);
    inBufLen_ = inBufPos_ = 0;
    totalSamplesDecoded_ = 0;

    /* Оценка длительности без полного прохода */
    auto dur = Mp3Duration::estimate(fs, fs.size());
    duration_   = dur.durationSec;
    sampleRate_ = (dur.sampleRate > 0) ? dur.sampleRate : 44100;
    channels_   = (dur.channels > 0) ? dur.channels : 2;

    /* Seek к началу аудиоданных (после ID3v2) */
    fs.seek(0);
    uint8_t id3[10];
    if (fs.read(id3, 10) >= 10 && id3[0]=='I' && id3[1]=='D' && id3[2]=='3') {
        uint32_t sz = ((uint32_t)(id3[6]&0x7F)<<21) | ((uint32_t)(id3[7]&0x7F)<<14) |
                      ((uint32_t)(id3[8]&0x7F)<<7)  | (uint32_t)(id3[9]&0x7F);
        fs.seek(sz + 10);
    } else {
        fs.seek(0);
    }

    status_ = Status::Ready;
    return true;
}

bool DecoderMp3::refillInput_() {
    if (!fs_) return false;
    /* Сдвигаем остаток в начало буфера */
    uint32_t remaining = inBufLen_ - inBufPos_;
    if (remaining > 0 && inBufPos_ > 0)
        std::memmove(inBuf_, inBuf_ + inBufPos_, remaining);
    inBufLen_ = remaining;
    inBufPos_ = 0;
    /* Дочитываем */
    size_t space = kInBufSize - inBufLen_;
    if (space > 0) {
        size_t rd = fs_->read(inBuf_ + inBufLen_, space);
        inBufLen_ += (uint32_t)rd;
    }
    return inBufLen_ > 0;
}

uint32_t DecoderMp3::decode(s16* buf, uint32_t maxSamples) {
    if (!fs_ || (status_ != Status::Ready && status_ != Status::Playing)) return 0;
    status_ = Status::Playing;

    uint32_t totalOut = 0;
    s16 pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];

    while (totalOut < maxSamples) {
        if (inBufLen_ - inBufPos_ < 1024) {
            if (!refillInput_()) break;
            if (inBufLen_ == 0) break;
        }

        mp3dec_frame_info_t info;
        int samples = mp3dec_decode_frame(&mp3d_,
            inBuf_ + inBufPos_, (int)(inBufLen_ - inBufPos_),
            pcm, &info);

        if (info.frame_bytes == 0) break; /* no more data */
        inBufPos_ += (uint32_t)info.frame_bytes;

        if (samples <= 0) continue;

        /* Update sample rate from actual frame */
        if (info.hz > 0) sampleRate_ = (uint32_t)info.hz;
        channels_ = (uint32_t)info.channels;

        /* Convert to mono */
        uint32_t monoSamples = (uint32_t)samples;
        uint32_t space = maxSamples - totalOut;
        if (monoSamples > space) monoSamples = space;

        if (info.channels == 2) {
            for (uint32_t i = 0; i < monoSamples; ++i)
                buf[totalOut + i] = (s16)(((int32_t)pcm[i*2] + pcm[i*2+1]) / 2);
        } else {
            std::memcpy(buf + totalOut, pcm, monoSamples * sizeof(s16));
        }
        totalOut += monoSamples;
        totalSamplesDecoded_ += monoSamples;
    }

    if (totalOut == 0) status_ = Status::Closed;
    return totalOut;
}

void DecoderMp3::seek(uint32_t sec) {
    if (!fs_) return;
    /* Грубый seek: оценка по среднему битрейту */
    uint32_t fileSize = fs_->size();
    if (duration_ > 0) {
        uint32_t bytePos = (uint32_t)((uint64_t)fileSize * sec / duration_);
        fs_->seek(bytePos);
    } else {
        fs_->seek(0);
    }
    inBufLen_ = inBufPos_ = 0;
    mp3dec_init(&mp3d_);
    totalSamplesDecoded_ = (uint64_t)sec * sampleRate_;
}

uint32_t DecoderMp3::position() const {
    return (sampleRate_ > 0) ? (uint32_t)(totalSamplesDecoded_ / sampleRate_) : 0;
}

uint32_t DecoderMp3::duration() const { return duration_; }

void DecoderMp3::close() {
    fs_ = nullptr;
    status_ = Status::Closed;
    inBufLen_ = inBufPos_ = 0;
    totalSamplesDecoded_ = 0;
}

} // namespace ae2

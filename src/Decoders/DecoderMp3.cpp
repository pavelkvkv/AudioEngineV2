/// @file DecoderMp3.cpp
/// @brief MP3-декодер на базе Helix fixed-point decoder.
///
/// Helix оптимизирован для ARM Cortex-M: fixed-point, ~6KB RAM, быстрые
/// 32-bit multiplies через smull. Значительно быстрее minimp3 на Cortex-M4.
#include "DecoderMp3.hpp"
#include "FsAdapter/FsAdapter.hpp"
#include "Mp3Duration/Mp3Duration.hpp"
#include <cstring>
#include <algorithm>

namespace ae2 {

bool DecoderMp3::open(FsAdapter& fs) {
    close();
    fs_ = &fs;

    hDec_ = MP3InitDecoder();
    if (!hDec_) return false;

    inBufLen_ = inBufPos_ = 0;
    totalSamplesDecoded_ = 0;

    /* Оценка длительности без полного прохода */
    auto dur = Mp3Duration::estimate(fs, fs.size());
    duration_   = dur.durationSec;
    sampleRate_ = (dur.sampleRate > 0) ? dur.sampleRate : 44100;
    channels_   = (dur.channels > 0) ? dur.channels : 2;

    /* Пропуск ID3v2 тега */
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

int DecoderMp3::findSyncAndDecode_(s16* pcm, MP3FrameInfo& info) {
    /* Ищем sync word в доступных данных */
    for (;;) {
        int avail = (int)(inBufLen_ - inBufPos_);
        if (avail < 4) return -1;  /* нужно больше данных */

        int offset = MP3FindSyncWord(inBuf_ + inBufPos_, avail);
        if (offset < 0) {
            /* sync не найден — весь буфер мусор, сдвигаем */
            inBufPos_ = inBufLen_;
            return -1;
        }
        inBufPos_ += (uint32_t)offset;

        /* Декодируем фрейм */
        unsigned char* ptr = inBuf_ + inBufPos_;
        int bytesLeft = (int)(inBufLen_ - inBufPos_);
        int err = MP3Decode(hDec_, &ptr, &bytesLeft, pcm, 0);

        /* Обновляем позицию (MP3Decode двигает ptr) */
        inBufPos_ = inBufLen_ - (uint32_t)bytesLeft;

        if (err == ERR_MP3_NONE) {
            MP3GetLastFrameInfo(hDec_, &info);
            return info.outputSamps;  /* total samples (channels * samplesPerCh) */
        }
        if (err == ERR_MP3_INDATA_UNDERFLOW || err == ERR_MP3_MAINDATA_UNDERFLOW) {
            return -1;  /* нужно больше данных */
        }
        /* Другие ошибки — пропускаем байт и пробуем дальше */
        if (inBufPos_ < inBufLen_) inBufPos_++;
    }
}

uint32_t DecoderMp3::decode(s16* buf, uint32_t maxSamples) {
    if (!hDec_ || !fs_ || (status_ != Status::Ready && status_ != Status::Playing)) return 0;
    status_ = Status::Playing;

    uint32_t totalOut = 0;

    /* ── Сначала отдаём остаток от предыдущего фрейма ── */
    if (leftoverLen_ > leftoverPos_) {
        uint32_t avail = leftoverLen_ - leftoverPos_;
        uint32_t n = std::min(avail, maxSamples);
        std::memcpy(buf, leftover_ + leftoverPos_, n * sizeof(s16));
        leftoverPos_ += n;
        totalOut += n;
        totalSamplesDecoded_ += n;
        if (leftoverPos_ >= leftoverLen_)
            leftoverLen_ = leftoverPos_ = 0;
    }

    /* Helix декодирует до 1152 stereo сэмплов = 2304 s16 значений на фрейм */
    s16 pcm[MAX_NSAMP * MAX_NCHAN * MAX_NGRAN];  /* 576*2*2 = 2304 */

    while (totalOut < maxSamples) {
        if (inBufLen_ - inBufPos_ < MAINBUF_SIZE) {
            if (!refillInput_()) break;
            if (inBufLen_ == 0) break;
        }

        MP3FrameInfo info{};
        int totalSamps = findSyncAndDecode_(pcm, info);
        if (totalSamps <= 0) {
            /* Попробуем дочитать */
            if (!refillInput_() || inBufLen_ == 0) break;
            totalSamps = findSyncAndDecode_(pcm, info);
            if (totalSamps <= 0) break;
        }

        /* Обновляем параметры из фактического фрейма */
        if (info.samprate > 0) sampleRate_ = (uint32_t)info.samprate;
        channels_ = (uint32_t)info.nChans;

        uint32_t monoSamples = (uint32_t)(totalSamps / info.nChans);
        uint32_t space = maxSamples - totalOut;

        if (monoSamples > space) {
            /* Фрейм не помещается целиком — даунмикс в leftover_, вывод сколько есть места */
            if (info.nChans == 2) {
                for (uint32_t i = 0; i < monoSamples; ++i)
                    leftover_[i] = (s16)(((int32_t)pcm[i*2] + pcm[i*2+1]) / 2);
            } else {
                std::memcpy(leftover_, pcm, monoSamples * sizeof(s16));
            }
            std::memcpy(buf + totalOut, leftover_, space * sizeof(s16));
            totalOut += space;
            totalSamplesDecoded_ += space;
            leftoverPos_ = space;
            leftoverLen_ = monoSamples;
            break;
        }

        /* Весь фрейм помещается */
        if (info.nChans == 2) {
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
    /* Грубый seek по среднему битрейту */
    uint32_t fileSize = fs_->size();
    if (duration_ > 0) {
        uint32_t bytePos = (uint32_t)((uint64_t)fileSize * sec / duration_);
        fs_->seek(bytePos);
    } else {
        fs_->seek(0);
    }
    inBufLen_ = inBufPos_ = 0;
    leftoverLen_ = leftoverPos_ = 0;
    /* Helix не имеет mp3dec_init; пересоздаём декодер для сброса состояния */
    if (hDec_) { MP3FreeDecoder(hDec_); }
    hDec_ = MP3InitDecoder();
    totalSamplesDecoded_ = (uint64_t)sec * sampleRate_;
}

uint32_t DecoderMp3::position() const {
    return (sampleRate_ > 0) ? (uint32_t)(totalSamplesDecoded_ / sampleRate_) : 0;
}

uint32_t DecoderMp3::duration() const { return duration_; }

void DecoderMp3::close() {
    if (hDec_) {
        MP3FreeDecoder(hDec_);
        hDec_ = nullptr;
    }
    fs_ = nullptr;
    status_ = Status::Closed;
    inBufLen_ = inBufPos_ = 0;
    leftoverLen_ = leftoverPos_ = 0;
    totalSamplesDecoded_ = 0;
}

} // namespace ae2

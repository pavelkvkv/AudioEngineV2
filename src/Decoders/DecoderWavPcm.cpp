/// @file DecoderWavPcm.cpp
#include "DecoderWavPcm.hpp"
#include "FsAdapter/FsAdapter.hpp"
#include <cstring>
#include <algorithm>

namespace ae2 {

static uint16_t r16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t r32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}

bool DecoderWavPcm::open(FsAdapter& fs) {
    close();
    fs_ = &fs;
    fs.seek(0);

    uint8_t hdr[12];
    if (fs.read(hdr, 12) < 12) return false;
    if (std::memcmp(hdr, "RIFF", 4) != 0 || std::memcmp(hdr + 8, "WAVE", 4) != 0) return false;

    bool fmtFound = false;
    bool dataFound = false;
    uint32_t pos = 12;

    while (pos + 8 < fs.size()) {
        fs.seek(pos);
        uint8_t ch[8];
        if (fs.read(ch, 8) < 8) break;
        uint32_t chSize = r32(ch + 4);

        if (std::memcmp(ch, "fmt ", 4) == 0 && chSize >= 16) {
            uint8_t fmt[16];
            if (fs.read(fmt, 16) < 16) break;
            uint16_t audioFmt = r16(fmt);
            channels_ = r16(fmt + 2);
            sampleRate_ = r32(fmt + 4);
            bitsPerSample_ = r16(fmt + 14);
            if (audioFmt != 1) return false;  /* only PCM */
            if (channels_ == 0 || bitsPerSample_ == 0) return false;
            fmtFound = true;
        } else if (std::memcmp(ch, "data", 4) == 0) {
            dataOffset_ = pos + 8;
            dataSize_ = chSize;
            dataFound = true;
        }

        pos += 8 + chSize;
        if (chSize & 1) pos++;
        if (fmtFound && dataFound) break;
    }

    if (!fmtFound || !dataFound) return false;
    bytesRead_ = 0;
    fs.seek(dataOffset_);
    status_ = Status::Ready;
    return true;
}

uint32_t DecoderWavPcm::decode(s16* buf, uint32_t maxSamples) {
    if (status_ != Status::Ready && status_ != Status::Playing) return 0;
    status_ = Status::Playing;
    if (!fs_) return 0;

    uint32_t bpf = bytesPerFrame_();
    if (bpf == 0) return 0;
    uint32_t framesToRead = maxSamples;
    uint32_t bytesLeft = (dataSize_ > bytesRead_) ? (dataSize_ - bytesRead_) : 0;
    uint32_t framesLeft = bytesLeft / bpf;
    if (framesToRead > framesLeft) framesToRead = framesLeft;
    if (framesToRead == 0) { status_ = Status::Closed; return 0; }

    /* Читаем сырые данные */
    uint32_t rawBytes = framesToRead * bpf;
    /* Используем стек для небольших чанков, иначе временный буфер */
    uint8_t tmpStack[4096];
    uint8_t* raw = tmpStack;
    bool allocated = false;
    if (rawBytes > sizeof(tmpStack)) {
        raw = new uint8_t[rawBytes];
        allocated = true;
    }

    size_t read = fs_->read(raw, rawBytes);
    if (read == 0) {
        if (allocated) delete[] raw;
        status_ = Status::Closed;
        return 0;
    }
    uint32_t actualFrames = (uint32_t)(read / bpf);
    bytesRead_ += (uint32_t)(actualFrames * bpf);

    /* Конвертация в s16 mono */
    for (uint32_t i = 0; i < actualFrames; ++i) {
        const uint8_t* frame = raw + i * bpf;
        int32_t monoSum = 0;
        for (uint16_t ch = 0; ch < channels_; ++ch) {
            const uint8_t* sample = frame + ch * (bitsPerSample_ / 8);
            int32_t val = 0;
            switch (bitsPerSample_) {
                case 8:
                    val = ((int32_t)sample[0] - 128) << 8;
                    break;
                case 16:
                    val = (int16_t)(sample[0] | (sample[1] << 8));
                    break;
                case 24:
                    val = (int32_t)((uint32_t)sample[0] | ((uint32_t)sample[1] << 8) |
                          ((uint32_t)sample[2] << 16));
                    if (val & 0x800000) val |= (int32_t)0xFF000000u;
                    val >>= 8;
                    break;
                case 32:
                    val = (int32_t)(sample[0] | ((uint32_t)sample[1]<<8) |
                          ((uint32_t)sample[2]<<16) | ((uint32_t)sample[3]<<24));
                    val >>= 16;
                    break;
                default:
                    break;
            }
            monoSum += val;
        }
        buf[i] = (s16)(monoSum / channels_);
    }

    if (allocated) delete[] raw;
    return actualFrames;
}

void DecoderWavPcm::seek(uint32_t sec) {
    if (!fs_) return;
    uint32_t bpf = bytesPerFrame_();
    uint32_t bytePos = sec * sampleRate_ * bpf;
    if (bytePos > dataSize_) bytePos = dataSize_;
    bytesRead_ = bytePos;
    fs_->seek(dataOffset_ + bytePos);
    if (status_ == Status::Closed) status_ = Status::Ready;
}

uint32_t DecoderWavPcm::position() const {
    if (sampleRate_ == 0) return 0;
    uint32_t bpf = channels_ * (bitsPerSample_ / 8);
    if (bpf == 0) return 0;
    return bytesRead_ / bpf / sampleRate_;
}

uint32_t DecoderWavPcm::duration() const {
    if (sampleRate_ == 0) return 0;
    uint32_t bpf = channels_ * (bitsPerSample_ / 8);
    if (bpf == 0) return 0;
    return dataSize_ / bpf / sampleRate_;
}

void DecoderWavPcm::close() {
    fs_ = nullptr;
    status_ = Status::Closed;
    bytesRead_ = 0;
}

} // namespace ae2

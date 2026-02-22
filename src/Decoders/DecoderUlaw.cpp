/// @file DecoderUlaw.cpp
#include "DecoderUlaw.hpp"
#include "FsAdapter/FsAdapter.hpp"
#include <cstring>

namespace ae2 {

static uint16_t r16(const uint8_t* p) { return (uint16_t)(p[0]|(p[1]<<8)); }
static uint32_t r32(const uint8_t* p) {
    return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}

bool DecoderUlaw::open(FsAdapter& fs) {
    close();
    fs_ = &fs;
    fs.seek(0);
    uint8_t hdr[12];
    if (fs.read(hdr,12)<12) return false;
    if (std::memcmp(hdr,"RIFF",4)||std::memcmp(hdr+8,"WAVE",4)) return false;

    uint32_t pos = 12;
    bool gotFmt = false, gotData = false;
    while (pos+8 < fs.size()) {
        fs.seek(pos);
        uint8_t ch[8]; if (fs.read(ch,8)<8) break;
        uint32_t sz = r32(ch+4);
        if (!std::memcmp(ch,"fmt ",4) && sz>=16) {
            uint8_t f[16]; if (fs.read(f,16)<16) break;
            if (r16(f) != 7) return false; /* must be μ-law */
            channels_   = r16(f+2);
            sampleRate_ = r32(f+4);
            gotFmt = true;
        } else if (!std::memcmp(ch,"data",4)) {
            dataOffset_ = pos+8; dataSize_ = sz; gotData = true;
        }
        pos += 8+sz; if (sz&1) pos++;
        if (gotFmt && gotData) break;
    }
    if (!gotFmt || !gotData || channels_ == 0) return false;
    bytesRead_ = 0;
    fs.seek(dataOffset_);
    status_ = Status::Ready;
    return true;
}

s16 DecoderUlaw::decodeSample(uint8_t ulaw) {
    ulaw = ~ulaw;
    int sign = (ulaw & 0x80) ? -1 : 1;
    int exp  = (ulaw >> 4) & 7;
    int mant = ulaw & 0x0F;
    int val  = ((mant << 3) + 0x84) << exp;
    val -= 0x84;
    return (s16)(sign * val);
}

uint32_t DecoderUlaw::decode(s16* buf, uint32_t maxSamples) {
    if (!fs_ || (status_ != Status::Ready && status_ != Status::Playing)) return 0;
    status_ = Status::Playing;
    uint32_t bytesLeft = (dataSize_ > bytesRead_) ? (dataSize_ - bytesRead_) : 0;
    uint32_t framesToRead = bytesLeft / channels_;
    if (framesToRead > maxSamples) framesToRead = maxSamples;
    if (framesToRead == 0) { status_ = Status::Closed; return 0; }

    uint32_t rawBytes = framesToRead * channels_;
    uint8_t tmp[2048];
    uint8_t* raw = tmp;
    bool alloc = false;
    if (rawBytes > sizeof(tmp)) { raw = new uint8_t[rawBytes]; alloc = true; }
    size_t rd = fs_->read(raw, rawBytes);
    framesToRead = (uint32_t)(rd / channels_);
    bytesRead_ += framesToRead * channels_;

    for (uint32_t i = 0; i < framesToRead; ++i) {
        int32_t sum = 0;
        for (uint16_t c = 0; c < channels_; ++c)
            sum += decodeSample(raw[i * channels_ + c]);
        buf[i] = (s16)(sum / channels_);
    }
    if (alloc) delete[] raw;
    return framesToRead;
}

void DecoderUlaw::seek(uint32_t sec) {
    if (!fs_) return;
    uint32_t bytePos = sec * sampleRate_ * channels_;
    if (bytePos > dataSize_) bytePos = dataSize_;
    bytesRead_ = bytePos;
    fs_->seek(dataOffset_ + bytePos);
}

uint32_t DecoderUlaw::position() const {
    return (sampleRate_ && channels_) ? bytesRead_ / channels_ / sampleRate_ : 0;
}

uint32_t DecoderUlaw::duration() const {
    return (sampleRate_ && channels_) ? dataSize_ / channels_ / sampleRate_ : 0;
}

void DecoderUlaw::close() { fs_ = nullptr; status_ = Status::Closed; bytesRead_ = 0; }

} // namespace ae2

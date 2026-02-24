/// @file DecoderAdpcm.cpp — IMA ADPCM
#include "DecoderAdpcm.hpp"
#include "FsAdapter/FsAdapter.hpp"
#include <cstring>
#include <algorithm>

namespace ae2 {

/* IMA step table */
static const int16_t kStepTable[89] = {
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,
    50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,
    230,253,279,307,337,371,408,449,494,544,598,658,724,796,
    876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,
    2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,
    7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,
    20350,22385,24623,27086,29794,32767
};

static const int8_t kIndexTable[16] = {
    -1,-1,-1,-1, 2,4,6,8,
    -1,-1,-1,-1, 2,4,6,8
};

static uint16_t r16(const uint8_t* p) { return (uint16_t)(p[0]|(p[1]<<8)); }
static uint32_t r32(const uint8_t* p) {
    return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}

bool DecoderAdpcm::open(FsAdapter& fs) {
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
        if (!std::memcmp(ch,"fmt ",4) && sz >= 20) {
            uint8_t f[20]; if (fs.read(f, std::min(sz,(uint32_t)20))<20) break;
            if (r16(f) != 0x11) return false; /* IMA ADPCM */
            channels_     = r16(f+2);
            sampleRate_   = r32(f+4);
            blockAlign_   = r16(f+12);
            if (sz >= 20) samplesPerBlock_ = r16(f+18);
            if (samplesPerBlock_ == 0 && blockAlign_ > 0 && channels_ > 0)
                samplesPerBlock_ = (blockAlign_ - 4 * channels_) * 2 / channels_ + 1;
            gotFmt = true;
        } else if (!std::memcmp(ch,"data",4)) {
            dataOffset_ = pos+8; dataSize_ = sz; gotData = true;
        }
        pos += 8+sz; if (sz&1) pos++;
        if (gotFmt && gotData) break;
    }
    if (!gotFmt || !gotData || channels_ == 0 || blockAlign_ == 0) return false;
    totalBlocks_ = dataSize_ / blockAlign_;
    blocksRead_ = 0;
    fs.seek(dataOffset_);
    status_ = Status::Ready;
    return true;
}

s16 DecoderAdpcm::decodeNibble(uint8_t nibble, AdpcmState& state) {
    int step = kStepTable[state.stepIndex];
    int diff = step >> 3;
    if (nibble & 1) diff += step >> 2;
    if (nibble & 2) diff += step >> 1;
    if (nibble & 4) diff += step;
    if (nibble & 8) diff = -diff;

    int pred = state.predictor + diff;
    if (pred > 32767) pred = 32767;
    if (pred < -32768) pred = -32768;
    state.predictor = (s16)pred;

    int idx = state.stepIndex + kIndexTable[nibble & 0x0F];
    if (idx < 0) idx = 0;
    if (idx > 88) idx = 88;
    state.stepIndex = (uint8_t)idx;

    return state.predictor;
}

uint32_t DecoderAdpcm::decode(s16* buf, uint32_t maxSamples) {
    if (!fs_ || (status_ != Status::Ready && status_ != Status::Playing)) return 0;
    status_ = Status::Playing;

    uint32_t totalOut = 0;

    /* ── Cначала отдаём остаток от предыдущего блока ── */
    if (blockDecPos_ < blockDecLen_) {
        uint32_t avail = blockDecLen_ - blockDecPos_;
        uint32_t n = std::min(avail, maxSamples);
        std::memcpy(buf, blockDecBuf_ + blockDecPos_, n * sizeof(s16));
        blockDecPos_ += n;
        totalOut += n;
        if (blockDecPos_ >= blockDecLen_)
            blockDecLen_ = blockDecPos_ = 0;
    }

    /* ── Декодируем следующие блоки, пока не заполним буфер ── */
    while (totalOut < maxSamples) {
        if (blocksRead_ >= totalBlocks_) {
            if (totalOut == 0) status_ = Status::Closed;
            break;
        }

        uint32_t blockSamples = decodeOneBlock_();
        if (blockSamples == 0) {
            if (totalOut == 0) status_ = Status::Closed;
            break;
        }

        uint32_t space = maxSamples - totalOut;
        uint32_t n = std::min(blockSamples, space);
        std::memcpy(buf + totalOut, blockDecBuf_, n * sizeof(s16));
        totalOut += n;

        if (n < blockSamples) {
            /* Остаток сохраняем для следующего вызова */
            blockDecPos_ = n;
            blockDecLen_ = blockSamples;
            break;
        }
    }

    return totalOut;
}

uint32_t DecoderAdpcm::decodeOneBlock_() {
    /* Читаем один блок из файла */
    uint8_t blockBuf[4096];
    uint8_t* block = blockBuf;
    bool alloc = false;
    if (blockAlign_ > sizeof(blockBuf)) { block = new uint8_t[blockAlign_]; alloc = true; }

    size_t rd = fs_->read(block, blockAlign_);
    if (rd < blockAlign_) {
        if (alloc) delete[] block;
        return 0;
    }
    blocksRead_++;

    uint32_t outSamples = 0;
    AdpcmState states[2];

    /* Block header: для каждого канала 4 байта */
    for (uint16_t c = 0; c < channels_ && c < 2; ++c) {
        uint32_t off = c * 4;
        states[c].predictor = (s16)(block[off] | (block[off+1] << 8));
        states[c].stepIndex = block[off+2];
        if (states[c].stepIndex > 88) states[c].stepIndex = 88;
    }

    /* Первый сэмпл — predictor из заголовка */
    if (channels_ == 1) {
        blockDecBuf_[outSamples++] = states[0].predictor;
    } else {
        blockDecBuf_[outSamples++] = (s16)(((int32_t)states[0].predictor + states[1].predictor) / 2);
    }

    /* Данные nibbles */
    uint32_t dataStart = 4 * channels_;
    uint32_t dataBytes = blockAlign_ - dataStart;

    if (channels_ == 1) {
        for (uint32_t i = 0; i < dataBytes && outSamples < kMaxBlockSamples; ++i) {
            uint8_t byte = block[dataStart + i];
            blockDecBuf_[outSamples++] = decodeNibble(byte & 0x0F, states[0]);
            if (outSamples < kMaxBlockSamples)
                blockDecBuf_[outSamples++] = decodeNibble((byte >> 4) & 0x0F, states[0]);
        }
    } else {
        /* Stereo: interleaved 4-byte chunks per channel */
        uint32_t pos_ = dataStart;
        while (pos_ + 8 <= blockAlign_ && outSamples < kMaxBlockSamples) {
            s16 ch0[8], ch1[8];
            int n0 = 0, n1 = 0;
            for (int b = 0; b < 4 && pos_ < blockAlign_; ++b, ++pos_) {
                uint8_t byte = block[pos_];
                ch0[n0++] = decodeNibble(byte & 0x0F, states[0]);
                ch0[n0++] = decodeNibble((byte >> 4) & 0x0F, states[0]);
            }
            for (int b = 0; b < 4 && pos_ < blockAlign_; ++b, ++pos_) {
                uint8_t byte = block[pos_];
                ch1[n1++] = decodeNibble(byte & 0x0F, states[1]);
                ch1[n1++] = decodeNibble((byte >> 4) & 0x0F, states[1]);
            }
            int pairs = std::min(n0, n1);
            for (int j = 0; j < pairs && outSamples < kMaxBlockSamples; ++j)
                blockDecBuf_[outSamples++] = (s16)(((int32_t)ch0[j] + ch1[j]) / 2);
        }
    }

    if (alloc) delete[] block;
    return outSamples;
}

void DecoderAdpcm::seek(uint32_t sec) {
    if (!fs_ || blockAlign_ == 0 || samplesPerBlock_ == 0) return;
    uint32_t targetSample = sec * sampleRate_;
    uint32_t targetBlock = targetSample / samplesPerBlock_;
    if (targetBlock >= totalBlocks_) targetBlock = totalBlocks_ > 0 ? totalBlocks_ - 1 : 0;
    blocksRead_ = targetBlock;
    blockDecLen_ = blockDecPos_ = 0;  /* сброс буфера остатка */
    fs_->seek(dataOffset_ + targetBlock * blockAlign_);
}

uint32_t DecoderAdpcm::position() const {
    if (sampleRate_ == 0 || samplesPerBlock_ == 0) return 0;
    return (blocksRead_ * samplesPerBlock_) / sampleRate_;
}

uint32_t DecoderAdpcm::duration() const {
    if (sampleRate_ == 0 || samplesPerBlock_ == 0) return 0;
    return (totalBlocks_ * samplesPerBlock_) / sampleRate_;
}

void DecoderAdpcm::close() {
    fs_ = nullptr; status_ = Status::Closed; blocksRead_ = 0;
    blockDecLen_ = blockDecPos_ = 0;
}

} // namespace ae2

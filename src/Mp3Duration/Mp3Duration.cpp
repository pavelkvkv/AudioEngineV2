/// @file Mp3Duration.cpp
#include "Mp3Duration.hpp"
#include "FsAdapter/FsAdapter.hpp"
#include <cstring>

namespace ae2 {
namespace Mp3Duration {

/* ── Таблицы MPEG ── */

/* bitrate_kbps[version_idx][layer_idx][bitrate_index] */
/* version_idx: 0=MPEG1, 1=MPEG2/2.5 */
/* layer_idx:   0=Layer1, 1=Layer2, 2=Layer3 */
static const uint16_t kBitrate[2][3][16] = {
    { /* MPEG1 */
        {0,32,64,96,128,160,192,224,256,288,320,352,384,416,448,0},
        {0,32,48,56,64,80,96,112,128,160,192,224,256,320,384,0},
        {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0}
    },
    { /* MPEG2 / MPEG2.5 */
        {0,32,48,56,64,80,96,112,128,144,160,176,192,224,256,0},
        {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0},
        {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0}
    }
};

static const uint32_t kSampleRate[4][4] = {
    {11025,12000,8000,0},   /* MPEG2.5 */
    {0,0,0,0},              /* reserved */
    {22050,24000,16000,0},  /* MPEG2 */
    {44100,48000,32000,0}   /* MPEG1 */
};

static const uint16_t kSamplesPerFrame[4][3] = {
    /* MPEG2.5 */ {384, 1152, 576},
    /* reserved */ {0,0,0},
    /* MPEG2   */ {384, 1152, 576},
    /* MPEG1   */ {384, 1152, 1152}
};

struct FrameInfo {
    uint32_t bitrate;    /* bps */
    uint32_t sampleRate;
    uint16_t samplesPerFrame;
    uint16_t frameSize;
    uint8_t  channels;
    bool     valid;
};

static FrameInfo parseFrame(const uint8_t* h) {
    FrameInfo fi{};
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return fi;

    uint8_t versionBits = (h[1] >> 3) & 3;
    uint8_t layerBits   = (h[1] >> 1) & 3;
    uint8_t brIdx       = (h[2] >> 4) & 0xF;
    uint8_t srIdx       = (h[2] >> 2) & 3;
    uint8_t padding     = (h[2] >> 1) & 1;
    uint8_t mode        = (h[3] >> 6) & 3;

    if (versionBits == 1 || layerBits == 0 || brIdx == 0 || brIdx == 15 || srIdx == 3)
        return fi;

    uint8_t vIdx = (versionBits == 3) ? 0 : 1;  /* 0=MPEG1, 1=MPEG2/2.5 */
    uint8_t lIdx = 3 - layerBits;                /* 0=L1, 1=L2, 2=L3 */

    fi.bitrate    = (uint32_t)kBitrate[vIdx][lIdx][brIdx] * 1000;
    fi.sampleRate = kSampleRate[versionBits][srIdx];
    fi.samplesPerFrame = kSamplesPerFrame[versionBits][lIdx];
    fi.channels   = (mode == 3) ? 1 : 2;
    fi.valid      = (fi.bitrate > 0 && fi.sampleRate > 0 && fi.samplesPerFrame > 0);

    if (fi.valid) {
        if (lIdx == 0) /* Layer 1 */
            fi.frameSize = (uint16_t)((12 * fi.bitrate / fi.sampleRate + padding) * 4);
        else
            fi.frameSize = (uint16_t)(fi.samplesPerFrame / 8 * fi.bitrate / fi.sampleRate + padding);
    }
    return fi;
}

static uint32_t skipId3v2(FsAdapter& fs) {
    fs.seek(0);
    uint8_t hdr[10];
    if (fs.read(hdr, 10) < 10) return 0;
    if (hdr[0] == 'I' && hdr[1] == 'D' && hdr[2] == '3') {
        uint32_t sz = ((uint32_t)(hdr[6] & 0x7F) << 21) |
                      ((uint32_t)(hdr[7] & 0x7F) << 14) |
                      ((uint32_t)(hdr[8] & 0x7F) << 7) |
                      (uint32_t)(hdr[9] & 0x7F);
        return sz + 10;
    }
    return 0;
}

Result estimate(FsAdapter& fs, uint32_t fileSize) {
    Result res{};

    uint32_t dataStart = skipId3v2(fs);
    fs.seek(dataStart);

    /* Найти первый валидный фрейм */
    uint8_t buf[4];
    uint32_t scanLimit = 8192;
    uint32_t pos = dataStart;
    FrameInfo first{};

    while (pos < dataStart + scanLimit) {
        fs.seek(pos);
        if (fs.read(buf, 4) < 4) return res;
        first = parseFrame(buf);
        if (first.valid) break;
        pos++;
    }
    if (!first.valid) return res;

    res.sampleRate = first.sampleRate;
    res.channels   = first.channels;
    uint32_t firstFramePos = pos;

    /* Проверяем Xing/VBRI/Info заголовок */
    uint8_t xbuf[256];
    fs.seek(firstFramePos);
    size_t xn = fs.read(xbuf, std::min((size_t)first.frameSize, sizeof(xbuf)));

    /* Xing/Info offset: side info size зависит от версии и каналов */
    uint32_t sideOffset = 4; /* frame header */
    /* MPEG1: mono=17, stereo=32. MPEG2: mono=9, stereo=17 */
    bool isMpeg1 = ((xbuf[1] >> 3) & 3) == 3;
    sideOffset += (isMpeg1) ? ((first.channels == 1) ? 17 : 32)
                            : ((first.channels == 1) ? 9 : 17);

    if (sideOffset + 12 < xn) {
        bool isXing = (std::memcmp(xbuf + sideOffset, "Xing", 4) == 0 ||
                       std::memcmp(xbuf + sideOffset, "Info", 4) == 0);
        if (isXing) {
            uint32_t flags = ((uint32_t)xbuf[sideOffset+4] << 24) |
                             ((uint32_t)xbuf[sideOffset+5] << 16) |
                             ((uint32_t)xbuf[sideOffset+6] << 8) |
                             xbuf[sideOffset+7];
            if (flags & 1) { /* frames field present */
                uint32_t totalFrames = ((uint32_t)xbuf[sideOffset+8] << 24) |
                                       ((uint32_t)xbuf[sideOffset+9] << 16) |
                                       ((uint32_t)xbuf[sideOffset+10] << 8) |
                                       xbuf[sideOffset+11];
                if (first.sampleRate > 0 && first.samplesPerFrame > 0) {
                    res.durationSec = (uint32_t)((uint64_t)totalFrames * first.samplesPerFrame / first.sampleRate);
                    res.isExact = true;
                    return res;
                }
            }
        }
    }

    /* Нет Xing — сканируем фреймы до стабилизации среднего битрейта */
    uint64_t totalBitrate = 0;
    uint32_t frameCount   = 0;
    uint32_t prevAvg      = 0;
    uint32_t convergenceCount = 0;
    static constexpr uint32_t kMaxFrames = 200;

    pos = firstFramePos;
    while (frameCount < kMaxFrames && pos + 4 < fileSize) {
        fs.seek(pos);
        if (fs.read(buf, 4) < 4) break;
        FrameInfo fi = parseFrame(buf);
        if (!fi.valid) { pos++; continue; }

        totalBitrate += fi.bitrate;
        frameCount++;
        pos += fi.frameSize;

        /* Проверка сходимости каждые 5 фреймов */
        if (frameCount >= 5 && (frameCount % 5) == 0) {
            uint32_t avg = (uint32_t)(totalBitrate / frameCount);
            if (prevAvg > 0) {
                int32_t delta = (int32_t)avg - (int32_t)prevAvg;
                if (delta < 0) delta = -delta;
                /* < 1% изменения */
                if ((uint32_t)delta * 100 < prevAvg) {
                    convergenceCount++;
                    if (convergenceCount >= 2) break;
                } else {
                    convergenceCount = 0;
                }
            }
            prevAvg = avg;
        }
    }

    if (frameCount > 0 && totalBitrate > 0) {
        uint32_t avgBitrate = (uint32_t)(totalBitrate / frameCount);
        uint32_t dataSize = fileSize - dataStart;
        res.durationSec = (uint32_t)((uint64_t)dataSize * 8 / avgBitrate);
    }
    return res;
}

} // namespace Mp3Duration
} // namespace ae2

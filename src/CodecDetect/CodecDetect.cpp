/// @file CodecDetect.cpp
#include "CodecDetect.hpp"
#include "FsAdapter/FsAdapter.hpp"
#include <cstring>

namespace ae2 {
namespace CodecDetect {

static uint16_t readU16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

Type detect(FsAdapter& fs) {
    fs.seek(0);
    uint8_t hdr[512];
    size_t n = fs.read(hdr, sizeof(hdr));
    fs.seek(0);
    if (n < 12) {
        /* fallback по расширению */
        auto ext = fs.extension();
        if (ext == "mp3") return Type::Mp3;
        if (ext == "wav") return Type::WavPcm;
        return Type::Unknown;
    }

    /* WAV: RIFF....WAVE */
    if (std::memcmp(hdr, "RIFF", 4) == 0 && std::memcmp(hdr + 8, "WAVE", 4) == 0) {
        /* Ищем fmt chunk */
        size_t pos = 12;
        while (pos + 8 <= n) {
            uint32_t chunkSize = (uint32_t)(hdr[pos+4] | (hdr[pos+5]<<8) |
                                 (hdr[pos+6]<<16) | (hdr[pos+7]<<24));
            if (std::memcmp(hdr + pos, "fmt ", 4) == 0 && pos + 10 <= n) {
                uint16_t fmt = readU16(hdr + pos + 8);
                switch (fmt) {
                    case 1:  return Type::WavPcm;
                    case 6:  return Type::WavAlaw;
                    case 7:  return Type::WavUlaw;
                    case 0x11: return Type::WavAdpcm;
                    default: return Type::WavPcm;  /* best guess */
                }
            }
            pos += 8 + chunkSize;
            if (chunkSize & 1) pos++;  /* padding */
        }
        return Type::WavPcm;
    }

    /* MP3: sync word или ID3 */
    if (n >= 3 && hdr[0] == 'I' && hdr[1] == 'D' && hdr[2] == '3')
        return Type::Mp3;
    if (n >= 2 && hdr[0] == 0xFF && (hdr[1] & 0xE0) == 0xE0)
        return Type::Mp3;

    /* fallback */
    auto ext = fs.extension();
    if (ext == "mp3") return Type::Mp3;
    return Type::Unknown;
}

} // namespace CodecDetect
} // namespace ae2

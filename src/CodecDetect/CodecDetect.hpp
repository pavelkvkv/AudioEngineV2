#pragma once
#include <cstdint>

namespace ae2 {
class FsAdapter;

namespace CodecDetect {

enum class Type : uint8_t {
    Unknown = 0,
    WavPcm,
    WavAdpcm,
    WavAlaw,
    WavUlaw,
    Mp3
};

/// Определить формат по содержимому файла (читает первые ~512 байт).
/// Позиция в файле сбрасывается на 0 перед чтением.
Type detect(FsAdapter& fs);

} // namespace CodecDetect
} // namespace ae2

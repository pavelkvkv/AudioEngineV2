#pragma once
#include <cstdint>

namespace ae2 {

/// Общие типы AudioEngineV2
using s16 = int16_t;
using u16 = uint16_t;

/// Логический аудиовыход
enum class Output : uint8_t {
    FrontSpeaker = 0,
    RearLineout  = 1
};

/// Идентификатор источника звука
enum class SrcId : uint8_t {
    Disabled      = 0,
    Player        = 1,
    AdcDirect     = 2,
    FrontExternal = 3,
    Diag          = 4,
    Count         = 5
};

/// Таблица громкостей (0..10). Индекс 7 = passthrough (0x7FFF).
/// Формат Q15 для arm_scale_q15.
static constexpr int16_t kVolumeTable[11] = {
    0,      // 0  — тишина
    1638,   // 1
    3277,   // 2
    6554,   // 3
    9830,   // 4
    13107,  // 5
    19661,  // 6
    0x7FFF, // 7  — без усиления (passthrough)
    0x7FFF, // 8
    0x7FFF, // 9
    0x7FFF  // 10
};

} // namespace ae2

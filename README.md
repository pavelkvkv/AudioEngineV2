# AudioEngineV2

Модульный аудио-движок для AT32F437 (Cortex-M4F) с FreeRTOS.

## Ключевые особенности

- **Один FreeRTOS-таск** (AudioMgr) для всего пайплайна
- **Нулевые аллокации** на hot path (decode → volume → resample → DMA)
- **Mailbox-архитектура**: внешний код только отправляет команды
- **Модульные декодеры**: WAV PCM, MP3 (minimp3), IMA ADPCM, A-law, μ-law
- **Прямая запись** в DMA ring buffer через acquireWrite/commitWrite
- **Множество источников** с приоритетами и автопереключением

## Структура

```
include/AudioEngineV2/     — публичные заголовки
src/                       — реализация
third_party/minimp3/       — minimp3 header-only декодер
```

## Интеграция

```cmake
add_subdirectory(AudioEngineV2)
target_link_libraries(your_target PRIVATE AudioEngineV2)
# + предоставить FreeRTOS headers и arm_math.h
```

## API

C-интерфейс: `#include "AudioEngineV2/AudioEngine_C.h"`
C++-интерфейс: `#include "AudioEngineV2/AudioMgr.hpp"`

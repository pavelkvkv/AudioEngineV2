#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Типы, совместимые со старым API ── */

typedef struct {
    void*    port;  /* gpio_type* на железе, void* для переносимости */
    uint32_t pin;
} ae2_pin_t;

typedef enum {
    AE_PIPE_DISABLED       = 0,
    AE_PIPE_PLAYER         = 1,
    AE_PIPE_ADC_DIRECT     = 2,
    AE_PIPE_FRONT_EXTERNAL = 3,
    AE_PIPE_DIAG           = 4
} ae_pipe_id_t;

typedef struct {
    char     filename[64];
    uint32_t duration;          /* секунды */
    uint32_t position;          /* секунды */
    uint8_t  position_percent;  /* 0..100 */
    uint8_t  file_ready : 1;
    uint8_t  playing    : 1;
    uint8_t  pause      : 1;
    uint8_t  online     : 1;
    uint8_t  front      : 1;
    uint8_t  play_autostarted : 1;
} ae2_player_status_t;

/* ── API ── */

void aeInit(void);

/* Плеер */
void aePlayerEnqueueFile(const char* path, bool front);
void aePlayerPlayFileImmediately(const char* path, bool front);
void aePlayerPlay(void);
void aePlayerPause(void);
void aePlayerStop(void);
void aePlayerForward(void);
void aePlayerRewind(void);
void aePlayerStatus(ae2_player_status_t* st);

/* Источники */
bool aeSelectPipe(ae_pipe_id_t id);
ae_pipe_id_t aeCurrentPipe(void);

/* Настройки */
void aeSetSampleRateParam(int param);
void aeVolumeChanged(void);
void aeSetVolume(ae_pipe_id_t id, uint8_t vol);

#ifdef __cplusplus
}
#endif

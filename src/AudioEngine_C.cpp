/// @file AudioEngine_C.cpp
/// @brief C-обёртки AudioEngineV2.
#include "AudioEngineV2/AudioEngine_C.h"
#include "AudioEngineV2/AudioMgr.hpp"
#include <cstring>

using namespace ae2;

extern "C" {

void aeInit(void) {
    (void)AudioMgr::instance();
}

void aePlayerEnqueueFile(const char* path, bool front) {
    if (!path) return;
    Output out = front ? Output::FrontSpeaker : Output::RearLineout;
    AudioMgr::instance().addFile(path, 0, out, false);
}

void aePlayerPlayFileImmediately(const char* path, bool front) {
    if (!path) return;
    Output out = front ? Output::FrontSpeaker : Output::RearLineout;
    AudioMgr::instance().addFile(path, 0, out, true);
}

void aePlayerPlay(void)    { AudioMgr::instance().play(); }
void aePlayerPause(void)   { AudioMgr::instance().pause(); }
void aePlayerStop(void)    { AudioMgr::instance().stop(); }
void aePlayerForward(void) { AudioMgr::instance().forward(10); }
void aePlayerRewind(void)  { AudioMgr::instance().rewind(10); }

void aePlayerStatus(ae2_player_status_t* st) {
    if (!st) return;
    auto s = AudioMgr::instance().playerStatus();
    std::memset(st, 0, sizeof(*st));
    std::strncpy(st->filename, s.filename, sizeof(st->filename) - 1);
    st->duration = s.duration;
    st->position = s.position;
    st->position_percent = s.positionPercent;
    st->file_ready = s.fileReady ? 1 : 0;
    st->playing    = s.playing ? 1 : 0;
    st->pause      = s.paused ? 1 : 0;
    st->online     = (AudioMgr::instance().currentSource() == SrcId::AdcDirect) ? 1 : 0;
    st->front      = 1; /* TODO: track output */
}

bool aeSelectPipe(ae_pipe_id_t id) {
    auto& mgr = AudioMgr::instance();
    if (id == AE_PIPE_DISABLED) {
        /* Деактивировать текущий */
        auto cur = mgr.currentSource();
        if (cur != SrcId::Disabled)
            mgr.requestDeactivate(cur);
        return true;
    }
    mgr.requestActivate((SrcId)id);
    return true;
}

ae_pipe_id_t aeCurrentPipe(void) {
    return (ae_pipe_id_t)AudioMgr::instance().currentSource();
}

void aeSetSampleRateParam(int param) {
    static const uint32_t kRates[] = {128000, 96000, 88200, 176400};
    uint32_t rate = 128000;
    if (param >= 0 && param < (int)(sizeof(kRates)/sizeof(kRates[0])))
        rate = kRates[param];
    AudioMgr::instance().setSampleRate(rate);
}

void aeVolumeChanged(void) {
    AudioMgr::instance().volumeChanged();
}

void aeSetVolume(ae_pipe_id_t id, uint8_t vol) {
    AudioMgr::instance().setVolume((SrcId)id, vol);
}

} /* extern "C" */

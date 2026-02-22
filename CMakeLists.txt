cmake_minimum_required(VERSION 3.16)
project(AudioEngineV2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

add_library(AudioEngineV2 STATIC
    src/AudioHw/AudioHw.cpp
    src/Resampler/Resampler.cpp
    src/FsAdapter/FsAdapter.cpp
    src/CodecDetect/CodecDetect.cpp
    src/Mp3Duration/Mp3Duration.cpp
    src/Decoders/DecoderWavPcm.cpp
    src/Decoders/DecoderMp3.cpp
    src/Decoders/minimp3_impl.c
    src/Decoders/DecoderAdpcm.cpp
    src/Decoders/DecoderAlaw.cpp
    src/Decoders/DecoderUlaw.cpp
    src/AudioMgr/AudioMgr.cpp
    src/AudioEngine_C.cpp
)

target_include_directories(AudioEngineV2
    PUBLIC  include
    PRIVATE src
            third_party/minimp3
)

# Потребители должны предоставить:
#   - FreeRTOS headers (или стабы)
#   - arm_math.h (или стаб)
# через target_link_libraries(... AudioEngineV2 ...)

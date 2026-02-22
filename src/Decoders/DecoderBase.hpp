#pragma once
/// @file DecoderBase.hpp
/// @brief Базовый интерфейс синхронного декодера.

#include "AudioEngineV2/Types.hpp"
#include <cstdint>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace ae2 {

class FsAdapter;

class DecoderBase {
public:
    virtual ~DecoderBase() = default;

    virtual bool     open(FsAdapter& fs) = 0;
    virtual uint32_t decode(s16* buf, uint32_t maxSamples) = 0;
    virtual void     seek(uint32_t sec) = 0;
    virtual uint32_t position() const = 0;
    virtual uint32_t duration() const = 0;
    virtual uint32_t sampleRate() const = 0;
    virtual void     close() = 0;

    enum class Status : uint8_t { Closed, Ready, Playing, Error };
    Status status() const { return status_; }

protected:
    Status status_ = Status::Closed;
};

static constexpr size_t kMaxDecoderSize  = 80000;
static constexpr size_t kMaxDecoderAlign = 16;

template<typename T, typename... Args>
T* emplaceDecoder(uint8_t* mem, DecoderBase*& ptr, Args&&... args) {
    static_assert(sizeof(T) <= kMaxDecoderSize, "Decoder too large");
    static_assert(alignof(T) <= kMaxDecoderAlign, "Decoder alignment too large");
    if (ptr) { ptr->close(); ptr->~DecoderBase(); ptr = nullptr; }
    auto* p = new (mem) T(std::forward<Args>(args)...);
    ptr = p;
    return p;
}

inline void destroyDecoder(DecoderBase*& ptr) {
    if (ptr) {
        ptr->close();
        ptr->~DecoderBase();
        ptr = nullptr;
    }
}

} // namespace ae2

/// @file FsAdapter.cpp
#include "FsAdapter.hpp"
#include <algorithm>
#include <cctype>
#include <string_view>

namespace ae2 {

FsAdapter::FsAdapter(uint8_t* buf, size_t bufSize)
    : buf_(buf), bufSize_(bufSize), ownBuf_(false) {}

FsAdapter::FsAdapter(size_t bufSize)
    : buf_(new uint8_t[bufSize]), bufSize_(bufSize), ownBuf_(true) {}

FsAdapter::~FsAdapter() {
    close();
    if (ownBuf_) delete[] buf_;
}

bool FsAdapter::open(const char* path) {
    close();
    file_ = std::fopen(path, "rb");
    if (!file_) return false;
    std::strncpy(path_, path, kMaxPath - 1);
    path_[kMaxPath - 1] = '\0';
    bufPos_ = bufLen_ = 0;
    fileOffset_ = 0;
    /* Определяем размер файла */
    std::fseek(file_, 0, SEEK_END);
    fileSize_ = (uint32_t)std::ftell(file_);
    std::fseek(file_, 0, SEEK_SET);
    return true;
}

void FsAdapter::close() {
    if (file_) { std::fclose(file_); file_ = nullptr; }
    path_[0] = '\0';
    bufPos_ = bufLen_ = 0;
    fileOffset_ = 0;
    fileSize_ = 0;
}

size_t FsAdapter::read(uint8_t* dst, size_t len) {
    size_t total = 0;
    while (total < len) {
        if (bufPos_ >= bufLen_) {
            if (!refill_()) break;
        }
        size_t chunk = std::min(len - total, bufLen_ - bufPos_);
        std::memcpy(dst + total, buf_ + bufPos_, chunk);
        bufPos_ += chunk;
        total += chunk;
    }
    return total;
}

bool FsAdapter::seek(uint32_t pos) {
    if (!file_) return false;
    /* В пределах буфера? */
    if (pos >= fileOffset_ && pos < fileOffset_ + (uint32_t)bufLen_) {
        bufPos_ = pos - fileOffset_;
        return true;
    }
    /* Физический seek */
    if (std::fseek(file_, (long)pos, SEEK_SET) != 0) return false;
    fileOffset_ = pos;
    bufPos_ = bufLen_ = 0;
    return true;
}

uint32_t FsAdapter::tell() const {
    return fileOffset_ + (uint32_t)bufPos_;
}

uint32_t FsAdapter::size() const { return fileSize_; }

std::string_view FsAdapter::name() const {
    const char* slash = std::strrchr(path_, '/');
    if (!slash) slash = std::strrchr(path_, '\\');
    return slash ? (slash + 1) : path_;
}

std::string_view FsAdapter::extension() const {
    auto n = name();
    auto dot = n.rfind('.');
    if (dot == std::string_view::npos || dot == 0) { ext_[0] = '\0'; return {ext_, 0}; }
    auto raw = n.substr(dot + 1);
    size_t len = raw.size();
    if (len >= sizeof(ext_)) len = sizeof(ext_) - 1;
    for (size_t i = 0; i < len; ++i)
        ext_[i] = (char)std::tolower((unsigned char)raw[i]);
    ext_[len] = '\0';
    return {ext_, len};
}

bool FsAdapter::refill_() {
    if (!file_) return false;
    fileOffset_ += (uint32_t)bufLen_;
    bufLen_ = std::fread(buf_, 1, bufSize_, file_);
    bufPos_ = 0;
    return bufLen_ > 0;
}

} // namespace ae2

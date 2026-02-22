#pragma once
/// @file FsAdapter.hpp
/// @brief Буферизованный адаптер файловой системы (FILE* на хосте).

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace ae2 {

class FsAdapter {
public:
    /// Конструктор с внешним буфером (0 аллокаций).
    FsAdapter(uint8_t* buf, size_t bufSize);
    /// Конструктор с собственной аллокацией буфера.
    explicit FsAdapter(size_t bufSize = 8192);
    ~FsAdapter();

    FsAdapter(const FsAdapter&) = delete;
    FsAdapter& operator=(const FsAdapter&) = delete;

    bool open(const std::string& path);
    void close();

    /// Прочитать len байт в dst.
    size_t read(uint8_t* dst, size_t len);
    /// Переместить позицию.
    bool seek(uint32_t pos);
    /// Текущая позиция.
    uint32_t tell() const;
    /// Размер файла.
    uint32_t size() const;
    /// Открыт ли файл.
    bool isOpen() const { return file_ != nullptr; }
    /// Путь к файлу.
    const std::string& path() const { return path_; }

    /// Имя файла (без пути).
    std::string name() const;
    /// Расширение в нижнем регистре (без точки).
    std::string extension() const;

private:
    bool refill_();

    FILE* file_ = nullptr;
    uint8_t* buf_;
    size_t bufSize_;
    bool ownBuf_;
    size_t bufPos_ = 0;
    size_t bufLen_ = 0;
    uint32_t fileOffset_ = 0;  ///< смещение начала буфера в файле
    uint32_t fileSize_ = 0;
    std::string path_;
};

} // namespace ae2

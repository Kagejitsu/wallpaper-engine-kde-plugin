#pragma once

#include "Fs/MemBinaryStream.h"
#include "Fs/VFS.h"

#include <unordered_map>

namespace
{
class ParserTestFs : public wallpaper::fs::Fs {
public:
    std::unordered_map<std::string, std::vector<uint8_t>> files;

    bool Contains(std::string_view path) const override {
        return files.contains(std::string(path));
    }
    std::shared_ptr<wallpaper::fs::IBinaryStream> Open(std::string_view path) override {
        auto it = files.find(std::string(path));
        if (it == files.end()) return nullptr;
        return std::make_shared<wallpaper::fs::MemBinaryStream>(std::vector<uint8_t>(it->second));
    }
    std::shared_ptr<wallpaper::fs::IBinaryStreamW> OpenW(std::string_view) override {
        return nullptr;
    }
};
} // namespace

#include "archive.hpp"
#include <StormLib.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>

namespace d2x {
Bytes readFile(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f)
        throw std::runtime_error("Cannot read " + p.string());
    auto size = f.tellg();
    if (size < 0 || size > 1024ll * 1024 * 1024)
        throw std::runtime_error("Invalid file size");
    Bytes b(static_cast<size_t>(size));
    f.seekg(0);
    f.read(reinterpret_cast<char *>(b.data()), size);
    if (!f)
        throw std::runtime_error("Read failed: " + p.string());
    return b;
}
void writeFile(const std::filesystem::path &p, std::span<const uint8_t> b) {
    if (p.has_parent_path())
        std::filesystem::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char *>(b.data()), b.size());
    if (!f)
        throw std::runtime_error("Write failed: " + p.string());
}
Archives::~Archives() {
    for (auto h : handles)
        SFileCloseArchive(h);
}
void Archives::mount(const std::filesystem::path &p) {
    HANDLE h = nullptr;
    if (!SFileOpenArchive(p.string().c_str(), 0, MPQ_OPEN_READ_ONLY, &h))
        throw std::runtime_error("Cannot open MPQ: " + p.string());
    handles.push_back(h);
    names.push_back(p.filename().string());
    if (std::filesystem::exists("downloads/listfile.txt"))
        SFileAddListFile(h, "downloads/listfile.txt");
}
void Archives::mountDirectory(const std::filesystem::path &p) {
    if (std::filesystem::is_regular_file(p)) {
        mount(p);
        return;
    }
    if (!std::filesystem::is_directory(p))
        return;
    std::vector<std::filesystem::path> paths;
    for (auto &e : std::filesystem::directory_iterator(p))
        if (normalize(e.path().extension().string()) == ".mpq")
            paths.push_back(e.path());
    auto priority = [](const auto &p) {
        auto s = normalize(p.filename().string());
        return s == "patch_d2.mpq" ? 3 : s == "d2exp.mpq" ? 2 : s == "d2x-mvp.mpq" ? 1 : 0;
    };
    std::sort(paths.begin(), paths.end(), [&](auto &a, auto &b) {
        return priority(a) == priority(b) ? a < b : priority(a) < priority(b);
    });
    for (auto &path : paths)
        mount(path);
}
Bytes Archives::read(const std::string &path, bool required) const {
    auto name = normalize(path);
    for (auto i = handles.rbegin(); i != handles.rend(); ++i) {
        HANDLE f = nullptr;
        if (!SFileOpenFileEx(*i, name.c_str(), SFILE_OPEN_FROM_MPQ, &f))
            continue;
        DWORD high = 0, size = SFileGetFileSize(f, &high);
        if (high || size == SFILE_INVALID_SIZE || size > 256u * 1024 * 1024) {
            SFileCloseFile(f);
            throw std::runtime_error("Oversized MPQ member: " + name);
        }
        Bytes b(size);
        DWORD count = 0;
        bool ok = SFileReadFile(f, b.data(), size, &count, nullptr);
        SFileCloseFile(f);
        if (!ok || count != size)
            throw std::runtime_error("MPQ decompression failed: " + name);
        used.insert(name);
        return b;
    }
    if (required)
        throw std::runtime_error("Missing MPQ member: " + name);
    return {};
}
bool Archives::contains(const std::string &path) const {
    auto name = normalize(path);
    for (auto i = handles.rbegin(); i != handles.rend(); ++i)
        if (SFileHasFile(*i, name.c_str()))
            return true;
    return false;
}
std::vector<std::string> Archives::list(const std::string &pattern) const {
    std::set<std::string> result;
    for (auto h : handles) {
        SFILE_FIND_DATA info{};
        HANDLE f = SFileFindFirstFile(h, pattern.c_str(), &info, nullptr);
        if (f == nullptr || f == INVALID_HANDLE_VALUE)
            continue;
        do {
            result.insert(normalize(info.cFileName));
        } while (SFileFindNextFile(f, &info));
        SFileFindClose(f);
    }
    return {result.begin(), result.end()};
}
void Archives::packUsed(const std::filesystem::path &destination) const {
    if (std::filesystem::exists(destination))
        throw std::runtime_error("Pack already exists: " + destination.string());
    if (destination.has_parent_path())
        std::filesystem::create_directories(destination.parent_path());
    HANDLE out = nullptr;
    if (!SFileCreateArchive(destination.string().c_str(), MPQ_CREATE_ARCHIVE_V1 | MPQ_CREATE_LISTFILE,
                            static_cast<DWORD>(used.size() + 16), &out))
        throw std::runtime_error("Cannot create MPQ");
    try {
        auto members = used;
        for (auto &name : members) {
            auto b = read(name);
            HANDLE f = nullptr;
            if (!SFileCreateFile(out, name.c_str(), 0, static_cast<DWORD>(b.size()), 0, MPQ_FILE_COMPRESS,
                                 &f))
                throw std::runtime_error("Cannot create MPQ member");
            bool ok = SFileWriteFile(f, b.data(), static_cast<DWORD>(b.size()), MPQ_COMPRESSION_ZLIB);
            bool done = SFileFinishFile(f);
            if (!ok || !done)
                throw std::runtime_error("Cannot write MPQ member");
        }
    } catch (...) {
        SFileCloseArchive(out);
        throw;
    }
    SFileCloseArchive(out);
}
} // namespace d2x

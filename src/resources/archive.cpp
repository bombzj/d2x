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
    // Known resource paths are hash lookups; loading all member names is needed
    // only by the asset tool's list command, never by game startup.
    if (!SFileOpenArchive(p.string().c_str(), 0, MPQ_OPEN_READ_ONLY | MPQ_OPEN_NO_LISTFILE, &h))
        throw std::runtime_error("Cannot open MPQ: " + p.string());
    handles.push_back(h);
    mountedPaths_.push_back(std::filesystem::absolute(p));
    names.push_back(p.filename().string());
    mountIdentity_.add(p.filename().string());
    mountIdentity_.add(std::to_string(std::filesystem::file_size(p)));
    mountIdentity_.add(std::to_string(std::filesystem::last_write_time(p).time_since_epoch().count()));
    listed_ = false;
    readCache_.clear(); cacheBytes_ = 0; prepared_.clear();
}
void Archives::mountDirectory(const std::filesystem::path &p) {
    gameDirectory = std::filesystem::is_regular_file(p) ? p.parent_path() : p;
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
        // Historical compact packs must not shadow the original game archives.
        return s == "patch_d2.mpq" ? 3 : s == "d2exp.mpq" ? 2
               : s == "d2x-mvp.mpq" || s == "d2x-act1.mpq" ? -1 : 0;
    };
    std::sort(paths.begin(), paths.end(), [&](auto &a, auto &b) {
        return priority(a) == priority(b) ? a < b : priority(a) < priority(b);
    });
    for (auto &path : paths)
        mount(path);
}
void Archives::mountLooseDirectory(const std::filesystem::path &root, std::string_view prefix) {
    if (!std::filesystem::is_directory(root))
        throw std::runtime_error("Loose member root is not a directory: " + root.string());
    const std::string base = prefix.empty() ? std::string{} : normalize(std::string(prefix)) + "\\";
    size_t count = 0;
    for (std::filesystem::recursive_directory_iterator entry(root), end; entry != end; ++entry) {
        if (!entry->is_regular_file())
            continue;
        const auto relative = normalize(std::filesystem::relative(entry->path(), root).generic_string());
        if (relative.empty())
            continue;
        loose_.emplace(base + relative, entry->path());
        ++count;
    }
    if (count == 0)
        throw std::runtime_error("Loose member root is empty: " + root.string());
    // Deliberately absent from mountedPaths()/mountIdentity(): those describe the
    // archive set a worker re-opens as MPQs, which a directory cannot satisfy.
    // A loose root is fixed for the process lifetime, so nothing re-reads it.
    readCache_.clear(); cacheBytes_ = 0; prepared_.clear();
}
void Archives::retain(const std::string &name, const Bytes &value) const {
    // Bound retained decompressed bytes; large media stays demand-loaded.
    // Full-cache reset avoids storing an unbounded second copy of MPQs.
    constexpr size_t budget = 32u * 1024 * 1024, memberLimit = 2u * 1024 * 1024;
    if (value.size() > memberLimit) return;
    if (cacheBytes_ + value.size() > budget) { readCache_.clear(); cacheBytes_ = 0; }
    cacheBytes_ += value.size(); readCache_.emplace(name, value);
}
Bytes Archives::read(const std::string &path, bool required) const {
    pulseLoading();
    auto name = normalize(path);
    if (const auto cached = readCache_.find(name); cached != readCache_.end()) {
        used.insert(name);
        return cached->second;
    }
    // The client's own loose tree is the authority for the paths it ships.
    if (const auto loose = loose_.find(name); loose != loose_.end()) {
        auto b = readFile(loose->second);
        used.insert(name);
        retain(name, b);
        return b;
    }
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
        retain(name, b);
        return b;
    }
    if (required)
        throw std::runtime_error("Missing MPQ member: " + name);
    return {};
}
bool Archives::contains(const std::string &path) const {
    auto name = normalize(path);
    if (loose_.find(name) != loose_.end())
        return true;
    for (auto i = handles.rbegin(); i != handles.rend(); ++i)
        if (SFileHasFile(*i, name.c_str()))
            return true;
    return false;
}
std::vector<std::string> Archives::list(const std::string &pattern) const {
    if (!listed_) {
        for (auto h : handles) {
            SFileAddListFile(h, nullptr);
            if (std::filesystem::exists("downloads/listfile.txt"))
                SFileAddListFile(h, "downloads/listfile.txt");
        }
        listed_ = true;
    }
    std::set<std::string> result;
    // Loose members are already normalized; match them with the same '*'/'?'
    // wildcards StormLib applies to archive members.
    const auto wanted = normalize(pattern);
    const auto matches = [&](std::string_view name) {
        size_t patternAt = 0, nameAt = 0, star = std::string_view::npos, restart = 0;
        while (nameAt < name.size()) {
            if (patternAt < wanted.size() && (wanted[patternAt] == name[nameAt] || wanted[patternAt] == '?')) {
                ++patternAt; ++nameAt;
            } else if (patternAt < wanted.size() && wanted[patternAt] == '*') {
                star = patternAt++; restart = nameAt;
            } else if (star != std::string_view::npos) {
                patternAt = star + 1; nameAt = ++restart;
            } else return false;
        }
        while (patternAt < wanted.size() && wanted[patternAt] == '*') ++patternAt;
        return patternAt == wanted.size();
    };
    for (const auto &entry : loose_)
        if (matches(entry.first)) result.insert(entry.first);
    for (auto h : handles) {
        SFILE_FIND_DATA info{};
        HANDLE f = SFileFindFirstFile(h, pattern.c_str(), &info, nullptr);
        if (f == nullptr)
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
void Archives::packDirectory(const std::filesystem::path &source, std::string_view prefix,
                             const std::filesystem::path &destination) const {
    if (std::filesystem::exists(destination))
        throw std::runtime_error("Pack already exists: " + destination.string());
    if (!std::filesystem::is_directory(source))
        throw std::runtime_error("Pack source is not a directory: " + source.string());
    std::vector<std::pair<std::string, std::filesystem::path>> members;
    for (auto &entry : std::filesystem::recursive_directory_iterator(source))
        if (entry.is_regular_file()) {
            auto relative = std::filesystem::relative(entry.path(), source).generic_string();
            auto member = prefix.empty() ? relative : std::string(prefix) + "/" + relative;
            members.emplace_back(normalize(member), entry.path());
        }
    std::sort(members.begin(), members.end());
    if (destination.has_parent_path())
        std::filesystem::create_directories(destination.parent_path());
    HANDLE out = nullptr;
    if (!SFileCreateArchive(destination.string().c_str(), MPQ_CREATE_ARCHIVE_V1 | MPQ_CREATE_LISTFILE,
                            static_cast<DWORD>(members.size() + 16), &out))
        throw std::runtime_error("Cannot create MPQ");
    try {
        for (auto &[name, path] : members) {
            auto b = readFile(path);
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

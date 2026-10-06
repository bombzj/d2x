#include "save_file.hpp"
#include "save_codec.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace d2x {
CharacterSaveData loadSave(const std::filesystem::path &path, const ClassicData &content) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("Cannot open save: " + path.string());
    auto length = file.tellg();
    if (length < 0 || uint64_t(length) > maxSaveBytes)
        throw std::runtime_error("Invalid save file size");
    Bytes bytes(static_cast<size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size())))
        throw std::runtime_error("Cannot read save: " + path.string());
    return decodeSave(bytes, content);
}
void writeSave(const std::filesystem::path &path, const CharacterSaveData &snapshot, const ClassicData &content) {
    if (path.empty() || path.filename().empty())
        throw std::runtime_error("Save path must name a file");
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
        return char(std::tolower(character));
    });
    if (extension != ".d2s")
        throw std::runtime_error("Character saves require the .d2s extension");
    auto bytes = encodeSave(snapshot, content);
    decodeSave(bytes, content);
    writeFileAtomically(path, bytes, true);
}
} // namespace d2x

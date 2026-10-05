#include "debug_pipe.hpp"
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
// Windows types must be defined before SDDL declarations (also with MinGW).
// clang-format off
#include <windows.h>
#include <sddl.h>
// clang-format on
#endif

namespace d2x {
namespace {
void eraseRequest(std::string &value) {
    volatile char *bytes = value.data();
    for (size_t i = 0; i < value.size(); ++i)
        bytes[i] = 0;
    value.clear();
}
} // namespace
struct DebugPipe::Impl {
#ifdef _WIN32
    HANDLE pipe = INVALID_HANDLE_VALUE;
    bool connected = false;
    std::string input, output;
    size_t sent = 0;
    std::chrono::steady_clock::time_point deadline;
    ~Impl() {
        eraseRequest(input);
        if (pipe != INVALID_HANDLE_VALUE)
            CloseHandle(pipe);
    }
    void reset() {
        DisconnectNamedPipe(pipe);
        connected = false;
        eraseRequest(input);
        output.clear();
        sent = 0;
    }
#endif
};
DebugPipe::DebugPipe(const std::string &name) {
    if (name.empty())
        return;
    if (name.size() > 80 || !std::all_of(name.begin(), name.end(), [](unsigned char character) {
            return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                   (character >= '0' && character <= '9') || character == '-' || character == '_';
        }))
        throw std::runtime_error("Debug pipe name must be 1-80 ASCII letters, digits, '-' or '_'");
#ifdef _WIN32
    impl_ = std::make_unique<Impl>();
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        throw std::runtime_error("Cannot read debug pipe owner");
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    std::vector<unsigned char> buffer(bytes);
    bool obtained = GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes) != FALSE;
    CloseHandle(token);
    LPSTR sid = nullptr;
    if (!obtained || !ConvertSidToStringSidA(reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid, &sid))
        throw std::runtime_error("Cannot resolve debug pipe owner SID");
    std::string sddl = "D:P(A;;GA;;;" + std::string(sid) + ")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(sddl.c_str(), SDDL_REVISION_1, &descriptor,
                                                              nullptr))
        throw std::runtime_error("Cannot create debug pipe ACL");
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    std::string path = "\\\\.\\pipe\\" + name;
    impl_->pipe =
        CreateNamedPipeA(path.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                         PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
                         65536, 65536, 0, &security);
    LocalFree(descriptor);
    if (impl_->pipe == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot create debug pipe (name may already be in use)");
#else
    throw std::runtime_error("--debug-pipe is available only on Windows");
#endif
}
DebugPipe::~DebugPipe() = default;
void DebugPipe::poll(const std::function<std::string(const std::string &)> &handler) {
    if (!impl_)
        return;
#ifdef _WIN32
    auto &state = *impl_;
    if (!state.connected) {
        bool connected = ConnectNamedPipe(state.pipe, nullptr) != FALSE;
        DWORD error = connected ? ERROR_SUCCESS : GetLastError();
        if (error == ERROR_NO_DATA) {
            state.reset();
            return;
        }
        if (!connected && error != ERROR_PIPE_CONNECTED)
            return;
        state.connected = true;
        state.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    }
    if (std::chrono::steady_clock::now() > state.deadline) {
        state.reset();
        return;
    }
    if (state.output.empty()) {
        char buffer[4096];
        DWORD bytes = 0;
        if (ReadFile(state.pipe, buffer, sizeof(buffer), &bytes, nullptr)) {
            state.input.append(buffer, bytes);
            if (state.input.size() > 16384) {
                state.output = "{\"ok\":false,\"error\":\"Request exceeds 16 KiB\"}\n";
            } else if (auto end = state.input.find('\n'); end != std::string::npos) {
                auto request = state.input.substr(0, end);
                eraseRequest(state.input);
                try {
                    state.output = handler(request) + "\n";
                } catch (...) {
                    eraseRequest(request);
                    throw;
                }
                eraseRequest(request);
                if (state.output.size() > 4 * 1024 * 1024)
                    state.output = "{\"ok\":false,\"error\":\"Response exceeds 4 MiB\"}\n";
            }
            volatile char *readBytes = buffer;
            for (size_t i = 0; i < sizeof(buffer); ++i)
                readBytes[i] = 0;
        } else if (GetLastError() != ERROR_NO_DATA) {
            state.reset();
            return;
        }
    }
    if (!state.output.empty() && state.sent < state.output.size()) {
        DWORD bytes = 0;
        if (WriteFile(state.pipe, state.output.data() + state.sent,
                      DWORD(std::min<size_t>(4096, state.output.size() - state.sent)), &bytes, nullptr))
            state.sent += bytes;
        else if (GetLastError() != ERROR_NO_DATA) {
            state.reset();
            return;
        }
    }
    if (!state.output.empty() && state.sent == state.output.size()) {
        DWORD available = 0;
        if (!PeekNamedPipe(state.pipe, nullptr, 0, nullptr, &available, nullptr))
            state.reset();
    }
#else
    (void)handler;
#endif
}
} // namespace d2x

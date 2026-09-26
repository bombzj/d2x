#pragma once

namespace d2x {
void installCrashReporting() noexcept;
void writeFatalErrorReport(const char *message) noexcept;
} // namespace d2x
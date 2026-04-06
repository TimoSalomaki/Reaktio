#pragma once

#include <cstdio>
#include <filesystem>
#include <iosfwd>
#include <string_view>

namespace reaktio::foundation {

enum class LogLevel {
    Info,
    Warning,
    Error,
};

class CrashSafeLog {
  public:
    CrashSafeLog() = default;
    ~CrashSafeLog();

    CrashSafeLog(const CrashSafeLog&) = delete;
    CrashSafeLog& operator=(const CrashSafeLog&) = delete;

    bool open_file(const std::filesystem::path& path);
    void attach_mirror_stream(std::ostream* mirror_stream) noexcept;
    void write(LogLevel level, std::string_view message) noexcept;

    [[nodiscard]] const std::filesystem::path& file_path() const noexcept;

  private:
    void close_file() noexcept;
    void flush_file() noexcept;

    std::ostream* mirror_stream_{};
    std::FILE* file_{};
    std::filesystem::path file_path_;
};

[[nodiscard]] std::string_view to_string(LogLevel level) noexcept;

} // namespace reaktio::foundation
#include "reaktio/foundation/CrashSafeLog.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <ostream>

#if defined(_WIN32)
#include <io.h>
#endif

namespace reaktio::foundation {

CrashSafeLog::~CrashSafeLog() {
    close_file();
}

bool CrashSafeLog::open_file(const std::filesystem::path& path) {
    close_file();

    if (!path.has_parent_path()) {
        file_path_ = path;
    } else {
        std::error_code error_code;
        std::filesystem::create_directories(path.parent_path(), error_code);
        if (error_code) {
            return false;
        }

        file_path_ = path;
    }

#if defined(_WIN32)
    std::FILE* file = nullptr;
    if (_wfopen_s(&file, file_path_.wstring().c_str(), L"a") != 0 || file == nullptr) {
        file_path_.clear();
        return false;
    }
#else
    std::FILE* file = std::fopen(file_path_.string().c_str(), "a");
    if (file == nullptr) {
        file_path_.clear();
        return false;
    }
#endif

    file_ = file;
    return true;
}

void CrashSafeLog::attach_mirror_stream(std::ostream* mirror_stream) noexcept {
    mirror_stream_ = mirror_stream;
}

void CrashSafeLog::write(LogLevel level, std::string_view message) noexcept {
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);

    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &now_time);
#else
    localtime_r(&now_time, &local_time);
#endif

    char prefix[32]{};
    const int prefix_length = std::snprintf(
        prefix,
        sizeof(prefix),
        "%02d:%02d:%02d [%.*s] ",
        local_time.tm_hour,
        local_time.tm_min,
        local_time.tm_sec,
        static_cast<int>(to_string(level).size()),
        to_string(level).data());

    if (prefix_length <= 0) {
        return;
    }

    const auto safe_prefix_length = static_cast<std::size_t>(prefix_length);

    if (mirror_stream_ != nullptr) {
        try {
            mirror_stream_->write(prefix, static_cast<std::streamsize>(safe_prefix_length));
            mirror_stream_->write(message.data(), static_cast<std::streamsize>(message.size()));
            mirror_stream_->put('\n');
            mirror_stream_->flush();
        } catch (...) {
        }
    }

    if (file_ != nullptr) {
        std::fwrite(prefix, sizeof(char), safe_prefix_length, file_);
        std::fwrite(message.data(), sizeof(char), message.size(), file_);
        std::fputc('\n', file_);
        flush_file(level != LogLevel::Info);
    }
}

const std::filesystem::path& CrashSafeLog::file_path() const noexcept {
    return file_path_;
}

void CrashSafeLog::close_file() noexcept {
    if (file_ != nullptr) {
        flush_file(true);
        std::fclose(file_);
        file_ = nullptr;
    }
}

void CrashSafeLog::flush_file(bool durable) noexcept {
    if (file_ == nullptr) {
        return;
    }

    std::fflush(file_);

#if defined(_WIN32)
    if (durable) {
        _commit(_fileno(file_));
    }
#endif
}

std::string_view to_string(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Info:
        return "info";
    case LogLevel::Warning:
        return "warning";
    case LogLevel::Error:
        return "error";
    }

    return "unknown";
}

} // namespace reaktio::foundation
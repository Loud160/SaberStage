#pragma once

#include "NativeLoggerQuest/NativeLogger.hpp"

#include <fmt/format.h>

#include <exception>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace saberstage {

template <typename... Args>
struct LogFormatString {
    fmt::format_string<Args...> format;
    std::source_location source;

    template <typename String>
        requires(std::is_convertible_v<const String&, fmt::basic_string_view<char>>)
    consteval LogFormatString(
        const String& value,
        std::source_location location = std::source_location::current())
        : format(value), source(location) {}
};

// Preserve SaberStage's established Logging::Logger call sites while routing
// every record into a private, statically linked logger. The facade captures
// source locations at the call site and formats once before the asynchronous
// backend receives an owned string.
class LoggerFacade final {
public:
    void Initialize(std::string_view version) const noexcept;
    void Flush() const noexcept;
    void Shutdown() const noexcept;

    template <typename... Args>
    void debug(const LogFormatString<std::type_identity_t<Args>...>& format, Args&&... args) const noexcept {
        FormatAndEmit(NativeLoggerQuest::LogSeverity::Debug, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void info(const LogFormatString<std::type_identity_t<Args>...>& format, Args&&... args) const noexcept {
        FormatAndEmit(NativeLoggerQuest::LogSeverity::Info, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void warn(const LogFormatString<std::type_identity_t<Args>...>& format, Args&&... args) const noexcept {
        FormatAndEmit(NativeLoggerQuest::LogSeverity::Warning, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void error(const LogFormatString<std::type_identity_t<Args>...>& format, Args&&... args) const noexcept {
        FormatAndEmit(NativeLoggerQuest::LogSeverity::Error, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void critical(const LogFormatString<std::type_identity_t<Args>...>& format, Args&&... args) const noexcept {
        FormatAndEmit(NativeLoggerQuest::LogSeverity::Critical, format, std::forward<Args>(args)...);
    }

private:
    template <typename... Args>
    void FormatAndEmit(
        NativeLoggerQuest::LogSeverity severity,
        const LogFormatString<std::type_identity_t<Args>...>& format,
        Args&&... args) const noexcept {
        try {
            Emit(
                severity,
                fmt::format(format.format, std::forward<Args>(args)...),
                {format.source.file_name(), format.source.function_name(), format.source.line()});
        } catch (const std::exception& exception) {
            ReportFormattingFailure(exception.what());
        } catch (...) {
            ReportFormattingFailure("unknown formatting failure");
        }
    }

    static void Emit(
        NativeLoggerQuest::LogSeverity severity,
        std::string message,
        NativeLoggerQuest::LogSource source) noexcept;
    static void ReportFormattingFailure(const char* detail) noexcept;
};

class Logging final {
public:
    static inline LoggerFacade Logger{};
};

} // namespace saberstage

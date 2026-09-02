#include "saberstage/Logging.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>

namespace {

void ShutdownLoggerAtExit() noexcept {
    saberstage::Logging::Logger.Shutdown();
}

} // namespace

namespace saberstage {

void LoggerFacade::Initialize(std::string_view version) const noexcept {
    try {
        NativeLoggerQuest::NativeLoggerOptions options;
        options.activePath =
            "/sdcard/ModData/com.beatgames.beatsaber/Mods/SaberStage/Logs/"
            "saberstage-native.log";
        options.previousPath =
            "/sdcard/ModData/com.beatgames.beatsaber/Mods/SaberStage/Logs/"
            "saberstage-native.previous.log";
        options.emitToLogcat = true;

        NativeLoggerQuest::NativeLogger::Instance().Initialize(
            std::move(options),
            "SaberStage " + std::string(version) +
                " native log session started; backend mode: SaberStage native only");

        static std::atomic_flag registered = ATOMIC_FLAG_INIT;
        if (!registered.test_and_set(std::memory_order_relaxed)) {
            std::atexit(ShutdownLoggerAtExit);
        }
    } catch (...) {
        ReportFormattingFailure("logger initialization failed");
    }
}

void LoggerFacade::Flush() const noexcept {
    NativeLoggerQuest::NativeLogger::Instance().Flush(std::chrono::milliseconds(200));
}

void LoggerFacade::Shutdown() const noexcept {
    auto& logger = NativeLoggerQuest::NativeLogger::Instance();
    logger.Flush(std::chrono::milliseconds(250));
    logger.Shutdown();
}

void LoggerFacade::Emit(
    NativeLoggerQuest::LogSeverity severity,
    std::string message,
    NativeLoggerQuest::LogSource source) noexcept {
    auto& logger = NativeLoggerQuest::NativeLogger::Instance();
    logger.Log(severity, std::move(message), source);
    if (severity == NativeLoggerQuest::LogSeverity::Critical) {
        // Critical startup/hook failures may be followed immediately by an
        // abort. Give the writer one small bounded chance to preserve the tail
        // without turning a crash path into an unbounded wait.
        logger.Flush(std::chrono::milliseconds(100));
    }
}

void LoggerFacade::ReportFormattingFailure(const char* detail) noexcept {
    try {
        Emit(
            NativeLoggerQuest::LogSeverity::Error,
            std::string("SaberStage logger could not format a message: ") +
                (detail ? detail : "unknown error"),
            {});
    } catch (...) {
        Emit(
            NativeLoggerQuest::LogSeverity::Error,
            "SaberStage logger could not format a message",
            {});
    }
}

} // namespace saberstage

// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Owns SaberStage logging initialization and the process-wide logger handle.
// - This is the only first-party bridge to the bundled native logger runtime.

#include "saberstage/Logging.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <regex>

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
    logger.Log(severity, SanitizeDiagnosticText(std::move(message)), source);
    if (severity == NativeLoggerQuest::LogSeverity::Critical) {
        // Critical startup/hook failures may be followed immediately by an
        // abort. Give the writer one small bounded chance to preserve the tail
        // without turning a crash path into an unbounded wait.
        logger.Flush(std::chrono::milliseconds(100));
    }
}

std::string LoggerFacade::SanitizeDiagnosticText(std::string message) noexcept {
    try {
        // IP addresses are not useful in a SaberStage support log. Redact both
        // common IPv4 values and full/abbreviated IPv6-looking sequences.
        message = std::regex_replace(
            message,
            std::regex(R"(\b(?:[0-9]{1,3}\.){3}[0-9]{1,3}\b)"),
            "[redacted-ip]");
        message = std::regex_replace(
            message,
            std::regex(R"(\b(?:[0-9A-Fa-f]{1,4}:){2,7}[0-9A-Fa-f]{0,4}\b)"),
            "[redacted-ip]");

        // Libraries sometimes include query parameters or HTTP-style header
        // values in their exception text. Preserve the field name so the log
        // remains diagnostic while removing the credential itself.
        message = std::regex_replace(
            message,
            std::regex(
                R"(((?:authorization)\s*(?:=|:)\s*(?:bearer\s+)?)[^\s&,;]+)",
                std::regex_constants::icase),
            "$1[redacted]");
        message = std::regex_replace(
            message,
            std::regex(
                R"((\bbearer\s+)[A-Za-z0-9._~+/=-]+)",
                std::regex_constants::icase),
            "$1[redacted]");
        message = std::regex_replace(
            message,
            std::regex(
                R"(((?:stream[_-]?key|access[_-]?token|refresh[_-]?token|oauth)\s*(?:=|:)\s*)[^\s&,;]+)",
                std::regex_constants::icase),
            "$1[redacted]");
        message = std::regex_replace(
            message,
            std::regex(R"(\blive_[A-Za-z0-9_-]{12,}\b)"),
            "[redacted-stream-key]");
    } catch (...) {
        // A scrubber failure must never replace the original application
        // failure. Return a safe fixed message rather than risk leaking the
        // unredacted diagnostic text.
        return "SaberStage diagnostic text was withheld because redaction failed";
    }
    return message;
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

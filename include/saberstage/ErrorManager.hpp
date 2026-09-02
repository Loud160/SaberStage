// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Centralizes recoverable error reporting, deduplication, and the user-visible error queue.
// - Keeps logging independent from Unity UI so failures can be recorded before a menu exists.

#pragma once

#include <exception>
#include <cstdint>
#include <mutex>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

namespace saberstage {

// Central recovery boundary for unexpected native failures. Worker threads may
// enqueue messages, but only TickMainThread is allowed to touch Unity UI.
class ErrorManager final {
public:
    static ErrorManager& Instance() noexcept;

    void ReportInternal(
        std::string_view context,
        std::string_view detail,
        std::source_location source = std::source_location::current()) noexcept;
    void ReportUserVisible(
        std::string title,
        std::string detail) noexcept;
    // Called only after Beat Saber's MainFlowCoordinator has completed its
    // activation callback. Until that point BSML's main-flow lookup can touch
    // incomplete IL2CPP metadata and must not be queried, even from Unity's
    // main thread.
    void NotifyMainFlowActivated() noexcept;
    void TickMainThread() noexcept;

    template <typename Function>
    bool Guard(
        std::string_view context,
        Function&& function,
        std::string_view userTitle = {},
        std::string_view userDetail = {},
        std::source_location source = std::source_location::current()) noexcept {
        try {
            std::forward<Function>(function)();
            return true;
        } catch (const std::exception& exception) {
            ReportInternal(context, exception.what(), source);
        } catch (...) {
            ReportInternal(context, "unknown native exception", source);
        }
        if (!userTitle.empty()) {
            ReportUserVisible(
                std::string(userTitle),
                userDetail.empty()
                    ? "SaberStage could not complete this operation. Details were written to the SaberStage log."
                    : std::string(userDetail));
        }
        return false;
    }

private:
    ErrorManager() = default;

    void TickMainThreadImpl();
    void Acknowledge(std::uint64_t generation) noexcept;
    void Release(std::uint64_t generation, bool requeue) noexcept;
    void RecordDialogFailure(std::string_view detail) noexcept;

    std::mutex mutex_;
    std::optional<std::pair<std::string, std::string>> pendingDialog_;
    std::optional<std::pair<std::string, std::string>> activeDialog_;
    std::uint64_t dialogGeneration_ = 0;
    bool uiDiscoveryReady_ = false;
    bool dialogVisible_ = false;
    bool dialogAcknowledged_ = false;
    bool dialogFailureLogged_ = false;
};

} // namespace saberstage

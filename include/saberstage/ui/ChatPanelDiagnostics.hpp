// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Observes the existing native chat scrollbar without changing its UI state.
// - Bounds diagnostic work and keeps all Unity reads on the panel's update thread.

#pragma once

#include <cmath>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <source_location>
#include <string_view>

namespace BSML { class ScrollView; }
namespace UnityEngine { class GameObject; class Component; }

namespace saberstage::ui {

// This gate is platform-neutral so the sampling/log budget can be tested without
// Unity. A live chat must not turn detailed hierarchy logging into per-frame work.
class ChatPanelDiagnosticSchedule final {
public:
    enum class Report { None, Detail, Summary };

    bool Advance(float seconds) noexcept {
        if (!std::isfinite(seconds) || seconds <= 0.0F) return false;
        sampleSeconds_ += seconds;
        reportSeconds_ += seconds;
        if (sampleSeconds_ < 0.5F) return false;
        sampleSeconds_ = 0.0F;
        return true;
    }

    Report SelectReport(bool changed) noexcept {
        if (!reported_ || (changed && reportSeconds_ >= 5.0F)) {
            reported_ = true;
            reportSeconds_ = 0.0F;
            return Report::Detail;
        }
        if (reportSeconds_ >= 30.0F) {
            reportSeconds_ = 0.0F;
            return Report::Summary;
        }
        return Report::None;
    }

private:
    float sampleSeconds_ = 0.0F;
    float reportSeconds_ = 0.0F;
    bool reported_ = false;
};

// A mismatch alone does not identify the writer. Only report an overwrite when
// SetContentSize first read back correctly and a later observation disagrees.
inline bool ChatContentHeightWasOverwritten(float requested, float applied, float observed) noexcept {
    return requested >= 0.0F && std::isfinite(requested) &&
        std::isfinite(applied) && std::isfinite(observed) &&
        std::abs(applied - requested) <= 0.5F && std::abs(observed - requested) > 0.5F;
}

struct ChatPanelDiagnosticContext {
    UnityEngine::GameObject* panel = nullptr;
    BSML::ScrollView* scroll = nullptr;
    // CreateScrollView returns the INNER container, not HMUI's outer content.
    UnityEngine::GameObject* innerContent = nullptr;
    UnityEngine::Component* background = nullptr;
    std::size_t entries = 0;
    std::size_t pooledRows = 0;
    bool contentOverflows = false;
    bool resizing = false;
};

class ChatPanelDiagnostics final {
public:
    // All context references are borrowed only for the duration of the call.
    // Nothing is retained across scene teardown; Reset never dereferences Unity.
    void NativeCreated(const ChatPanelDiagnosticContext& context) noexcept;
    void ContentSizeApplied(float requested, float readback) noexcept;
    void Tick(const ChatPanelDiagnosticContext& context, float deltaSeconds) noexcept;
    void Reset() noexcept;
    // Literal operation names/source locations avoid allocations on the hot path.
    void SetOperation(const char* operation, int row = -1,
                      std::source_location source = std::source_location::current()) noexcept {
        operation_ = operation;
        operationRow_ = row;
        operationSource_ = source;
    }
    void ReportUpdateFailure(std::string_view detail) noexcept;

private:
    ChatPanelDiagnosticSchedule schedule_;
    float requestedHeight_ = -1.0F;
    float appliedHeight_ = -1.0F;
    float secondsSinceWrite_ = 0.0F;
    std::uint64_t writes_ = 0;
    std::uint32_t lastFlags_ = 0;
    int lastIndicatorId_ = 0;
    float lastWidth_ = -1.0F;
    float lastHeight_ = -1.0F;
    bool initialized_ = false;
    bool failed_ = false;
    const char* operation_ = "not yet initialized";
    int operationRow_ = -1;
    std::source_location operationSource_ = std::source_location::current();
    std::chrono::steady_clock::time_point lastFailureLog_{};
    std::uint64_t updateFailures_ = 0;
    std::uint64_t suppressedFailures_ = 0;
};

} // namespace saberstage::ui

// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Runs the guided calibration state machine and captures accepted samples.
// - Calibration is transactional: cancellation restores the prior profile and only completion persists changes.

#pragma once

#include "saberstage/avatar/calibration/PlayerCalibrationProfile.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace saberstage::avatar::calibration {

// Snapshot consumed by both the fixed menu and movable calibration panel. The
// revision counters let UI code update only when text or a cue actually changes.
struct CalibrationStatus {
    CalibrationPhase phase = CalibrationPhase::Idle;
    CalibrationMode mode = CalibrationMode::Basic;
    CalibrationProgression progression = CalibrationProgression::Automatic;
    CalibrationStep step = CalibrationStep::Neutral;
    std::size_t stepIndex = 0;
    std::size_t stepCount = 0;
    float phaseProgress = 0.0F;
    float lastSampleConfidence = 0.0F;
    int countdownSecondsRemaining = 0;
    CalibrationCue cue = CalibrationCue::None;
    std::uint64_t cueRevision = 0;
    std::uint64_t validationRevision = 0;
    bool lastCaptureAccepted = false;
    bool pendingProfileReady = false;
    std::uint64_t revision = 0;
    std::string message;
    std::string validationDetails;
};

class PlayerCalibrationSession final {
public:
    explicit PlayerCalibrationSession(std::filesystem::path profilePath);

    ProfileLoadResult Load() noexcept;
    ProfileLoadResult SwitchProfilePath(std::filesystem::path profilePath) noexcept;
    // Prepare builds the requested plan but does not begin the countdown. This
    // gives the player time to read instructions before tracking samples matter.
    bool Prepare(CalibrationMode mode, std::string* error = nullptr) noexcept;
    bool StartPrepared(CalibrationProgression progression, std::string* error = nullptr) noexcept;
    bool Start(CalibrationMode mode, std::string* error = nullptr) noexcept;
    bool StartCurrentStep(std::string* error = nullptr) noexcept;
    bool Continue(std::string* error = nullptr) noexcept;
    bool Retry(std::string* error = nullptr) noexcept;
    bool Restart(std::string* error = nullptr) noexcept;
    bool Complete(std::string* error = nullptr) noexcept;
    // Cancel is transactional: it restores the profile and runtime values that
    // were active before this session began.
    void Cancel() noexcept;
    bool ResetProfile(std::string* error = nullptr) noexcept;
    void Update(const TrackingSample& sample) noexcept;

    [[nodiscard]] const CalibrationStatus& Status() const noexcept;
    [[nodiscard]] const PlayerCalibrationProfile& Profile() const noexcept;
    [[nodiscard]] const RuntimePlayerProfile& RuntimeProfile() const noexcept;
    [[nodiscard]] bool Active() const noexcept;

private:
    bool BeginSession(
        CalibrationMode mode,
        CalibrationProgression progression,
        std::string* error) noexcept;
    void BeginCurrentStep(double timestamp) noexcept;
    void FinishCapture() noexcept;
    void AdvanceOrComplete() noexcept;
    void SetMessage(std::string message) noexcept;
    void EmitCue(CalibrationCue cue) noexcept;
    void SetValidation(bool accepted, std::string details) noexcept;

    std::filesystem::path profilePath_;
    PlayerCalibrationProfile profile_{};
    RuntimePlayerProfile runtime_{};
    PlayerCalibrationProfile profileBeforeSession_{};
    RuntimePlayerProfile runtimeBeforeSession_{};
    CalibrationStatus status_{};
    std::vector<CalibrationStep> plan_;
    // Captured frames are session-local until Complete validates and persists
    // the pending profile; an interrupted calibration cannot poison live IK.
    std::vector<CalibrationFrame> frames_;
    double phaseStartedAt_ = 0.0;
    double lastRecordedAt_ = -1.0;
    int lastCountdownSecond_ = -1;
    std::uint64_t cueRevisionCounter_ = 0;
    std::uint64_t validationRevisionCounter_ = 0;
};

} // namespace saberstage::avatar::calibration

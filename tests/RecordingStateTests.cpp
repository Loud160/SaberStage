// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Exercises RecordingState behavior on the host without starting Beat Saber.
// - Regression coverage focuses on deterministic state, validation, and boundary conditions.

#include "saberstage/recording/CaptureTimeline.hpp"
#include "saberstage/recording/RecordingState.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

using saberstage::recording::CanPause;
using saberstage::recording::CanResume;
using saberstage::recording::CanStart;
using saberstage::recording::CanStop;
using saberstage::recording::CanTransition;
using saberstage::recording::CapturePresentationTimeNanos;
using saberstage::recording::DecideCaptureTimelineFrame;
using saberstage::recording::HasRecordingTimeline;
using saberstage::recording::IncludesLocalRecording;
using saberstage::recording::NormalizeCapturePresentationFrame;
using saberstage::recording::RecordingOutputType;
using saberstage::recording::RecordingOutputTypeName;
using saberstage::recording::RecordingState;

void Require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

} // namespace

int main() {
    Require(RecordingOutputTypeName(RecordingOutputType::Local) == std::string_view("LOCAL"),
            "local recording output has the compact world-panel label");
    Require(RecordingOutputTypeName(RecordingOutputType::LiveStream) == std::string_view("LIVE STREAM"),
            "stream-only output has the compact world-panel label");
    Require(RecordingOutputTypeName(RecordingOutputType::DiscordScreen) == std::string_view("DISCORD SCREEN"),
            "Discord-only output has a distinct compact world-panel label");
    Require(RecordingOutputTypeName(RecordingOutputType::LocalAndLive) == std::string_view("LOCAL + LIVE"),
            "an intentional local recording with an attached broadcast has a clear compact label");
    Require(RecordingOutputTypeName(RecordingOutputType::LocalAndDiscord) == std::string_view("LOCAL + DISCORD"),
            "a local recording with Discord sharing has a clear compact label");
    Require(RecordingOutputTypeName(RecordingOutputType::LiveAndDiscord) == std::string_view("LIVE + DISCORD"),
            "two network outputs without a local file have a clear compact label");
    Require(RecordingOutputTypeName(RecordingOutputType::LocalLiveAndDiscord) ==
                std::string_view("LOCAL + LIVE + DISCORD"),
            "all three outputs have a clear compact label");
    Require(IncludesLocalRecording(RecordingOutputType::LocalAndDiscord),
            "local plus Discord retains local recording controls");
    Require(!IncludesLocalRecording(RecordingOutputType::LiveAndDiscord),
            "network-only output never exposes local recording controls");

    Require(CanStart(RecordingState::Idle), "idle can start");
    Require(CanStart(RecordingState::Failed), "failed session can retry");
    Require(!CanStart(RecordingState::Finalizing), "finalizing cannot start");

    Require(CanTransition(RecordingState::Idle, RecordingState::Starting), "start transition");
    Require(CanTransition(RecordingState::Starting, RecordingState::Recording), "start success transition");
    Require(CanTransition(RecordingState::Recording, RecordingState::Pausing), "pause request transition");
    Require(CanTransition(RecordingState::Pausing, RecordingState::Paused), "pause completion transition");
    Require(CanTransition(RecordingState::Paused, RecordingState::Resuming), "resume request transition");
    Require(CanTransition(RecordingState::Resuming, RecordingState::Recording), "resume completion transition");
    Require(CanTransition(RecordingState::Recording, RecordingState::Stopping), "stop transition");
    Require(CanTransition(RecordingState::Stopping, RecordingState::Finalizing), "finalization transition");
    Require(CanTransition(RecordingState::Finalizing, RecordingState::Idle), "save completion transition");

    Require(!CanTransition(RecordingState::Paused, RecordingState::Starting), "paused cannot start again");
    Require(!CanTransition(RecordingState::Finalizing, RecordingState::Stopping), "finalizing cannot stop twice");
    Require(!CanTransition(RecordingState::Idle, RecordingState::Paused), "idle cannot become paused");

    Require(CanPause(RecordingState::Recording), "recording can pause");
    Require(!CanPause(RecordingState::Paused), "paused cannot pause twice");
    Require(CanResume(RecordingState::Paused), "paused can resume");
    Require(!CanResume(RecordingState::Recording), "recording cannot resume");
    Require(CanStop(RecordingState::Recording), "recording can stop");
    Require(CanStop(RecordingState::Paused), "paused recording can stop and save");
    Require(!CanStop(RecordingState::Finalizing), "finalizing cannot stop");
    Require(HasRecordingTimeline(RecordingState::Recording), "recording has elapsed timeline");
    Require(HasRecordingTimeline(RecordingState::Paused), "paused retains elapsed timeline");
    Require(!HasRecordingTimeline(RecordingState::Armed), "armed has not started a timeline");

    const auto firstFrame = DecideCaptureTimelineFrame(0.0, 30, -1);
    Require(firstFrame.frameDue && firstFrame.presentationFrame == 0 &&
                firstFrame.skippedDeadlines == 0,
            "direct capture begins at the first real timeline deadline");
    const auto tooSoon = DecideCaptureTimelineFrame(0.02, 30, firstFrame.presentationFrame);
    Require(!tooSoon.frameDue, "direct capture does not duplicate a frame before the next deadline");
    const auto afterHitch = DecideCaptureTimelineFrame(0.141, 30, firstFrame.presentationFrame);
    Require(afterHitch.frameDue && afterHitch.presentationFrame == 4 &&
                afterHitch.skippedDeadlines == 3,
            "a render hitch preserves elapsed A/V time instead of compressing missed frames");
    Require(CapturePresentationTimeNanos(afterHitch.presentationFrame, 30) == 133'333'333,
            "presentation nanoseconds use the configured video time base");
    Require(NormalizeCapturePresentationFrame(17, 17) == 0,
            "the first packet emitted after encoder pre-roll starts the saved video at zero");
    Require(NormalizeCapturePresentationFrame(21, 17) == 4,
            "normalization preserves real deadline gaps after encoder pre-roll");

    std::cout << "Recording state tests passed\n";
    return 0;
}

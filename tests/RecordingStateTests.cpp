#include "saberstage/recording/ControllerShortcut.hpp"
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
using saberstage::recording::ControllerShortcut;
using saberstage::recording::ControllerShortcutAction;
using saberstage::recording::DecideCaptureTimelineFrame;
using saberstage::recording::HasRecordingTimeline;
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
            "future live output has the compact world-panel label");
    Require(RecordingOutputTypeName(RecordingOutputType::LocalAndLive) == std::string_view("LOCAL + LIVE"),
            "simultaneous safety recording and broadcast has a clear compact label");

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

    ControllerShortcut shortcut;
    Require(
        shortcut.Update(true, true, true, 0.5) == ControllerShortcutAction::None,
        "controller shortcut waits while held");
    Require(
        shortcut.Update(true, true, false, 0.0) == ControllerShortcutAction::None,
        "too-short controller hold is ignored");
    Require(shortcut.Update(true, true, true, 0.8) == ControllerShortcutAction::None,
            "toggle shortcut waits for release");
    Require(shortcut.Update(true, true, false, 0.0) == ControllerShortcutAction::ToggleRecording,
            "short controller hold toggles recording once on release");
    Require(shortcut.Update(true, true, false, 1.0) == ControllerShortcutAction::None,
            "released shortcut does not rapidly repeat");
    Require(shortcut.Update(true, true, true, 2.6) == ControllerShortcutAction::None,
            "stop shortcut waits for release");
    Require(shortcut.Update(true, true, false, 0.0) == ControllerShortcutAction::StopAndSave,
            "long controller hold stops and saves");
    Require(shortcut.Update(true, true, true, 1.0) == ControllerShortcutAction::None,
            "shortcut can begin another hold");
    Require(shortcut.Update(false, true, false, 0.0) == ControllerShortcutAction::None,
            "disabling shortcut clears an in-progress hold");
    Require(shortcut.Update(true, true, false, 0.0) == ControllerShortcutAction::None,
            "disabled shortcut cannot fire after re-enable");

    std::cout << "Recording state tests passed\n";
    return 0;
}

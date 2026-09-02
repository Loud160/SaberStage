// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Tests diagnostic throttling and the evidence required to report a height overwrite.
// - Does not claim to simulate Unity layout, clipping, or headset rendering.

#include "saberstage/ui/ChatPanelDiagnostics.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void Require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}
}

int main() {
    using saberstage::ui::ChatContentHeightWasOverwritten;
    using saberstage::ui::ChatPanelDiagnosticSchedule;
    using Report = ChatPanelDiagnosticSchedule::Report;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    Require(ChatContentHeightWasOverwritten(150, 150, 1),
        "later native height loss is identified after a correct write");
    Require(ChatContentHeightWasOverwritten(50, 50, 100),
        "unexpected later growth is also an overwrite, not just shrinkage");
    Require(!ChatContentHeightWasOverwritten(150, 1, 1),
        "a write that never read back correctly is not mislabeled a later overwrite");
    Require(!ChatContentHeightWasOverwritten(150, 150.2F, 149.8F),
        "subpixel layout differences do not cause warnings");
    Require(!ChatContentHeightWasOverwritten(-1, -1, 1), "no-write sentinel is ignored");
    Require(!ChatContentHeightWasOverwritten(150, 150, nan), "non-finite observation is not evidence");
    Require(!ChatContentHeightWasOverwritten(infinity, 150, 1), "non-finite request is not evidence");

    ChatPanelDiagnosticSchedule schedule;
    Require(!schedule.Advance(nan) && !schedule.Advance(infinity) && !schedule.Advance(-1),
        "bad frame intervals do not corrupt the sampler");
    Require(!schedule.Advance(0.25F), "no Unity traversal on every frame");
    Require(schedule.Advance(0.25F), "first sample waits for layout to settle");
    Require(schedule.SelectReport(true) == Report::Detail, "first settled sample captures details");
    for (int i = 0; i < 9; ++i) {
        Require(schedule.Advance(0.5F), "state sampled twice per second");
        Require(schedule.SelectReport(true) == Report::None,
            "even continuously changing state cannot flood hierarchy logs");
    }
    Require(schedule.Advance(0.5F), "five seconds elapsed");
    Require(schedule.SelectReport(true) == Report::Detail, "changed state is captured after cooldown");
    for (int i = 0; i < 59; ++i) {
        schedule.Advance(0.5F);
        Require(schedule.SelectReport(false) == Report::None, "stable state is quiet");
    }
    schedule.Advance(0.5F);
    Require(schedule.SelectReport(false) == Report::Summary, "stable heartbeat is summary only at thirty seconds");
    schedule = {};
    Require(!schedule.Advance(0.25F), "new panel gets a fresh settle interval");
    schedule.Advance(0.25F);
    Require(schedule.SelectReport(false) == Report::Detail, "new panel gets an initial detailed report");
    std::cout << "Chat panel diagnostic policy tests passed\n";
}

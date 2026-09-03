// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility: owns the independent chat settings/moderation/request world-panel lifetimes.
#pragma once
#include <memory>
namespace saberstage::app {
class ApplicationRoot;
}
namespace saberstage::ui {
class ChatControls final {
  public:
    explicit ChatControls(app::ApplicationRoot &root);
    ~ChatControls();
    void Show();
    void Tick() noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace saberstage::ui

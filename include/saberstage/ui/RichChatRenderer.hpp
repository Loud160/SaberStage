// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// File responsibility: main-thread sprite atlas and safe inline message formatting.
#pragma once
#include "saberstage/broadcast/ChatProtocol.hpp"
#include <memory>
namespace TMPro {
class TMP_SpriteAsset;
class TextMeshProUGUI;
} // namespace TMPro
namespace saberstage::broadcast {
class ChatAssets;
}
namespace saberstage::settings {
struct ChatSettings;
}
namespace saberstage::ui {
class RichChatRenderer final {
  public:
    explicit RichChatRenderer(broadcast::ChatAssets &assets);
    ~RichChatRenderer();
    bool Tick(float dt);
    TMPro::TMP_SpriteAsset *SpriteAsset() const;
    // Reflow/reuse only. TMP dirties its mesh/layout on every sprite setter
    // call, even when both pointers are null or the atlas has not changed.
    void BindSpriteAsset(TMPro::TextMeshProUGUI *row) const;
    void BeginPass();
    std::string Format(const broadcast::TwitchChatMessage &message, const settings::ChatSettings &settings,
                       std::string_view channelLogin);
    void Decorate(TMPro::TextMeshProUGUI *row, float width, float height, bool platformAccent);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace saberstage::ui

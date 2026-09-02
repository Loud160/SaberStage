// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the SaberStage contributors
//
// Part of SaberStage.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.

// File responsibility:
// - Stores and retrieves sensitive account tokens through Android Keystore-backed encryption.
// - Plaintext credentials are kept out of repository defaults and ordinary settings serialization.

#pragma once

#include <string>
#include <string_view>

namespace saberstage::security {

// Small Android-only vault used for Twitch OAuth credentials. The AES key is
// generated inside Android Keystore and can never be exported by SaberStage;
// settings.json stores only the random IV and authenticated ciphertext.
class AndroidKeystore final {
public:
    [[nodiscard]] bool Encrypt(
        std::string_view plaintext,
        std::string& envelope,
        std::string* error = nullptr) const noexcept;

    [[nodiscard]] bool Decrypt(
        std::string_view envelope,
        std::string& plaintext,
        std::string* error = nullptr) const noexcept;
};

} // namespace saberstage::security

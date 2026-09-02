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

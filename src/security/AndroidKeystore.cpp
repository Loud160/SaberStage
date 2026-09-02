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

#include "saberstage/security/AndroidKeystore.hpp"

#include "UnityEngine/AndroidJNI.hpp"
#include "UnityEngine/jvalue.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace saberstage::security {
namespace {

using Jni = UnityEngine::AndroidJNI;
using JValue = UnityEngine::jvalue;
using JHandle = System::IntPtr;

constexpr std::string_view kEnvelopePrefix = "ak1:";
constexpr std::string_view kKeyAlias =
    "com.beatgames.beatsaber.saberstage.twitch.oauth.v1";
constexpr std::string_view kAdditionalAuthenticatedData =
    "SaberStage/TwitchOAuth/v1";

bool IsNull(JHandle handle) noexcept { return handle.m_value == nullptr; }

ArrayW<JValue> NoArguments() noexcept { return nullptr; }

JValue ObjectArgument(JHandle object) {
    JValue result{};
    result.__cordl_internal_set_l(object);
    return result;
}

JValue IntArgument(std::int32_t value) {
    JValue result{};
    result.__cordl_internal_set_i(value);
    return result;
}

JValue BoolArgument(bool value) {
    JValue result{};
    result.__cordl_internal_set_z(value);
    return result;
}

void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

bool CheckJni(std::string_view stage, std::string* error) {
    const auto exception = Jni::ExceptionOccurred();
    if (IsNull(exception)) return true;
    // Never describe Java exceptions here: provider messages can contain
    // sensitive input. The caller gets only the stage at which the secure
    // operation failed, and no token material is written to SaberStage logs.
    Jni::ExceptionClear();
    Jni::DeleteLocalRef(exception);
    SetError(error, "Android Keystore failed while " + std::string(stage));
    return false;
}

class LocalFrame final {
public:
    explicit LocalFrame(std::string* error) : active_(Jni::PushLocalFrame(96) == 0) {
        if (!active_) SetError(error, "Android Keystore could not reserve JNI references");
    }
    ~LocalFrame() {
        if (active_) Jni::PopLocalFrame({});
    }
    [[nodiscard]] bool Active() const noexcept { return active_; }

private:
    bool active_ = false;
};

JHandle JavaString(std::string_view value) {
    return Jni::NewStringUTF(std::string(value));
}

JHandle JavaBytes(std::string_view value) {
    const auto bytes = std::span(
        reinterpret_cast<const std::uint8_t*>(value.data()), value.size());
    return Jni::ToByteArray(ArrayW<std::uint8_t>(bytes));
}

JHandle JavaBytes(std::span<const std::uint8_t> value) {
    return Jni::ToByteArray(ArrayW<std::uint8_t>(value));
}

bool CopyJavaBytes(JHandle array, std::vector<std::uint8_t>& bytes, std::string* error) {
    if (IsNull(array)) {
        SetError(error, "Android Keystore returned an empty byte array");
        return false;
    }
    const auto managed = Jni::FromByteArray(array);
    bytes.assign(managed.begin(), managed.end());
    return CheckJni("copying protected bytes", error);
}

std::string Hex(std::span<const std::uint8_t> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.resize(bytes.size() * 2);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        result[index * 2] = digits[bytes[index] >> 4];
        result[index * 2 + 1] = digits[bytes[index] & 0x0f];
    }
    return result;
}

int HexNibble(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool ParseHex(std::string_view value, std::vector<std::uint8_t>& bytes) {
    if (value.empty() || value.size() % 2 != 0) return false;
    bytes.resize(value.size() / 2);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const auto high = HexNibble(value[index * 2]);
        const auto low = HexNibble(value[index * 2 + 1]);
        if (high < 0 || low < 0) return false;
        bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

JHandle StringArray(std::string_view value, std::string* error) {
    const auto stringClass = Jni::FindClass("java/lang/String");
    if (IsNull(stringClass) || !CheckJni("finding java.lang.String", error)) return {};
    const auto array = Jni::NewObjectArray(1, stringClass, {});
    const auto item = JavaString(value);
    if (IsNull(array) || IsNull(item) ||
            !CheckJni("creating a KeyStore parameter array", error)) {
        return {};
    }
    Jni::SetObjectArrayElement(array, 0, item);
    if (!CheckJni("setting a KeyStore parameter", error)) return {};
    return array;
}

JHandle GetOrCreateKey(std::string* error) {
    const auto keyStoreClass = Jni::FindClass("java/security/KeyStore");
    if (IsNull(keyStoreClass) || !CheckJni("finding KeyStore", error)) return {};
    const auto getInstance = Jni::GetStaticMethodID(
        keyStoreClass, "getInstance",
        "(Ljava/lang/String;)Ljava/security/KeyStore;");
    const auto androidKeyStore = JavaString("AndroidKeyStore");
    const auto keyStore = Jni::CallStaticObjectMethod(
        keyStoreClass, getInstance, {ObjectArgument(androidKeyStore)});
    if (IsNull(keyStore) || !CheckJni("opening AndroidKeyStore", error)) return {};

    const auto load = Jni::GetMethodID(
        keyStoreClass, "load", "(Ljava/security/KeyStore$LoadStoreParameter;)V");
    Jni::CallVoidMethod(keyStore, load, {ObjectArgument({})});
    if (!CheckJni("loading AndroidKeyStore", error)) return {};

    const auto alias = JavaString(kKeyAlias);
    const auto containsAlias = Jni::GetMethodID(
        keyStoreClass, "containsAlias", "(Ljava/lang/String;)Z");
    const auto exists = Jni::CallBooleanMethod(
        keyStore, containsAlias, {ObjectArgument(alias)});
    if (!CheckJni("checking the SaberStage key", error)) return {};

    if (!exists) {
        const auto generatorClass = Jni::FindClass("javax/crypto/KeyGenerator");
        const auto getGenerator = Jni::GetStaticMethodID(
            generatorClass, "getInstance",
            "(Ljava/lang/String;Ljava/lang/String;)Ljavax/crypto/KeyGenerator;");
        const auto aes = JavaString("AES");
        const auto generator = Jni::CallStaticObjectMethod(
            generatorClass, getGenerator,
            {ObjectArgument(aes), ObjectArgument(androidKeyStore)});
        if (IsNull(generator) || !CheckJni("opening the AES key generator", error)) return {};

        const auto builderClass = Jni::FindClass(
            "android/security/keystore/KeyGenParameterSpec$Builder");
        const auto builderConstructor = Jni::GetMethodID(
            builderClass, "<init>", "(Ljava/lang/String;I)V");
        // PURPOSE_ENCRYPT | PURPOSE_DECRYPT. The key remains non-exportable
        // because AndroidKeyStore owns the generated SecretKey material.
        const auto builder = Jni::NewObject(
            builderClass, builderConstructor,
            {ObjectArgument(alias), IntArgument(3)});
        if (IsNull(builder) || !CheckJni("creating the key policy", error)) return {};

        const auto gcmModes = StringArray("GCM", error);
        const auto noPadding = StringArray("NoPadding", error);
        if (IsNull(gcmModes) || IsNull(noPadding)) return {};
        const auto builderSignature =
            "Landroid/security/keystore/KeyGenParameterSpec$Builder;";
        const auto setBlockModes = Jni::GetMethodID(
            builderClass, "setBlockModes",
            (std::string("([Ljava/lang/String;)") + builderSignature).c_str());
        Jni::CallObjectMethod(builder, setBlockModes, {ObjectArgument(gcmModes)});
        if (!CheckJni("restricting the key to GCM", error)) return {};
        const auto setPaddings = Jni::GetMethodID(
            builderClass, "setEncryptionPaddings",
            (std::string("([Ljava/lang/String;)") + builderSignature).c_str());
        Jni::CallObjectMethod(builder, setPaddings, {ObjectArgument(noPadding)});
        if (!CheckJni("restricting the key to NoPadding", error)) return {};
        const auto setKeySize = Jni::GetMethodID(
            builderClass, "setKeySize",
            (std::string("(I)") + builderSignature).c_str());
        Jni::CallObjectMethod(builder, setKeySize, {IntArgument(256)});
        if (!CheckJni("setting the AES key size", error)) return {};
        const auto requireRandomIv = Jni::GetMethodID(
            builderClass, "setRandomizedEncryptionRequired",
            (std::string("(Z)") + builderSignature).c_str());
        Jni::CallObjectMethod(builder, requireRandomIv, {BoolArgument(true)});
        if (!CheckJni("requiring randomized encryption", error)) return {};

        const auto build = Jni::GetMethodID(
            builderClass, "build", "()Landroid/security/keystore/KeyGenParameterSpec;");
        const auto specification = Jni::CallObjectMethod(builder, build, NoArguments());
        if (IsNull(specification) || !CheckJni("building the key policy", error)) return {};
        const auto initialize = Jni::GetMethodID(
            generatorClass, "init", "(Ljava/security/spec/AlgorithmParameterSpec;)V");
        Jni::CallVoidMethod(generator, initialize, {ObjectArgument(specification)});
        if (!CheckJni("initializing the key generator", error)) return {};
        const auto generate = Jni::GetMethodID(
            generatorClass, "generateKey", "()Ljavax/crypto/SecretKey;");
        const auto generated = Jni::CallObjectMethod(generator, generate, NoArguments());
        if (IsNull(generated) || !CheckJni("generating the SaberStage key", error)) return {};
    }

    const auto getKey = Jni::GetMethodID(
        keyStoreClass, "getKey", "(Ljava/lang/String;[C)Ljava/security/Key;");
    const auto key = Jni::CallObjectMethod(
        keyStore, getKey, {ObjectArgument(alias), ObjectArgument({})});
    if (IsNull(key) || !CheckJni("reading the SaberStage key handle", error)) return {};
    return key;
}

JHandle NewCipher(std::string* error) {
    const auto cipherClass = Jni::FindClass("javax/crypto/Cipher");
    if (IsNull(cipherClass) || !CheckJni("finding Cipher", error)) return {};
    const auto getInstance = Jni::GetStaticMethodID(
        cipherClass, "getInstance",
        "(Ljava/lang/String;)Ljavax/crypto/Cipher;");
    const auto transformation = JavaString("AES/GCM/NoPadding");
    const auto cipher = Jni::CallStaticObjectMethod(
        cipherClass, getInstance, {ObjectArgument(transformation)});
    if (IsNull(cipher) || !CheckJni("opening AES/GCM/NoPadding", error)) return {};
    return cipher;
}

bool AddAuthenticatedData(JHandle cipher, std::string* error) {
    const auto cipherClass = Jni::GetObjectClass(cipher);
    const auto update = Jni::GetMethodID(cipherClass, "updateAAD", "([B)V");
    const auto aad = JavaBytes(kAdditionalAuthenticatedData);
    Jni::CallVoidMethod(cipher, update, {ObjectArgument(aad)});
    return CheckJni("authenticating the token envelope", error);
}

} // namespace

bool AndroidKeystore::Encrypt(
    std::string_view plaintext,
    std::string& envelope,
    std::string* error) const noexcept {
    envelope.clear();
    try {
        LocalFrame frame(error);
        if (!frame.Active()) return false;
        const auto key = GetOrCreateKey(error);
        const auto cipher = NewCipher(error);
        if (IsNull(key) || IsNull(cipher)) return false;
        const auto cipherClass = Jni::GetObjectClass(cipher);
        const auto initialize = Jni::GetMethodID(
            cipherClass, "init", "(ILjava/security/Key;)V");
        // Cipher.ENCRYPT_MODE. Omitting an IV is intentional: Android
        // Keystore generates a fresh random GCM nonce for every envelope.
        Jni::CallVoidMethod(cipher, initialize, {IntArgument(1), ObjectArgument(key)});
        if (!CheckJni("initializing token encryption", error) ||
                !AddAuthenticatedData(cipher, error)) {
            return false;
        }
        const auto doFinal = Jni::GetMethodID(cipherClass, "doFinal", "([B)[B");
        const auto input = JavaBytes(plaintext);
        const auto encryptedArray = Jni::CallObjectMethod(
            cipher, doFinal, {ObjectArgument(input)});
        if (!CheckJni("encrypting Twitch authorization", error)) return false;
        const auto getIv = Jni::GetMethodID(cipherClass, "getIV", "()[B");
        const auto ivArray = Jni::CallObjectMethod(cipher, getIv, NoArguments());
        if (!CheckJni("reading the random encryption IV", error)) return false;

        std::vector<std::uint8_t> iv;
        std::vector<std::uint8_t> encrypted;
        if (!CopyJavaBytes(ivArray, iv, error) ||
                !CopyJavaBytes(encryptedArray, encrypted, error) ||
                iv.size() < 12 || encrypted.size() < 16) {
            SetError(error, "Android Keystore returned an invalid AES-GCM envelope");
            return false;
        }
        envelope = std::string(kEnvelopePrefix) + Hex(iv) + ":" + Hex(encrypted);
        return true;
    } catch (...) {
        SetError(error, "Android Keystore token encryption failed safely");
        envelope.clear();
        return false;
    }
}

bool AndroidKeystore::Decrypt(
    std::string_view envelope,
    std::string& plaintext,
    std::string* error) const noexcept {
    plaintext.clear();
    try {
        if (!envelope.starts_with(kEnvelopePrefix)) {
            SetError(error, "saved Twitch authorization uses an unknown secure format");
            return false;
        }
        const auto separator = envelope.find(':', kEnvelopePrefix.size());
        if (separator == std::string_view::npos) {
            SetError(error, "saved Twitch authorization is incomplete");
            return false;
        }
        std::vector<std::uint8_t> iv;
        std::vector<std::uint8_t> encrypted;
        if (!ParseHex(envelope.substr(kEnvelopePrefix.size(), separator - kEnvelopePrefix.size()), iv) ||
                !ParseHex(envelope.substr(separator + 1), encrypted) ||
                iv.size() < 12 || encrypted.size() < 16) {
            SetError(error, "saved Twitch authorization is malformed");
            return false;
        }

        LocalFrame frame(error);
        if (!frame.Active()) return false;
        const auto key = GetOrCreateKey(error);
        const auto cipher = NewCipher(error);
        if (IsNull(key) || IsNull(cipher)) return false;
        const auto gcmClass = Jni::FindClass("javax/crypto/spec/GCMParameterSpec");
        const auto constructor = Jni::GetMethodID(gcmClass, "<init>", "(I[B)V");
        const auto ivArray = JavaBytes(iv);
        const auto parameters = Jni::NewObject(
            gcmClass, constructor, {IntArgument(128), ObjectArgument(ivArray)});
        if (IsNull(parameters) || !CheckJni("creating the AES-GCM parameters", error)) return false;

        const auto cipherClass = Jni::GetObjectClass(cipher);
        const auto initialize = Jni::GetMethodID(
            cipherClass, "init",
            "(ILjava/security/Key;Ljava/security/spec/AlgorithmParameterSpec;)V");
        Jni::CallVoidMethod(
            cipher, initialize,
            {IntArgument(2), ObjectArgument(key), ObjectArgument(parameters)});
        if (!CheckJni("initializing token decryption", error) ||
                !AddAuthenticatedData(cipher, error)) {
            return false;
        }
        const auto doFinal = Jni::GetMethodID(cipherClass, "doFinal", "([B)[B");
        const auto encryptedArray = JavaBytes(encrypted);
        const auto plaintextArray = Jni::CallObjectMethod(
            cipher, doFinal, {ObjectArgument(encryptedArray)});
        if (!CheckJni("decrypting Twitch authorization", error)) return false;
        std::vector<std::uint8_t> bytes;
        if (!CopyJavaBytes(plaintextArray, bytes, error)) return false;
        plaintext.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        std::fill(bytes.begin(), bytes.end(), 0);
        return true;
    } catch (...) {
        SetError(error, "Android Keystore token decryption failed safely");
        plaintext.clear();
        return false;
    }
}

} // namespace saberstage::security
